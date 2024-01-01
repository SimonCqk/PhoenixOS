#pragma once

#include <iostream>

#include "pos/include/common.h"
#include "pos/include/utils/bipartite_graph.h"
#include "pos/include/dag.h"

#include "pos/cuda_impl/handle.h"
#include "pos/cuda_impl/runtime.h"
#include "pos/cuda_impl/client.h"
#include "pos/cuda_impl/api_context.h"
#include "pos/cuda_impl/utils/fatbin.h"

namespace rt_functions {


/*!
 *  \related    cudaMalloc
 *  \brief      allocate a memory area
 */
namespace cuda_malloc {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        T_POSTransport *transport;
        POSClient_CUDA *client;
        POSHandle_CUDA_Memory_ptr memory_handle;
        POSHandleManager<POSHandle_CUDA_Device>* hm_device;
        POSHandleManager<POSHandle_CUDA_Memory>* hm_memory;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        transport = (T_POSTransport*)(wqe->transport);
        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(transport);
        POS_CHECK_POINTER(client);

    #if POS_ENABLE_DEBUG_CHECK
        // check whether given parameter is valid
        if(unlikely(wqe->api_cxt->params.size() != 1)){
            POS_WARN(
                "parse(cuda_malloc): failed to parse cuda_malloc, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 1
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_device = client->handle_managers[kPOS_ResourceTypeId_CUDA_Device];
        hm_memory = client->handle_managers[kPOS_ResourceTypeId_CUDA_Memory];
        POS_CHECK_POINTER(hm_device); POS_CHECK_POINTER(hm_device->latest_used_handle);
        POS_CHECK_POINTER(hm_memory);

        // operate on handler manager
        retval = hm_memory->allocate_mocked_resource(
            /* handle */ &memory_handle,
            /* related_handles */ std::map<uint64_t, std::vector<POSHandle_ptr>>({{ 
                /* id */ kPOS_ResourceTypeId_CUDA_Device, 
                /* handles */ std::vector<POSHandle_ptr>({hm_device->latest_used_handle}) 
            }}),
            /* size */ pos_api_param_value(wqe, 0, size_t),
            /* expected_addr */ 0,
            /* state_size */ (uint64_t)pos_api_param_value(wqe, 0, size_t)
        );

        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN("parse(cuda_malloc): failed to allocate mocked resource within the CUDA memory handler manager");
            goto exit;
        }
        
        // record the related handle to QE
        wqe->record_handle(kPOS_ResourceTypeId_CUDA_Memory, POSHandleView_t(memory_handle, kPOS_Edge_Direction_Create));

        // allocate the memory handle in the dag
        retval = client->dag.allocate_handle(memory_handle);
        if(unlikely(retval != POS_SUCCESS)){
            goto exit;
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_malloc


/*!
 *  \related    cudaLaunchKernel
 *  \brief      launch a user-define computation kernel
 */
namespace cuda_launch_kernel {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS, tmp_retval;
        POSClient_CUDA *client;
        POSHandle_CUDA_Function_ptr function_handle;
        POSHandle_CUDA_Stream_ptr stream_handle;
        POSHandle_CUDA_Memory_ptr memory_handle;
        uint64_t i;
        void *args, *arg_addr, *arg_value;
        POSHandleManager<POSHandle_CUDA_Function>* hm_function;
        POSHandleManager<POSHandle_CUDA_Stream>* hm_stream;
        POSHandleManager<POSHandle_CUDA_Memory>* hm_memory;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 6)){
            POS_WARN(
                "parse(cuda_launch_kernel): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 6
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        // obtain handle managers of function, stream and memory
        hm_function = client->handle_managers[kPOS_ResourceTypeId_CUDA_Function];
        POS_CHECK_POINTER(hm_function);
        hm_stream = client->handle_managers[kPOS_ResourceTypeId_CUDA_Stream];
        POS_CHECK_POINTER(hm_stream);
        hm_memory = client->handle_managers[kPOS_ResourceTypeId_CUDA_Memory];
        POS_CHECK_POINTER(hm_memory);

        // find out the involved function
        retval = hm_function->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &function_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_launch_kernel): no function was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
            goto exit;
        }
        wqe->record_handle(kPOS_ResourceTypeId_CUDA_Function, POSHandleView_t(function_handle, kPOS_Edge_Direction_In));

        // find out the involved stream
        retval = hm_stream->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 5, uint64_t),
            /* handle */ &stream_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_launch_kernel): no stream was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 5, uint64_t)
            );
            goto exit;
        }
        wqe->record_handle(kPOS_ResourceTypeId_CUDA_Stream, POSHandleView_t(stream_handle, kPOS_Edge_Direction_In));

        // the 3rd parameter of the API call contains parameter to launch the kernel
        args = pos_api_param_addr(wqe, 3);
        POS_CHECK_POINTER(args);

        // [Cricket Adapt] skip the metadata used by cricket
        args += (sizeof(size_t) + sizeof(uint16_t) * function_handle->nb_params);
        
        /*!
         *  \brief  find out all involved memory
         *  \note   use a cache to optimize this part
         */
        for(i=0; i<function_handle->nb_params; i++){
            arg_addr = args + function_handle->param_offsets[i];
            POS_CHECK_POINTER(arg_addr);

            // the argument stores here MIGHT be a client-side address, we would give it a shot below
            arg_value = *((void**)arg_addr);

            /*!
             *  \warning    this could be buggy here: what if an non-pointer value exactly 
             *              equals to an client-side memory address?
             */
            tmp_retval = hm_memory->get_handle_by_client_addr(
                /* client_addr */ arg_value,
                /* handle */ &memory_handle
            );
            if(likely(tmp_retval == POS_SUCCESS)){
                /*!
                 *  \note   we found out the memory handle here, but we can't replace the client-side address
                 *          to the server-side address right here, as the memory might hasn't been physically 
                 *          allocated yet, we must leave this procedure in the worker launching function :-(
                 */
                wqe->record_handle(
                    /* id */ kPOS_ResourceTypeId_CUDA_Memory,
                    /* handle_view */ POSHandleView_t(
                        /* handle_ */ memory_handle,
                        /* dir_ */ kPOS_Edge_Direction_InOut,
                        /* host_value_s */ nullptr,
                        /* host_value_size_ */ 0,
                        /* param_index_ */ i
                    )
                );
                hm_memory->record_modified_handle(memory_handle);
            }
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

        goto exit;

        typedef struct __dim3 { uint32_t x; uint32_t y; uint32_t z; } __dim3_t;
        POS_DEBUG(
            "parse(cuda_launch_kernel): function(%s), stream(%p), grid_dim(%u,%u,%u), block_dim(%u,%u,%u), SM_size(%lu), #buffer(%lu)",
            function_handle->name.get(), stream_handle->server_addr,
            ((__dim3_t*)pos_api_param_addr(wqe, 1))->x,
            ((__dim3_t*)pos_api_param_addr(wqe, 1))->y,
            ((__dim3_t*)pos_api_param_addr(wqe, 1))->z,
            ((__dim3_t*)pos_api_param_addr(wqe, 2))->x,
            ((__dim3_t*)pos_api_param_addr(wqe, 2))->y,
            ((__dim3_t*)pos_api_param_addr(wqe, 2))->z,
            pos_api_param_value(wqe, 4, size_t),
            wqe->handle_view_map[kPOS_ResourceTypeId_CUDA_Memory]->size()
        );

    exit:
        return retval;
    }

} // namespace cuda_launch_kernel




