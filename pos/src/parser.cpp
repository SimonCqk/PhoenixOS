/*
 * Copyright 2024 The PhoenixOS Authors. All rights reserved.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include "pos/include/common.h"
#include "pos/include/workspace.h"
#include "pos/include/client.h"
#include "pos/include/transport.h"
#include "pos/include/parser.h"


POSParser::POSParser(POSWorkspace* ws, POSClient* client) 
    : _ws(ws), _client(client), _stop_flag(false)
{   
    POS_CHECK_POINTER(ws);
    POS_CHECK_POINTER(client);

    // start daemon thread
    _daemon_thread = new std::thread(&POSParser::__daemon, this);
    POS_CHECK_POINTER(_daemon_thread);

    POS_LOG_C("parser started");
};


POSParser::~POSParser(){ 
    this->shutdown(); 
}


pos_retval_t POSParser::init(){
    if(unlikely(POS_SUCCESS != this->init_ps_functions())){
        POS_ERROR_C_DETAIL("failed to insert functions");
    }
}


void POSParser::shutdown(){ 
    this->_stop_flag = true;
    if(this->_daemon_thread != nullptr){
        this->_daemon_thread->join();
        delete this->_daemon_thread;
        this->_daemon_thread = nullptr;
        POS_LOG_C("Runtime daemon thread shutdown");
    }
}


void POSParser::__daemon(){
    uint64_t i, api_id;
    pos_retval_t parser_retval, cmd_retval;
    POSAPIMeta_t api_meta;
    uint64_t last_ckpt_tick = 0, current_tick;
    POSAPIContext_QE* apicxt_wqe;
    std::vector<POSAPIContext_QE*> apicxt_wqes;
    POSCommand_QE_t *cmd_wqe;
    std::vector<POSCommand_QE_t*> cmd_wqes;

    if(unlikely(POS_SUCCESS != this->daemon_init())){
        POS_WARN_C("failed to init daemon, worker daemon exit");
        return;
    }

    while(!_stop_flag){
        // if the client isn't ready, the queue might not exist, we can't do any queue operation
        if(this->_client->status != kPOS_ClientStatus_Active){ continue; }

        // step 1: digest cmd from oob work queue
        cmd_wqes.clear();
        this->_ws->poll_q<kPOS_QueueDirection_Oob2Parser, kPOS_QueueType_Cmd_WQ>(this->_client->id, &cmd_wqes);
        for(i=0; i<cmd_wqes.size(); i++){
            POS_CHECK_POINTER(cmd_wqe = cmd_wqes[i]);
            this->__process_cmd(cmd_wqe);
        }

        // step 2: digest cmd from worker work queue
        cmd_wqes.clear();
        this->_ws->poll_q<kPOS_QueueDirection_Worker2Parser, kPOS_QueueType_Cmd_WQ>(this->_client->id, &cmd_wqes);
        for(i=0; i<cmd_wqes.size(); i++){
            POS_CHECK_POINTER(cmd_wqe = cmd_wqes[i]);
            this->__process_cmd(cmd_wqe);
        }

        // step 3: digest apicxt from rpc work queue
        apicxt_wqes.clear();
        this->_ws->poll_q<kPOS_QueueDirection_Rpc2Parser, kPOS_QueueType_ApiCxt_WQ>(
            this->_client->id, &apicxt_wqes
        );

        for(i=0; i<apicxt_wqes.size(); i++){
            POS_CHECK_POINTER(apicxt_wqe = apicxt_wqes[i]);

            api_id = apicxt_wqe->api_cxt->api_id;
            api_meta = _ws->api_mgnr->api_metas[api_id];

        #if POS_CONF_RUNTIME_EnableDebugCheck
            if(unlikely(_parser_functions.count(api_id) == 0)){
                POS_ERROR_C_DETAIL(
                    "runtime has no parser function for api %lu, need to implement", api_id
                );
            }
        #endif

            /*!
            *  \brief  ================== phrase 1 - parse API semantics ==================
            */
            apicxt_wqe->runtime_s_tick = POSUtilTimestamp::get_tsc();
            parser_retval = (*(this->_parser_functions[api_id]))(this->_ws, apicxt_wqe);
            apicxt_wqe->runtime_e_tick = POSUtilTimestamp::get_tsc();

            // set the return code
            apicxt_wqe->api_cxt->return_code = this->_ws->api_mgnr->cast_pos_retval(
                /* pos_retval */ parser_retval, 
                /* library_id */ api_meta.library_id
            );

            if(unlikely(POS_SUCCESS != parser_retval)){
                POS_WARN_C(
                    "failed to execute parser function: client_id(%lu), api_id(%lu)",
                    apicxt_wqe->client_id, api_id
                );
                apicxt_wqe->status = kPOS_API_Execute_Status_Parser_Failed;
                apicxt_wqe->return_tick = POSUtilTimestamp::get_tsc();                    
                this->_ws->template push_q<kPOS_QueueDirection_Rpc2Parser, kPOS_QueueType_ApiCxt_CQ>(apicxt_wqe);

                continue;
            }

            /*!
             *  \note       for api in type of Delete_Resource, one can directly send
             *              response to the client right after operating on mocked resources
             *  \warning    we can't apply this rule for all Create_Resource, consider the memory
             *              situation, which is passthrough addressed
             *  TODO: delete this block, should be implement in autogen system
             */
            if(unlikely(api_meta.api_type == kPOS_API_Type_Delete_Resource)){
                POS_DEBUG_C("api(%lu) is type of Delete_Resource, set as \"Return_After_Parse\"", api_id);
                apicxt_wqe->status = kPOS_API_Execute_Status_Return_After_Parse;
            }

            /*!
             *  \note       for sync api that mark as kPOS_API_Execute_Status_Return_After_Parse,
             *              we directly return the result back to the frontend side
             */
            if(     apicxt_wqe->status == kPOS_API_Execute_Status_Return_After_Parse 
                ||  apicxt_wqe->status == kPOS_API_Execute_Status_Return_Without_Worker
            ){
                apicxt_wqe->return_tick = POSUtilTimestamp::get_tsc();
                this->_ws->template push_q<kPOS_QueueDirection_Rpc2Parser, kPOS_QueueType_ApiCxt_CQ>(apicxt_wqe);
            }

            // insert apicxt_wqe to worker queue
            if(apicxt_wqe->status != kPOS_API_Execute_Status_Return_Without_Worker){
                this->_ws->template push_q<kPOS_QueueDirection_Parser2Worker, kPOS_QueueType_ApiCxt_WQ>(apicxt_wqe);
            }
        }

        /*!
        *  \brief  ================== phrase 2 - checkpoint insertion ==================
        */
        #if POS_CONF_EVAL_CkptOptLevel > 0
            if(this->_client->is_time_for_ckpt()){
                if(unlikely(POS_SUCCESS != this->__checkpoint_insertion())){
                    POS_WARN_C("failed to insert checkpointing op");
                }
            }
        #else
            /* do nothing */ ;
        #endif
    }
}


