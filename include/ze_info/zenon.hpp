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

#define MAX_EVENTS_COUNT 55

struct gpu_results
{
    std::vector<uint64_t> kernel_time;
    std::vector<std::string>  kernel_name;
    uint64_t execuction_time = 0;
    uint64_t gpu_time;
};

class zenon
{
public:
    zenon(std::vector<uint8_t>* in, std::vector<uint8_t>* in2, std::vector<uint8_t>* out);
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
    void set_input1(std::vector<uint8_t>& in1) { input1 = &in1; };
    void set_input2(std::vector<uint8_t>& in2) { input2 = &in2; };
    void set_output(std::vector<uint8_t>& out) { output = &out; };
    std::vector<uint8_t>* get_input1() { return input1; };
    std::vector<uint8_t>* get_input2() { return input2; };
    std::vector<uint8_t>* get_output() { return output; };
    void create_cmd_list(bool mixed);
    void submit_kernel_to_cmd_list(ze_kernel_handle_t& _kernel, std::vector<void*> input, void* output, ze_event_handle_t output_event, std::vector<ze_event_handle_t*> input_events, uint32_t input_event_count); 
    void submit_kernel_to_cmd_list(ze_kernel_handle_t& _kernel, std::vector<void*> input, void* output, ze_event_handle_t output_event, std::vector<ze_event_handle_t*> input_events, uint32_t input_event_count, int counter);
    gpu_results run(uint32_t id);
    void init();
    int get_id() { return id; };
    int get_ccs_id() { return ccs_id; };

private:
    static bool ze_initalized;
    bool log;
    bool multi_ccs = true;
    void* input1_buffer = nullptr, * input2_buffer = nullptr, * output_buffer = nullptr, * im_buf1 = nullptr, * im_buf2 = nullptr, * im_buf3 = nullptr, * im_buf4 = nullptr, * im_buf5 = nullptr, * im_buf6 = nullptr;
    void* sharedBuffer = nullptr;
    void* sharedBuffer_Resnet = nullptr;

    std::vector<uint8_t>* input1;
    std::vector<uint8_t>* input2;
    std::vector<uint8_t>* output;
    std::vector<uint8_t>* output_resnet;
    gpu_results gpu_result;
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
    static uint32_t computeQueueGroupOrdinal, copyOnlyQueueGroupOrdinal;
    ze_module_desc_t module_descriptor = {};
    ze_module_handle_t module = nullptr;
    ze_kernel_desc_t kernel_descriptor = {};
    ze_kernel_handle_t kernel = nullptr;
    ze_kernel_handle_t heavy_kernel = nullptr;
    ze_kernel_handle_t add_buffers_kernel = nullptr;
    ze_kernel_handle_t mul_buffers_kernel = nullptr;
    ze_kernel_handle_t cmp_bound_kernel = nullptr;
    ze_kernel_handle_t mem_bound_kernel = nullptr;
    ze_kernel_handle_t copy_buffer_kernel = nullptr;
    
    ze_device_mem_alloc_desc_t memory_descriptor = {};
    ze_command_list_desc_t command_list_descriptor = {};
    ze_command_list_handle_t command_list = nullptr;
    ze_group_count_t group_count = {};
    ze_command_queue_desc_t command_queue_descriptor = {};
    ze_command_queue_handle_t command_queue;
    
    ze_command_queue_handle_t input_copy_command_queue;
    ze_command_list_handle_t input_copy_command_list;
    ze_command_queue_desc_t input_copy_command_queue_descriptor = {};
    ze_command_list_desc_t input_copy_command_list_descriptor = {};

    ze_command_queue_handle_t output_copy_command_queue;
    ze_command_list_handle_t output_copy_command_list;
    ze_command_queue_desc_t output_copy_command_queue_descriptor = {};
    ze_command_list_desc_t output_copy_command_list_descriptor = {};

    ze_host_mem_alloc_desc_t hostDesc = { ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC };

    static uint32_t command_queue_count, zenon_cntr;

    ze_event_pool_handle_t event_pool;
    ze_event_handle_t kernel_ts_event[MAX_EVENTS_COUNT];
    ze_kernel_timestamp_result_t kernel_ts_results[MAX_EVENTS_COUNT];
    uint32_t graph_event_count = 0;
    std::vector<std::string> kernel_names;
};

#endif