/*!
 *  \related    cudaMemcpy (Host to Device)
 *  \brief      copy memory buffer from host to device
 */
namespace cuda_memcpy_h2d {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS, tmp_retval;

        POSClient_CUDA *client;
        POSHandle_CUDA_Memory_ptr memory_handle;
        POSHandleManager<POSHandle_CUDA_Memory>* hm_memory;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 2)){
            POS_WARN(
                "parse(cuda_memcpy_h2d): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 2
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_memory = client->handle_managers[kPOS_ResourceTypeId_CUDA_Memory];
        POS_CHECK_POINTER(hm_memory);

        // try obtain the destination memory handle
        retval = hm_memory->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &memory_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_h2d): no memory was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Memory, 
                POSHandleView_t(memory_handle, kPOS_Edge_Direction_Out, 0)
            );
            hm_memory->record_modified_handle(memory_handle);
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_memcpy_h2d



/*!
 *  \related    cudaMemcpy (Device to Host)
 *  \brief      copy memory buffer from device to host
 */
namespace cuda_memcpy_d2h {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS, tmp_retval;

        POSClient_CUDA *client;
        POSHandle_CUDA_Memory_ptr memory_handle;
        POSHandleManager<POSHandle_CUDA_Memory>* hm_memory;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 2)){
            POS_WARN(
                "parse(cuda_memcpy_d2h): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 2
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_memory = client->handle_managers[kPOS_ResourceTypeId_CUDA_Memory];
        POS_CHECK_POINTER(hm_memory);

        // try obtain the source memory handle
        retval = hm_memory->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &memory_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_d2h): no memory was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Memory, 
                POSHandleView_t(memory_handle, kPOS_Edge_Direction_In, 0)
            );
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_memcpy_d2h




