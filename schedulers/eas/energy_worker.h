#ifndef GHOST_SCHEDULERS_EAS_ENERGY_WORKER_H_
#define GHOST_SCHEDULERS_EAS_ENERGY_WORKER_H_

#include "absl/synchronization/mutex.h"

#define EAS_ENERGY_GAMMA 0.5
#define EAS_ENERGY_SCORE_MAX 15
#define EAS_ENERGY_SCORE_MIN -16
#define EAS_ENERGY_SCORE_DEFAULT 0

namespace ghost {

class EnergyState {

public:
    EnergyState() :
        max_watts(0),
        min_watts(0)
        {}

    void update(pid_t pid, double consumption) {
        absl::MutexLock lock(&mu_);

        auto it = pid_to_watts.find(pid);
        if (it == pid_to_watts.end()) {
            it->second = consumption;
        } else {
            it->second = (1 - EAS_ENERGY_GAMMA) * it->second + EAS_ENERGY_GAMMA * consumption;
        }

        if (it->second > max_watts) {
            max_watts = it->second;
        } else if (it->second < min_watts) {
            min_watts = it->second;
        }
    }

    int score(pid_t pid) {
        absl::MutexLock lock(&mu_);

        auto it = pid_to_watts.find(pid);
        if (it == pid_to_watts.end()) {
            return EAS_ENERGY_SCORE_DEFAULT;
        }

        double this_watts = it->second;

        if ((max_watts - min_watts) == 0) {
            return EAS_ENERGY_SCORE_DEFAULT;
        }

        double k = (this_watts - min_watts) / (max_watts - min_watts);
        int score = (int)((float)(EAS_ENERGY_SCORE_MAX - EAS_ENERGY_SCORE_MIN) * k) + EAS_ENERGY_SCORE_MIN;
        
        return score;
    }


    void remove(pid_t pid) {
        absl::MutexLock lock(&mu_);

        auto it = pid_to_watts.find(pid);
        if (it == pid_to_watts.end()) {
            return;
        }

        double watts = it->second;
        pid_to_watts.erase(pid);

        if (pid_to_watts.size() == 0) {
            max_watts = 0;
            min_watts = 0;
            return;
        }

        if (watts == max_watts) {
            max_watts = 0;
            for (auto [_, v] : pid_to_watts) {
                max_watts = std::max(max_watts, v);
            }
        } else if (watts == min_watts) {
            min_watts = std::numeric_limits<double>::max();
            for (auto [_, v] : pid_to_watts) {
                min_watts = std::min(min_watts, v);
            }
        }
    }

private:
    mutable absl::Mutex mu_;

    std::unordered_map<pid_t, double> pid_to_watts;
    double max_watts;
    double min_watts;
};

extern EnergyState energy_state;
}

#endif // HOST_SCHEDULERS_EAS_ENERGY_WORKER_H_
