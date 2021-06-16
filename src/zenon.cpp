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

zenon::zenon(bool _log)
{
    log = _log;
    input = new std::vector<uint8_t>(INPUT_SIZE, 0);
    output = new std::vector<uint8_t>(INPUT_SIZE, 0);
    init();
}

zenon::zenon(std::vector<uint8_t>* in, std::vector<uint8_t>* out)
{
    input = in;
    output = out;
    init();
}

void zenon::init()
{
    if (!ze_initalized)
    {
        if (log)
            std::cout << "Initalization start\n";
        result = zeInit(ZE_INIT_FLAG_GPU_ONLY);
        if (result != ZE_RESULT_SUCCESS) {
            std::cout << "Failed to initialize Level Zero" << '\n';
        }
        ze_initalized = true;

        result = zeDriverGet(&number_of_drivers, nullptr);
        if (result != ZE_RESULT_SUCCESS) {
            std::cout << "Failed to get number of availabile drivers" << '\n';
        }

        if (log)
            std::cout << "Number of drivers: " << number_of_drivers << std::endl;

        drivers.resize(number_of_drivers);
        result = zeDriverGet(&number_of_drivers, drivers.data());
        if (result != ZE_RESULT_SUCCESS) {
            std::cout << "Failed to get drivers" << '\n';

        }
        driver = drivers[0];
        context_descriptor.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;
        result = zeContextCreate(driver, &context_descriptor, &context);
        if (result != ZE_RESULT_SUCCESS) {
            std::cout << "Failed to create context" << '\n';
        }
        result = zeDeviceGet(driver, &number_of_devices, nullptr);
        if (result != ZE_RESULT_SUCCESS) {
            std::cout << "Failed to get number of availabile devices" << '\n';
        }

        devices.resize(number_of_devices);
        result = zeDeviceGet(driver, &number_of_devices, devices.data());
        if (result != ZE_RESULT_SUCCESS) {
            std::cout << "Failed to get devices" << '\n';
        }
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
                if ( !(device_properties.flags & ZE_DEVICE_PROPERTY_FLAG_INTEGRATED)) 
                {
                    if (log)
                    {
                        std::cout << "Device is discreete!" << std::endl;
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
        }

        command_queue_count = cmdqueueGroupProperties[computeQueueGroupOrdinal].numQueues;
      
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
    
    result = zeCommandQueueCreate(context, device, &command_queue_descriptor,
            &command_queue);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to create command queue" << '\n';
    }
}
zenon::~zenon()
{


    result = zeCommandListDestroy(command_list);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to destroy command list" << '\n';
        return ;
    }

    result = zeMemFree(context, output_buffer);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to free output buffer" << '\n';
        return ;
    }

    result = zeMemFree(context, input_buffer);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to free input buffer" << '\n';
        return ;
    }

    result = zeKernelDestroy(kernel);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to destroy kernel" << '\n';
        return ;
    }

    result = zeModuleDestroy(module);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to destroy module" << '\n';
        return ;
    }


    if (ze_initalized)
    {
        for (uint32_t i = 0; i < command_queue_count; i++)
            result = zeCommandQueueDestroy(command_queue);
        if (result != ZE_RESULT_SUCCESS) {
            std::cout << "Failed to destroy command queue" << '\n';
            return;
        }

        result = zeContextDestroy(context);
        if (result != ZE_RESULT_SUCCESS) {
            std::cout << "Failed to destroy context" << '\n';
            return;
        }

        ze_initalized = false;
    }

    delete input;
    delete output;
}

void zenon::create_module(const std::string& cl_file_path)
{
    const std::vector<uint8_t> spirv = generate_spirv("module.cl", "");
    module_descriptor.stype = ZE_STRUCTURE_TYPE_MODULE_DESC;
    module_descriptor.format = ZE_MODULE_FORMAT_IL_SPIRV;
    module_descriptor.inputSize = spirv.size();
    module_descriptor.pInputModule = spirv.data();
    result =
        zeModuleCreate(context, device, &module_descriptor, &module, nullptr);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to build module" << '\n';
    }

    kernel_descriptor.stype = ZE_STRUCTURE_TYPE_KERNEL_DESC;
    kernel_descriptor.pKernelName = "copy_buffer";

    result = zeKernelCreate(module, &kernel_descriptor, &kernel);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to create kernel" << '\n';
    }
    kernel_descriptor.pKernelName = "heavy";
    result = zeKernelCreate(module, &kernel_descriptor, &heavy_kernel);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to create heavy kernel" << '\n';
    }

}