/*!
 *  \brief  insert checkpoint op to the DAG based on certain conditions
 *  \note   aware of the macro POS_CONF_EVAL_CkptEnableIncremental
 *  \return POS_SUCCESS for successfully checkpoint insertion
 */
pos_retval_t POSParser::__checkpoint_insertion() {
    #if POS_CONF_EVAL_CkptEnableIncremental == 1
        return this->__checkpoint_insertion_incremental();
    #else
        return this->__checkpoint_insertion_naive();
    #endif
}


/*!
 *  \brief  naive implementation of checkpoint insertion procedure
 *  \note   this implementation naively insert a checkpoint op to the dag, 
 *          without any optimization hint
 *  \return POS_SUCCESS for successfully checkpoint insertion
 */
pos_retval_t POSParser::__checkpoint_insertion_naive() { 
    pos_retval_t retval = POS_SUCCESS;
    POSHandle *handle;
    POSHandleManager<POSHandle>* hm;
    POSAPIContext_QE *ckpt_wqe;
    uint64_t i, nb_handles;

    ckpt_wqe = new POSAPIContext_QE_t(
        /* api_id*/ this->_ws->checkpoint_api_id,
        /* client */ this->_client
    );
    POS_CHECK_POINTER(ckpt_wqe);

    for(auto &stateful_handle_id : this->_ws->stateful_handle_type_idx){
        hm = pos_get_client_typed_hm(this->_client, stateful_handle_id, POSHandleManager<POSHandle>);
        POS_CHECK_POINTER(hm);
        nb_handles = hm->get_nb_handles();
        for(i=0; i<nb_handles; i++){
            handle = hm->get_handle_by_id(i);
            POS_CHECK_POINTER(handle);
            ckpt_wqe->record_checkpoint_handles(handle);
        }
    }

    retval = this->_client->dag.launch_op(ckpt_wqe);

exit:
    return retval;
}


/*!
 *  \brief  level-1/2 optimization of checkpoint insertion procedure
 *  \note   this implementation give hints of those memory handles that
 *          been modified (INOUT/OUT) since last checkpoint
 *  \return POS_SUCCESS for successfully checkpoint insertion
 */
pos_retval_t POSParser::__checkpoint_insertion_incremental() {
    pos_retval_t retval = POS_SUCCESS;
    POSHandleManager<POSHandle>* hm;
    POSAPIContext_QE *ckpt_wqe;
    uint64_t i;

    ckpt_wqe = new POSAPIContext_QE_t(
        /* api_id*/ this->_ws->checkpoint_api_id,
        /* client */ this->_client
    );
    POS_CHECK_POINTER(ckpt_wqe);

    /*!
        *  \note   we only checkpoint those resources that has been modified since last checkpoint
        */
    for(auto &stateful_handle_id : this->_ws->stateful_handle_type_idx){
        hm = pos_get_client_typed_hm(this->_client, stateful_handle_id, POSHandleManager<POSHandle>);
        POS_CHECK_POINTER(hm);
        std::set<POSHandle*>& modified_handles = hm->get_modified_handles();
        if(likely(modified_handles.size() > 0)){
            ckpt_wqe->record_checkpoint_handles(modified_handles);
        }
        hm->clear_modified_handle();
    }

    retval = this->_client->dag.launch_op(ckpt_wqe);
    
exit:
    return retval;
}


pos_retval_t POSParser::__process_cmd(POSCommand_QE_t *cmd){
    pos_retval_t retval = POS_SUCCESS;

    POS_CHECK_POINTER(cmd);

    switch (cmd->type)
    {
    case kPOS_Command_OobToParser_PreDumpStart:
        // TODO: todo
        cmd->retval = POS_SUCCESS;
        this->_ws->template push_q<kPOS_QueueDirection_Oob2Parser, kPOS_QueueType_Cmd_CQ>(cmd);
        break;

    default:
        POS_ERROR_C_DETAIL("unknown command type %u, this is a bug", cmd->type);
    }

exit:
    return retval;
}
