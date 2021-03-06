/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <ctime>
#include <sstream>
#include <future>
#include <iomanip>
#include <queue>
#include <random>
#include <thread>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "ze_info/offline_compiler.hpp"
#include "ze_info/zenon.hpp"
#include "ze_info/ze_utils.hpp"

extern bool verbose, profiling, resnet, disable_blitte, single_thread;
extern float compute_bound_kernel_multiplier;
extern short number_of_threads;
extern short memory_used_by_mem_bound_kernel;
extern int input_size;
bool verbose = false;
bool profiling = false;
bool single_thread = false;
bool resnet = false;
bool disable_blitter = false;
float compute_bound_kernel_multiplier = 1.0;
short memory_used_by_mem_bound_kernel = 1;
short number_of_threads = 32;
int input_size = 4096;


std::vector <ze_event_handle_t> global_kernel_ts_event;

zenon::zenon(bool _log, bool _multi_ccs)
{
    log = _log;
    multi_ccs = _multi_ccs;
    input1 = new std::vector<uint8_t>( input_size, 0);
    input2 = new std::vector<uint8_t>( input_size, 0);
    output = new std::vector<uint8_t>( input_size, 0);
    mem_input1 = new std::vector<uint8_t>(input_size * memory_used_by_mem_bound_kernel, 0);
    mem_input2 = new std::vector<uint8_t>(input_size * memory_used_by_mem_bound_kernel, 0);
    mem_output = new std::vector<uint8_t>(input_size * memory_used_by_mem_bound_kernel, 0);
    init();
}

zenon::zenon(std::vector<uint8_t>* in1, std::vector<uint8_t>* in2, std::vector<uint8_t>* out)
{
    input1 = in1;
    input2 = in2;
    output = out;
    init();
}