void zenon::allocate_buffers()
{
   
    memory_descriptor.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
    memory_descriptor.ordinal = 0;

    result = zeMemAllocDevice(context, &memory_descriptor,
        sizeof(uint8_t) * input->size(), 1, device,
        &input_buffer);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to create input buffer" << '\n';
    }

    result = zeMemAllocDevice(context, &memory_descriptor,
        sizeof(uint8_t) * output->size(), 1, device,
        &output_buffer);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to create output buffer" << '\n';
    }

    result = zeMemAllocDevice(context, &memory_descriptor,
        sizeof(uint8_t) * output->size(), 1, device,
        &im_buf1);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to create output buffer" << '\n';
    }

    result = zeMemAllocDevice(context, &memory_descriptor,
        sizeof(uint8_t) * output->size(), 1, device,
        &im_buf2);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to create output buffer" << '\n';
    }

}

void zenon::submit_kernel_to_cmd_list(ze_kernel_handle_t& _kernel, void* input, void* output)
{
    result =
        zeKernelSetArgumentValue(_kernel, 0, sizeof(input_buffer), &input);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to set input buffer as kernel argument" << '\n';
    }

    result = zeKernelSetArgumentValue(_kernel, 1, sizeof(output_buffer), &output);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to set output buffer as kernel argument" << '\n';
    }

    result = zeCommandListAppendLaunchKernel(command_list, _kernel, &group_count,
        nullptr, 0, nullptr);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to append kernel launch" << '\n';
    }

    result = zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to append barrier" << '\n';
    }
}

void zenon::create_cmd_list()
{
    uint32_t group_size_x = 0;
    uint32_t group_size_y = 0;
    uint32_t group_size_z = 0;
    result = zeKernelSuggestGroupSize(kernel, input->size(), 1U, 1U, &group_size_x,
        &group_size_y, &group_size_z);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to set suggest kernel group size" << '\n';
    }
    result =
        zeKernelSetGroupSize(kernel, group_size_x, group_size_y, group_size_z);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to set kernel group size" << '\n';
    }

   
    command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
    command_list_descriptor.commandQueueGroupOrdinal = 0;

    result = zeCommandListCreate(context, device, &command_list_descriptor,
        &command_list);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to create command list" << '\n';
    }

    result = zeCommandListAppendMemoryCopy(
        command_list, input_buffer, input->data(), sizeof(uint8_t) * input->size(),
        nullptr, 0, nullptr);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to append memory copy to input buffer" << '\n';
    }

    result = zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to append barrier" << '\n';
    }

    group_count.groupCountX = input->size() / group_size_x;
    group_count.groupCountY = 1;
    group_count.groupCountZ = 1;

    submit_kernel_to_cmd_list(kernel, input_buffer, im_buf1);
    submit_kernel_to_cmd_list(kernel, im_buf1, im_buf2);
    submit_kernel_to_cmd_list(heavy_kernel, im_buf2, im_buf1);
    submit_kernel_to_cmd_list(kernel, im_buf1, im_buf2);
    submit_kernel_to_cmd_list(kernel, im_buf2, im_buf1);
    submit_kernel_to_cmd_list(kernel, im_buf1, im_buf2);
    submit_kernel_to_cmd_list(heavy_kernel, im_buf2, im_buf1);
    submit_kernel_to_cmd_list(kernel, im_buf1, im_buf2);
    submit_kernel_to_cmd_list(kernel, im_buf2, im_buf1);
    submit_kernel_to_cmd_list(kernel, im_buf1, im_buf2);
    submit_kernel_to_cmd_list(heavy_kernel, im_buf2, im_buf1);
    submit_kernel_to_cmd_list(kernel, im_buf1, im_buf2);
    submit_kernel_to_cmd_list(kernel, im_buf2, im_buf1);
    submit_kernel_to_cmd_list(heavy_kernel, im_buf1, output_buffer);   


    result = zeCommandListAppendMemoryCopy(
        command_list, output->data(), output_buffer,
        sizeof(uint8_t) * output->size(), nullptr, 0, nullptr);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to append memory copy from output buffer" << '\n';
    }
    result = zeCommandListClose(command_list);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to close command list" << '\n';
    }

}

int zenon::run(uint32_t clinet_id)
{
    result = zeCommandQueueExecuteCommandLists(command_queue, 1, &command_list,
        nullptr);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to execute command list" << '\n';
    }

    result = zeCommandQueueSynchronize(command_queue, UINT64_MAX);
    if (result != ZE_RESULT_SUCCESS) {
        std::cout << "Failed to synchronize command queue" << '\n';
    }
    if (log) {
        std::cout << "Output:\n";
        for each (auto var in *output)
        {
            std::cout << (int)var << " ";
        }
        std::cout << std::endl;
    }
    return 0;
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
uint32_t zenon::zenon_cntr = 0;