/*
 * Copyright (C) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "ze_info/ze_info.hpp"
#include "ze_info/capabilities.hpp"
#include "ze_info/text_formatter.hpp"
#include "ze_info/simple_run.hpp"
#include "ze_api.h"

#include <vector>




void check_caps()
{
	const auto result = zeInit(0);
	
	LOG_DEBUG << "Drivers initialized";

	const std::vector<ze_driver_handle_t> drivers = get_drivers();
	const std::vector<DriverCapabilities> capabilities =
		get_drivers_capabilities(drivers);
	LOG_DEBUG << "Level Zero info:\n"
		<< drivers_capabilities_to_text(capabilities, 0);



}

void print_help()
{
	std::cout << std::endl;
	std::cout << "--check_caps - print device and driver info"<< std::endl;
}

int main(int argc, const char **argv) {

	if (argc <= 1)
	{
		std::cout << "no arguments passed";
		print_help();
		return 1;
	}

	for (int i = 0; i < argc; i++)
		std::cout << argv[i] << " ";
	std::cout << std::endl;

	int queries = 100;
	int qps = 20;
	int consumers_count = 8;
	bool logging = false;
	bool multi_ccs = true;
	bool fixed_dist = false;

	for( int i = 1; i < argc ; i++)
	{
		if (!strcmp(argv[i],"--check_caps"))
		{
			check_caps();
		}
		else if (!strcmp(argv[i], "--run_simple"))
		{
			run_simple();
		}
		else if (!strcmp(argv[i], "--single_ccs"))
		{
			multi_ccs = false;
		}
		else if (!strcmp(argv[i], "--q"))
		{
			i++;
			queries = atoi(argv[i]);
		}
		else if (!strcmp(argv[i], "--qps"))
		{
			i++;
			qps = atoi(argv[i]);
		}
		else if (!strcmp(argv[i], "--s"))
		{
			i++;
			consumers_count = atoi(argv[i]);
		}
		else if (!strcmp(argv[i], "--log"))
		{
			logging = true;
		}
		else if (!strcmp(argv[i], "--fixed_dist"))
		{
			fixed_dist = true;
		}
		else if (!strcmp(argv[i], "--run_simple_zenon"))
		{
			run_simple_zenon();
		}
		else if (!strcmp(argv[i], "--run_mt"))
		{
			run_mt(queries,qps, consumers_count, multi_ccs,fixed_dist, logging);
		}
		else
		{
			std::cout << "Uknown argument: " << argv[i];
			print_help();
			return 1;
		}
	}
	return 0;

}
