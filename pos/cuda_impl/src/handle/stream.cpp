#include <iostream>
#include <string>
#include <cstdlib>

#include <sys/resource.h>
#include <stdint.h>
#include <cuda.h>
#include <cuda_runtime_api.h>

#include "pos/include/common.h"
#include "pos/include/log.h"
#include "pos/include/handle.h"
#include "pos/cuda_impl/handle.h"
#include "pos/cuda_impl/handle/stream.h"
#include "pos/cuda_impl/proto/stream.pb.h"


POSHandle_CUDA_Stream::POSHandle_CUDA_Stream(void *client_addr_, size_t size_, void* hm, pos_u64id_t id_, size_t state_size_)
    : POSHandle_CUDA(client_addr_, size_, hm, id_, state_size_)
{
    this->resource_type_id = kPOS_ResourceTypeId_CUDA_Stream;
}


POSHandle_CUDA_Stream::POSHandle_CUDA_Stream(void* hm) : POSHandle_CUDA(hm)
{
    this->resource_type_id = kPOS_ResourceTypeId_CUDA_Stream;
}


POSHandle_CUDA_Stream::POSHandle_CUDA_Stream(size_t size_, void* hm, pos_u64id_t id_, size_t state_size_)
    : POSHandle_CUDA(size_, hm, id_, state_size_)
{
    POS_ERROR_C_DETAIL("shouldn't be called");
}


pos_retval_t POSHandle_CUDA_Stream::__add(uint64_t version_id, uint64_t stream_id){
    return POS_SUCCESS;
}


pos_retval_t POSHandle_CUDA_Stream::__commit(
    uint64_t version_id, uint64_t stream_id, bool from_cache, bool is_sync, std::string ckpt_dir
){
    // TODO: currently we not supporting graph capture mode for stream, should be supported later
    return this->__persist(nullptr, ckpt_dir, stream_id);
}


pos_retval_t POSHandle_CUDA_Stream::__generate_protobuf_binary(google::protobuf::Message** binary, google::protobuf::Message** base_binary){
    pos_retval_t retval = POS_SUCCESS;
    pos_protobuf::Bin_POSHandle_CUDA_Stream *cuda_stream_binary;

    POS_CHECK_POINTER(binary);
    POS_CHECK_POINTER(base_binary);

    cuda_stream_binary = new pos_protobuf::Bin_POSHandle_CUDA_Stream();
    POS_CHECK_POINTER(cuda_stream_binary);

    *binary = reinterpret_cast<google::protobuf::Message*>(cuda_stream_binary);
    POS_CHECK_POINTER(*binary);
    *base_binary = cuda_stream_binary->mutable_base();
    POS_CHECK_POINTER(*base_binary);

    // serialize handle specific fields
    /* currently nothing */

    return retval;
}


pos_retval_t POSHandle_CUDA_Stream::__restore(){
    pos_retval_t retval = POS_SUCCESS;
    cudaError_t cuda_rt_res;
    cudaStream_t stream_addr;

    if((cuda_rt_res = cudaStreamCreate(&stream_addr)) != cudaSuccess){
        POS_WARN_C("cudaStreamCreate failed: %d", cuda_rt_res);
        retval = POS_FAILED_DRIVER;
        goto exit;
    }
    this->set_server_addr((void*)(stream_addr));
    this->mark_status(kPOS_HandleStatus_Active);

exit:
    return retval;
}


pos_retval_t POSHandleManager_CUDA_Stream::init(std::map<uint64_t, std::vector<POSHandle*>> related_handles){
    pos_retval_t retval = POS_SUCCESS;

    POSHandle_CUDA_Stream *stream_handle;

    POS_ASSERT(related_handles.count(kPOS_ResourceTypeId_CUDA_Context) == 1);
    std::vector<POSHandle*> &context_handles = related_handles[kPOS_ResourceTypeId_CUDA_Context];

    /*!
     *  \note   we won't use the default stream, and we will create a new non-default stream
     *          within the worker thread, so that we can achieve overlap checkpointing
     *  \todo   we bind this default stream to all upstream context, actually this isn't 
     *          correct, we need to creat one default stream for each context
     *          we would implement this once we have context system
     */
    if(unlikely(POS_SUCCESS != this->allocate_mocked_resource(
        /* handle */ &stream_handle,
        /* related_handle */ std::map<uint64_t, std::vector<POSHandle*>>({
            { kPOS_ResourceTypeId_CUDA_Context, context_handles }
        }),
        /* size */ sizeof(CUstream),
        /* use_expected_addr */ true,
        /* expected_addr */ 0
    ))){
        POS_ERROR_C_DETAIL("failed to allocate mocked CUDA stream in the manager");
    }

    // record in the manager
    this->_handles.push_back(stream_handle);
    this->latest_used_handle = this->_handles[0];
    this->default_handle = this->_handles[0];

exit:
    return retval;
}


pos_retval_t POSHandleManager_CUDA_Stream::allocate_mocked_resource(
    POSHandle_CUDA_Stream** handle,
    std::map</* type */ uint64_t, std::vector<POSHandle*>> related_handles,
    size_t size,
    bool use_expected_addr,
    uint64_t expected_addr,
    uint64_t state_size
){
    pos_retval_t retval = POS_SUCCESS;
    POSHandle *context_handle;
    uint64_t i;

    POS_CHECK_POINTER(handle);

    POS_ASSERT(related_handles.count(kPOS_ResourceTypeId_CUDA_Context) == 1);

    // TODO: set to == 1 when we have context control
    POS_ASSERT(related_handles[kPOS_ResourceTypeId_CUDA_Context].size() > 1);

    POS_CHECK_POINTER(context_handle = related_handles[kPOS_ResourceTypeId_CUDA_Context][0]);

    retval = this->__allocate_mocked_resource(
        /* handle */ handle,
        /* size */ size,
        /* use_expected_addr */ use_expected_addr,
        /* expected_addr */ expected_addr,
        /* state_size */ state_size
    );
    if(unlikely(retval != POS_SUCCESS)){
        POS_WARN_C("failed to allocate mocked CUDA stream in the manager");
        goto exit;
    }

    POS_CHECK_POINTER(*handle);

    // TODO: set to "(*handle)->record_parent_handle(context_handle);" when we have context control
    for(i=0; i<related_handles[kPOS_ResourceTypeId_CUDA_Context].size(); i++){
        POS_CHECK_POINTER(context_handle = related_handles[kPOS_ResourceTypeId_CUDA_Context][i]);
        (*handle)->record_parent_handle(context_handle);
    }

exit:
    return retval;
}


pos_retval_t POSHandleManager_CUDA_Stream::preserve_pooled_handles(uint64_t amount){
    return POS_SUCCESS;
}


pos_retval_t POSHandleManager_CUDA_Stream::try_restore_from_pool(POSHandle_CUDA_Stream* handle){
    return POS_FAILED;
}