void zenon::init()
{
    if (!ze_initalized)
    {
        if (log)
            std::cout << "Initalization start\n";
        SUCCESS_OR_TERMINATE(zeInit(ZE_INIT_FLAG_GPU_ONLY));
        ze_initalized = true;
        SUCCESS_OR_TERMINATE(zeDriverGet(&number_of_drivers, nullptr));

        if (log)
            std::cout << "Number of drivers: " << number_of_drivers << std::endl;

        drivers.resize(number_of_drivers);
        SUCCESS_OR_TERMINATE(zeDriverGet(&number_of_drivers, drivers.data()));
        driver = drivers[0];
        context_descriptor.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;
        SUCCESS_OR_TERMINATE(zeContextCreate(driver, &context_descriptor, &context));

        SUCCESS_OR_TERMINATE(zeDeviceGet(driver, &number_of_devices, nullptr));

        devices.resize(number_of_devices);
        SUCCESS_OR_TERMINATE(zeDeviceGet(driver, &number_of_devices, devices.data()));
        ze_device_properties_t device_properties;

        if (log)
        {
            std::cout << "number of devices: " << number_of_devices << std::endl;
        }
        device = devices[0];
        for (uint32_t d = 0; d < number_of_devices; ++d) {
            zeDeviceGetProperties(devices[d], &device_properties);
            if (ZE_DEVICE_TYPE_GPU == device_properties.type) {
                if (log)
                {
                    std::cout << "GPU device found:" << std::hex << device_properties.deviceId << std::endl;
                }
                if (!(device_properties.flags & ZE_DEVICE_PROPERTY_FLAG_INTEGRATED))
                {
                    if (log)
                    {
                        std::cout << "Device is discrete!" << std::endl;
                    }
                    device = devices[d];
                    break;
                }
            }
        }
        // Discover all command queue groups
        uint32_t cmdqueueGroupCount = 0;
        zeDeviceGetCommandQueueGroupProperties(device, &cmdqueueGroupCount, nullptr);

        ze_command_queue_group_properties_t* cmdqueueGroupProperties = (ze_command_queue_group_properties_t*)
            malloc(cmdqueueGroupCount * sizeof(ze_command_queue_group_properties_t));
        zeDeviceGetCommandQueueGroupProperties(device, &cmdqueueGroupCount, cmdqueueGroupProperties);

        // Find a command queue type that support compute
        computeQueueGroupOrdinal = cmdqueueGroupCount;
        for (uint32_t i = 0; i < cmdqueueGroupCount; ++i) {
            if (cmdqueueGroupProperties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE) {
                computeQueueGroupOrdinal = i;
            }

            if (!(cmdqueueGroupProperties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE) &&
                (cmdqueueGroupProperties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COPY))
            {
                copyOnlyQueueGroupOrdinal = i;
                break;
            }
        }

        command_queue_count = cmdqueueGroupProperties[computeQueueGroupOrdinal].numQueues;

        //if( !( device_properties.flags & ZE_DEVICE_PROPERTY_FLAG_INTEGRATED ) )
        //    command_queue_count += cmdqueueGroupProperties[ copyOnlyQueueGroupOrdinal ].numQueues;

    }

    command_queue_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
    command_queue_descriptor.ordinal = computeQueueGroupOrdinal;
    command_queue_descriptor.mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS;
    command_queue_descriptor.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
    ccs_id = (zenon_cntr++) % command_queue_count;
    if (log)
        std::cout << "command_queue_count: " << command_queue_count << std::endl;

    if (!multi_ccs)
        ccs_id = 0;
    command_queue_descriptor.index = ccs_id;

    SUCCESS_OR_TERMINATE(zeCommandQueueCreate(context, device, &command_queue_descriptor, &command_queue));

    input_copy_command_queue_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
    input_copy_command_queue_descriptor.pNext = nullptr;
    input_copy_command_queue_descriptor.flags = 0;
    input_copy_command_queue_descriptor.mode = ZE_COMMAND_QUEUE_MODE_DEFAULT;
    input_copy_command_queue_descriptor.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
    input_copy_command_queue_descriptor.ordinal = copyOnlyQueueGroupOrdinal;
    input_copy_command_queue_descriptor.index = 0;
    SUCCESS_OR_TERMINATE(zeCommandQueueCreate(context, device, &input_copy_command_queue_descriptor, &input_copy_command_queue));

    output_copy_command_queue_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
    output_copy_command_queue_descriptor.pNext = nullptr;
    output_copy_command_queue_descriptor.flags = 0;
    output_copy_command_queue_descriptor.mode = ZE_COMMAND_QUEUE_MODE_DEFAULT;
    output_copy_command_queue_descriptor.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
    output_copy_command_queue_descriptor.ordinal = copyOnlyQueueGroupOrdinal;
    output_copy_command_queue_descriptor.index = 0;
    SUCCESS_OR_TERMINATE(zeCommandQueueCreate(context, device, &output_copy_command_queue_descriptor, &output_copy_command_queue));
}
zenon::~zenon()
{
    zenon_cntr--;
    if (!disable_blitter)
        SUCCESS_OR_TERMINATE(zeCommandListDestroy(input_copy_command_list));

    SUCCESS_OR_TERMINATE(zeCommandQueueDestroy(input_copy_command_queue));

    SUCCESS_OR_TERMINATE(zeCommandListDestroy(output_copy_command_list));

    SUCCESS_OR_TERMINATE(zeCommandQueueDestroy(output_copy_command_queue));

    SUCCESS_OR_TERMINATE(zeCommandListDestroy(command_list));

    SUCCESS_OR_TERMINATE(zeMemFree(context, output_buffer));

    SUCCESS_OR_TERMINATE(zeMemFree(context, input1_buffer));

    SUCCESS_OR_TERMINATE(zeMemFree(context, input2_buffer));

    SUCCESS_OR_TERMINATE(zeMemFree(context, mem_output_buffer));

    SUCCESS_OR_TERMINATE(zeMemFree(context, mem_input1_buffer));

    SUCCESS_OR_TERMINATE(zeMemFree(context, mem_input2_buffer));

    SUCCESS_OR_TERMINATE(zeKernelDestroy(kernel));

    SUCCESS_OR_TERMINATE(zeModuleDestroy(module));

    if (ze_initalized && zenon_cntr == 0)
    {
        SUCCESS_OR_TERMINATE(zeEventDestroy(*kernel_ts_event));
        SUCCESS_OR_TERMINATE(zeEventPoolDestroy(event_pool));

        for (uint32_t i = 0; i < command_queue_count; i++)
        {
            SUCCESS_OR_TERMINATE(zeCommandQueueDestroy(command_queue));
            SUCCESS_OR_TERMINATE(zeCommandQueueDestroy(input_copy_command_queue));
            SUCCESS_OR_TERMINATE(zeCommandQueueDestroy(output_copy_command_queue));
        }

        SUCCESS_OR_TERMINATE(zeContextDestroy(context));

        ze_initalized = false;
    }

    delete input1;
    delete input2;
    delete output;
}

