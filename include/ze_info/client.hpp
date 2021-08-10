/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef CLIENT_HPP
#define CLIENT_HPP
#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <random>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <numeric>
#include <algorithm>
#include  "ze_info/server.hpp"


using namespace std::chrono;
extern bool profiling;

class client
{
private:
    int queries, qps;
    std::vector<std::chrono::milliseconds> dist;
    std::vector<double> results;
    std::vector<double> gpu_time;
    std::mutex mtx;
    bool logging;
    bool fixed_distribution;
    bool warm_up;
    server serv;
    std::vector<gpu_results> gpu_results_vec;
    std::chrono::duration<double, std::micro> all_kernels_time ;

    void create_distribution()
    {
        long long value;
        std::string filename = "./dist_" + std::to_string(queries) + ".txt";
        if (fixed_distribution)
        {
            std::ifstream inputFile(filename);
            if (inputFile) {
                if (logging)
                    std::cout << "Reading dist from file" << std::endl;

                bool file_correct = true;

                // read the elements in the file into a vector  
                int n = 0;
                while (inputFile >> value) {
                    if (n >= queries)
                    {
                        std::cout << "File is too long." << std::endl;
                        file_correct = false;
                        break;
                    }
                    dist[n] = std::chrono::milliseconds(value);
                    n++;
                }
                if (file_correct)
                    return;
            }
            if (logging)
                std::cout << "Fixed distribution selected, correct file not found." << std::endl;

        }
        std::random_device rd;
        std::mt19937 gen(rd());
        std::exponential_distribution<> d(qps);
        for (int n = 0; n < queries; ++n)
            dist[n] = std::chrono::milliseconds((long long)(d(gen) * 1000));
        if (fixed_distribution)
        {
            if (logging)
                std::cout << "Generating dist file" << std::endl;
            std::ofstream outfile(filename);
            for (int n = 0; n < dist.size(); ++n)
                outfile << (long long)dist[n].count() << " ";

        }
    }

public:
    client(int _queries, int _qps, int zenon_pool_size, bool multi_ccs, bool fixed_dist, bool _warm_up, bool log = false) :
        serv(zenon_pool_size, multi_ccs, log)
    {
        queries = _queries;
        qps = _qps;
        dist.resize(queries);
        results.resize(queries);
        warm_up = _warm_up;
        logging = log;
        fixed_distribution = fixed_dist;
        gpu_results_vec.resize(queries);
        create_distribution();
    }

    void run_all()
    {
        if (logging)
            print_dist();

        // Warmup
        if (warm_up)
            run_single(0);

        high_resolution_clock::time_point start_time = high_resolution_clock::now();
        std::thread** thread_pool = new std::thread * [queries];
        for (int i = 0; i < queries; i++)
        {
            thread_pool[i] = new std::thread(&client::run_single, this, i);
            //std::this_thread::sleep_for(dist[i]);
        }
        high_resolution_clock::time_point end_time = high_resolution_clock::now();
        all_kernels_time = end_time - start_time;

        for (int i = 0; i < queries; i++)
        {
            thread_pool[i]->join();
            delete thread_pool[i];
        }
        print_results();
        serv.delete_zenek();
    }

    void run_single(int qid)
    {
        high_resolution_clock::time_point start_time = high_resolution_clock::now();
        try
        {
            gpu_results_vec[qid] = serv.query_sample(qid);
        }
        catch (std::exception ex)
        {
            std::cout << ex.what();
        }
        high_resolution_clock::time_point end_time = high_resolution_clock::now();
        std::chrono::duration<double, std::micro> ms = end_time - start_time;
        if (logging)
            std::cout << "thread:" << qid << " duration: " << ms.count() << std::endl;

        results[qid] = ms.count();
    }

    void print_dist()
    {
        for (int n = 0; n < queries; ++n)
            std::cout << dist[n].count() << ";";
        std::cout << std::endl;
    }

    void print_profiling()
    {
        auto kernels_count = gpu_results_vec.at(0).kernel_time.size();
        std::vector<uint64_t> total_exec_time;

        for (int j = 0; j < kernels_count; j++)
        {
            std::vector<uint64_t> kernel_exec_times;
            for (int i = 0; i < gpu_results_vec.size(); i++)
            {
                kernel_exec_times.push_back(gpu_results_vec.at(i).kernel_time.at(j));
                if (j == 0)
                    total_exec_time.push_back(gpu_results_vec.at(i).execuction_time);
            }
            uint64_t kernel_max = *std::max_element(kernel_exec_times.begin(), kernel_exec_times.end());
            uint64_t kernel_min = *std::min_element(kernel_exec_times.begin(), kernel_exec_times.end());
            double kernel_avg_v = avg_u(kernel_exec_times);
            std::cout << "kernel " << j << " " << gpu_results_vec.at(0).kernel_name.at(j) << ": Min: " << kernel_min << " ns \t" << "Max: " << kernel_max << " ns \t" << "Avg: " << kernel_avg_v << " ns \n";
        }

        uint64_t gpu_max = *std::max_element(total_exec_time.begin(), total_exec_time.end()) / 1000;
        uint64_t gpu_min = *std::min_element(total_exec_time.begin(), total_exec_time.end()) / 1000;
        double gpu_avg_v = avg_u(total_exec_time) / 1000;

        std::cout << "GPU: Min: " << gpu_min << " us \t Max: " << gpu_max << " us \t Avg: " << gpu_avg_v << " us \t" << "Overall time: " << all_kernels_time.count() << " us \n";
    }

    double avg(std::vector<double> const& v)
    {
        return 1.0 * std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }

    double avg_u(std::vector<uint64_t> const& v)
    {
        return std::accumulate(v.begin(), v.end(), 0) / v.size();
    }

    void print_results()
    {
        double max = *std::max_element(results.begin(), results.end());
        double min = *std::min_element(results.begin(), results.end());
        double avg_v = avg(results);
        if (profiling)
            print_profiling();
        std::cout << "CPU: Min: " << min << " us \t Max: " << max << " us \t Avg: " << avg_v << " us \n";

    }


};


#endif