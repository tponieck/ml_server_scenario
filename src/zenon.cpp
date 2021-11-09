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

extern bool verbose, profiling, resnet;
bool verbose = false;
bool profiling = false;
bool resnet = false;

zenon::zenon(bool _log)
{
    log = _log;
    input1 = new std::vector<uint8_t>(INPUT_SIZE, 0);
    input2 = new std::vector<uint8_t>(INPUT_SIZE, 0);
    output = new std::vector<uint8_t>(INPUT_SIZE, 0);
    init();
}

zenon::zenon( std::vector<uint8_t>* in1, std::vector<uint8_t>* in2, std::vector<uint8_t>* out )
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

            if( !( cmdqueueGroupProperties[ i ].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE ) &&
                ( cmdqueueGroupProperties[ i ].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COPY ) )
            {
                copyOnlyQueueGroupOrdinal = i;
                break;
            }
        }

        command_queue_count = cmdqueueGroupProperties[ computeQueueGroupOrdinal ].numQueues;

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
    SUCCESS_OR_TERMINATE( zeCommandQueueCreate( context, device, &input_copy_command_queue_descriptor, &input_copy_command_queue ) );

    output_copy_command_queue_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
    output_copy_command_queue_descriptor.pNext = nullptr;
    output_copy_command_queue_descriptor.flags = 0;
    output_copy_command_queue_descriptor.mode = ZE_COMMAND_QUEUE_MODE_DEFAULT;
    output_copy_command_queue_descriptor.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
    output_copy_command_queue_descriptor.ordinal = copyOnlyQueueGroupOrdinal;
    output_copy_command_queue_descriptor.index = 0;
    SUCCESS_OR_TERMINATE( zeCommandQueueCreate( context, device, &output_copy_command_queue_descriptor, &output_copy_command_queue ) );
}
zenon::~zenon()
{
    zenon_cntr--;

    SUCCESS_OR_TERMINATE( zeCommandListDestroy( input_copy_command_list ) );

    SUCCESS_OR_TERMINATE( zeCommandQueueDestroy( input_copy_command_queue ) );

    SUCCESS_OR_TERMINATE( zeCommandListDestroy( output_copy_command_list ) );

    SUCCESS_OR_TERMINATE( zeCommandQueueDestroy( output_copy_command_queue ) );

    SUCCESS_OR_TERMINATE(zeCommandListDestroy(command_list));

    SUCCESS_OR_TERMINATE(zeMemFree(context, output_buffer));

    SUCCESS_OR_TERMINATE( zeMemFree( context, input1_buffer ) );

    SUCCESS_OR_TERMINATE(zeMemFree(context, input2_buffer));

    SUCCESS_OR_TERMINATE(zeKernelDestroy(kernel));

    SUCCESS_OR_TERMINATE(zeModuleDestroy(module));

    if (ze_initalized && zenon_cntr == 0)
    {
        SUCCESS_OR_TERMINATE(zeEventDestroy(*kernel_ts_event));
        SUCCESS_OR_TERMINATE(zeEventPoolDestroy(event_pool));

        for( uint32_t i = 0; i < command_queue_count; i++ )
        {
            SUCCESS_OR_TERMINATE( zeCommandQueueDestroy( command_queue ) );
            SUCCESS_OR_TERMINATE( zeCommandQueueDestroy( input_copy_command_queue ) );
            SUCCESS_OR_TERMINATE( zeCommandQueueDestroy( output_copy_command_queue ) );
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
}

void zenon::allocate_buffers()
{
    memory_descriptor.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
    memory_descriptor.ordinal = 0;
    auto alloc_size = sizeof( uint8_t ) * input1->size();

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device, &input1_buffer ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device, &input2_buffer ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device, &output_buffer ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device, &im_buf1 ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device, &im_buf2 ) );
    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device,
        &im_buf3 ) );
    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device, &im_buf4 ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device, &im_buf5 ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        alloc_size, 1, device, &im_buf6 ) );

}

