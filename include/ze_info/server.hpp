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

class server
{
public:
    server(int pool_size, bool multi_ccs, bool log = false) :
        log_lock(mtx, std::defer_lock),
        logging(log)
    {
        zenek.resize(pool_size);
        zenek_pool_boost.reserve(pool_size);
        for (int i = 0; i < pool_size; i++)
        {
            zenek[i] = new zenon(i, multi_ccs, log);
            zenek[i]->create_module();
            zenek[i]->allocate_buffers();
            zenek[i]->create_cmd_list();
            zenek_pool_boost.push(zenek[i]);
        }
    }

    gpu_results query_sample(int id)
    {
        zenon* zenek = get_zenon_atomic();
        int zen_id = zenek->get_id();
        std::vector<uint8_t>* in1 = zenek->get_input1();
        std::vector<uint8_t>* in2 = zenek->get_input2();
        std::fill(in1->begin(), in1->end(), id);
        std::fill(in2->begin(), in2->end(), id - 1);
        gpu_results gpu_result = zenek->run(id);
        int ccs_id = zenek->get_ccs_id();
        return_zenon_atomic(zenek);
        log("sample id:", id);
        log("will use zenek no:", zen_id);
        log("with ccs: ", ccs_id);
        return gpu_result;
    }

    void delete_zenek()
    {
        for (int i = 1; i < zenek_pool_size; i++) {
            delete zenek[i];
        }
    }

private:
    bool logging = false;
    std::mutex mtx;
    std::unique_lock<std::mutex> log_lock;
    int zenek_pool_size = 0;
    std::deque<std::shared_ptr<zenon>> zenek_pool;
    std::vector<zenon*> zenek;

    boost::lockfree::queue < zenon*/*, boost::lockfree::capacity<1>*/ > zenek_pool_boost;

    void log(char* msg, int a = 0)
    {
        if (logging)
        {
            log_lock.lock();
            std::cout << msg << a << std::endl;
            log_lock.unlock();
        }
    }

    zenon* get_zenon_atomic()
    {
        zenon* zenek;
        while (!zenek_pool_boost.pop(zenek));
        return zenek;
    }

    void return_zenon_atomic(zenon* zenek)
    {
        zenek_pool_boost.push(zenek);
    }
};

#endif