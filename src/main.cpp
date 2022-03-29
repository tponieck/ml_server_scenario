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
extern bool profiling;
extern bool single_thread;
extern bool resnet;
extern bool disable_blitter;
extern float compute_bound_kernel_multiplier;
extern short number_of_threads;
extern short memory_used_by_mem_bound_kernel;
extern int input_size;

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
    std::cout << "--profiling       - gpu kernel stats" << std::endl;
    std::cout << "--resnet          - run resnet 50 simulation" << std::endl;
    std::cout << "--disable_blitter - disable blitter" << std::endl;
    std::cout << "--cbk_mul         - set multiplier of duration cmp_bound_kernel" << std::endl;
    std::cout << "--t               - number of threads" << std::endl;
    std::cout << "--mem             - memory used by memory bound kernel in kB" << std::endl;
    std::cout << "--single_thread   - memory used by memory bound kernel in kB" << std::endl;
    std::cout << "--input_size      - memory bound kernel input size" << std::endl;
}

int main(int argc, const char** argv) {

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
    single_thread = false;
    profiling = false;
    verbose = false;
    resnet = false;
    disable_blitter = false;
    compute_bound_kernel_multiplier = 1.0;
    number_of_threads = 32;
    memory_used_by_mem_bound_kernel = 4;
    input_size = 2048;

    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "--check_caps"))
        {
            check_caps();
        }
        else if (!strcmp(argv[i], "--single_ccs"))
        {
            multi_ccs = false;
        }
        else if( !strcmp( argv[ i ], "--single_thread" ) )
        {
            single_thread = true;
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
        else if (!strcmp(argv[i], "--cbk_mul"))
        {
            i++;
            compute_bound_kernel_multiplier = atof(argv[i]);
        }
        else if (!strcmp(argv[i], "--mem"))
        {
            i++;
            memory_used_by_mem_bound_kernel = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "--s"))
        {
            i++;
            consumers_count = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "--input size"))
        {
            i++;
            input_size = atoi(argv[i]);
        }
        else if (!strcmp(argv[i], "--log"))
        {
            logging = true;
        }
        else if (!strcmp(argv[i], "--fixed_dist"))
        {
            fixed_dist = true;
        }
        else if (!strcmp(argv[i], "--disable_wu"))
        {
            warm_up = false;
        }
        else if (!strcmp(argv[i], "--verbose"))
        {
            verbose = true;
        }
        else if (!strcmp(argv[i], "--profiling"))
        {
            profiling = true;
        }
        else if (!strcmp(argv[i], "--resnet"))
        {
            resnet = true;
        }
        else if (!strcmp(argv[i], "--disable_blitter"))
        {
            disable_blitter = true;
        }
        else if( !strcmp( argv[ i ], "--t" ) )
        {
            i++;
            number_of_threads = atoi( argv[ i ] );
            if( number_of_threads > 4096 )
            {
                printf( "too high thread number, setting it to 4096\n" );
                number_of_threads = 4096;
            }
            else if( number_of_threads < 2 )
            {
                printf( "too low thread number, setting it to 2\n" );
                number_of_threads = 2;
            }

        }
        else if( !strcmp( argv[ i ], "--input_size" ) )
        {
            i++;
            input_size = atoi( argv[ i ] );
            
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
    if( number_of_threads > input_size )
    {
        printf( "too high thread number, setting it to the same as input_size\n" );
        number_of_threads = input_size;
    }


    run_mt(queries, qps, consumers_count, multi_ccs, fixed_dist, warm_up, logging);

    return 0;

}
