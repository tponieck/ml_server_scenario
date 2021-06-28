/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include "ze_api.h"
#include "ze_info/offline_compiler.hpp"

#include "ze_info/simple_run.hpp"
#include "ze_info/zenon.hpp"
#include "ze_info/client.hpp"
#include "ze_info/utils.hpp"



void run_mt( int queries, int qps, int pool_size, bool multi_ccs, bool fixed_dist, bool warm_up, bool log )
{
    client cli( queries, qps, pool_size, multi_ccs, fixed_dist, warm_up, log );
    cli.run_all();
    std::cout << "\nDone...\n";
}