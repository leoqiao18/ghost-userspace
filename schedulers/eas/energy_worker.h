#ifndef GHOST_SCHEDULERS_EAS_ENERGY_WORKER_H_
#define GHOST_SCHEDULERS_EAS_ENERGY_WORKER_H_

#include "absl/synchronization/mutex.h"
#include <unordered_set>
#include <iostream>
#include "lib/ghost.h"

#include <nlohmann/json.hpp>
#include <unordered_map>

#define EAS_ENERGY_GAMMA 0.5
#define EAS_ENERGY_SCORE_MAX 3
#define EAS_ENERGY_SCORE_MIN -3
#define EAS_ENERGY_SCORE_DEFAULT 0

namespace ghost {


class EnergyState {

public:
    EnergyState() :
        max_watts(0),
        min_watts(0)
        {}

    void update(nlohmann::json consumers) {
        absl::MutexLock lock(&mu_);

        std::unordered_map<pid_t, double> pid_to_consumption;

        for (auto& c : consumers) {
            pid_to_consumption[c["pid"].get<int>()] = c["consumption"].get<double>();
        }

        for (auto& [pid, _] : pid_to_watts) {
            if (pid_to_consumption.find(pid) == pid_to_consumption.end()) {
                update(pid, 0);
            }
        }

        for (auto& [pid, consumption] : pid_to_consumption) {
            update(pid, consumption);
        }

        if (pid_to_watts.size() == 0) {
            min_watts = 0;
            max_watts = 0;
            return;
        }

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


    void add_task(Gtid gtid) {
        pid_t pid = gtid.tgid();
        pid_t tid = gtid.tid();
        std::cout << "add_task - gtid: " << gtid.id() << ", pid: " << pid << ", tid: " << tid << std::endl;

        absl::MutexLock lock(&mu_);
        pid_to_tasks[pid].insert(tid);
        // printf("size: %ld, set size: %ld\n", pid_to_tasks.size(), pid_to_tasks[pid].size());
    }

    void remove_task(Gtid gtid) {
        pid_t pid = gtid.tgid();
        pid_t tid = gtid.tid();
        std::cout << "remove_task - gtid: " << gtid.id() << ", pid: " << pid << ", tid: " << tid << std::endl;

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

    int score(pid_t pid) {

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

    int score(Gtid gtid) {
        absl::MutexLock lock(&mu_);
        pid_t pid = gtid.tgid();
        return score(pid);
    }

    void print_current_state() {
        absl::MutexLock lock(&mu_);
        std::cout << "* pid (tids)" << std::endl;
        for (auto& [pid, tids] : pid_to_tasks) {
            std::cout << "\t" << pid << " (score: " << score((pid_t) pid) << ") (";
            for (auto& tid : tids) {
                std::cout << tid << ",";
            }
            std::cout << ")" << std::endl;
        }
        std::cout << "* pid (watts)" << std::endl;
        for (auto& [p, w] : pid_to_watts) {
            std::cout << "\t" << p 
                      << "(" << w 
                      << ")" << std::endl;
        }
        std::cout << "* min watts: " << min_watts << std::endl;
        std::cout << "* max watts: " << max_watts << std::endl;
        std::cout << "--------------------------------- " << std::endl << std::flush;
    }


private:
    mutable absl::Mutex mu_;

    std::unordered_map<pid_t, double> pid_to_watts;
    std::unordered_map<pid_t, std::unordered_set<pid_t>> pid_to_tasks;
    double max_watts;
    double min_watts;

    void update(pid_t pid, double consumption) {
        // skip if we don't care about this pid
        if (pid_to_tasks.find(pid) == pid_to_tasks.end()) return;
        
        // update the consumption
        auto it = pid_to_watts.find(pid);
        if (it == pid_to_watts.end()) {
            pid_to_watts[pid] = consumption;
        } else {
            pid_to_watts[pid] = (1 - EAS_ENERGY_GAMMA) * pid_to_watts[pid] + EAS_ENERGY_GAMMA * consumption;
        }
    }
};

extern EnergyState energy_state;
}

#endif // HOST_SCHEDULERS_EAS_ENERGY_WORKER_H_