/*!
 *  \related    cudaMemcpy (Device to Device)
 *  \brief      copy memory buffer from device to device
 */
namespace cuda_memcpy_d2d {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS, tmp_retval;
        POSClient_CUDA *client;
        POSHandle_CUDA_Memory_ptr dst_memory_handle, src_memory_handle;
        POSHandleManager<POSHandle_CUDA_Memory>* hm_memory;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 3)){
            POS_WARN(
                "parse(cuda_memcpy_d2d): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 3
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_memory = client->handle_managers[kPOS_ResourceTypeId_CUDA_Memory];
        POS_CHECK_POINTER(hm_memory);

        // try obtain the destination memory handle
        retval = hm_memory->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &dst_memory_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_d2d): no destination memory was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
            goto exit;
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Memory, 
                POSHandleView_t(dst_memory_handle, kPOS_Edge_Direction_Out, 0)
            );
            hm_memory->record_modified_handle(dst_memory_handle);
        }

        // try obtain the source memory handles
        retval = hm_memory->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 1, uint64_t),
            /* handle */ &src_memory_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_d2d): no source memory was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
            goto exit;
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Memory,
                POSHandleView_t(src_memory_handle, kPOS_Edge_Direction_In, 1)
            );
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_memcpy_d2d




/*!
 *  \related    cudaMemcpyAsync (Host to Device)
 *  \brief      async copy memory buffer from host to device
 */
namespace cuda_memcpy_h2d_async {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS, tmp_retval;

