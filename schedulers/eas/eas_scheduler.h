#ifndef GHOST_SCHEDULERS_EAS_SCHEDULER_H
#define GHOST_SCHEDULERS_EAS_SCHEDULER_H

#include "lib/agent.h"
#include "lib/scheduler.h"

namespace ghost {

struct EasTask : public Task<> {
  enum class RunState {
    kBlocked,
    kQueued,
    kOnCpu,
    kPaused,
  };

  explicit EasTask(Gtid Eas_task_gtid, ghost_sw_info sw_info)
      : Task<>(Eas_task_gtid, sw_info) {}
  ~EasTask() override {}

  bool paused() const { return run_state == RunState::kPaused; }
  bool blocked() const { return run_state == RunState::kBlocked; }
  bool queued() const { return run_state == RunState::kQueued; }
  bool oncpu() const { return run_state == RunState::kOnCpu; }

  static std::string_view RunStateToString(EasTask::RunState run_state) {
    switch (run_state) {
    case EasTask::RunState::kBlocked:
      return "Blocked";
    case EasTask::RunState::kQueued:
      return "Queued";
    case EasTask::RunState::kOnCpu:
      return "OnCpu";
    case EasTask::RunState::kPaused:
      return "Paused";
      // We will get a compile error if a new member is added to the
      // `EasTask::RunState` enum and a corresponding case is not added here.
    }
    CHECK(false);
    return "Unknown run state";
  }

  friend inline std::ostream &operator<<(std::ostream &os,
                                         EasTask::RunState run_state) {
    return os << RunStateToString(run_state);
  }

  RunState run_state = RunState::kBlocked;

  void CalculateSchedEnergy();

  // Comparator for min-heap runqueue.
  struct SchedEnergyGreater {
    // Returns true if 'a' should be ordered after 'b' in the min-heap
    // and false otherwise.
    bool operator()(EasTask *a, EasTask *b) const {
      return true; // TODO: compare energy
    }
  };
};

class EasScheduler : public BasicDispatchScheduler<EasTask> {
public:
  explicit EasScheduler(Enclave *enclave, CpuList cpulist,
                        std::shared_ptr<TaskAllocator<EasTask>> allocator);
  ~EasScheduler() final {}

  void Schedule(const Cpu &cpu, const StatusWord &sw);

  void EnclaveReady() final;
  Channel &GetDefaultChannel() final { return *default_channel_; };

  bool Empty(const Cpu &cpu) {
    CpuState *cs = cpu_state(cpu);
    return cs->run_queue.Empty();
  }

  void DumpState(const Cpu &cpu, int flags) final;
  std::atomic<bool> debug_runqueue_ = false;

  int CountAllTasks() {
    int num_tasks = 0;
    allocator()->ForEachTask([&num_tasks](Gtid gtid, const EasTask *task) {
      ++num_tasks;
      return true;
    });
    return num_tasks;
  }

  static constexpr int kDebugRunqueue = 1;
  static constexpr int kCountAllTasks = 2;

protected:
  void TaskNew(EasTask *task, const Message &msg) final;
  void TaskRunnable(EasTask *task, const Message &msg) final;
  void TaskDeparted(EasTask *task, const Message &msg) final;
  void TaskDead(EasTask *task, const Message &msg) final;
  void TaskYield(EasTask *task, const Message &msg) final;
  void TaskBlocked(EasTask *task, const Message &msg) final;
  void TaskPreempted(EasTask *task, const Message &msg) final;
  void TaskSwitchto(EasTask *task, const Message &msg) final;

private:
  void EasSchedule(const Cpu &cpu, BarrierToken agent_barrier,
                   bool prio_boosted);
  void TaskOffCpu(EasTask *task, bool blocked, bool from_switchto);
  void TaskOnCpu(EasTask *task, Cpu cpu);
  void Migrate(EasTask *task, Cpu cpu, BarrierToken seqnum);
  Cpu AssignCpu(EasTask *task);
  void DumpAllTasks();

  struct CpuState {
    EasTask *current = nullptr;
    std::unique_ptr<Channel> channel = nullptr;
    EasRq run_queue;
  } ABSL_CACHELINE_ALIGNED;

  inline CpuState *cpu_state(const Cpu &cpu) { return &cpu_states_[cpu.id()]; }

  inline CpuState *cpu_state_of(const EasTask *task) {
    CHECK_GE(task->cpu, 0);
    CHECK_LT(task->cpu, MAX_CPUS);
    return &cpu_states_[task->cpu];
  }

  CpuState cpu_states_[MAX_CPUS];
  Channel *default_channel_ = nullptr;
};

} // namespace ghost
#endif // GHOST_SCHEDULERS_EAS_SCHEDULER_H