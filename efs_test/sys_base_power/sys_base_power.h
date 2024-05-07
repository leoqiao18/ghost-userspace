#ifndef __SYS_BASE_POWER_H__
#define __SYS_BASE_POWER_H__

// #define PERF_TYPE_POWER 21

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
    uint64_t energy_delta;
    uint64_t time_delta;
};

#endif
