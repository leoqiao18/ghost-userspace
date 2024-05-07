// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright (c) 2020 Facebook */
/* Adapted by yanniszark in 2024 */

// All linux kernel type definitions are in vmlinux.h
#include "vmlinux.h"
// BPF helpers
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "sys_base_power.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct
{
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY); // map type
    __uint(key_size, sizeof(__u32));                            // key type
    __uint(value_size, sizeof(__u32));                          // value type
    __uint(max_entries, 128);                      // number of entries
} perf_event_descriptors SEC(".maps");

struct
{
    __uint(type, BPF_MAP_TYPE_ARRAY); // map type
    __type(key, u32);              // key type
    __type(value, struct energy_snapshot);              // value type
    __uint(max_entries, 1);      // number of entries
} energy_snapshot SEC(".maps");

// SEC name is important! libbpf infers program type from it.
// See: https://docs.kernel.org/bpf/libbpf/program_types.html#program-types-and-elf
SEC("tracepoint/sched/sched_switch")
int sys_base_power_handle_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    u32 cpu_id = bpf_get_smp_processor_id();

    // INFO: only the primary core of each socket can perform energy reading
    // This is assuming that primary core is CPU 0
    if (cpu_id != 0) {
        return 0;
    }

    long err;

    // get prev time and energy
    uint32_t zero = 0;

    // get current time and energy
    uint64_t ts = bpf_ktime_get_ns();

    // get current time and energy (pkg)
    u64 perf_fd_index_pkg = 0 & BPF_F_INDEX_MASK;
    struct bpf_perf_event_value v_pkg;
    
    err = bpf_perf_event_read_value(&perf_event_descriptors, perf_fd_index_pkg, &v_pkg, sizeof(v_pkg));
    if (err < 0)
    {
        return 0;
    }

    // get current time and energy (pkg)
    u64 perf_fd_index_ram = 1 & BPF_F_INDEX_MASK;
    struct bpf_perf_event_value v_ram;

    err = bpf_perf_event_read_value(&perf_event_descriptors, perf_fd_index_ram, &v_ram, sizeof(v_ram));
    if (err < 0)
    {
        return 0;
    }

    // update map with new data
    struct energy_snapshot new_snap;
    new_snap.energy = v_pkg.counter + v_ram.counter;
    new_snap.timestamp = ts;

    if (bpf_map_update_elem(&energy_snapshot, &zero, &new_snap, BPF_ANY) < 0) {
        bpf_printk("Failed to update energy snapshot map");
    }
    return 0;
}