        POSClient_CUDA *client;
        POSHandle_CUDA_Memory_ptr memory_handle;
        POSHandle_CUDA_Stream_ptr stream_handle;
        POSHandleManager<POSHandle_CUDA_Memory>* hm_memory;
        POSHandleManager<POSHandle_CUDA_Stream>* hm_stream;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 3)){
            POS_WARN(
                "parse(cuda_memcpy_h2d_async): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 3
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_memory = client->handle_managers[kPOS_ResourceTypeId_CUDA_Memory];
        POS_CHECK_POINTER(hm_memory);
        hm_stream = client->handle_managers[kPOS_ResourceTypeId_CUDA_Stream];
        POS_CHECK_POINTER(hm_stream);

        // try obtain the destination memory handle
        retval = hm_memory->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &memory_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_h2d_async): no memory was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
            goto exit;
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Memory,
                POSHandleView_t(memory_handle, kPOS_Edge_Direction_Out, 0)
            );
            hm_memory->record_modified_handle(memory_handle);
        }

        // try obtain the stream handle
        retval = hm_stream->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 2, uint64_t),
            /* handle */ &stream_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_h2d_async): no stream was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 2, uint64_t)
            );
            goto exit;
        } else {
            wqe->record_handle(kPOS_ResourceTypeId_CUDA_Stream, POSHandleView_t(stream_handle, kPOS_Edge_Direction_In));
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_memcpy_h2d_async




/*!
 *  \related    cudaMemcpyAsync (Device to Host)
 *  \brief      async copy memory buffer from device to host
 */
namespace cuda_memcpy_d2h_async {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS, tmp_retval;

        POSClient_CUDA *client;
        POSHandle_CUDA_Memory_ptr memory_handle;
        POSHandle_CUDA_Stream_ptr stream_handle;
        POSHandleManager<POSHandle_CUDA_Memory>* hm_memory;
        POSHandleManager<POSHandle_CUDA_Stream>* hm_stream;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 3)){
            POS_WARN(
                "parse(cuda_memcpy_d2h_async): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 3
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_memory = client->handle_managers[kPOS_ResourceTypeId_CUDA_Memory];
        POS_CHECK_POINTER(hm_memory);
        hm_stream = client->handle_managers[kPOS_ResourceTypeId_CUDA_Stream];
        POS_CHECK_POINTER(hm_stream);

        // try obtain the source memory handle
        retval = hm_memory->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &memory_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_d2h_async): no memory was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Memory,
                POSHandleView_t(memory_handle, kPOS_Edge_Direction_In, 0)
            );
        }

        // try obtain the stream handle
        retval = hm_stream->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 2, uint64_t),
            /* handle */ &stream_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_d2h_async): no stream was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 2, uint64_t)
            );
        } else {
            wqe->record_handle(kPOS_ResourceTypeId_CUDA_Stream, POSHandleView_t(stream_handle, kPOS_Edge_Direction_In));
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_memcpy_d2h_async




/*!
 *  \related    cudaMemcpyAsync (Device to Device)
 *  \brief      async copy memory buffer from device to device
 */
namespace cuda_memcpy_d2d_async {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS, tmp_retval;

        POSClient_CUDA *client;
        POSHandle_CUDA_Memory_ptr dst_memory_handle, src_memory_handle;
        POSHandle_CUDA_Stream_ptr stream_handle;
        POSHandleManager<POSHandle_CUDA_Memory>* hm_memory;
        POSHandleManager<POSHandle_CUDA_Stream>* hm_stream;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 4)){
            POS_WARN(
                "parse(cuda_memcpy_d2d_async): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 4
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_memory = client->handle_managers[kPOS_ResourceTypeId_CUDA_Memory];
        POS_CHECK_POINTER(hm_memory);
        hm_stream = client->handle_managers[kPOS_ResourceTypeId_CUDA_Stream];
        POS_CHECK_POINTER(hm_stream);

        // try obtain the destination memory handle
        retval = hm_memory->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &dst_memory_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_d2d_async): no destination memory was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
            goto exit;
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Memory,
                POSHandleView_t(dst_memory_handle, kPOS_Edge_Direction_Out)
            );
            hm_memory->record_modified_handle(dst_memory_handle);
        }

        // try obtain the source memory handle
        retval = hm_memory->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 1, uint64_t),
            /* handle */ &src_memory_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_d2d_async): no source memory was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
            goto exit;
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Memory,
                POSHandleView_t(src_memory_handle, kPOS_Edge_Direction_In)
            );
        }

        // try obtain the stream handle
        retval = hm_stream->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 3, uint64_t),
            /* handle */ &stream_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_memcpy_d2d_async): no stream was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 3, uint64_t)
            );
            goto exit;
        } else {
            wqe->record_handle(kPOS_ResourceTypeId_CUDA_Stream, POSHandleView_t(stream_handle, kPOS_Edge_Direction_In));
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_memcpy_d2d_async



/*!
 *  \related    cudaSetDevice
 *  \brief      specify a CUDA device to use
 */
namespace cuda_set_device {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;

        POSHandleManager_CUDA_Device* hm_device;
        POSHandle_CUDA_Device_ptr device_handle;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 1)){
            POS_WARN(
                "parse(cuda_set_device): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 1
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        // obtain handle managers of device
        hm_device = client->handle_managers[kPOS_ResourceTypeId_CUDA_Device];
        POS_CHECK_POINTER(hm_device);

        // find out the involved device
        retval = hm_device->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, int),
            /* handle */ &device_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_set_device): no device was founded: client_addr(%d)",
                (uint64_t)pos_api_param_value(wqe, 0, int)
            );
            goto exit;
        }
        wqe->record_handle(kPOS_ResourceTypeId_CUDA_Device, POSHandleView_t(device_handle, kPOS_Edge_Direction_In));
        hm_device->latest_used_handle = device_handle;

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_set_device



/*!
 *  \related    cudaGetLastError
 *  \brief      obtain the latest error within the CUDA context
 */
namespace cuda_get_last_error {
    // parser function
    POS_RT_FUNC_PARSER(){
        return POS_SUCCESS;
    }
} // namespace cuda_get_last_error



/*!
 *  \related    cudaGetErrorString
 *  \brief      obtain the error string from the CUDA context
 */
namespace cuda_get_error_string {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;
        
        client = (POSClient_CUDA*)(wqe->client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 1)){
            POS_WARN(
                "parse(cuda_get_error_string): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 1
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:   
        return retval;
    }

} // namespace cuda_get_error_string




/*!
 *  \related    cudaGetDeviceCount
 *  \brief      obtain the number of devices
 */
namespace cuda_get_device_count {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;
        uint64_t nb_handles;
        int nb_handles_int;

