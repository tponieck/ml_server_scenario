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

class client
{
private:
	int queries, qps;
	std::vector<std::chrono::milliseconds> dist;
	std::vector<double> results;
	std::mutex mtx;
	bool logging;
	bool fixed_distribution;
	bool warm_up;
	server serv;

	void create_distribution()
	{
		long long value;
		if (fixed_distribution)
		{
			std::ifstream inputFile("./dist.txt");
			if (inputFile) {
				if (logging)
					std::cout << "Reading dist from file" << std::endl;

				// read the elements in the file into a vector  
				int n = 0;
				while (inputFile >> value) {
					dist[n] = std::chrono::milliseconds(value);
					n++;
				}
				return;
			}
			if (logging)
				std::cout << "Fixed distribution selected, file not found." << std::endl;

		}
		std::random_device rd;
		std::mt19937 gen(rd());
		std::exponential_distribution<> d(qps);
		long long gen_value = d(gen) * 1000;
		for (int n = 0; n < queries; ++n)
            dist[ n ] = std::chrono::milliseconds( gen_value );
		if (fixed_distribution)
		{
			if (logging)
				std::cout << "Generating dist file" << std::endl;
			std::ofstream outfile("./dist.txt");
			for (int n = 0; n < dist.size(); ++n)
				outfile << (long long) dist[n].count()<< " ";

		}
	}

public:
	client(int _queries, int _qps, int zenon_pool_size, bool multi_ccs,bool fixed_dist,bool _warm_up, bool log = false) :
		serv(zenon_pool_size,multi_ccs, log)
	{
		queries = _queries;
		qps = _qps;
		dist.resize(queries);
		results.resize(queries);
		warm_up = _warm_up;
		logging = log;
		fixed_distribution = fixed_dist;
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
		std::thread** thread_pool = new std::thread*[queries];
		for (int i = 0; i < queries; i++)
		{
			thread_pool[i] = new std::thread(&client::run_single, this, i);
			//std::this_thread::sleep_for(dist[i]);
		}

		for (int i = 0; i < queries; i++)
		{
			thread_pool[i]->join();
			delete thread_pool[i];
		}
		print_results();
	}

	void run_single(int qid)
	{
		high_resolution_clock::time_point start_time = high_resolution_clock::now();
		try {
			serv.query_sample(qid);
		}
		catch (std::exception ex)
		{
			std::cout << ex.what();
		}
		high_resolution_clock::time_point end_time = high_resolution_clock::now();
		std::chrono::duration<double, std::milli> ms = end_time - start_time;
		if (logging)
			std::cout << "thread:" << qid << " duration: " << ms.count() << std::endl;
		results[qid] =  ms.count();
	}

	void print_dist()
	{
		for (int n = 0; n < queries; ++n)
			std::cout << dist[n].count() << ";";
		std::cout << std::endl;
	}

	double avg(std::vector<double> const& v) {
		return 1.0 * std::accumulate(v.begin(), v.end(), 0.0) / v.size();
	}

	void print_results()
	{
		double max = *std::max_element(results.begin(), results.end());
		double min = *std::min_element(results.begin(), results.end());
		double avg_v = avg(results);

		std::cout << "Max: " << max << std::endl << "Min: " << min << std::endl << "Avg: " << avg_v << std::endl;
	}




};


#endif