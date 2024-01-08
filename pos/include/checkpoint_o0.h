#pragma once

#include <iostream>
#include <vector>

#include <stdint.h>

#include "pos/include/common.h"
#include "pos/include/log.h"

class POSCheckpointSlot;
using POSCheckpointSlot_ptr = std::shared_ptr<POSCheckpointSlot>;

class POSCheckpointBag {
 public:
    POSCheckpointBag(uint64_t state_size) {}
    ~POSCheckpointBag() = default;
    
    /*!
     *  \brief  allocate a new checkpoint slot inside this bag
     *  \param  version     version (i.e., dag index) of this checkpoint
     *  \param  ptr         pointer to the new checkpoint slot
     *  \return POS_SUCCESS for successfully allocation
     */
    pos_retval_t apply_new_checkpoint(uint64_t version, POSCheckpointSlot_ptr* ptr){
        return POS_FAILED_NOT_IMPLEMENTED;
    }

    /*!
     *  \brief  obtain overall memory consumption of this checkpoint bag
     *  \return overall memory consumption of this checkpoint bag
     */
    inline uint64_t get_memory_consumption(){
        return 0;
    }

 private:
};