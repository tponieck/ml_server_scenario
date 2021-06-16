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



void run_mt(int queries, int qps, int pool_size, bool multi_ccs, bool fixed_dist, bool log)
{
	client cli(queries, qps, pool_size,multi_ccs, fixed_dist, log);
	cli.run_all();
	std::cout << "\nDone...\n";

}



int run_simple_zenon()
{
    std::vector<uint8_t> input = { 1, 1, 1, 1, 1, 1,
			  1, 1, 1, 1, 1, 1,
			  1, 1, 1, 1, 1, 1 };
    std::vector<uint8_t> output(input.size(), 0);
	
	zenon zenek(&input, &output);
	zenek.create_module();
	zenek.allocate_buffers();
	zenek.create_cmd_list();
	zenek.run(0);

	input = { 2,2,2,2,2,2,
		      2,2,2,2,2,2,
		      2,2,2,2,2,2 };

	zenek.run(0);
	zenek.run(0);

	return 0;
}



int run_simple() {
	ze_result_t result = ZE_RESULT_SUCCESS;

	result = zeInit(ZE_INIT_FLAG_GPU_ONLY);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to initialize Level Zero" << '\n';
		return result;
	}

	uint32_t number_of_drivers = 0;
	result = zeDriverGet(&number_of_drivers, nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to get number of availabile drivers" << '\n';
		return result;
	}
	std::vector<ze_driver_handle_t> drivers(number_of_drivers);
	result = zeDriverGet(&number_of_drivers, drivers.data());
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to get drivers" << '\n';
		return result;
	}
	ze_driver_handle_t driver = drivers[0];

	ze_context_desc_t context_descriptor = {};
	context_descriptor.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;
	ze_context_handle_t context = nullptr;
	result = zeContextCreate(driver, &context_descriptor, &context);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to create context" << '\n';
		return result;
	}
	
	ze_context_handle_t context2 = nullptr;
	result = zeContextCreate(driver, &context_descriptor, &context2);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to create context2" << '\n';
		return result;
	}


	uint32_t number_of_devices = 0;
	result = zeDeviceGet(driver, &number_of_devices, nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to get number of availabile devices" << '\n';
		return result;
	}

	std::vector<ze_device_handle_t> devices(number_of_devices);
	result = zeDeviceGet(driver, &number_of_devices, devices.data());
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to get devices" << '\n';
		return result;
	}
	ze_device_handle_t device = devices[0];

#ifdef SPRIV_SOURCE
	const std::string module_path = "module.spv";
	const std::vector<uint8_t> spirv = load_binary_file(module_path);
#else

	const std::vector<uint8_t> spirv = generate_spirv("module.cl", "");
