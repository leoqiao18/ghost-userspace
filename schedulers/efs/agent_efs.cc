// Copyright 2022 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <linux/perf_event.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <bpf/libbpf.h>
#include <bpf/bpf.h>
#include <errno.h>

#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "lib/agent.h"
#include "lib/enclave.h"
#include "schedulers/efs/efs_scheduler.h"
#include "schedulers/efs/efs_bpf.skel.h"

#define PERF_COUNT_ENERGY_CORES 1
#define PERF_COUNT_ENERGY_PKG 2
#define PERF_COUNT_ENERGY_RAM 3
#define PERF_COUNT_ENERGY_GPU 4
#define PERF_COUNT_ENERGY_PSYS 5

ABSL_FLAG(std::string, ghost_cpus, "1-5", "cpulist");
ABSL_FLAG(double, base_watts, "0.0", "system base watts");
ABSL_FLAG(std::string, enclave, "", "Connect to preexisting enclave directory");

// Scheduling tuneables
ABSL_FLAG(
    absl::Duration, min_granularity, absl::Milliseconds(1),
    "The minimum time a task will run before being preempted by another task");
ABSL_FLAG(absl::Duration, latency, absl::Milliseconds(10),
          "The target time period in which all tasks will run at least once");

namespace ghost {

static int CreatePerfEvent(struct bpf_map *map, uint32_t type, uint32_t config, uint32_t idx)
{
    int map_fd = bpf_map__fd(map);
    
    struct perf_event_attr attr = {};
    attr.type = type;
    attr.config = config;
    attr.size = sizeof(struct perf_event_attr);

    // TODO: only assuming a single socket at CPU "0"
    int perf_fd = syscall(__NR_perf_event_open, &attr, -1 /*pid*/, 0 /*cpu*/, -1 /*group_fd*/, 0 /*flags*/);
    if (perf_fd < 0)
    {
        fprintf(stderr, "ERROR: Failed to create perf event %d\n", errno);
        return -1;
    }

    if (bpf_map_update_elem(map_fd, &idx, &perf_fd, BPF_ANY) < 0) {
        fprintf(stderr, "ERROR: putting perf_event_fd to map failed\n");
        close(perf_fd);
        return -1;
    }

    return perf_fd;
}

static void SetupPerfEvents(struct efs_bpf *efs_bpf) {
  // read power type from file
  uint32_t type;
  FILE *power_type;
  int perf_fd;

  power_type = fopen("/sys/bus/event_source/devices/power/type", "r");
  if (power_type == NULL)
  {
      fprintf(stderr, "Unable to retrieve power type.\n");
      exit(1);
  }
  fscanf(power_type, "%d", &type);
  fclose(power_type);

  if((perf_fd = CreatePerfEvent(efs_bpf->maps.perf_event_descriptors, type, PERF_COUNT_ENERGY_PKG, 0)) < 0)
  {
      fprintf(stderr, "Unable to create perf event.\n");
      exit(1);
  }

  if((perf_fd = CreatePerfEvent(efs_bpf->maps.perf_event_descriptors, type, PERF_COUNT_ENERGY_RAM, 1)) < 0)
  {
      fprintf(stderr, "Unable to create perf event.\n");
      exit(1);
  }
}

static void ParseAgentConfig(EfsConfig* config) {
  CpuList ghost_cpus =
      MachineTopology()->ParseCpuStr(absl::GetFlag(FLAGS_ghost_cpus));
  CHECK(!ghost_cpus.Empty());

  Topology* topology = MachineTopology();
  config->topology_ = topology;
  config->cpus_ = ghost_cpus;
  std::string enclave = absl::GetFlag(FLAGS_enclave);
  if (!enclave.empty()) {
    int fd = open(enclave.c_str(), O_PATH);
    CHECK_GE(fd, 0);
    config->enclave_fd_ = fd;
  }

  config->min_granularity_ = absl::GetFlag(FLAGS_min_granularity);
  config->latency_ = absl::GetFlag(FLAGS_latency);
  config->base_watts = absl::GetFlag(FLAGS_base_watts);
}

}  // namespace ghost

int main(int argc, char* argv[]) {
  absl::InitializeSymbolizer(argv[0]);
  absl::ParseCommandLine(argc, argv);

  ghost::EfsConfig config;
  ghost::ParseAgentConfig(&config);

  printf("Initializing...\n");

  // Initialize eBPF part
  struct efs_bpf *bpf = efs_bpf__open_and_load();
  ghost::SetupPerfEvents(bpf);
  efs_bpf__attach(bpf);

  config.bpf = bpf;

  // Using new so we can destruct the object before printing Done
  auto uap = new ghost::AgentProcess<ghost::FullEfsAgent<ghost::LocalEnclave>,
                                     ghost::EfsConfig>(config);

  ghost::GhostHelper()->InitCore();
  printf("Initialization complete, ghOSt active.\n");
  // When `stdout` is directed to a terminal, it is newline-buffered. When
  // `stdout` is directed to a non-interactive device (e.g, a Python subprocess
  // pipe), it is fully buffered. Thus, in order for the Python script to read
  // the initialization message as soon as it is passed to `printf`, we need to
  // manually flush `stdout`.
  fflush(stdout);

  ghost::Notification exit;
  ghost::GhostSignals::AddHandler(SIGINT, [&exit](int) {
    static bool first = true;  // We only modify the first SIGINT.

    if (first) {
      exit.Notify();
      first = false;
      return false;  // We'll exit on subsequent SIGTERMs.
    }
    return true;
  });

  // TODO: this is racy - uap could be deleted already
  ghost::GhostSignals::AddHandler(SIGUSR1, [uap](int) {
    uap->Rpc(ghost::EfsScheduler::kDebugRunqueue);
    return false;
  });

  exit.WaitForNotification();

  delete uap;

  printf("\nDone!\n");

  return 0;
}