void zenon::create_module(const std::string& cl_file_path)
{
    const std::vector<uint8_t> spirv = generate_spirv("module.cl", "");
    module_descriptor.stype = ZE_STRUCTURE_TYPE_MODULE_DESC;
    module_descriptor.format = ZE_MODULE_FORMAT_IL_SPIRV;
    module_descriptor.inputSize = spirv.size();
    module_descriptor.pInputModule = spirv.data();
    SUCCESS_OR_TERMINATE(zeModuleCreate(context, device, &module_descriptor, &module, nullptr));

    kernel_descriptor.stype = ZE_STRUCTURE_TYPE_KERNEL_DESC;
    kernel_descriptor.pKernelName = "copy_buffer";

    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernel_descriptor, &kernel));

    kernel_descriptor.pKernelName = "heavy";
    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernel_descriptor, &heavy_kernel));

    kernel_descriptor.pKernelName = "add_buffers";
    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernel_descriptor, &add_buffers_kernel));

    kernel_descriptor.pKernelName = "mul_buffers";
    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernel_descriptor, &mul_buffers_kernel));

    kernel_descriptor.pKernelName = "cmp_bound_kernel";
    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernel_descriptor, &cmp_bound_kernel));

    kernel_descriptor.pKernelName = "mem_bound_kernel";
    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernel_descriptor, &mem_bound_kernel));

    kernel_descriptor.pKernelName = "set_n_to_output";
    SUCCESS_OR_TERMINATE(zeKernelCreate(module, &kernel_descriptor, &set_n_to_output));
}

void zenon::allocate_buffers()
{
    memory_descriptor.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
    memory_descriptor.ordinal = 0;
    auto alloc_size = sizeof(uint8_t) * input1->size();
    auto alloc_size_mem_buffers = sizeof( uint8_t ) * mem_input1->size();

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size, 1, device, &input1_buffer));

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size, 1, device, &input2_buffer));

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size, 1, device, &im_buf1));

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size_mem_buffers, 1, device, &mem_input1_buffer));

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size_mem_buffers, 1, device, &mem_input2_buffer));

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size_mem_buffers, 1, device, &mem_output_buffer));

        SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size_mem_buffers, 1, device, &mem_output_buffer2));

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size, 1, device, &im_buf2));
    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size, 1, device,
        &im_buf3));
    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size, 1, device, &im_buf4));

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size, 1, device, &im_buf5));

    SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
        alloc_size, 1, device, &im_buf6));

    if (disable_blitter) {
        hostDesc.flags = ZE_HOST_MEM_ALLOC_FLAG_BIAS_UNCACHED;
        SUCCESS_OR_TERMINATE(zeMemAllocShared(context, &memory_descriptor, &hostDesc, alloc_size, 1, device, &output_buffer));
    }
    else {
        SUCCESS_OR_TERMINATE(zeMemAllocDevice(context, &memory_descriptor,
            alloc_size, 1, device, &output_buffer));
    }
}

void zenon::submit_kernel_to_cmd_list(ze_kernel_handle_t& _kernel,
    std::vector<void*> input,
    void* output,
    ze_event_handle_t output_event,
    std::vector<ze_event_handle_t*> input_events,
    uint32_t input_event_count,
    int number_of_threads,
    int input_size)
{
    int param_cnt = 0;
    for (int i = 0; i < input.size(); i++)
    {
        SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(input1_buffer), &input.at(i)));
    }
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(output_buffer), &output));
    SUCCESS_OR_TERMINATE( zeKernelSetArgumentValue( _kernel, param_cnt++, sizeof( int ), &number_of_threads ) );
    SUCCESS_OR_TERMINATE( zeKernelSetArgumentValue( _kernel, param_cnt++, sizeof( int ), &input_size ) );
    SUCCESS_OR_TERMINATE(zeCommandListAppendLaunchKernel(command_list, _kernel, &group_count,
        output_event, input_event_count, input_events.at(0)));
    graph_event_count++;
    if (profiling)
    {
        size_t kernel_name_length = 0;
        SUCCESS_OR_TERMINATE(zeKernelGetName(_kernel, &kernel_name_length, nullptr));
        char* kernel_name = new char[kernel_name_length];
        SUCCESS_OR_TERMINATE(zeKernelGetName(_kernel, &kernel_name_length, kernel_name));
        kernel_names.push_back(kernel_name);
    }
}

