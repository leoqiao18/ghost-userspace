#include "schedulers/efs/wattmeter.h"
#include "schedulers/efs/efs_bpf.skel.h"
#include "third_party/bpf/efs_bpf.h"
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

namespace ghost {

void Wattmeter::AddTask(Gtid gtid) {
  absl::MutexLock lock(&mu_);

  pid_t tid = gtid.tid();
  // printf("Adding task %d\n", tid);
  struct task_consumption empty = {};
  if (bpf_map_update_elem(efs_bpf_map_fd, &tid, &empty, BPF_ANY) < 0) {
    printf("Failed to add task consumption map");
  }
  pid_to_task_consumption[tid] = empty;
}

void Wattmeter::RemoveTask(Gtid gtid) {
  absl::MutexLock lock(&mu_);

  pid_t tid = gtid.tid();
  if (bpf_map_delete_elem(efs_bpf_map_fd, &tid) < 0) {
    printf("Failed to delete task consumption map");
  }
  pid_to_task_consumption.erase(tid);
  auto watts_it = pid_to_watts.find(tid);
  if (watts_it != pid_to_watts.end()) {
    double watts = watts_it->second;
    pid_to_watts.erase(tid);
    if (watts == min_watts || watts == max_watts) {
      ComputeMinMaxWatts();
    }
  }
}

double Wattmeter::ComputeScore(Gtid gtid) {
  absl::MutexLock lock(&mu_);

  pid_t pid = gtid.tid();
  auto it = pid_to_watts.find(pid);
  if (it == pid_to_watts.end()) {
    return 1;
  }
  double this_watts = it->second;
  if (min_watts == 0) {
    return 1.0;
  }
  double score = this_watts / min_watts;
  score = (score - 1) * 1 + 1;
  return score;
}

void Wattmeter::Update(Gtid gtid) {
  absl::MutexLock lock(&mu_);

  pid_t pid = gtid.tid();
  // printf("Updating task %d\n", pid);

  // get the local task_consumption
  auto cons_it = pid_to_task_consumption.find(pid);
  if (cons_it == pid_to_task_consumption.end()) {
    return;
  }
  struct task_consumption old_cons = cons_it->second;

  // get the newest bpf recorded task_consumption
  struct task_consumption new_cons;
  if ((bpf_map_lookup_elem(efs_bpf_map_fd, &pid, &new_cons)) < 0) {
    return;
  }
  // if the bpf recorded task_consumption is not newer, then no updates
  if (!(new_cons.timestamp > old_cons.timestamp)) {
    return;
  }

  pid_to_consumption_buffer[pid].push_back(new_cons);

  // update local task_consumption
  pid_to_task_consumption[pid] = {.energy = new_cons.energy,
                                  .time = new_cons.time,
                                  .timestamp = new_cons.timestamp};

  // calculate new watts
  uint64_t energy_delta = new_cons.energy - old_cons.energy;
  uint64_t time_delta = new_cons.time - old_cons.time;
  if (time_delta <= 0 || energy_delta <= 0) {
    return;
  }
  double new_watts = (double)(energy_delta) / (double)(time_delta)-base_watts;
  if (new_watts < 0) {
    new_watts = 0;
  }

  auto it = pid_to_watts.find(pid);
  if (it == pid_to_watts.end()) {
    pid_to_watts[pid] = new_watts;
  } else {
    pid_to_watts[pid] = (1 - EFS_ENERGY_GAMMA) * pid_to_watts[pid] +
                        EFS_ENERGY_GAMMA * new_watts;
  }

  ComputeMinMaxWatts();
}

void Wattmeter::ComputeMinMaxWatts() {
  auto it = pid_to_watts.begin();
  min_watts = it->second;
  max_watts = it->second;
  for (it++; it != pid_to_watts.end(); it++) {
    if (min_watts > it->second) {
      min_watts = it->second;
    }
    if (max_watts < it->second) {
      max_watts = it->second;
    }
  }
}

bool Wattmeter::ReachedLimit(Gtid gtid) {
  absl::MutexLock lock(&mu_);

  pid_t pid = gtid.tid();

  if (pid % 3 == 0) {
    return false;
  }

  if (pid_to_consumption_buffer.find(pid) == pid_to_consumption_buffer.end()) {
    return false;
  }

  // current time
  uint64_t currentTime =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();

  while (pid_to_consumption_buffer[pid].size() > 0 &&
         pid_to_consumption_buffer[pid].front().timestamp <=
             currentTime - 1000000000) {
    pid_to_consumption_buffer[pid].pop_front();
  }

  if (pid_to_consumption_buffer[pid].size() == 0) {
    return false;
  }

  uint64_t energy = pid_to_consumption_buffer[pid].back().energy -
                    pid_to_consumption_buffer[pid].front().energy;

  if (energy > (25769803776 * 5.0/6.0)) {
    printf("%ld\n", energy);
    return true;
  }

  return false;
}

} // namespace ghost
