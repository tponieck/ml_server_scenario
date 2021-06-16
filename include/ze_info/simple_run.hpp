/*
 * Copyright (C) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef SIMPLE_RUN
#define SIMPLE_RUN

	int run_simple();
	int run_simple_zenon();
	void run_mt(int queries, int qps, int pool_size,bool multi_ccs, bool fixed_dist, bool log = false);
#endif