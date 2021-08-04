/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef SERVER_HPP
#define SERVER_HPP
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <vector>
#include <deque>
#include <mutex>
#include <algorithm>
#include "ze_info/zenon.hpp"
#include <memory>
#include "boost/lockfree/queue.hpp"
#include "tbb/concurrent_queue.h"

class server
{
public:
    server( int pool_size, bool multi_ccs, bool log = false ) :
        log_lock( mtx, std::defer_lock ),
        logging( log )
    {
        zenek = new zenon(0, multi_ccs, log);
        zenek->create_module();
        zenek->allocate_buffers();
        zenek->create_cmd_list();

        zenek_pool_size = pool_size;
        for( int i = 0; i < pool_size; i++ )
        {
            zenek_pool_boost.push( zenek );
        }
    }

    gpu_results query_sample( int id )
    {
        zenon * zenek = get_zenon_atomic();
        int zen_id = zenek->get_id();
        std::vector<uint8_t>* in = zenek->get_input();
        std::vector<uint8_t>* in2 = zenek->get_input2();
        std::fill( in->begin(), in->end(), id );
        std::fill( in2->begin(), in2->end(), id - 1 );
        gpu_results gpu_result = zenek->run( id );
        int ccs_id = zenek->get_ccs_id();
        return_zenon_atomic( zenek );
        log( "sample id:", id );
        log( "will use zenek no:", zen_id );
        log( "with ccs: ", ccs_id );
        return gpu_result;
    }

    void delete_zenek() 
    {
        delete zenek;
    }

private:
    bool logging = false;
    std::mutex mtx;
    std::unique_lock<std::mutex> log_lock;
    int zenek_pool_size;
    std::deque<std::shared_ptr<zenon>> zenek_pool;
    zenon* zenek;

    //tbb::concurrent_queue<std::shared_ptr<zenon>> zenek_pool_tbb;

    boost::lockfree::queue<zenon*> zenek_pool_boost;

    void log( char* msg, int a = 0 )
    {
        if( logging )
        {
            log_lock.lock();
            std::cout << msg << a << std::endl;
            log_lock.unlock();
        }
    }

    zenon * get_zenon_atomic()
    {
        zenon * zenek;
        while( !zenek_pool_boost.pop( zenek ) );
        return zenek;
    }

    void return_zenon_atomic( zenon * zenek )
    {
        zenek_pool_boost.push( zenek );
    }




};

#endif