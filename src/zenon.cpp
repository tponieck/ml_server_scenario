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

extern bool verbose, profiling;
bool verbose = false;
bool profiling = false;

zenon::zenon(bool _log)
{
    log = _log;
    input = new std::vector<uint8_t>(INPUT_SIZE, 0);
    input2 = new std::vector<uint8_t>( INPUT_SIZE, 0 );
    output = new std::vector<uint8_t>(INPUT_SIZE, 0);
    init();
}

zenon::zenon( std::vector<uint8_t>* in, std::vector<uint8_t>* in2, std::vector<uint8_t>* out )
{
    input = in;
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
        SUCCESS_OR_TERMINATE( zeInit( ZE_INIT_FLAG_GPU_ONLY ));
        ze_initalized = true;
        SUCCESS_OR_TERMINATE( zeDriverGet( &number_of_drivers, nullptr ));

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
                if ( !(device_properties.flags & ZE_DEVICE_PROPERTY_FLAG_INTEGRATED)) 
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
    
    SUCCESS_OR_TERMINATE(zeCommandQueueCreate(context, device, &command_queue_descriptor, &command_queue));
}
zenon::~zenon()
{

    zenon_cntr--;
    SUCCESS_OR_TERMINATE(zeCommandListDestroy(command_list));

    SUCCESS_OR_TERMINATE( zeMemFree( context, output_buffer ) );

    SUCCESS_OR_TERMINATE( zeMemFree( context, input_buffer ) );

    SUCCESS_OR_TERMINATE(zeMemFree( context, input2_buffer ));

    SUCCESS_OR_TERMINATE(zeKernelDestroy( kernel ));

    SUCCESS_OR_TERMINATE( zeModuleDestroy( module ) );


    if (ze_initalized && zenon_cntr == 0)
    {
        SUCCESS_OR_TERMINATE( zeEventDestroy( *kernel_ts_event ));
        SUCCESS_OR_TERMINATE( zeEventPoolDestroy( event_pool ));

        for (uint32_t i = 0; i < command_queue_count; i++)
            SUCCESS_OR_TERMINATE( zeCommandQueueDestroy(command_queue));

        SUCCESS_OR_TERMINATE( zeContextDestroy( context ) );
        
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
    SUCCESS_OR_TERMINATE( zeModuleCreate( context, device, &module_descriptor, &module, nullptr ) );

    kernel_descriptor.stype = ZE_STRUCTURE_TYPE_KERNEL_DESC;
    kernel_descriptor.pKernelName = "copy_buffer";

    SUCCESS_OR_TERMINATE( zeKernelCreate( module, &kernel_descriptor, &kernel ) );
    
    kernel_descriptor.pKernelName = "heavy";
    SUCCESS_OR_TERMINATE( zeKernelCreate( module, &kernel_descriptor, &heavy_kernel));

    kernel_descriptor.pKernelName = "add_buffers";
    SUCCESS_OR_TERMINATE( zeKernelCreate( module, &kernel_descriptor, &add_buffers_kernel ) );

    kernel_descriptor.pKernelName = "mul_buffers";
    SUCCESS_OR_TERMINATE( zeKernelCreate( module, &kernel_descriptor, &mul_buffers_kernel ) );

}

void zenon::allocate_buffers()
{
   
    memory_descriptor.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
    memory_descriptor.ordinal = 0;

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        sizeof( uint8_t ) * input->size(), 1, device,
        &input_buffer ) );
    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        sizeof( uint8_t ) * input->size(), 1, device,
        &input2_buffer ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice(context, &memory_descriptor,
        sizeof(uint8_t) * output->size(), 1, device,
        &output_buffer));

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        sizeof( uint8_t ) * output->size(), 1, device,
        &im_buf1 ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        sizeof( uint8_t ) * output->size(), 1, device,
        &im_buf2 ) );
    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        sizeof( uint8_t ) * output->size(), 1, device,
        &im_buf3 ) );
    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        sizeof( uint8_t ) * output->size(), 1, device,
        &im_buf4 ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        sizeof( uint8_t ) * output->size(), 1, device,
        &im_buf5 ) );

    SUCCESS_OR_TERMINATE( zeMemAllocDevice( context, &memory_descriptor,
        sizeof( uint8_t ) * output->size(), 1, device,
        &im_buf6 ) );

}

void zenon::submit_kernel_to_cmd_list( ze_kernel_handle_t& _kernel, 
    std::vector<void*> input, 
    void* output, 
    ze_event_handle_t output_event,
    std::vector<ze_event_handle_t*> input_events, 
    uint32_t input_event_count )
{
    int param_cnt = 0;
    for( int i = 0; i < input.size(); i++ )
    {
        SUCCESS_OR_TERMINATE( zeKernelSetArgumentValue( _kernel, param_cnt++, sizeof( input_buffer ), &input.at( i ) ));
    }
    SUCCESS_OR_TERMINATE( zeKernelSetArgumentValue( _kernel, param_cnt++, sizeof( output_buffer ), &output ));
    SUCCESS_OR_TERMINATE( zeCommandListAppendLaunchKernel( command_list, _kernel, &group_count,
        output_event, input_event_count, input_events.at( 0 )) );
    graph_event_count++;
    if( profiling )
    {
        size_t kernel_name_length = 0;
        SUCCESS_OR_TERMINATE( zeKernelGetName( _kernel, &kernel_name_length, nullptr ) );
        char* kernel_name = new char[ kernel_name_length ];
        SUCCESS_OR_TERMINATE( zeKernelGetName( _kernel, &kernel_name_length, kernel_name ) );
        kernel_names.push_back( kernel_name );
    }
}