void zenon::submit_kernel_to_cmd_list(ze_kernel_handle_t& _kernel,
    std::vector<void*> input,
    void* output,
    ze_event_handle_t output_event,
    std::vector<ze_event_handle_t*> input_events,
    uint32_t input_event_count)
{
    int param_cnt = 0;
    for (int i = 0; i < input.size(); i++)
    {
        SUCCESS_OR_TERMINATE( zeKernelSetArgumentValue( _kernel, param_cnt++, sizeof( input1_buffer ), &input.at( i ) ));
    }
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(output_buffer), &output));
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
    int time_in_nanoseconds)
{
    int param_cnt = 0;
    int counter = 0;

    if (_kernel == cmp_bound_kernel) {
        counter = (int)(time_in_nanoseconds * 0.0114416 - 37.4022);
    }
    else {
        counter = (int)(time_in_nanoseconds * 0.01133048 - 256.87);
    }
    for (int i = 0; i < input.size(); i++)
    {
        SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(input1_buffer), &input.at(i)));
    }
    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(output_buffer), &output));

    SUCCESS_OR_TERMINATE(zeKernelSetArgumentValue(_kernel, param_cnt++, sizeof(int), &counter));
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
    input_copy_command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
    input_copy_command_list_descriptor.pNext = nullptr;
    input_copy_command_list_descriptor.flags = 0;
    input_copy_command_list_descriptor.commandQueueGroupOrdinal = copyOnlyQueueGroupOrdinal;
    SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &input_copy_command_list_descriptor, &input_copy_command_list));

    SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(input_copy_command_list, input1_buffer, input1->data(), allocSize, nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendBarrier(input_copy_command_list, nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(input_copy_command_list, input2_buffer, input2->data(), allocSize, nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListAppendBarrier(input_copy_command_list, nullptr, 0, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandListClose(input_copy_command_list));

    //compute engine
    uint32_t group_size_x = 0;
    uint32_t group_size_y = 0;
    uint32_t group_size_z = 0;
    SUCCESS_OR_TERMINATE(zeKernelSuggestGroupSize(kernel, input1->size(), 1U, 1U, &group_size_x, &group_size_y, &group_size_z));
    SUCCESS_OR_TERMINATE(zeKernelSetGroupSize(kernel, group_size_x, group_size_y, group_size_z));

    command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
    command_list_descriptor.commandQueueGroupOrdinal = 0;

    SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &command_list_descriptor, &command_list));

    createEventPoolAndEvents(context, device, event_pool, ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP, MAX_EVENTS_COUNT, &kernel_ts_event[0]);
    group_count.groupCountX = input1->size() / group_size_x;
    group_count.groupCountY = 1;
    group_count.groupCountZ = 1;

    kernel_names.clear();
    uint32_t number_of_kernels;
    if (!resnet)
    {
        submit_kernel_to_cmd_list(add_buffers_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[0], { nullptr }, 0);
        submit_kernel_to_cmd_list(add_buffers_kernel, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[1], { nullptr }, 0);
        submit_kernel_to_cmd_list(add_buffers_kernel, { input1_buffer, input2_buffer }, im_buf3, kernel_ts_event[2], { nullptr }, 0);

        number_of_kernels = 40;

        for (int i = 1; i < number_of_kernels; i++)
        {
            if (i % 3 == 0)
                submit_kernel_to_cmd_list(add_buffers_kernel, { im_buf1, im_buf2 }, im_buf3, kernel_ts_event[i + 2], { &kernel_ts_event[i] , &kernel_ts_event[i + 1] }, 2);
            if (i % 3 == 1)
                submit_kernel_to_cmd_list(add_buffers_kernel, { im_buf3, im_buf2 }, im_buf1, kernel_ts_event[i + 2], { &kernel_ts_event[i] , &kernel_ts_event[i + 1] }, 2);
            if (i % 3 == 2)
                submit_kernel_to_cmd_list(add_buffers_kernel, { im_buf1, im_buf3 }, im_buf2, kernel_ts_event[i + 2], { &kernel_ts_event[i] , &kernel_ts_event[i + 1] }, 2);
        }

        submit_kernel_to_cmd_list(add_buffers_kernel, { im_buf3, im_buf2 }, output_buffer, kernel_ts_event[number_of_kernels + 2], { &kernel_ts_event[number_of_kernels], &kernel_ts_event[number_of_kernels + 1] }, 2);
        SUCCESS_OR_TERMINATE(zeCommandListClose(command_list));

        //Output copy engine
        output_copy_command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
        output_copy_command_list_descriptor.pNext = nullptr;
        output_copy_command_list_descriptor.flags = 0;
        output_copy_command_list_descriptor.commandQueueGroupOrdinal = copyOnlyQueueGroupOrdinal;

        SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &output_copy_command_list_descriptor, &output_copy_command_list));
        SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(output_copy_command_list, output->data(), output_buffer, allocSize, nullptr, 1, &kernel_ts_event[number_of_kernels + 2]));
        SUCCESS_OR_TERMINATE(zeCommandListClose(output_copy_command_list));
    }
    else {
        //number_of_kernels = 0;
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[0], { nullptr }, 0, 187717);                                           //conv1
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[1], { &kernel_ts_event[0] }, 1, 145798);                               //pool1
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf2, kernel_ts_event[2], { &kernel_ts_event[1] }, 1, 201456);                               //<-res2a_branch1
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[3], { &kernel_ts_event[1] }, 1, 67940);                                //->res2a_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[4], { &kernel_ts_event[3] }, 1, 56114);                                //->res2a_branch2b
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[5], { &kernel_ts_event[4] }, 1, 356590);                               //->res2a_branch2c
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[6], { &kernel_ts_event[2], &kernel_ts_event[5] }, 2, 200166);          //->res2b_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[7], { &kernel_ts_event[6] }, 1, 56114);                                //->res2b_branch2b
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[8], { &kernel_ts_event[7] }, 1, 356590);                               //->res2b_branch2c
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[9], { &kernel_ts_event[8] }, 1, 200166);                               //->res2c_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[10], { &kernel_ts_event[9] }, 1, 56114);                               //->res2c_branch2b
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[11], { &kernel_ts_event[10] }, 1, 356590);                             //->res2c_branch2c
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[12], { &kernel_ts_event[11] }, 1, 183438);                             //<-res3a_branch1
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[13], { &kernel_ts_event[11] }, 1, 56519);                              //->res3a_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[14], { &kernel_ts_event[13] }, 1, 55891);                              //->res3a_branch2b
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[15], { &kernel_ts_event[14] }, 1, 149931);                             //->res3a_branch2c
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[16], { &kernel_ts_event[12], &kernel_ts_event[15] }, 1, 71228);        //->res3b_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[17], { &kernel_ts_event[16] }, 1, 55891);                              //->res3b_branch2b
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[18], { &kernel_ts_event[17] }, 1, 138323);                             //->res3b_branch2c
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[19], { &kernel_ts_event[18] }, 1, 71228);                              //->res3c_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[20], { &kernel_ts_event[19] }, 1, 55891);                              //->res3c_branch2b
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[21], { &kernel_ts_event[20] }, 1, 138323);                             //->res3c_branch2c
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[22], { &kernel_ts_event[21] }, 1, 71228);                              //->res3c_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[23], { &kernel_ts_event[22] }, 1, 55891);                              //->res3c_branch2b
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[24], { &kernel_ts_event[23] }, 1, 138323);                             //->res3c_branch2c
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[25], { &kernel_ts_event[24] }, 1, 84758);                              //<-res4a_branch1
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[26], { &kernel_ts_event[24] }, 1, 27988);                               //<-res4a_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[27], { &kernel_ts_event[26] }, 1, 55420);                              //<-res4a_branch2b
            submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[28], { &kernel_ts_event[27] }, 1, 60486);                              //<-res4a_branch2c
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[29], { &kernel_ts_event[25], &kernel_ts_event[28] }, 2, 26866);        //->res4b_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[30], { &kernel_ts_event[29] }, 1, 55420);                              //<-res4b_branch2b
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[31], { &kernel_ts_event[30] }, 1, 27559);                              //<-res4b_branch2c
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[32], { &kernel_ts_event[31] }, 1, 26866);                              //<-res4c_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[33], { &kernel_ts_event[32] }, 1, 55420);                              //<-res4c_branch2b
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[34], { &kernel_ts_event[33] }, 1, 27559);                              //<-res4c_branch2c
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[35], { &kernel_ts_event[34] }, 1, 26866);                              //<-res4d_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[36], { &kernel_ts_event[35] }, 1, 55420);                              //<-res4d_branch2b
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[37], { &kernel_ts_event[36] }, 1, 27559);                              //<-res4d_branch2c
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[38], { &kernel_ts_event[37] }, 1, 26866);                              //<-res4e_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[39], { &kernel_ts_event[38] }, 1, 55420);                              //<-res4e_branch2b
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[40], { &kernel_ts_event[39] }, 1, 27559);                              //<-res4e_branch2c
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[41], { &kernel_ts_event[40] }, 1, 26866);                              //<-res4f_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[42], { &kernel_ts_event[41] }, 1, 55420);                              //<-res4f_branch2b
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[43], { &kernel_ts_event[42] }, 1, 27559);                              //<-res4f_branch2c
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[44], { &kernel_ts_event[43] }, 1, 50121);                              //<-res5a_branch1
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[45], { &kernel_ts_event[43] }, 1, 16507);                              //<-res5a_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[46], { &kernel_ts_event[45] }, 1, 55398);                              //<-res5a_branch2b
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[47], { &kernel_ts_event[46] }, 1, 27278);                              //<-res5a_branch2c
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[48], { &kernel_ts_event[44], &kernel_ts_event[47] }, 2, 29580);        //<-res5b_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[49], { &kernel_ts_event[48] }, 1, 55398);                              //<-res5b_branch2b
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[50], { &kernel_ts_event[49] }, 1, 27278);                              //<-res5b_branch2c
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[51], { &kernel_ts_event[50]}, 1, 29580);                               //<-res5c_branch2a
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[52], { &kernel_ts_event[51] }, 1, 55398);                              //<-res5c_branch2b
            submit_kernel_to_cmd_list(cmp_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[53], { &kernel_ts_event[52] }, 1, 27278);                              //<-res5c_branch2c
           // submit_kernel_to_cmd_list(mem_bound_kernel, { input1_buffer, input2_buffer }, im_buf1, kernel_ts_event[54], { &kernel_ts_event[53] }, 1, 1);                                //<-fc1000

            SUCCESS_OR_TERMINATE(zeCommandListClose(command_list));

            //Output copy engine
            output_copy_command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
            output_copy_command_list_descriptor.pNext = nullptr;
            output_copy_command_list_descriptor.flags = 0;
            output_copy_command_list_descriptor.commandQueueGroupOrdinal = copyOnlyQueueGroupOrdinal;

            SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &output_copy_command_list_descriptor, &output_copy_command_list));
            SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy(output_copy_command_list, output->data(), output_buffer, allocSize, nullptr, 1, &kernel_ts_event[53]));
            SUCCESS_OR_TERMINATE(zeCommandListClose(output_copy_command_list));

    }
    
}

