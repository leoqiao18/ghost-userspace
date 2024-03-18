#ifndef GHOST_SCHEDULERS_EAS_SCHEDULER_H
#define GHOST_SCHEDULERS_EAS_SCHEDULER_H

#include "lib/agent.h"
#include "lib/scheduler.h"
#include <queue>

namespace ghost {

struct EasTask : public Task<> {
  enum class RunState {
    kBlocked,
    kQueued,
    kRunnable,
    kOnCpu,
    kPaused,
  };

  explicit EasTask(Gtid Eas_task_gtid, ghost_sw_info sw_info)
      : Task<>(Eas_task_gtid, sw_info) {}
  ~EasTask() override {}

  bool paused() const { return run_state == RunState::kPaused; }
  bool blocked() const { return run_state == RunState::kBlocked; }
  bool queued() const { return run_state == RunState::kQueued; }
  bool runnable() const { return run_state == RunState::kRunnable; }
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
    case EasTask::RunState::kRunnable:
      return "Runnable";
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
  int cpu = -1;

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

template <typename T, class Container = std::vector<T>,
          class Compare = std::less<typename Container::value_type>>
class removable_priority_queue
    : public std::priority_queue<T, Container, Compare> {
public:
  bool remove(const T &value) {
    auto it = std::find(this->c.begin(), this->c.end(), value);

    if (it == this->c.end()) {
      return false;
    }
    if (it == this->c.begin()) {
      // deque the top element
      this->pop();
    } else {
      // remove element and re-heap
      this->c.erase(it);
      std::make_heap(this->c.begin(), this->c.end(), this->comp);
    }
    return true;
  }
};

class EasRq {
public:
  EasRq() = default;
  EasRq(const EasRq &) = delete;
  EasRq &operator=(EasRq &) = delete;

  // Pop the top element in the priority queue
  EasTask *Dequeue();
  // Push into the priority queue
  void Enqueue(EasTask *task);

  // Erase 'task' from the runqueue.
  //
  // Caller must ensure that 'task' is on the runqueue in the first place
  // (e.g. via task->queued()).
  void Erase(EasTask *task);

  size_t Size() const {
    absl::MutexLock lock(&mu_);
    return rq_.size();
  }

  bool Empty() const { return Size() == 0; }

private:
  mutable absl::Mutex mu_;
  // std::vector<EasTask *> rq_ ABSL_GUARDED_BY(mu_);
  removable_priority_queue<EasTask *, std::vector<EasTask *>,
                           EasTask::SchedEnergyGreater>
      rq_ ABSL_GUARDED_BY(mu_);
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

std::unique_ptr<EasScheduler> MultiThreadedEasScheduler(Enclave *enclave,
                                                        CpuList cpulist);

class EasAgent : public LocalAgent {
public:
  EasAgent(Enclave *enclave, Cpu cpu, EasScheduler *scheduler)
      : LocalAgent(enclave, cpu), scheduler_(scheduler) {}

  void AgentThread() override;
  Scheduler *AgentScheduler() const override { return scheduler_; }

private:
  EasScheduler *scheduler_;
};

template <class EnclaveType>
class FullEasAgent : public FullAgent<EnclaveType> {
public:
  explicit FullEasAgent(AgentConfig config) : FullAgent<EnclaveType>(config) {
    scheduler_ =
        MultiThreadedEasScheduler(&this->enclave_, *this->enclave_.cpus());
    this->StartAgentTasks();
    this->enclave_.Ready();
  }

  ~FullEasAgent() override { this->TerminateAgentTasks(); }

  std::unique_ptr<Agent> MakeAgent(const Cpu &cpu) override {
    return std::make_unique<EasAgent>(&this->enclave_, cpu, scheduler_.get());
  }

  void RpcHandler(int64_t req, const AgentRpcArgs &args,
                  AgentRpcResponse &response) override {
    switch (req) {
    case EasScheduler::kDebugRunqueue:
      scheduler_->debug_runqueue_ = true;
      response.response_code = 0;
      return;
    case EasScheduler::kCountAllTasks:
      response.response_code = scheduler_->CountAllTasks();
      return;
    default:
      response.response_code = -1;
      return;
    }
  }

private:
  std::unique_ptr<EasScheduler> scheduler_;
};

} // namespace ghost
#endif // GHOST_SCHEDULERS_EAS_SCHEDULER_H