void zenon::submit_kernel_to_cmd_list(ze_kernel_handle_t& _kernel,
    std::vector<void*> input,
    void* output,
    ze_event_handle_t output_event,
    std::vector<ze_event_handle_t*> input_events,
    uint32_t input_event_count,
    int time_in_nanoseconds,
    int number_of_threads,
    int input_size)
{
    int param_cnt = 0;
    int counter = 0;

    if (_kernel == cmp_bound_kernel) {
        counter = (int)(time_in_nanoseconds * compute_bound_kernel_multiplier * 0.0114416 - 37.4022);
    }
    else if (_kernel == mem_bound_kernel) {
        counter = (int)(memory_used_by_mem_bound_kernel);
    }
    else if (_kernel == set_n_to_output) {
        counter = time_in_nanoseconds;
    }

    for (int i = 0; i < input.size(); i++)
    {
        SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(input1_buffer), &input.at(i)));
    }
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(output_buffer), &output));

    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(int), &counter));
    SUCCESS_OR_TERMINATE( zeKernelSetArgumentValue( _kernel, param_cnt++, sizeof( int ), &number_of_threads ) );
    SUCCESS_OR_TERMINATE( zeKernelSetArgumentValue( _kernel, param_cnt++, sizeof( int ), &input_size ) );
    SUCCESS_OR_TERMINATE(zeCommandListAppendLaunchKernel(command_list, _kernel, &group_count,
        output_event, input_event_count, input_events.at(0)));
    graph_event_count++;
    if (profiling)
    {
        size_t kernel_name_length = 0;
        SUCCESS_OR_TERMINATE(zeKernelGetName(_kernel, &kernel_name_length, nullptr));
        char* kernel_name = new char[kernel_name_length];
        SUCCESS_OR_TERMINATE(zeKernelGetName(_kernel, &kernel_name_length, kernel_name));
        kernel_names.push_back(kernel_name);
    }
}

void createEventPoolAndEvents(ze_context_handle_t& context,
    ze_device_handle_t& device,
    ze_event_pool_handle_t& eventPool,
    ze_event_pool_flag_t poolFlag,
    uint32_t poolSize,
    ze_event_handle_t* events)
{
    ze_event_pool_desc_t eventPoolDesc;
    ze_event_desc_t eventDesc;
    eventPoolDesc.count = poolSize;
    eventPoolDesc.flags = poolFlag;
    SUCCESS_OR_TERMINATE(zeEventPoolCreate(context, &eventPoolDesc, 1, &device, &eventPool));

    for (uint32_t i = 0; i < poolSize; i++)
    {
        eventDesc.index = i;
        eventDesc.signal = ZE_EVENT_SCOPE_FLAG_DEVICE;
        eventDesc.wait = ZE_EVENT_SCOPE_FLAG_DEVICE;
        SUCCESS_OR_TERMINATE(zeEventCreate(eventPool, &eventDesc, events + i));
    }
}