void createEventPoolAndEvents( ze_context_handle_t& context,
    ze_device_handle_t& device,
    ze_event_pool_handle_t& eventPool,
    ze_event_pool_flag_t poolFlag,
    uint32_t poolSize,
    ze_event_handle_t* events )
{
    ze_event_pool_desc_t eventPoolDesc;
    ze_event_desc_t eventDesc;
    eventPoolDesc.count = poolSize;
    eventPoolDesc.flags = poolFlag;
    SUCCESS_OR_TERMINATE(zeEventPoolCreate( context, &eventPoolDesc, 1, &device, &eventPool ));

    for( uint32_t i = 0; i < poolSize; i++ )
    {
        eventDesc.index = i;
        eventDesc.signal = ZE_EVENT_SCOPE_FLAG_DEVICE;
        eventDesc.wait = ZE_EVENT_SCOPE_FLAG_DEVICE;
        SUCCESS_OR_TERMINATE(zeEventCreate( eventPool, &eventDesc, events + i ));
    }
}

void zenon::create_cmd_list()
{
    uint32_t group_size_x = 0;
    uint32_t group_size_y = 0;
    uint32_t group_size_z = 0;
    SUCCESS_OR_TERMINATE(zeKernelSuggestGroupSize(kernel, input->size(), 1U, 1U, &group_size_x, &group_size_y, &group_size_z ) );
    SUCCESS_OR_TERMINATE(zeKernelSetGroupSize(kernel, group_size_x, group_size_y, group_size_z));

    command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
    command_list_descriptor.commandQueueGroupOrdinal = 0;

    SUCCESS_OR_TERMINATE(zeCommandListCreate(context, device, &command_list_descriptor, &command_list));

    SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy( command_list, input_buffer, input->data(), sizeof(uint8_t) * input->size(), nullptr, 0, nullptr));

    SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy( command_list, input2_buffer, input2->data(), sizeof( uint8_t ) * input2->size(), nullptr, 0, nullptr ));

    SUCCESS_OR_TERMINATE( zeCommandListAppendBarrier( command_list, nullptr, 0, nullptr ));

    createEventPoolAndEvents( context, device, event_pool, ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP, MAX_EVENTS_COUNT, &kernel_ts_event[0] );
    group_count.groupCountX = input->size() / group_size_x;
    group_count.groupCountY = 1;
    group_count.groupCountZ = 1;

    kernel_names.clear();
    
    submit_kernel_to_cmd_list( add_buffers_kernel, { input_buffer, input2_buffer }, im_buf1, kernel_ts_event[ 0 ], { nullptr }, 0 );
    submit_kernel_to_cmd_list( add_buffers_kernel, { input_buffer, input2_buffer }, im_buf2, kernel_ts_event[ 1 ], { nullptr }, 0 );

    submit_kernel_to_cmd_list( mul_buffers_kernel, { im_buf1, im_buf2 }, im_buf3, kernel_ts_event[ 2 ], { &kernel_ts_event[0], &kernel_ts_event[1] }, 2 );

    submit_kernel_to_cmd_list( mul_buffers_kernel, { im_buf1, im_buf2 }, im_buf4, kernel_ts_event[ 3 ], { &kernel_ts_event[ 0 ], &kernel_ts_event[ 1 ] }, 2 );
    submit_kernel_to_cmd_list( mul_buffers_kernel, { im_buf1, im_buf2 }, im_buf5, kernel_ts_event[ 4 ], { &kernel_ts_event[ 0 ], &kernel_ts_event[ 1 ] }, 2 );

    submit_kernel_to_cmd_list( mul_buffers_kernel, { im_buf4, im_buf5 }, im_buf6, kernel_ts_event[ 5 ], { &kernel_ts_event[ 3 ], &kernel_ts_event[ 4 ] }, 2 );

    submit_kernel_to_cmd_list( add_buffers_kernel, { im_buf3, im_buf6 }, output_buffer, kernel_ts_event[ 6 ], { &kernel_ts_event[ 2 ], &kernel_ts_event[ 5 ] }, 2 );
    
    SUCCESS_OR_TERMINATE(zeCommandListAppendMemoryCopy( command_list, output->data(), output_buffer, sizeof(uint8_t) * output->size(), nullptr, 1, &kernel_ts_event[ 6 ] ));
    SUCCESS_OR_TERMINATE(zeCommandListClose(command_list));
}

gpu_results zenon::run(uint32_t clinet_id)
{
    SUCCESS_OR_TERMINATE(zeCommandQueueExecuteCommandLists(command_queue, 1, &command_list, nullptr));
    SUCCESS_OR_TERMINATE(zeCommandQueueSynchronize(command_queue, UINT64_MAX));

    if( profiling )
    {
        ze_device_properties_t devProperties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
        SUCCESS_OR_TERMINATE( zeDeviceGetProperties( device, &devProperties ) );

        gpu_result.kernel_time.clear();
        gpu_result.execuction_time = 0;
        uint64_t timerResolution = devProperties.timerResolution;
        uint64_t kernelDuration = 0;
        for( int i = 0; i < graph_event_count; i++ )
        {
            SUCCESS_OR_TERMINATE( zeEventQueryKernelTimestamp( kernel_ts_event[ i ], &kernel_ts_results[ i ] ) );
            kernelDuration = ( kernel_ts_results[ i ].context.kernelEnd - kernel_ts_results[ i ].context.kernelStart ) * timerResolution;
            gpu_result.kernel_time.push_back( kernelDuration );
            gpu_result.execuction_time += kernelDuration;
        }
        gpu_result.kernel_name = kernel_names;
    }
    
    if (log) {
        std::cout << "Output:\n";
        for  (auto var : *output)
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
uint32_t zenon::zenon_cntr = 0;