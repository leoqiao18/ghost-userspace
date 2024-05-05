// #include <stdbool.h>

// clang-format off
// #include <linux/bpf.h>
// #include <linux/perf_event.h>
// #include <linux/kernel.h>
// #include <string.h>
#include "third_party/bpf/vmlinux_ghost.h"
#include "libbpf/bpf_helpers.h"
#include "libbpf/bpf_tracing.h"
// clang-format on

// #include "lib/ghost_uapi.h"
// #include "lib/queue.bpf.h"
// #include "third_party/bpf/common.bpf.h"
#include "third_party/bpf/efs_bpf.h"

// #include <asm-generic/errno.h>


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


// SEC name is important! libbpf infers program type from it.
// See: https://docs.kernel.org/bpf/libbpf/program_types.html#program-types-and-elf
SEC("tracepoint/sched/sched_switch")
int efs_handle_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    u32 cpu_id = bpf_get_smp_processor_id();

    // INFO: only the primary core of each socket can perform energy reading
    // This is assuming that primary core is CPU 0
    if (cpu_id != 0) {
        return 0;
    }

    u64 perf_fd_index = 0 & BPF_F_INDEX_MASK;
    struct bpf_perf_event_value v;
    long err;

    // get PIDs
    pid_t prev_pid = ctx->prev_pid;
    pid_t next_pid = ctx->next_pid;
    bpf_printk("prev PID: %d, next PID: %d", prev_pid, next_pid);

    // get prev time and energy

    uint32_t zero = 0;
    struct energy_snapshot *prev_snap = bpf_map_lookup_elem(&energy_snapshot, &zero);
    if (prev_snap == 0) {
        bpf_printk("Failed to find value from energy snapshot");
        return 1;
    }

    // get current time and energy
    uint64_t ts = bpf_ktime_get_ns();
    err = bpf_perf_event_read_value(&perf_event_descriptors, perf_fd_index, &v, sizeof(v));
    if (err < 0)
    {
        // Error
        bpf_printk("Failed to read value from perf event. ERRNO: %ld\n", err);
    } else {
        // Success
        // `v` is populated now
        // v.counter;
    }

    // update map with new data
    struct task_consumption cons;
    cons.time_delta = ts - prev_snap->timestamp;
    cons.energy_delta = v.counter - prev_snap->energy;

    struct energy_snapshot new_snap;
    new_snap.energy = v.counter;
    new_snap.timestamp = ts;

    bpf_printk("Energy delta: %lu\n", new_snap.energy);

    if (bpf_map_update_elem(&pid_to_consumption, &prev_pid, &cons, BPF_ANY) < 0) {
        bpf_printk("Failed to update task consumption map");
    }
    if (bpf_map_update_elem(&energy_snapshot, &zero, &new_snap, BPF_ANY) < 0) {
        bpf_printk("Failed to update energy snapshot map");
    }
    return 0;
}