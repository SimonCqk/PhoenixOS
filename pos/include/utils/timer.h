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

#include <algorithm>
#include <chrono>
#include <thread>

#include "pos/include/common.h"

#define POS_TSC_FREQ 0 // mock, delete later

#define POS_TSC_TO_MSEC(tick) \
    (double)(tick) * (double)1000.0f / (double) POS_TSC_FREQ 

#define POS_USEC_TO_TSC(usec) \
    (double)(usec) * (double) POS_TSC_FREQ / (double)1000000.0f 

#define POS_MESC_TO_TSC(mesc) \
    (double)(mesc) / (double)1000.0f * (double) POS_TSC_FREQ 


/*!
 *  \brief  HPET-based timer
 *  \note   we provide HPET-based timer mainly for measuring the frequency of TSC
 *          more accurately, note that HPET is expensive to call
 */
class POSUtilHpetTimer {
 public:
    POSUtilHpetTimer(){}
    ~POSUtilHpetTimer() = default;

    /*!
     *  \brief  start timing
     */
    inline void start(){
        this->_start_time = std::chrono::high_resolution_clock::now();
    }

    /*!
     *  \brief  stop timing and obtain duration (ns)
     *  \return duration (ns)
     */
    inline double stop_get_ns() const {
        return static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - this->_start_time
            ).count()
        );
    }

    /*!
     *  \brief  stop timing and obtain duration (us)
     *  \return duration (us)
     */
    inline double stop_get_us() const {
        return stop_get_ns() / 1e3;
    }

    /*!
     *  \brief  stop timing and obtain duration (ms)
     *  \return duration (ms)
     */
    inline double stop_get_ms() const {
        return stop_get_ns() / 1e6;
    }

    /*!
     *  \brief  stop timing and obtain duration (s)
     *  \return duration (s)
     */
    inline double stop_get_s() const {
        return stop_get_ns() / 1e9;
    }

 private:
    // start time of the timing
    std::chrono::time_point<std::chrono::high_resolution_clock> _start_time;
};


/*!
 *  \brief  TSC-based timer
 */
class POSUtilTscTimer {
 public:
    POSUtilTscTimer(){ 
        this->update_tsc_freq(); 
    }
    ~POSUtilTscTimer() = default;

    /*!
     *  \brief  ontain TSC tick
     *  \return TSC tick
     */
    static inline uint64_t get_tsc(){
        uint64_t a, d;
        __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
        return (d << 32) | a;
    }

    /*!
     *  \brief  update the TSC frequency
     */
    inline void update_tsc_freq(){
        POSUtilHpetTimer hpet;
        uint64_t sum = 5;

        hpet.start();

        // Do not change this loop! The hardcoded value below depends on this loop
        // and prevents it from being optimized out.
        const uint64_t rdtsc_start = this->get_tsc();
        for (uint64_t i = 0; i < 1000000; i++) {
            sum += i + (sum + i) * (i % sum);
        }
        POS_ASSERT(sum == 13580802877818827968ull);
        const uint64_t rdtsc_cycles = this->get_tsc() - rdtsc_start;

        this->_tsc_freq = rdtsc_cycles * 1.0 / hpet.stop_get_ns();
    }

    /*!
     *  \brief  calculate from tick range to duration (ms)
     *  \param  e_tick  end tick
     *  \param  s_tick  start tick
     *  \return duration (ms)
     */
    inline double tick_range_to_ms(uint64_t e_tick, uint64_t s_tick){
        return (double)(e_tick - s_tick) / (double) this->_tsc_freq * (double)1000.0f;
    }

    /*!
     *  \brief  calculate from tick range to duration (us)
     *  \param  e_tick  end tick
     *  \param  s_tick  start tick
     *  \return duration (us)
     */
    inline double tick_range_to_us(uint64_t e_tick, uint64_t s_tick){
        return (double)(e_tick - s_tick) / (double) this->_tsc_freq * (double)1000000.0f;
    }

    /*!
     *  \brief  calculate from duration (ms) to tick steps
     *  \param  duration  duration (ms)
     *  \return tick steps
     */
    inline double ms_to_tick(uint64_t duration){
        return (double)(duration) / (double)1000.0f * (double) this->_tsc_freq;
    }

    /*!
     *  \brief  calculate from duration (us) to tick steps
     *  \param  duration  duration (us)
     *  \return tick steps
     */
    inline double us_to_tick(uint64_t duration){
        return (double)(duration) / (double)1000000.0f * (double) this->_tsc_freq;
    }

 private:
    // frequency of TSC register (Ghz)
    double _tsc_freq;
};


class POSUtilTimestamp {
 public:
    /*!
     *  \brief  ontain TSC tick
     *  \return TSC tick
     */
    static inline uint64_t get_tsc(){
        uint64_t a, d;
        __asm__ volatile("rdtsc" : "=a"(a), "=d"(d));
        return (d << 32) | a;
    }

    /*!
     *  \brief  delay specified microsecond
     *  \param  duration_us specified microsecond
     */
    static inline void delay_us(uint32_t microseconds){
        std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
    }
};