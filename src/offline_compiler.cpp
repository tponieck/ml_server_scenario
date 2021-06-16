/*
 * Copyright (C) 2021 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */
#include <iostream>
#include <cassert>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "ze_info/utils.hpp"
#include "ze_info/offline_compiler.hpp"



    std::vector<uint8_t> generate_spirv(const std::string& cl_file_path, const std::string& build_options)
    {

        static const std::string src_file = cl_file_path;
        static const std::string spv_file = "kernel";
        static const std::string log_file = "stdout.log";

        std::string source = load_text_file(src_file);

        // TODO: Device detection is required
        const std::string device = "skl";

        std::vector<const char*> args = {
            "ocloc",   "compile",        "-device",   device.c_str(),
            "-file",   src_file.c_str(), "-options",  build_options.c_str(),
            "-output", spv_file.c_str(), "-spv_only", "-output_no_suffix",
        };

        const uint32_t num_sources = 1;
        const uint8_t* data_sources[] = {
            reinterpret_cast<const uint8_t*>(source.c_str()) };
        const uint64_t len_sources[] = { source.length() + 1 };
        const char* name_sources[] = { src_file.c_str() };

        const uint32_t num_includes = 0;
        const uint8_t** data_includes = nullptr;
        const uint64_t* len_includes = nullptr;
        const char** name_includes = nullptr;

        uint32_t num_outputs = 0;
        uint8_t** data_outputs = nullptr;
        uint64_t* len_outputs = nullptr;
        char** name_outputs = nullptr;


        int status = oclocInvoke(
            args.size(), args.data(),                                  // ocloc args
            num_sources, data_sources, len_sources, name_sources,      // source code
            num_includes, data_includes, len_includes, name_includes,  // includes
            &num_outputs, &data_outputs, &len_outputs, &name_outputs); // outputs

        if (status != 0) {
            auto* logp =
                std::find_if(name_outputs, name_outputs + num_outputs,
                    [&](const char* name) { return name == log_file; });

            const auto log_index = logp - name_outputs;
            std::string log(reinterpret_cast<char*>(data_outputs[log_index]),
                len_outputs[log_index]);

            std::cout << "Build log:\n" << log << '\n';
            throw std::runtime_error("Offline compiler failed");
        }

        auto* spvp =
            std::find_if(name_outputs, name_outputs + num_outputs,
                [&](const char* name) { return name == spv_file + ".spv"; });

        const auto spv_index = spvp - name_outputs;
        const uint8_t* spv = data_outputs[spv_index];
        const auto spv_length = len_outputs[spv_index];

        return { spv, spv + spv_length };
    }