        POSHandleManager_CUDA_Device* hm_device;
        
        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // obtain handle managers of device
        hm_device = client->handle_managers[kPOS_ResourceTypeId_CUDA_Device];
        POS_CHECK_POINTER(hm_device);

        nb_handles = hm_device->get_nb_handles();
        nb_handles_int = (int)nb_handles;

        POS_CHECK_POINTER(wqe->api_cxt->ret_data);
        memcpy(wqe->api_cxt->ret_data, &nb_handles_int, sizeof(int));

        wqe->status = kPOS_API_Execute_Status_Return_After_Parse;

        return retval;
    }
} // namespace cuda_get_device_count




/*!
 *  \related    cudaGetDeviceProperties
 *  \brief      obtain the properties of specified device
 */
namespace cuda_get_device_properties {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;

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
                "parse(cuda_memcpy_d2d_async): failed to parse, given %lu params, %lu expected",
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
                "parse(cuda_set_device): no device was founded: client_addr(%d)",
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

} // namespace cuda_get_device_properties




/*!
 *  \related    cudaGetDevice
 *  \brief      returns which device is currently being used
 */
namespace cuda_get_device {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;

        POSHandleManager_CUDA_Device* hm_device;
        
        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // obtain handle managers of device
        hm_device = client->handle_managers[kPOS_ResourceTypeId_CUDA_Device];
        POS_CHECK_POINTER(hm_device);

        POS_CHECK_POINTER(wqe->api_cxt->ret_data);
        memcpy(wqe->api_cxt->ret_data, &(hm_device->latest_used_handle->device_id), sizeof(int));

        // the api is finish, one can directly return
        wqe->status = kPOS_API_Execute_Status_Return_After_Parse;

    exit:
        return retval;
    }

} // namespace cuda_get_device




/*!
 *  \related    cudaStreamSynchronize
 *  \brief      sync a specified stream
 */
namespace cuda_stream_synchronize {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;
        POSHandle_CUDA_Stream_ptr stream_handle;
        POSHandleManager<POSHandle_CUDA_Stream>* hm_stream;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 1)){
            POS_WARN(
                "parse(cuda_stream_synchronize): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 1
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_stream = client->handle_managers[kPOS_ResourceTypeId_CUDA_Stream];
        POS_CHECK_POINTER(hm_stream);

        // try obtain the source memory handle
        retval = hm_stream->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &stream_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_stream_synchronize): no stream was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Stream, 
                POSHandleView_t(stream_handle, kPOS_Edge_Direction_In, 0)
            );
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);
    
    exit:
        return retval;
    }

} // namespace cuda_stream_synchronize




/*!
 *  \related    cudaStreamIsCapturing
 *  \brief      obtain the stream's capturing state
 */
namespace cuda_stream_is_capturing {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;
        POSHandle_CUDA_Stream_ptr stream_handle;
        POSHandleManager<POSHandle_CUDA_Stream>* hm_stream;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 1)){
            POS_WARN(
                "parse(cuda_stream_is_capturing): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 1
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_stream = client->handle_managers[kPOS_ResourceTypeId_CUDA_Stream];
        POS_CHECK_POINTER(hm_stream);

        // try obtain the source memory handle
        retval = hm_stream->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, uint64_t),
            /* handle */ &stream_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_stream_is_capturing): no stream was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, uint64_t)
            );
        } else {
            wqe->record_handle(
                kPOS_ResourceTypeId_CUDA_Stream, 
                POSHandleView_t(stream_handle, kPOS_Edge_Direction_In, 0)
            );
        }

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_stream_is_capturing




/*!
 *  \related    cuda_event_create_with_flags
 *  \brief      create cudaEvent_t with flags
 */
