#ifndef GHOST_SCHEDULERS_EFS_WATTMETER_H_
#define GHOST_SCHEDULERS_EFS_WATTMETER_H_

#include "absl/synchronization/mutex.h"
#include <unordered_set>
#include <unordered_map>
#include <iostream>
#include "lib/ghost.h"
#include "schedulers/efs/efs_bpf.skel.h"
#include "third_party/bpf/efs_bpf.h"

#define EFS_ENERGY_GAMMA 0.5
#define EFS_ENERGY_SCORE_MAX 2
#define EFS_ENERGY_SCORE_MIN -2
#define EFS_ENERGY_SCORE_DEFAULT 0

namespace ghost {

class Wattmeter {

public:
    Wattmeter(struct efs_bpf *efs_bpf) :
        max_watts(0),
        min_watts(0),
        efs_bpf(efs_bpf)
        {
            efs_bpf_map_fd = bpf_map__fd(efs_bpf->maps.pid_to_consumption);
        }

    void Update(Gtid gtid);
    void AddTask(Gtid gtid);
    void RemoveTask(Gtid gtid);
    double ComputeScore(Gtid gtid);

protected:
    void ComputeMinMaxWatts();

private:
    mutable absl::Mutex mu_;

    std::unordered_map<pid_t, double> pid_to_watts;
    std::unordered_map<pid_t, struct task_consumption> pid_to_task_consumption;
    double max_watts;
    double min_watts;
    struct efs_bpf *efs_bpf;
    int efs_bpf_map_fd;
};
}

#endif // HOST_SCHEDULERS_EFS_WATTMETER_H_
