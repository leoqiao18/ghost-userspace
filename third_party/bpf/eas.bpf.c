#include <stdbool.h>

// clang-format off
#include <linux/bpf.h>
#include "libbpf/bpf_helpers.h"
#include "libbpf/bpf_tracing.h"
// clang-format on

#include "lib/ghost_uapi.h"
#include "lib/queue.bpf.h"
#include "third_party/bpf/common.bpf.h"
#include "third_party/bpf/eas_bpf.h"

#include <asm-generic/errno.h>

bool initialized;

struct perf_event *pe = NULL;

struct {
  __uint(type, BPF_MAP_TYPE_ARRAY);
  __uint(max_entries, 1);
  __type(key, u32);
  __type(value, u64);
  __uint(map_flags, BPF_F_MMAPABLE);
} energy SEC(".maps");

int init_perf_energy(void) {
  struct perf_event_attr attr;

  memset(&attr, 0, sizeof(attr));
  attr.type = PERF_TYPE_POWER;
  attr.config = PERF_COUNT_HW_ENERGY_PKG;
  attr.size = sizeof(struct perf_event_attr);
  attr.disabled = 1;

  pe = perf_event_create_kernel_counter(&attr, -1, current, NULL, NULL);
  if (IS_ERR(pe)) {
    return PTR_ERR(pe);
  }

  perf_event_enable(pe);
  return 0;
}

u64 read_perf_energy(void) {
  if (pe) {
    u64 total_energy = local64_read(&pe->count);
    return total_energy;
  }
  return 0;
}

SEC("tracepoint/sched/sched_switch")
int tracepoint__sched__sched_switch(struct trace_event_raw_sched_switch *ctx) {
  if (!initialized) {
    if (rapl_init()) {
      initialized = true;
    }
  }

  u64 new_e = read_perf_energy();

  u32 zero = 0;
  u64 *e = bpf_map_lookup_elem(&energy, &zero);
  if (!next) {
    return 0;
  }

  *e = new_e;

  return 0;
}