namespace cuda_event_create_with_flags {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;
        POSHandle_CUDA_Event_ptr event_handle;
        POSHandleManager<POSHandle_CUDA_Event>* hm_event;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 1)){
            POS_WARN(
                "parse(cuda_event_create_with_flags): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 1
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_event = client->handle_managers[kPOS_ResourceTypeId_CUDA_Event];
        POS_CHECK_POINTER(hm_event);

        // operate on handler manager
        retval = hm_event->allocate_mocked_resource(
            /* handle */ &event_handle,
            /* related_handles */ std::map<uint64_t, std::vector<POSHandle_ptr>>()
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN("parse(cuda_event_create_with_flags): failed to allocate mocked resource within the CUDA event handler manager");
            memset(wqe->api_cxt->ret_data, 0, sizeof(cudaEvent_t));
            goto exit;
        } else {
            memcpy(wqe->api_cxt->ret_data, &(event_handle->client_addr), sizeof(cudaEvent_t));
        }
        
        // record the related handle to QE
        wqe->record_handle(kPOS_ResourceTypeId_CUDA_Event, POSHandleView_t(event_handle, kPOS_Edge_Direction_Create));

        // allocate the event handle in the dag
        retval = client->dag.allocate_handle(event_handle);
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

} // namespace cuda_event_create_with_flags




/*!
 *  \related    cuda_event_destory
 *  \brief      destory a CUDA event
 */
namespace cuda_event_destory {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;
        POSHandle_CUDA_Event_ptr event_handle;
        POSHandleManager<POSHandle_CUDA_Event>* hm_event;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 1)){
            POS_WARN(
                "parse(cublas_set_math_mode): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 1
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_event = client->handle_managers[kPOS_ResourceTypeId_CUDA_Event];
        POS_CHECK_POINTER(hm_event);

        // operate on handler manager
        retval = hm_event->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0,  cudaEvent_t),
            /* handle */ &event_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_event_destory): no CUDA event was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, cudaEvent_t)
            );
            goto exit;
        }
        wqe->record_handle(
            kPOS_ResourceTypeId_CUDA_Event, POSHandleView_t(event_handle, kPOS_Edge_Direction_Delete)
        );

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_event_destory




/*!
 *  \related    cuda_event_record
 *  \brief      record a CUDA event
 */
namespace cuda_event_record {
    // parser function
    POS_RT_FUNC_PARSER(){
        pos_retval_t retval = POS_SUCCESS;
        POSClient_CUDA *client;
        POSHandle_CUDA_Event_ptr event_handle;
        POSHandle_CUDA_Stream_ptr stream_handle;
        POSHandleManager<POSHandle_CUDA_Event>* hm_event;
        POSHandleManager<POSHandle_CUDA_Stream>* hm_stream;

        POS_CHECK_POINTER(wqe);
        POS_CHECK_POINTER(ws);

        client = (POSClient_CUDA*)(wqe->client);
        POS_CHECK_POINTER(client);

        // check whether given parameter is valid
    #if POS_ENABLE_DEBUG_CHECK
        if(unlikely(wqe->api_cxt->params.size() != 2)){
            POS_WARN(
                "parse(cublas_set_math_mode): failed to parse, given %lu params, %lu expected",
                wqe->api_cxt->params.size(), 2
            );
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
    #endif

        hm_event = client->handle_managers[kPOS_ResourceTypeId_CUDA_Event];
        POS_CHECK_POINTER(hm_event);
        hm_stream = client->handle_managers[kPOS_ResourceTypeId_CUDA_Stream];
        POS_CHECK_POINTER(hm_stream);

        // operate on handler manager
        retval = hm_event->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 0, cudaEvent_t),
            /* handle */ &event_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_event_record): no CUDA event was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 0, cudaEvent_t)
            );
            goto exit;
        }
        wqe->record_handle(
            kPOS_ResourceTypeId_CUDA_Event, POSHandleView_t(event_handle, kPOS_Edge_Direction_Out)
        );
        hm_event->record_modified_handle(event_handle);

        retval = hm_stream->get_handle_by_client_addr(
            /* client_addr */ (void*)pos_api_param_value(wqe, 1, cudaStream_t),
            /* handle */ &stream_handle
        );
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN(
                "parse(cuda_event_record): no CUDA stream was founded: client_addr(%p)",
                (void*)pos_api_param_value(wqe, 1, cudaStream_t)
            );
            goto exit;
        }
        wqe->record_handle(
            kPOS_ResourceTypeId_CUDA_Stream, POSHandleView_t(stream_handle, kPOS_Edge_Direction_In)
        );

        // launch the op to the dag
        retval = client->dag.launch_op(wqe);

    exit:
        return retval;
    }

} // namespace cuda_event_record




/*!
 *  \related    template_cuda
 *  \brief      template_cuda
 */
namespace template_cuda {
    // parser function
    POS_RT_FUNC_PARSER(){
        return POS_FAILED_NOT_IMPLEMENTED;
    }

} // namespace template_cuda


} // namespace rt_functions
