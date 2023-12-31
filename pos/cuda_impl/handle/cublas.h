#pragma once

#include <iostream>

#include <stdint.h>
#include <cuda.h>
#include <cuda_runtime_api.h>
#include <cublas_v2.h>
#include <cublas_api.h>

#include "pos/common.h"
#include "pos/handle.h"
#include "pos/cuda_impl/handle.h"


/* ==================================== Handle Definition ==================================== */


/*!
 *  \brief  handle for cuBLAS context
 */
class POSHandle_cuBLAS_Context : public POSHandle {
 public:
    /*!
     *  \brief  constructor
     *  \param  client_addr     the mocked client-side address of the handle
     *  \param  size            size of the resources represented by this handle
     *  \param  state_size      size of resource state behind this handle  
     */
    POSHandle_cuBLAS_Context(void *client_addr_, size_t size_, uint64_t state_size=0)
        : POSHandle(client_addr_, size_, state_size)
    {
        this->resource_type_id = kPOS_ResourceTypeId_cuBLAS_Context;
    }

    /*!
     *  \note   never called, just for passing compilation
     */
    POSHandle_cuBLAS_Context(size_t size_, uint64_t state_size=0) : POSHandle(size_, state_size){
        POS_ERROR_C_DETAIL("shouldn't be called");
    }

    /*!
     *  \brief  obtain the resource name begind this handle
     *  \return resource name begind this handle
     */
    std::string get_resource_name(){ return std::string("cuBLAS Context"); }
};
using POSHandle_cuBLAS_Context_ptr = std::shared_ptr<POSHandle_cuBLAS_Context>;


/* ================================ End of Handle Definition ================================= */


/*!
 *  \brief   manager for handles of POSHandle_cuBLAS_Context
 */
class POSHandleManager_cuBLAS_Context : public POSHandleManager<POSHandle_cuBLAS_Context> {
    /*!
     *  \brief  allocate new mocked cuBLAS context within the manager
     *  \param  handle          pointer to the mocked handle of the newly allocated resource
     *  \param  related_handles all related handles for helping allocate the mocked resource
     *                          (note: these related handles might be other types)
     *  \param  size            size of the newly allocated resource
     *  \param  expected_addr   the expected mock addr to allocate the resource (optional)
     *  \param  state_size      size of resource state behind this handle  
     *  \return POS_FAILED_DRAIN for run out of virtual address space; 
     *          POS_SUCCESS for successfully allocation
     */
    pos_retval_t allocate_mocked_resource(
        std::shared_ptr<POSHandle_cuBLAS_Context>* handle,
        std::map</* type */ uint64_t, std::vector<POSHandle_ptr>> related_handles,
        size_t size=kPOS_HandleDefaultSize,
        uint64_t expected_addr = 0,
        uint64_t state_size = 0
    ) override {
        pos_retval_t retval = POS_SUCCESS;
        POSHandle_CUDA_Context_ptr context_handle;
        POS_CHECK_POINTER(handle);

        // obtain the context to allocate buffer
        if(unlikely(related_handles.count(kPOS_ResourceTypeId_CUDA_Context) == 0)){
            POS_WARN_C("no binded context provided to created the CUDA module");
            retval = POS_FAILED_INVALID_INPUT;
            goto exit;
        }
        context_handle = std::dynamic_pointer_cast<POSHandle_CUDA_Context>
                        (related_handles[kPOS_ResourceTypeId_CUDA_Context][0]);

        retval = this->__allocate_mocked_resource(handle, size, expected_addr, state_size);
        if(unlikely(retval != POS_SUCCESS)){
            POS_WARN_C("failed to allocate mocked cuBLAS context in the manager");
            goto exit;
        }
        (*handle)->record_parent_handle(context_handle);

    exit:
        return retval;
    }
};


/* ================================ Handle Manager Definition ================================ */

/* ============================= End of Handle Manager Definition ============================ */