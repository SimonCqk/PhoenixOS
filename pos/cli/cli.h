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

#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "pos/include/common.h"
#include "pos/include/log.h"
#include "pos/include/oob.h"
#include "pos/include/oob/ckpt_predump.h"
#include "pos/include/oob/trace.h"


/*!
 *  \brief  type of the command
 */
enum pos_cli_arg : int {
    kPOS_CliAction_Unknown = 0,
    /* ============ actions ============ */
    /*!
     *  \brief  print help message
     */
    kPOS_CliAction_Help,

    /*!
     *  \brief  pre-dump state of an XPU process, but don't stop the execution
     *  \param  pid     [Required] PID of the process to be migrated
     *  \param  dir     [Required] path to the checkpoint file
     */
    kPOS_CliAction_PreDump,

    /*!
     *  \brief  final dump state of an XPU process, and stop the execution
     *  \param  pid     [Required] PID of the process to be migrated
     *  \param  dir     [Required] path to the checkpoint file
     */
    kPOS_CliAction_Dump,

    /*!
     *  \brief  restore the state of an XPU process, and continue the execution
     *  \param  dir     [Required] path to the checkpoint file
     */
    kPOS_CliAction_Restore,

    /*!
     *  \brief  tracing the resource dependecies during execution
     *  \param  dir         [Required] path to the trace file
     *  \param  subaction   [Required] start or stop the trace mode
     */
    kPOS_CliAction_TraceResource,

    /*!
     *  \brief  start certain PhOS components
     *  \param  target      [Required] target to start (options: daemon)
     */
    kPOS_CliAction_Start,

    /*!
     *  \brief  migrate context of a XPU process to a new process
     *  \param  pid     [Required] PID of the process to be migrated
     *  \param  oip     [Required] out-of-band control plane IP of the remote host
     *  \param  oport   [Required] out-of-band control plane UDP port of the remote host
     *  \param  dip     [Required] dataplane IP of the remote host
     *  \param  dport   [Required] dataplane port of the remote host
     */
    kPOS_CliAction_Migrate,

    /*!
     *  \brief  preserve XPU resources for fast restoring
     *  \param  rid                 [Required]  type id of the resource to be preserved
     *  \param  path_kernel_meta    [Optional]  path to the file that contains kernel parsing metadata, required when
     *                                          preserving XPU modules
     */
    kPOS_CliAction_Preserve,
    kPOS_CliAction_PLACEHOLDER,
    
    /* ============ metadatas ============ */
    // subaction of the action
    kPOS_CliMeta_SubAction,
    // generic operation target
    kPOS_CliMeta_Target,
    // target process id
    kPOS_CliMeta_Pid,
    // file path for checkpoint / trace, etc.
    kPOS_CliMeta_Dir,
    // oob ip
    kPOS_CliMeta_Dip,
    // oob port
    kPOS_CliMeta_Dport,
    // file path to the kernel metadata
    kPOS_CliMeta_KernelMeta,
    kPOS_CliMeta_PLACEHOLDER
};

typedef pos_cli_arg     pos_cli_action;
typedef pos_cli_arg     pos_cli_meta;

/*!
 *  \brief  convert action name from action type 
 */
static std::string pos_cli_action_name(pos_cli_arg action_type){
    switch (action_type)
    {
    case kPOS_CliAction_Help:
        return "help";

    case kPOS_CliAction_Migrate:
        return "migrate";

    case kPOS_CliAction_Preserve:
        return "preserve";
    
    default:
        return "unknown";
    }
}

typedef struct pos_cli_ckpt_metas {
    uint64_t pid;
    char ckpt_dir[oob_functions::cli_ckpt_predump::kCkptFilePathMaxLen];
} pos_cli_ckpt_metas_t;

typedef struct pos_cli_start_metas {
    char target_name[512];
} pos_cli_start_metas_t;

typedef struct pos_cli_trace_resource_metas {
    oob_functions::cli_trace_resource::trace_action action;
    char trace_dir[oob_functions::cli_trace_resource::kTraceFilePathMaxLen];
} pos_cli_trace_resource_metas_t;


typedef struct pos_cli_migrate_metas {
    uint64_t pid;
    in_addr_t dip;
    uint32_t dport;
} pos_cli_migrate_metas_t;


/*!
 *  \brief  descriptor of command line options
 */
typedef struct pos_cli_options {
    // type of the command
    pos_cli_action action_type;

    // raw option map
    std::map<pos_cli_meta, std::string> _raw_metas;
    inline void record_raw(pos_cli_meta key, std::string value){
        _raw_metas[key] = value;
    }

    POSOobClient *local_oob_client;
    POSOobClient *remote_oob_client;

    // metadata of corresponding cli option
    union {
        pos_cli_ckpt_metas_t ckpt;
        pos_cli_migrate_metas_t migrate;
        pos_cli_trace_resource_metas_t trace_resource;
        pos_cli_start_metas_t start;
    } metas;

    pos_cli_options() : local_oob_client(nullptr), remote_oob_client(nullptr), action_type(kPOS_CliAction_Unknown) {}
} pos_cli_options_t;


/*!
 *  \brief  checking rule for verifying CLI argument
 */
typedef struct pos_cli_meta_check_rule {
    pos_cli_meta meta_type;
    std::string meta_name;
    std::string meta_desp;
    using cast_func_t = pos_retval_t(*)(pos_cli_options_t&, std::string&);
    cast_func_t cast_func;
    bool is_required;
} pos_arg_check_rule_t;


/*!
 *  \brief  validate correctness of arguments
 *  \param  clio    all cli infomations
 *  \param  rules   checking rules
 */
static void validate_and_cast_args(pos_cli_options_t &clio, std::vector<pos_arg_check_rule_t> &&rules){
    for(auto& rule : rules){
        if(clio._raw_metas.count(rule.meta_type) == 0){
            if(rule.is_required){
                POS_ERROR(
                    "%s action requires option '%s'(%s)",
                    pos_cli_action_name(clio.action_type).c_str(),
                    rule.meta_name.c_str(),
                    rule.meta_desp.c_str()
                );
            } else {
                continue;
            }
        }

        if(unlikely(POS_SUCCESS != rule.cast_func(clio, clio._raw_metas[rule.meta_type]))){
            POS_ERROR("invalid format for '%s' option", rule.meta_name.c_str());
        }
    }
}

pos_retval_t handle_predump(pos_cli_options_t &clio);
pos_retval_t handle_dump(pos_cli_options_t &clio);
pos_retval_t handle_migrate(pos_cli_options_t &clio);
pos_retval_t handle_trace(pos_cli_options_t &clio);
pos_retval_t handle_restore(pos_cli_options_t &clio);
pos_retval_t handle_start(pos_cli_options_t &clio);
