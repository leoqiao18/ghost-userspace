// Copyright 2022 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "lib/agent.h"
#include "lib/enclave.h"
#include "schedulers/eas/eas_scheduler.h"
#include "schedulers/eas/energy_worker.h"

#include <nlohmann/json.hpp>
#include <ext/stdio_filebuf.h>
using json = nlohmann::json;

ABSL_FLAG(std::string, ghost_cpus, "1-5", "cpulist");
ABSL_FLAG(std::string, enclave, "", "Connect to preexisting enclave directory");

// Scheduling tuneables
ABSL_FLAG(
    absl::Duration, min_granularity, absl::Milliseconds(1),
    "The minimum time a task will run before being preempted by another task");
ABSL_FLAG(absl::Duration, latency, absl::Milliseconds(10),
          "The target time period in which all tasks will run at least once");

namespace ghost {


static void ParseAgentConfig(EasConfig *config) {
  CpuList ghost_cpus =
      MachineTopology()->ParseCpuStr(absl::GetFlag(FLAGS_ghost_cpus));
  CHECK(!ghost_cpus.Empty());

  Topology *topology = MachineTopology();
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
}

} // namespace ghost


void *thread_function(void *arg) {

    FILE *pipe = popen("scaphandre json -s 1", "r");
    if (!pipe) {
        std::cerr << "Error: Failed to open pipe\n";
        return EXIT_FAILURE;
    }

    // Read from the pipe
    nlohmann::json j;
    auto filebuf = new __gnu_cxx::stdio_filebuf<char>(pipe, std::ios::in);
    auto s = new std::istream(filebuf);

    // while (s >> j) {
    //   for (auto& process : j["processes"]) {
    //     std::cout << "pid:" << process["pid"] << "," 
    //               << "consumption:" << process["consumption"] << std::endl;
    //   }
    // }

    std::string line;
    while (std::getline(s, line)) {
      json parsed_json = json::parse(line);
      for (auto& process : parsed_json["processes"]) {
        std::cout << "pid:" << process["pid"] << "," 
                  << "consumption:" << process["consumption"] << std::endl;
      }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
  absl::InitializeSymbolizer(argv[0]);
  absl::ParseCommandLine(argc, argv);

  ghost::EasConfig config;
  ghost::ParseAgentConfig(&config);

  printf("Initializing...\n");

  // fork an energy worker
  pthread_t tid; // Thread ID
  int result;

  // Create a new thread
  result = pthread_create(&tid, NULL, thread_function, NULL);
  if (result != 0) {
      perror("Thread creation failed");
      return 1;
  }

  // Using new so we can destruct the object before printing Done
  auto uap = new ghost::AgentProcess<ghost::FullEasAgent<ghost::LocalEnclave>,
                                     ghost::EasConfig>(config);

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
    static bool first = true; // We only modify the first SIGINT.

    if (first) {
      exit.Notify();
      first = false;
      return false; // We'll exit on subsequent SIGTERMs.
    }
    return true;
  });

  // TODO: this is racy - uap could be deleted already
  ghost::GhostSignals::AddHandler(SIGUSR1, [uap](int) {
    uap->Rpc(ghost::EasScheduler::kDebugRunqueue);
    return false;
  });

  exit.WaitForNotification();

  delete uap;

  printf("\nDone!\n");

  pthread_join(tid, NULL);
  return 0;
}
