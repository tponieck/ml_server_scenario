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
extern float compute_bound_kernel_multiplier;
extern short number_of_threads;

struct start_end_time {
    high_resolution_clock::time_point start_time;
    high_resolution_clock::time_point end_time;
};

class client
{
private:
    int queries, qps, zenon_pool_size;
    std::vector<std::chrono::milliseconds> dist;
    std::vector<double> results;
    std::vector<double> gpu_time;
    std::mutex mtx;
    bool logging;
    bool fixed_distribution;
    bool warm_up;
    server serv;
    std::vector<gpu_results> gpu_results_vec;
    std::vector<uint64_t> total_gpu_time;
    std::vector<start_end_time> start_end_time_vec;

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
    client(int _queries, int _qps, int _zenon_pool_size, bool multi_ccs, bool fixed_dist, bool _warm_up, bool log = false) :
        serv(_zenon_pool_size, multi_ccs, log)
    {
        queries = _queries;
        qps = _qps;
        zenon_pool_size = _zenon_pool_size;
        dist.resize(queries);
        results.resize(queries);
        warm_up = _warm_up;
        logging = log;
        fixed_distribution = fixed_dist;
        gpu_results_vec.resize(queries);
        start_end_time_vec.resize(queries);
        create_distribution();
    }

    void run_all()
    {
        if (logging)
            print_dist();

        // Warmup
        if (warm_up)
            run_single(0);

        std::thread** thread_pool = new std::thread * [queries];
        for (int i = 0; i < queries; i++)
        {
            thread_pool[i] = new std::thread(&client::run_single, this, i);
            std::this_thread::sleep_for(dist[i]);
        }

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
        start_end_time_vec[qid] = { start_time, end_time };
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
        uint64_t kernels_starts = gpu_results_vec.at(0).kernels_start_time;
        uint64_t kernels_ends = 0;
        for (int i = 0; i < gpu_results_vec.size(); i++) {
            if (kernels_ends < gpu_results_vec.at(i).kernels_end_time)
                kernels_ends = gpu_results_vec.at(i).kernels_end_time;
        }
        uint64_t gpu_max = *std::max_element(total_exec_time.begin(), total_exec_time.end()) / 1000;
        uint64_t gpu_min = *std::min_element(total_exec_time.begin(), total_exec_time.end()) / 1000;
        double gpu_avg_v = avg_u(total_exec_time) / 1000;

        total_gpu_time = get_total_gpu_time_vec(gpu_results_vec);
        uint64_t total_gpu_max = *std::max_element(total_gpu_time.begin(), total_gpu_time.end()) / 1000; //1st kernel start -> last kernel end max
        uint64_t total_gpu_min = *std::min_element(total_gpu_time.begin(), total_gpu_time.end()) / 1000; //1st kernel start -> last kernel end min
        double total_gpu_avg = avg_u(total_gpu_time) / 1000; //1st kernel start -> last kernel end avg
        std::cout << "\nTime from 1st kernel start to last kernel end\t" << (kernels_ends - kernels_starts) / 1000 << " us \n\n";
        std::cout << "Total kernels time: Min: " << gpu_min << " us\t\t Max: " << gpu_max << " us \t\t Avg: " << gpu_avg_v << " us \n";
        std::cout << "Total GPU time:     Min: " << total_gpu_min << " us\t\t Max: " << total_gpu_max << " us \t\t Avg: " << total_gpu_avg << " us \n";

    }

    double avg(std::vector<double> const& v)
    {
        return 1.0 * std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    }

    double avg_u(std::vector<uint64_t> const& v)
    {
        return std::accumulate(v.begin(), v.end(), 0) / v.size();
    }

    std::vector<uint64_t> get_total_gpu_time_vec(std::vector<gpu_results> const& v)
    {
        std::vector<uint64_t> total_gpu_time_vec;
        for (int i = 0; i < v.size(); i++) {
            total_gpu_time_vec.push_back(v.at(i).gpu_time);
        }
        return total_gpu_time_vec;
    }

    high_resolution_clock::time_point find_end_time(std::vector<start_end_time> start_end_time_vec) {
        high_resolution_clock::time_point end_time = start_end_time_vec[0].start_time;
        for (int i = 0; i < start_end_time_vec.size(); i++) {
            if (end_time < start_end_time_vec[i].end_time) {
                end_time = start_end_time_vec[i].end_time;
            }
        }
        return end_time;
    }

    void save_result(double total_time) {
        std::string filename = "./results.csv";
        struct stat buffer;
        if (!stat(filename.c_str(), &buffer) == 0) {
            std::ofstream outfile;
            outfile.open(filename, std::ofstream::out | std::ofstream::app);
            outfile << "threads/kernel" << "," << "memory used by memory bound kernel" << "," << "compute bound kernel time multiplier" << ", " << "number of consumers" << ", " << "number of queries" << ", " << "queries per second" << ", " << "total time [us]" << "\n";
            outfile.close();
        }
        std::ofstream outfile;
        outfile.open(filename, std::ofstream::out | std::ofstream::app);
        outfile << number_of_threads << "," << "memory" << "," << compute_bound_kernel_multiplier << "," << zenon_pool_size << "," << queries << "," << qps << "," << total_time << "\n";
        outfile.close();
    }

    void print_results()
    {
        double max = *std::max_element(results.begin(), results.end());
        double min = *std::min_element(results.begin(), results.end());
        high_resolution_clock::time_point end_time = find_end_time(start_end_time_vec);
        std::chrono::duration<double, std::micro> total_time = end_time - start_end_time_vec[0].start_time;
        save_result(total_time.count());
        double avg_v = avg(results);
        if (profiling)
            print_profiling();
        std::cout << "CPU:                Min: " << min << " us \t Max: " << max << " us \t Avg: " << avg_v << " us \n";
    }
};

#endif