#endif

	ze_module_desc_t module_descriptor = {};
	module_descriptor.stype = ZE_STRUCTURE_TYPE_MODULE_DESC;
	module_descriptor.format = ZE_MODULE_FORMAT_IL_SPIRV;
	module_descriptor.inputSize = spirv.size();
	module_descriptor.pInputModule = spirv.data();
	ze_module_handle_t module = nullptr;
	result =
		zeModuleCreate(context, device, &module_descriptor, &module, nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to build module" << '\n';
		return result;
	}

	ze_kernel_desc_t kernel_descriptor = {};
	kernel_descriptor.stype = ZE_STRUCTURE_TYPE_KERNEL_DESC;
	kernel_descriptor.pKernelName = "copy_buffer";
	ze_kernel_handle_t kernel = nullptr;
	result = zeKernelCreate(module, &kernel_descriptor, &kernel);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to create kernel" << '\n';
		return result;
	}

	const std::vector<uint8_t> input = { 72, 101, 108, 108, 111, 32,
										76, 101, 118, 101, 108, 32,
										90, 101, 114, 111, 33 };
	std::vector<uint8_t> output(input.size(), 0);

	ze_device_mem_alloc_desc_t memory_descriptor = {};
	memory_descriptor.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
	memory_descriptor.ordinal = 0;

	void* input_buffer = nullptr;
	result = zeMemAllocDevice(context, &memory_descriptor,
		sizeof(uint8_t) * input.size(), 1, device,
		&input_buffer);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to create input buffer" << '\n';
		return result;
	}

	void* output_buffer = nullptr;
	result = zeMemAllocDevice(context, &memory_descriptor,
		sizeof(uint8_t) * output.size(), 1, device,
		&output_buffer);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to create output buffer" << '\n';
		return result;
	}

	result =
		zeKernelSetArgumentValue(kernel, 0, sizeof(input_buffer), &input_buffer);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to set input buffer as kernel argument" << '\n';
		return result;
	}

	result = zeKernelSetArgumentValue(kernel, 1, sizeof(output_buffer),
		&output_buffer);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to set output buffer as kernel argument" << '\n';
		return result;
	}

	uint32_t group_size_x = 0;
	uint32_t group_size_y = 0;
	uint32_t group_size_z = 0;
	result = zeKernelSuggestGroupSize(kernel, input.size(), 1U, 1U, &group_size_x,
		&group_size_y, &group_size_z);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to set suggest kernel group size" << '\n';
		return result;
	}
	result =
		zeKernelSetGroupSize(kernel, group_size_x, group_size_y, group_size_z);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to set kernel group size" << '\n';
		return result;
	}

	ze_command_list_desc_t command_list_descriptor = {};
	command_list_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC;
	command_list_descriptor.commandQueueGroupOrdinal = 0;
	ze_command_list_handle_t command_list = nullptr;
	result = zeCommandListCreate(context, device, &command_list_descriptor,
		&command_list);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to create command list" << '\n';
		return result;
	}

	result = zeCommandListAppendMemoryCopy(
		command_list, input_buffer, input.data(), sizeof(uint8_t) * input.size(),
		nullptr, 0, nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to append memory copy to input buffer" << '\n';
		return result;
	}

	result = zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to append barrier" << '\n';
		return result;
	}

	ze_group_count_t group_count = {};
	group_count.groupCountX = input.size() / group_size_x;
	group_count.groupCountY = 1;
	group_count.groupCountZ = 1;
	result = zeCommandListAppendLaunchKernel(command_list, kernel, &group_count,
		nullptr, 0, nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to append kernel launch" << '\n';
		return result;
	}

	result = zeCommandListAppendBarrier(command_list, nullptr, 0, nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to append barrier" << '\n';
		return result;
	}

	result = zeCommandListAppendMemoryCopy(
		command_list, output.data(), output_buffer,
		sizeof(uint8_t) * output.size(), nullptr, 0, nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to append memory copy from output buffer" << '\n';
		return result;
	}



	result = zeCommandListClose(command_list);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to close command list" << '\n';
		return result;
	}

	ze_command_queue_desc_t command_queue_descriptor = {};
	command_queue_descriptor.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
	command_queue_descriptor.ordinal = 0;
	command_queue_descriptor.index = 0;
	command_queue_descriptor.mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS;
	command_queue_descriptor.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
	ze_command_queue_handle_t command_queue = nullptr;
	result = zeCommandQueueCreate(context, device, &command_queue_descriptor,
		&command_queue);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to create command queue" << '\n';
		return result;
	}


	ze_command_queue_handle_t command_queue2 = nullptr;
	result = zeCommandQueueCreate(context, device, &command_queue_descriptor,
		&command_queue2);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to create command queue 2" << '\n';
		return result;
	}


	result = zeCommandQueueExecuteCommandLists(command_queue, 1, &command_list,
		nullptr);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to execute command list" << '\n';
		return result;
	}

	result = zeCommandQueueSynchronize(command_queue, UINT64_MAX);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to synchronize command queue" << '\n';
		return result;
	}

	std::cout << "Output:\n";
	for each (auto var in output)
	{
		std::cout << (int)var << " ";
	}

	result = zeCommandQueueDestroy(command_queue);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to destroy command queue" << '\n';
		return result;
	}

	result = zeCommandListDestroy(command_list);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to destroy command list" << '\n';
		return result;
	}

	result = zeMemFree(context, output_buffer);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to free output buffer" << '\n';
		return result;
	}

	result = zeMemFree(context, input_buffer);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to free input buffer" << '\n';
		return result;
	}

	result = zeKernelDestroy(kernel);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to destroy kernel" << '\n';
		return result;
	}

	result = zeModuleDestroy(module);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to destroy module" << '\n';
		return result;
	}

	result = zeContextDestroy(context);
	if (result != ZE_RESULT_SUCCESS) {
		std::cout << "Failed to destroy context" << '\n';
		return result;
	}

	std::cout << std::string(output.begin(), output.end()) << '\n';

	return 0;
	}

