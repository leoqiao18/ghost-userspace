/*
 * Copyright 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef GHOST_BPF_BPF_EFS_BPF_H_
#define GHOST_BPF_BPF_EFS_BPF_H_


#define PERF_COUNT_ENERGY_CORES 1
#define PERF_COUNT_ENERGY_PKG 2
#define PERF_COUNT_ENERGY_RAM 3
#define PERF_COUNT_ENERGY_GPU 4
#define PERF_COUNT_ENERGY_PSYS 5


struct energy_snapshot {
    uint64_t energy;
    uint64_t timestamp;
};

struct task_consumption {
    uint64_t energy;
    uint64_t time;
    uint64_t timestamp;
    // uint64_t running_avg_watts;
};

#endif // GHOST_BPF_BPF_EFS_BPF_H_
