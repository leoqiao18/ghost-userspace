// #include <stdbool.h>

// clang-format off
// #include <linux/bpf.h>
// #include <linux/perf_event.h>
// #include <linux/kernel.h>
// #include <string.h>
#include "vmlinux.h"
#include "bpf/bpf_helpers.h"
#include "bpf/bpf_tracing.h"
// clang-format on

// #include "lib/ghost_uapi.h"
// #include "lib/queue.bpf.h"
// #include "third_party/bpf/common.bpf.h"
#include "shiv.h"

// #include <asm-generic/errno.h>

#define GAMMA 0.5

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

struct
{
    __uint(type, BPF_MAP_TYPE_HASH); // map type
    __type(key, pid_t);              // key type
    __type(value, struct task_consumption);              // value type
    __uint(max_entries, 10000);      // number of entries
} pid_to_consumption SEC(".maps");

char LICENSE[] SEC("license") = "Dual BSD/GPL";

// SEC name is important! libbpf infers program type from it.
// See: https://docs.kernel.org/bpf/libbpf/program_types.html#program-types-and-elf
SEC("tracepoint/sched/sched_switch")
int shiv_handle_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    // INFO: only the primary core of each socket can perform energy reading
    // This is assuming that primary core is CPU 1
    u32 cpu_id = bpf_get_smp_processor_id();
    if (cpu_id != 1) {
        return 0;
    }

    long err;

    // get current time and energy (pkg)
    u64 perf_fd_index_pkg = 0 & BPF_F_INDEX_MASK;
    struct bpf_perf_event_value v_pkg;
    
    err = bpf_perf_event_read_value(&perf_event_descriptors, perf_fd_index_pkg, &v_pkg, sizeof(v_pkg));
    if (err < 0)
    {
        // bpf_printk("Failed to read perf event value 1");
        return 0;
    }

    // get current time and energy (ram)
    u64 perf_fd_index_ram = 1 & BPF_F_INDEX_MASK;
    struct bpf_perf_event_value v_ram;

    err = bpf_perf_event_read_value(&perf_event_descriptors, perf_fd_index_ram, &v_ram, sizeof(v_ram));
    if (err < 0)
    {
        // bpf_printk("Failed to read perf event value 2");
        return 0;
    }

    // get prev time and energy
    uint64_t ts = bpf_ktime_get_ns();
    uint32_t zero = 0;
    struct energy_snapshot *prev_snap = bpf_map_lookup_elem(&energy_snapshot, &zero);
    if (prev_snap == 0) {
        // bpf_printk("Failed to find value from energy snapshot");
        return 0;
    }

    struct energy_snapshot prev_snap_copy = {
        .energy = prev_snap->energy,
        .timestamp = prev_snap->timestamp
    };

    struct energy_snapshot new_snap;
    uint64_t v_energy = v_pkg.counter + v_ram.counter;

    new_snap.energy = v_energy;
    new_snap.timestamp = ts;

    if (bpf_map_update_elem(&energy_snapshot, &zero, &new_snap, BPF_ANY) < 0) {
        // bpf_printk("Failed to update energy snapshot map");
        return 0;
    }

    if (prev_snap_copy.timestamp == 0) {
        return 0;
    }

    // lookup pid to see if we are interested in it
    pid_t prev_pid = ctx->prev_pid;
    struct task_consumption *prev_cons = bpf_map_lookup_elem(&pid_to_consumption, &prev_pid);
    if (prev_cons == 0) 
    {
        bpf_printk("Failed to find value from pid to consumption map for pid %d", prev_pid);
        return 0;
    }

    // update map with new data
    struct task_consumption cons;
    cons.time = prev_cons->time + (ts - prev_snap_copy.timestamp);
    cons.energy = prev_cons->energy + (v_energy - prev_snap_copy.energy);
    cons.timestamp = ts;
    if (cons.time - prev_cons->time > 0) {
        bpf_printk("watts %d", (cons.energy - prev_cons->energy) / (cons.time - prev_cons->time));
    }
    // if (cons.time_delta != 0) 
    // {
    //     u64 *base = bpf_map_lookup_elem(&base_watts, &zero);
    //     // TODO: make this calculation more precise using u128
    //     cons.running_avg_watts = GAMMA * (energy_delta / time_delt - base) + 
    //                              (1 - GAMMA) * prev_cons->running_avg_watts;
    // } else 
    // {
    //     cons.running_avg_watts = prev_cons->running_avg_watts;
    // }
    
    if (bpf_map_update_elem(&pid_to_consumption, &prev_pid, &cons, BPF_EXIST) < 0) 
    {
        // bpf_printk("Failed to update task consumption map");
    }

    bpf_printk("Success for pid %d", prev_pid);
    return 0;
}