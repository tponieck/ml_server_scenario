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
#include <cstring>




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

extern bool verbose;

void print_help()
{
    std::cout << std::endl;
    std::cout << "--check_caps      - print device and driver info" << std::endl;
    std::cout << "--single_ccs      - using single css" << std::endl;
    std::cout << "--q               - number of queries" << std::endl;
    std::cout << "--s               - number of consumers" << std::endl;
    std::cout << "--qps             - average number of queries per second (poisson distribution)" << std::endl;
    std::cout << "--fixed_dist      - use fixed distribution list from file dist.txt" << std::endl;
    std::cout << "--log             - enable logging" << std::endl;
    std::cout << "--disable_wu      - disable warm up" << std::endl;
    std::cout << "--verbose         - verbose" << std::endl;
}

int main(int argc, const char **argv) {

    for (int i = 0; i < argc; i++)
        std::cout << argv[i] << " ";
    std::cout << std::endl;

    int queries = 100;
    int qps = 20;
    int consumers_count = 8;
    bool logging = false;
    bool multi_ccs = true;
    bool fixed_dist = false;
    bool warm_up = true;
    verbose = false;


    for( int i = 1; i < argc ; i++)
    {
        if (!strcmp(argv[i],"--check_caps"))
        {
            check_caps();
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
        else if( !strcmp( argv[ i ], "--disable_wu" ) )
        {
            warm_up = false;
        }
        else if( !strcmp( argv[ i ], "--verbose" ) )
        {
            verbose = true;
        }
        /*else if (!strcmp(argv[i], "--run_mt"))
        {
            run_mt( queries, qps, consumers_count, multi_ccs, fixed_dist, warm_up, logging );
        }*/
        else
        {
            std::cout << "Unknown argument: " << argv[i];
            print_help();
            return 1;
        }
    }

    run_mt( queries, qps, consumers_count, multi_ccs, fixed_dist, warm_up, logging );

    return 0;

}
