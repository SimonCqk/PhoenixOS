#pragma once

#include <iostream>

#include "pos/common.h"
#include "pos/cuda_impl/handle.h"
#include "pos/cuda_impl/runtime.h"
#include "pos/cuda_impl/client.h"
#include "pos/cuda_impl/api_context.h"
#include "pos/cuda_impl/utils/fatbin.h"
#include "pos/utils/bipartite_graph.h"
#include "pos/dag.h"


namespace rt_functions {


/*!
 *  \related    cuModuleLoadData
 *  \brief      load CUmodule down to the driver, which contains PTX/SASS binary
 */
namespace cu_module_load_data {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        uint64_t i;
        POSClient_CUDA *client;
        POSHandle_CUDA_Module_ptr module_handle;
        POSHandle_CUDA_Function_ptr function_handle;
        POSHandleManager<POSHandle_CUDA_Context>* hm_context;
        POSHandleManager<POSHandle_CUDA_Module>* hm_module;
        POSHandleManager<POSHandle_CUDA_Function>* hm_function;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);
    
        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 2)){
            POS_WARN(
                "parse(cu_module_load_data): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 2
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_context = client->handle_managers[kPOS_ResourceTypeId_CUDA_Context];
        hm_module = client->handle_managers[kPOS_ResourceTypeId_CUDA_Module];
        hm_function = client->handle_managers[kPOS_ResourceTypeId_CUDA_Function];
        POS_CHECK_POINTER(hm_context); POS_CHECK_POINTER(hm_context->latest_used_handle);
        POS_CHECK_POINTER(hm_module);
        POS_CHECK_POINTER(hm_function);

        // operate on handler manager
        retval = hm_module->allocate_mocked_resource(
            /* handle */ &module_handle,
            /* related_handles */ std::map<uint64_t, std::vector<POSHandle_ptr>>({{ 
                /* id */ kPOS_ResourceTypeId_CUDA_Context, 
                /* handles */ std::vector<POSHandle_ptr>({hm_context->latest_used_handle}) 
            }}),
            /* size */ kPOS_HandleDefaultSize,
            /* expected_addr */ pos_api_param_value(wqe, 0, uint64_t)
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN("parse(cu_module_load_data): failed to allocate mocked module within the CUDA module handler manager");
            goto exit;
        } else {
            POS_DEBUG(
                "parse(cu_module_load_data): allocate mocked module within the CUDA module handler manager: addr(%p), size(%lu), context_server_addr(%p)",
                module_handle->client_addr, module_handle->size,
                hm_context->latest_used_handle->server_addr
            )
        }

        // set current handle as the latest used handle
        hm_module->latest_used_handle = module_handle;

        // record the related handle to QE
        wqe->record_handle(
            /* id */ kPOS_ResourceTypeId_CUDA_Module, 
            /* handle_view */ POSHandleView_t(
                /* handle */ module_handle,
                /* dir */ kPOS_Edge_Direction_Create,
                /* host_value */ pos_api_param_addr(wqe, 1),
                /* host_value_size */ pos_api_param_size(wqe, 1)
            )
        );

        // analyse the fatbin and stores the function attributes in the handle
        retval = POSUtil_CUDA_Fatbin::obtain_functions_from_fatbin(
            (uint8_t*)(pos_api_param_addr(wqe, 1)), &(module_handle->function_desps)
        );
        POS_DEBUG(
            "parse(cu_module_load_data): found %lu functions in the fatbin",
            module_handle->function_desps.size()
        );

        #if POS_PRINT_DEBUG
            for(auto desp : module_handle->function_desps){
                char *offsets_info = malloc(1024); POS_CHECK_POINTER(offsets_info);
                char *sizes_info = malloc(1024); POS_CHECK_POINTER(sizes_info);
                memset(offsets_info, 0, 1024);
                memset(sizes_info, 0, 1024);

                for(auto offset : desp->param_offsets){ sprintf(offsets_info, "%s, %u", offsets_info, offset); }
                for(auto size : desp->param_sizes){ sprintf(sizes_info, "%s, %u", sizes_info, size); }

                POS_DEBUG(
                    "function_name(%s), offsets(%s), param_sizes(%s)",
                    desp->name.get(), offsets_info, sizes_info
                );

                free(offsets_info);
                free(sizes_info);
            }
        #endif

        // allocate the module handle in the dag
        retval = client->dag.allocate_handle(module_handle);
        if(unlikely(retval != POS_SUCCESS)){
            goto exit;
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

        // mark this sync call can be returned after parsing
        wqe->status = kPOS_API_Execute_Status_Return_After_Parse;

    exit:
        return retval;
    }

} // cu_module_load_data




/*!
 *  \related    cuModuleGetFunction 
 *  \brief      obtain kernel host pointer by given kernel name from specified CUmodule
 */
namespace cu_module_get_function {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        uint64_t i;
        POSClient_CUDA *client;
        POSHandle_CUDA_Module_ptr module_handle;
        POSHandle_CUDA_Function_ptr function_handle;
        POSCudaFunctionDesp_ptr function_desp;
        bool found_function_desp;
        POSHandleManager<POSHandle_CUDA_Module>* hm_module;
        POSHandleManager<POSHandle_CUDA_Function>* hm_function;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 5)){
            POS_WARN(
                "parse(cu_module_get_function): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 5
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_module = client->handle_managers[kPOS_ResourceTypeId_CUDA_Module];
        hm_function = client->handle_managers[kPOS_ResourceTypeId_CUDA_Function];
        POS_CHECK_POINTER(hm_module);
        POS_CHECK_POINTER(hm_function);

        // obtain target handle
        if(likely(
            hm_module->latest_used_handle->is_client_addr_in_range((void*)pos_api_param_value(wqe, 0, uint64_t))
        )){
            module_handle = hm_module->latest_used_handle;
        } else {
            if(unlikely(
                POS_SUCCESS != hm_module->get_handle_by_client_addr(
                    /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
                    /* handle */&module_handle
                )
            )){
                POS_WARN(
                    "parse(cu_module_get_function): failed to find module with client address %p",
                    pos_api_param_value(wqe, 0, uint64_t)
                );
                retval = POS_FAILED_NOT_EXIST;
                goto exit;
            }
        }
        
        // check whether the requested kernel name is recorded within the module
        found_function_desp = false;
        for(i=0; i<module_handle->function_desps.size(); i++){
            if(unlikely(
                !strcmp(module_handle->function_desps[i]->name.get(), pos_api_param_addr(wqe, 3))
            )){
                function_desp = module_handle->function_desps[i];
                found_function_desp = true;
                break;
            }
        }
        if(unlikely(found_function_desp == false)){
            POS_WARN(
                "parse(cu_module_get_function): failed to find function within the module: module_clnt_addr(%p), device_name(%s)",
                pos_api_param_value(wqe, 0, uint64_t), pos_api_param_addr(wqe, 3)
            );
            retval = POS_FAILED_NOT_EXIST;
            goto exit;
        }

        // operate on handler manager
        retval = hm_function->allocate_mocked_resource(
            /* handle */ &function_handle,
            /* related_handles */ std::map<uint64_t, std::vector<POSHandle_ptr>>({{ 
                /* id */ kPOS_ResourceTypeId_CUDA_Module, 
                /* handles */ std::vector<POSHandle_ptr>({module_handle}) 
            }}),
            /* size */ kPOS_HandleDefaultSize,
            /* expected_addr */ pos_api_param_value(wqe, 1, uint64_t)
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN("parse(cu_module_get_function): failed to allocate mocked function within the CUDA function handler manager");
            goto exit;
        } else {
            POS_DEBUG(
                "parse(cu_module_get_function): allocate mocked function within the CUDA function handler manager: addr(%p), size(%lu), module_server_addr(%p)",
                function_handle->client_addr, function_handle->size,
                module_handle->server_addr
            )
        }

        // transfer function descriptions from the descriptor
        function_handle->nb_params = function_desp->nb_params;
        function_handle->param_offsets = function_desp->param_offsets;
        function_handle->param_sizes = function_desp->param_sizes;
        function_handle->cbank_param_size = function_desp->cbank_param_size;
        function_handle->name = function_desp->name;

        // set handle state as pending to create
        function_handle->state = kPOS_HandleState_Create_Pending;

        // record the related handle to QE
        wqe->record_handle(
            /* id */ kPOS_ResourceTypeId_CUDA_Function, 
            /* handle_view */ POSHandleView_t(
                /* handle */ function_handle,
                /* dir */ kPOS_Edge_Direction_Create
            )
        );

        // allocate the function handle in the dag
        retval = client->dag.allocate_handle(function_handle);
        if(unlikely(retval != POS_SUCCESS)){
            goto exit;
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

        // mark this sync call can be returned after parsing
        wqe->status = kPOS_API_Execute_Status_Return_After_Parse;

    exit:
        return retval;
    }

} // cu_module_get_function




/*!
 *  \related    cuModuleGetGlobal
 *  \brief      obtain the host-side pointer of a global CUDA symbol
 */
namespace cu_module_get_global {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSHandle_CUDA_Module_ptr module_handle;
        POSHandle_CUDA_Var_ptr var_handle;
        POSHandleManager<POSHandle_CUDA_Module>* hm_module;
        POSHandleManager<POSHandle_CUDA_Var>* hm_var;
        POSClient_CUDA *client;
        
        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 8)){
            POS_WARN(
                "parse(cu_module_get_function): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 8
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_module = client->handle_managers[kPOS_ResourceTypeId_CUDA_Module];
        hm_var = client->handle_managers[kPOS_ResourceTypeId_CUDA_Var];
        POS_CHECK_POINTER(hm_module);
        POS_CHECK_POINTER(hm_var);

        // obtain target handle
        if(likely(
            hm_module->latest_used_handle->is_client_addr_in_range((void*)pos_api_param_value(wqe, 0, uint64_t))
        )){
            module_handle = hm_module->latest_used_handle;
        } else {
            if(unlikely(
                POS_SUCCESS != hm_module->get_handle_by_client_addr(
                    /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
                    /* handle */&module_handle
                )
            )){
                POS_WARN(
                    "parse(cu_module_get_global): failed to find module with client address %p",
                    pos_api_param_value(wqe, 0, uint64_t)
                );
                retval = POS_FAILED_NOT_EXIST;
                goto exit;
            }
        }

        // allocate a new var within the manager
        retval = hm_var->allocate_mocked_resource(
            /* handle */ &var_handle,
            /* related_handles */ std::map<uint64_t, std::vector<POSHandle_ptr>>({{ 
                /* id */ kPOS_ResourceTypeId_CUDA_Module, 
                /* handles */ std::vector<POSHandle_ptr>({module_handle}) 
            }}),
            /* size */ sizeof(CUdeviceptr),
            /* expected_addr */ pos_api_param_value(wqe, 1, uint64_t)
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN("parse(cu_module_get_global): failed to allocate mocked var within the CUDA var handler manager");
            goto exit;
        } else {
            POS_DEBUG(
                "parse(cu_module_get_global): allocate mocked var within the CUDA var handler manager: addr(%p), size(%lu), module_server_addr(%p)",
                var_handle->client_addr, var_handle->size, module_handle->server_addr
            )
        }

        // record the name of the var in the handle
        var_handle->name = std::make_unique<char[]>(pos_api_param_size(wqe, 3));
        POS_CHECK_POINTER(var_handle->name.get());
        strcpy(var_handle->name.get(), pos_api_param_addr(wqe, 3));

        // record the related handle to QE
        wqe->record_handle(
            /* id */ kPOS_ResourceTypeId_CUDA_Var, 
            /* handle_view */ POSHandleView_t(
                /* handle */ var_handle,
                /* dir */ kPOS_Edge_Direction_Create
            )
        );

        // allocate the var handle in the dag
        retval = client->dag.allocate_handle(var_handle);
        if(unlikely(retval != POS_SUCCESS)){
            goto exit;
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

        // mark this sync call can be returned after parsing
        wqe->status = kPOS_API_Execute_Status_Return_After_Parse;

    exit:
        return retval;
    }

} // namespace cu_module_get_global




/*!
 *  \related    cuDevicePrimaryCtxGetState
 *  \brief      obtain the state of the primary context
 */
namespace cu_device_primary_ctx_get_state {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        uint64_t i;
        POSClient_CUDA *client;
        POSHandle_CUDA_Device_ptr device_handle;
        POSHandleManager<POSHandle_CUDA_Device>* hm_device;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 1)){
            POS_WARN(
                "parse(cu_module_load_data): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 1
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_device = client->handle_managers[kPOS_ResourceTypeId_CUDA_Device];
        POS_CHECK_POINTER(hm_device);
        
        // find out the involved device
        retval = hm_device->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, int),
            /* handle */ &device_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cu_device_primary_ctx_get_state): no device was founded: client_addr(%d)",
                (uint64_t)pos_api_param_value(wqe, 0, int)
            );
            goto exit;
        }
        wqe->record_handle(kPOS_ResourceTypeId_CUDA_Device, POSHandleView_t(device_handle, kPOS_Edge_Direction_In));

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cu_device_primary_ctx_get_state




} // namespace rt_functions