void zenon::create_cmd_list()
{
    auto allocSize = sizeof(uint8_t) * input1->size();

    //input copy engine
    if (!disable_blitter) {
        input_copy_command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
        input_copy_command_list_descriptor.pNext = nullptr;
        input_copy_command_list_descriptor.flags = 0;
        input_copy_command_list_descriptor.commandQueueGroupOrdinal = copyOnlyQueueGroupOrdinal;
        SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &input_copy_command_list_descriptor, &input_copy_command_list));
        SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(input_copy_command_list, input1_buffer, input1->data(), allocSize, nullptr, 0, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandListAppendBarrier(input_copy_command_list, nullptr, 0, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(input_copy_command_list, input2_buffer, input2->data(), allocSize, nullptr, 0, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandListAppendBarrier(input_copy_command_list, nullptr, 0, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(input_copy_command_list, mem_input1_buffer, mem_input1->data(), input_size * memory_used_by_mem_bound_kernel, nullptr, 0, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandListAppendBarrier(input_copy_command_list, nullptr, 0, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(input_copy_command_list, mem_input2_buffer, mem_input2->data(), input_size * memory_used_by_mem_bound_kernel, nullptr, 0, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandListAppendBarrier(input_copy_command_list, nullptr, 0, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandListClose(input_copy_command_list));
    }

    //compute engine
    uint32_t group_size_x = 0;
    uint32_t group_size_y = 0;
    uint32_t group_size_z = 0;
    SUCCESS_OR_TERMINATE(zeKernelSuggestGroupSize(kernel, number_of_threads, 1U, 1U, &group_size_x, &group_size_y, &group_size_z));
    SUCCESS_OR_TERMINATE(zeKernelSetGroupSize(kernel, 32, 1, 1));
    command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
    command_list_descriptor.commandQueueGroupOrdinal = 0;
    //printf( "\n threads: %d \n", number_of_threads,input_size);
    SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &command_list_descriptor, &command_list));

    createEventPoolAndEvents(context, device, event_pool, ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP, MAX_EVENTS_COUNT, &kernel_ts_event[0]);
    group_count.groupCountX = number_of_threads/2;
    group_count.groupCountY = 1;
    group_count.groupCountZ = 1;

    kernel_names.clear();
    if (!resnet)
    {
        uint32_t number_of_kernels = 40;
        if (disable_blitter) {
            submit_kernel_to_cmd_list(set_n_to_output, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[0], { nullptr }, 0, 1, input_size );
            submit_kernel_to_cmd_list(set_n_to_output, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[1], { nullptr }, 0, 2, input_size );
            submit_kernel_to_cmd_list(set_n_to_output, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[2], { nullptr }, 0, 3, input_size );
        }
        else {
            submit_kernel_to_cmd_list(add_buffers_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[0], { nullptr }, 0,number_of_threads,input_size);
            submit_kernel_to_cmd_list(add_buffers_kernel, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[1], { nullptr }, 0, number_of_threads,input_size);
            submit_kernel_to_cmd_list(add_buffers_kernel, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[2], { nullptr }, 0, number_of_threads,input_size);
        }
        for (int i = 1; i < number_of_kernels; i++)
        {
            if (i % 3 == 0)
                submit_kernel_to_cmd_list(add_buffers_kernel, { im_buf1, im_buf2 }, im_buf3, kernel_ts_event[i + 2], { &kernel_ts_event[i] , &kernel_ts_event[i + 1] }, 2, number_of_threads,input_size);
            if (i % 3 == 1)
                submit_kernel_to_cmd_list(add_buffers_kernel, { im_buf3, im_buf2 }, im_buf1, kernel_ts_event[i + 2], { &kernel_ts_event[i] , &kernel_ts_event[i + 1] }, 2, number_of_threads,input_size);
            if (i % 3 == 2)
                submit_kernel_to_cmd_list(add_buffers_kernel, { im_buf1, im_buf3 }, im_buf2, kernel_ts_event[i + 2], { &kernel_ts_event[i] , &kernel_ts_event[i + 1] }, 2, number_of_threads,input_size);
        }

        submit_kernel_to_cmd_list(kernel, { im_buf3 }, output_buffer, kernel_ts_event[number_of_kernels + 2], { &kernel_ts_event[number_of_kernels], &kernel_ts_event[number_of_kernels + 1] }, 2, number_of_threads,input_size);
        global_kernel_ts_event.push_back(kernel_ts_event[number_of_kernels + 2]);
        SUCCESS_OR_TERMINATE(zeCommandListClose(command_list));
        if (!disable_blitter) {
            //Output copy engine
            output_copy_command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
            output_copy_command_list_descriptor.pNext = nullptr;
            output_copy_command_list_descriptor.flags = 0;
            output_copy_command_list_descriptor.commandQueueGroupOrdinal = copyOnlyQueueGroupOrdinal;

            SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &output_copy_command_list_descriptor, &output_copy_command_list));
            SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(output_copy_command_list, output->data(), im_buf2, allocSize, nullptr, 1, &kernel_ts_event[number_of_kernels + 2]));
            SUCCESS_OR_TERMINATE(zeCommandListClose(output_copy_command_list));
        }
    }
    else {
        if (disable_blitter) {
            submit_kernel_to_cmd_list(set_n_to_output, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[0], { nullptr }, 0, 1,number_of_threads,input_size);
            submit_kernel_to_cmd_list(set_n_to_output, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[1], { nullptr }, 0, 2,number_of_threads,input_size);
        }
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[0], { nullptr }, 0, 187717,number_of_threads,input_size);                                           //conv1
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[1], { &kernel_ts_event[0] }, 1, 145798,number_of_threads,input_size);                               //pool1
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[2], { &kernel_ts_event[1] }, 1, 201456,number_of_threads,input_size);                               //<-res2a_branch1
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[3], { &kernel_ts_event[1] }, 1, 67940,number_of_threads,input_size);                                //->res2a_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf5, kernel_ts_event[4], { &kernel_ts_event[3] }, 1, 56114,number_of_threads,input_size);                                //->res2a_branch2b
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[5], { &kernel_ts_event[4] }, 1, 356590,number_of_threads,input_size);                               //->res2a_branch2c
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[6], { &kernel_ts_event[2], &kernel_ts_event[5] }, 2, 200166,number_of_threads,input_size);          //->res2b_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[7], { &kernel_ts_event[6] }, 1, 56114,number_of_threads,input_size);                                //->res2b_branch2b
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[8], { &kernel_ts_event[7] }, 1, 356590,number_of_threads,input_size);                               //->res2b_branch2c
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[9], { &kernel_ts_event[8] }, 1, 200166,number_of_threads,input_size);                               //->res2c_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf5, kernel_ts_event[10], { &kernel_ts_event[9] }, 1, 56114,number_of_threads,input_size);                               //->res2c_branch2b
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[11], { &kernel_ts_event[10] }, 1, 356590,number_of_threads,input_size);                             //->res2c_branch2c
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[12], { &kernel_ts_event[11] }, 1, 183438,number_of_threads,input_size);                             //<-res3a_branch1
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[13], { &kernel_ts_event[11] }, 1, 56519,number_of_threads,input_size);                              //->res3a_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[14], { &kernel_ts_event[13] }, 1, 55891,number_of_threads,input_size);                              //->res3a_branch2b
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[15], { &kernel_ts_event[14] }, 1, 149931,number_of_threads,input_size);                             //->res3a_branch2c
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[16], { &kernel_ts_event[12], &kernel_ts_event[15] }, 2, 71228,number_of_threads,input_size);        //->res3b_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf6, kernel_ts_event[17], { &kernel_ts_event[16] }, 1, 55891,number_of_threads,input_size);                              //->res3b_branch2b
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[18], { &kernel_ts_event[17] }, 1, 138323,number_of_threads,input_size);                             //->res3b_branch2c
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[19], { &kernel_ts_event[18] }, 1, 71228,number_of_threads,input_size);                              //->res3c_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[20], { &kernel_ts_event[19] }, 1, 55891,number_of_threads,input_size);                              //->res3c_branch2b
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[21], { &kernel_ts_event[20] }, 1, 138323,number_of_threads,input_size);                             //->res3c_branch2c
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[22], { &kernel_ts_event[21] }, 1, 71228,number_of_threads,input_size);                              //->res3c_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf6, kernel_ts_event[23], { &kernel_ts_event[22] }, 1, 55891,number_of_threads,input_size);                              //->res3c_branch2b
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[24], { &kernel_ts_event[23] }, 1, 138323,number_of_threads,input_size);                             //->res3c_branch2c
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[25], { &kernel_ts_event[24] }, 1, 84758,number_of_threads,input_size);                              //<-res4a_branch1
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer, kernel_ts_event[26], { &kernel_ts_event[24] }, 1, 27988,number_of_threads,input_size);                              //<-res4a_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[27], { &kernel_ts_event[26] }, 1, 55420,number_of_threads,input_size);                              //<-res4a_branch2b
        submit_kernel_to_cmd_list(mem_bound_kernel, { mem_input1_buffer, mem_input2_buffer }, mem_output_buffer2, kernel_ts_event[28], { &kernel_ts_event[27] }, 1, 60486,number_of_threads,input_size);                              //<-res4a_branch2c
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf6, kernel_ts_event[29], { &kernel_ts_event[25], &kernel_ts_event[28] }, 2, 26866,number_of_threads,input_size);        //->res4b_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[30], { &kernel_ts_event[29] }, 1, 55420,number_of_threads,input_size);                              //<-res4b_branch2b
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[31], { &kernel_ts_event[30] }, 1, 27559,number_of_threads,input_size);                              //<-res4b_branch2c
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[32], { &kernel_ts_event[31] }, 1, 26866,number_of_threads,input_size);                              //<-res4c_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[33], { &kernel_ts_event[32] }, 1, 55420,number_of_threads,input_size);                              //<-res4c_branch2b
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[34], { &kernel_ts_event[33] }, 1, 27559,number_of_threads,input_size);                              //<-res4c_branch2c
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf6, kernel_ts_event[35], { &kernel_ts_event[34] }, 1, 26866,number_of_threads,input_size);                              //<-res4d_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[36], { &kernel_ts_event[35] }, 1, 55420,number_of_threads,input_size);                              //<-res4d_branch2b
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[37], { &kernel_ts_event[36] }, 1, 27559,number_of_threads,input_size);                              //<-res4d_branch2c
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[38], { &kernel_ts_event[37] }, 1, 26866,number_of_threads,input_size);                              //<-res4e_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[39], { &kernel_ts_event[38] }, 1, 55420,number_of_threads,input_size);                              //<-res4e_branch2b
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[40], { &kernel_ts_event[39] }, 1, 27559,number_of_threads,input_size);                              //<-res4e_branch2c
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf6, kernel_ts_event[41], { &kernel_ts_event[40] }, 1, 26866,number_of_threads,input_size);                              //<-res4f_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[42], { &kernel_ts_event[41] }, 1, 55420,number_of_threads,input_size);                              //<-res4f_branch2b
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[43], { &kernel_ts_event[42] }, 1, 27559,number_of_threads,input_size);                              //<-res4f_branch2c
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[44], { &kernel_ts_event[43] }, 1, 50121,number_of_threads,input_size);                              //<-res5a_branch1
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[45], { &kernel_ts_event[43] }, 1, 16507,number_of_threads,input_size);                              //<-res5a_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[46], { &kernel_ts_event[45] }, 1, 55398,number_of_threads,input_size);                              //<-res5a_branch2b
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf6, kernel_ts_event[47], { &kernel_ts_event[46] }, 1, 27278,number_of_threads,input_size);                              //<-res5a_branch2c
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[48], { &kernel_ts_event[44], &kernel_ts_event[47] }, 2, 29580,number_of_threads,input_size);        //<-res5b_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[49], { &kernel_ts_event[48] }, 1, 55398,number_of_threads,input_size);                              //<-res5b_branch2b
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[50], { &kernel_ts_event[49] }, 1, 27278,number_of_threads,input_size);                              //<-res5b_branch2c
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[51], { &kernel_ts_event[50] }, 1, 29580,number_of_threads,input_size);                               //<-res5c_branch2a
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf4, kernel_ts_event[52], { &kernel_ts_event[51] }, 1, 55398,number_of_threads,input_size);                              //<-res5c_branch2b
        submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, output_buffer, kernel_ts_event[53], { &kernel_ts_event[52] }, 1, 27278,number_of_threads,input_size);                         //<-res5c_branch2c
        global_kernel_ts_event.push_back(kernel_ts_event[53]);

        SUCCESS_OR_TERMINATE(zeCommandListClose(command_list));
        if (!disable_blitter) {
            //Output copy engine
            output_copy_command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
            output_copy_command_list_descriptor.pNext = nullptr;
            output_copy_command_list_descriptor.flags = 0;
            output_copy_command_list_descriptor.commandQueueGroupOrdinal = copyOnlyQueueGroupOrdinal;

            SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &output_copy_command_list_descriptor, &output_copy_command_list));
            SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(output_copy_command_list, output->data(), im_buf4, allocSize, nullptr, 1, &kernel_ts_event[53]));
            SUCCESS_OR_TERMINATE(zeCommandListClose(output_copy_command_list));
        }
    }

}