gpu_results zenon::run(uint32_t clinet_id)
{
    SUCCESS_OR_TERMINATE( zeCommandQueueExecuteCommandLists( input_copy_command_queue, 1, &input_copy_command_list, nullptr ) );
    SUCCESS_OR_TERMINATE( zeCommandQueueSynchronize( input_copy_command_queue, UINT64_MAX ) );

    SUCCESS_OR_TERMINATE( zeCommandQueueExecuteCommandLists( command_queue, 1, &command_list, nullptr ) );
    SUCCESS_OR_TERMINATE( zeCommandQueueSynchronize( command_queue, UINT64_MAX ) );

    SUCCESS_OR_TERMINATE( zeCommandQueueExecuteCommandLists( output_copy_command_queue, 1, &output_copy_command_list, nullptr ) );
    SUCCESS_OR_TERMINATE( zeCommandQueueSynchronize( output_copy_command_queue, UINT64_MAX ) );

    if( profiling )
    {
        ze_device_properties_t devProperties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
        SUCCESS_OR_TERMINATE(zeDeviceGetProperties(device, &devProperties));

        gpu_result.kernel_time.clear();
        gpu_result.execuction_time = 0;
        uint64_t timerResolution = devProperties.timerResolution;
        uint64_t kernelDuration = 0;
        for( int i = 0; i < graph_event_count; i++ )
        {
            SUCCESS_OR_TERMINATE(zeEventQueryKernelTimestamp(kernel_ts_event[i], &kernel_ts_results[i]));
            kernelDuration = (kernel_ts_results[i].context.kernelEnd - kernel_ts_results[i].context.kernelStart) * timerResolution;
            gpu_result.kernel_name.push_back(kernel_names.at(i));
            gpu_result.kernel_time.push_back(kernelDuration);
            gpu_result.execuction_time += kernelDuration;
        }
        gpu_result.gpu_time = (kernel_ts_results[graph_event_count - 2].context.kernelEnd - kernel_ts_results[0].context.kernelStart) * timerResolution;
    }

    if (log) {
        std::cout << "Output:\n";
        for (auto var : *output)
        {
            std::cout << (int)var << " ";
        }
        std::cout << std::endl;
    }
    return gpu_result;
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
