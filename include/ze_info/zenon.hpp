/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef ZENON_HPP
#define ZENON_HPP



#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include "ze_api.h"
#include "ze_info/offline_compiler.hpp"
#include "ze_info/utils.hpp"

class zenon
{
public:
    zenon(std::vector<uint8_t>* in, std::vector<uint8_t>* out);
    zenon(bool log = false);
    zenon(int _id, bool multi_ccs_enable, bool _log = false) : zenon(_log)
    {
        multi_ccs = multi_ccs_enable;
        id = _id;
        log = _log;
    }
    ~zenon();
    void create_module(const std::string& cl_file_path = "module.cl");
    void allocate_buffers();
    void set_input(std::vector<uint8_t>&in) { input = &in; };
    void set_output(std::vector<uint8_t>& out) { output = &out;};
    std::vector<uint8_t>* get_input() { return input; };
    std::vector<uint8_t>* get_output() { return output; };
    void create_cmd_list();
    void submit_kernel_to_cmd_list(ze_kernel_handle_t& kernel, void* input, void* output);
    int run(uint32_t id);
    void init();
    int get_id() { return id; };
    int get_ccs_id() { return ccs_id; };

private:
	static bool ze_initalized;
    bool log;
    bool multi_ccs = true;
    void* input_buffer = nullptr, *output_buffer = nullptr, *im_buf1 = nullptr, *im_buf2 = nullptr ;

    std::vector<uint8_t>* input;
    std::vector<uint8_t>* output;
    int id, ccs_id;
    ze_result_t result = ZE_RESULT_SUCCESS;
    uint32_t number_of_drivers = 0;
    static std::vector<ze_driver_handle_t> drivers;
    static ze_driver_handle_t driver;

    static uint32_t number_of_devices;
    static std::vector<ze_device_handle_t> devices;
    static ze_device_handle_t device;
    static ze_context_desc_t context_descriptor;
    static ze_context_handle_t context;
    static uint32_t computeQueueGroupOrdinal;
    ze_module_desc_t module_descriptor = {};
    ze_module_handle_t module = nullptr;
    ze_kernel_desc_t kernel_descriptor = {};
    ze_kernel_handle_t kernel = nullptr;
    ze_kernel_handle_t heavy_kernel = nullptr;
    ze_device_mem_alloc_desc_t memory_descriptor = {};
    ze_command_list_desc_t command_list_descriptor = {};
    ze_command_list_handle_t command_list = nullptr;
    ze_group_count_t group_count = {};
    ze_command_queue_desc_t command_queue_descriptor = {};
    ze_command_queue_handle_t command_queue;
    static uint32_t command_queue_count, zenon_cntr;
    

   
};

#endif
