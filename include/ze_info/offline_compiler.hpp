/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef SIMPLE_RUN
#define SIMPLE_RUN

#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include "ocloc_api.h"




        std::vector<uint8_t> generate_spirv(const std::string& cl_file_path, const std::string& build_options);

	
#endif