#ifndef GHOST_SCHEDULERS_EAS_ENERGY_WORKER_H_
#define GHOST_SCHEDULERS_EAS_ENERGY_WORKER_H_

#include "absl/synchronization/mutex.h"
#include <unordered_set>
#include <iostream>

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

        // skip if we don't care about this pid
        if (pid_to_tasks.find(pid) == pid_to_tasks.end()) return;


        // update the consumption
        auto it = pid_to_watts.find(pid);
        if (it == pid_to_watts.end()) {
            pid_to_watts[pid] = consumption;
        } else {
            pid_to_watts[pid] = (1 - EAS_ENERGY_GAMMA) * pid_to_watts[pid] + EAS_ENERGY_GAMMA * consumption;
        }

        if (it->second > max_watts) {
            max_watts = it->second;
        } else if (it->second < min_watts) {
            min_watts = it->second;
        }
    }

    pid_t pid_of_tid(pid_t tid) {
        char filename[1024];
        snprintf(filename, sizeof(filename), "/proc/%d/status", tid);

        FILE *file = fopen(filename, "r");
        if (!file) {
            // perror("fopen proc status");
            return -1;
        }

        pid_t pid = -1;
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            if (sscanf(line, "Tgid: %d", &pid) == 1) {
                break;
            }
        }

        fclose(file);
        return pid;
    }

    void add_task(pid_t pid) {
        printf("add_task - tid: %d\n", tid);
        //pid_t pid = pid_of_tid(tid);

        absl::MutexLock lock(&mu_);
        pid_to_tasks[pid].insert(tid);
    }

    void remove_task(pid_t pid) {
        //pid_t pid = pid_of_tid(tid);

        absl::MutexLock lock(&mu_);
        auto it = pid_to_tasks.find(pid);
        if (it == pid_to_tasks.end()) return;

        pid_to_tasks[pid].erase(tid);

        // reference counting reaches 0
        if (pid_to_tasks[pid].size() <= 0) {
            // erase the ref counting entry
            pid_to_tasks.erase(pid);

            // update the energy stuff
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
    }

    int score(pid_t tid) {
        pid_t pid = pid_of_tid(tid);
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

    void print_current_state() {
        absl::MutexLock lock(&mu_);
        for (auto& [p, w] : pid_to_watts) {
            std::cout << "pid: " << p 
                      << "\twatts: " << w 
                      << std::endl;
        }
        std::cout << "min watts: " << min_watts << std::endl;
        std::cout << "max watts: " << max_watts << std::endl;
        std::cout << "--------------------------------- " << std::endl << std::flush;
    }


private:
    mutable absl::Mutex mu_;

    std::unordered_map<pid_t, double> pid_to_watts;
    std::unordered_map<pid_t, std::unordered_set<pid_t>> pid_to_tasks;
    double max_watts;
    double min_watts;
};

extern EnergyState energy_state;
}

#endif // HOST_SCHEDULERS_EAS_ENERGY_WORKER_H_