gpu_results zenon::run(uint32_t clinet_id)
{
    if (!disable_blitter) {
        SUCCESS_OR_TERMINATE(zeCommandQueueExecuteCommandLists(input_copy_command_queue, 1, &input_copy_command_list, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandQueueSynchronize(input_copy_command_queue, UINT64_MAX));
    }
    SUCCESS_OR_TERMINATE(zeCommandQueueExecuteCommandLists(command_queue, 1, &command_list, nullptr));

    if( !single_thread )
        SUCCESS_OR_TERMINATE(zeCommandQueueSynchronize(command_queue, UINT64_MAX));
    //SUCCESS_OR_TERMINATE(zeEventHostSynchronize(global_kernel_ts_event.at(clinet_id), UINT32_MAX));
    if (!disable_blitter && !single_thread) {
        SUCCESS_OR_TERMINATE(zeCommandQueueExecuteCommandLists(output_copy_command_queue, 1, &output_copy_command_list, nullptr));
        SUCCESS_OR_TERMINATE(zeCommandQueueSynchronize(output_copy_command_queue, UINT64_MAX));
    }
    if (profiling && !single_thread)
    {
        ze_device_properties_t devProperties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
        SUCCESS_OR_TERMINATE(zeDeviceGetProperties(device, &devProperties));

        gpu_result.kernel_time.clear();
        gpu_result.execuction_time = 0;
        uint64_t timerResolution = devProperties.timerResolution;
        uint64_t kernelDuration = 0;
        for (int i = 0; i < graph_event_count - 2; i++)
        {
            SUCCESS_OR_TERMINATE(zeEventQueryKernelTimestamp(kernel_ts_event[i], &kernel_ts_results[i]));
            kernelDuration = (kernel_ts_results[i].context.kernelEnd - kernel_ts_results[i].context.kernelStart) * timerResolution;
            gpu_result.kernel_name.push_back(kernel_names.at(i));
            gpu_result.kernel_time.push_back(kernelDuration);
            gpu_result.execuction_time += kernelDuration;
        }
        gpu_result.kernels_start_time = kernel_ts_results[0].context.kernelStart * timerResolution;
        gpu_result.kernels_end_time = kernel_ts_results[graph_event_count - 3].context.kernelStart * timerResolution;
        gpu_result.gpu_time = (kernel_ts_results[graph_event_count - 3].context.kernelEnd - kernel_ts_results[0].context.kernelStart) * timerResolution;
    }
    if( !single_thread )
    {
        for( int i = 0; i < 54; i++ )
        {
            zeEventHostReset( kernel_ts_event[ i ] );
        }

        if( log )
        {
            std::cout << "Output:\n";
            if( disable_blitter )
            {
                auto castedSharedBuffer = reinterpret_cast<uint64_t*>( output_buffer );
                uint8_t* a2 = (uint8_t*)( castedSharedBuffer );
                for( int i = 0; i < output->size(); i++ )
                {
                    std::cout << (unsigned)a2[ i ] << " ";
                }
            }
            else
            {
                for( auto var : *output )
                {
                    std::cout << (int)var << " ";
                }
            }
            printf( "\n" );
        }
    }
    return gpu_result;
}

bool zenon::is_finished( uint32_t clinet_id )
{
    auto result = zeEventQueryStatus( global_kernel_ts_event.at( clinet_id ) );
    return  result==ZE_RESULT_SUCCESS;
}

gpu_results zenon::get_result( uint32_t clinet_id )
{    
    SUCCESS_OR_TERMINATE( zeCommandQueueSynchronize( command_queue, UINT64_MAX ) );
    if( !disable_blitter )
    {
        SUCCESS_OR_TERMINATE( zeCommandQueueExecuteCommandLists( output_copy_command_queue, 1, &output_copy_command_list, nullptr ) );
        SUCCESS_OR_TERMINATE( zeCommandQueueSynchronize( output_copy_command_queue, UINT64_MAX ) );
    }
    if( log )
    {
        std::cout << "Output:\n";
        if( disable_blitter )
        {
            auto castedSharedBuffer = reinterpret_cast<uint64_t*>( output_buffer );
            uint8_t* a2 = (uint8_t*)( castedSharedBuffer );
            for( int i = 0; i < output->size(); i++ )
            {
                std::cout << (unsigned)a2[ i ] << " ";
            }
        }
        else
        {
            for( auto var : *output )
            {
                std::cout << (int)var << " ";
            }
        }
        printf( "\n" );
    }
    for( int i = 0; i < 54; i++ )
    {
        zeEventHostReset( kernel_ts_event[ i ] );
    }
    return gpu_result;
}

void zenon::set_timestamps() {
    ze_device_properties_t devProperties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
    SUCCESS_OR_TERMINATE(zeDeviceGetProperties(device, &devProperties));

    gpu_result.kernel_time.clear();
    gpu_result.execuction_time = 0;
    uint64_t timerResolution = devProperties.timerResolution;
    uint64_t kernelDuration = 0;
    for (int i = 0; i < graph_event_count - 2; i++)
    {
        SUCCESS_OR_TERMINATE(zeEventQueryKernelTimestamp(kernel_ts_event[i], &kernel_ts_results[i]));
        kernelDuration = (kernel_ts_results[i].context.kernelEnd - kernel_ts_results[i].context.kernelStart) * timerResolution;
        gpu_result.kernel_name.push_back(kernel_names.at(i));
        gpu_result.kernel_time.push_back(kernelDuration);
        gpu_result.execuction_time += kernelDuration;
    }
    gpu_result.kernels_start_time = kernel_ts_results[0].context.kernelStart * timerResolution;
    gpu_result.kernels_end_time = kernel_ts_results[graph_event_count - 3].context.kernelStart * timerResolution;
    gpu_result.gpu_time = (kernel_ts_results[graph_event_count - 3].context.kernelEnd - kernel_ts_results[0].context.kernelStart) * timerResolution;
}

bool zenon::ze_initalized = false;
std::vector<ze_driver_handle_t> zenon::drivers;
ze_driver_handle_t zenon::driver;
ze_context_desc_t zenon::context_descriptor = {};
ze_context_handle_t zenon::context = nullptr;
uint32_t zenon::number_of_devices = 0;
std::vector<ze_device_handle_t> zenon::devices;
ze_device_handle_t zenon::device;
uint32_t zenon::command_queue_count = 1;
uint32_t zenon::computeQueueGroupOrdinal = 0;
uint32_t zenon::copyOnlyQueueGroupOrdinal = 0;
uint32_t zenon::zenon_cntr = 0;
