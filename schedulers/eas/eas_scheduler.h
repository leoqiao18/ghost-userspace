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
      : Task<>(Eas_task_gtid, sw_info), vruntime(absl::ZeroDuration()) {}
  ~EasTask() override {}

  absl::Duration vruntime;

  // std::multiset expects one to pass a strict (< not <=) weak ordering
  // function as a template parameter. Technically, this doesn't have to be
  // inside of the struct, but it seems logical to keep this here.
  static bool Less(EasTask *a, EasTask *b) {
    if (a->vruntime == b->vruntime) {
      return (uintptr_t)a < (uintptr_t)b;
    }
    return a->vruntime < b->vruntime;
  }

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
};

class EasRq {
public:
  explicit EasRq();
  EasRq(const EasRq &) = delete;
  EasRq &operator=(EasRq &) = delete;

  // See EasRq::granularity_ for a description of how these parameters work.
  void SetMinGranularity(absl::Duration t) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);
  void SetLatency(absl::Duration t) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Returns the length of time that the task should run in real time before it
  // is preempted. This value is equivalent to:
  // IF min_granularity * num_tasks > latency THEN min_granularity
  // ELSE latency / num_tasks
  // The purpose of having granularity is so that even if a task has a lot
  // of vruntime to makeup, it doesn't hog all the cputime.
  // TODO: update this when we introduce nice values.
  // NOTE: This needs to be updated everytime we change the number of tasks
  // associated with the runqueue changes. e.g. simply pulling a task out of
  // rq to give it time on the cpu doesn't require a change as we still manage
  // the same number of tasks. But a task blocking, departing, or adding
  // a new task, does require an update.
  absl::Duration MinPreemptionGranularity() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // PickNextTask checks if prev should run again, and if so, returns prev.
  // Otherwise, it picks the task with the smallest vruntime.
  // PickNextTask also is the sync up point for processing state changes to
  // prev. PickNextTask sets the state of its returned task to kOnCpu.
  EasTask *PickNextTask(EasTask *prev, TaskAllocator<EasTask> *allocator,
                        CpuState *cs) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Enqueues a new task or a task that is coming from being blocked.
  void EnqueueTask(EasTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Enqueue a task that is transitioning from being on the cpu to off the cpu.
  void PutPrevTask(EasTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // DequeueTask 'task' from the runqueue. Task must be on rq.
  void DequeueTask(EasTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // The enqueued task with the smallest vruntime, or a nullptr if there are not
  // enqueued tasks.
  EasTask *LeftmostRqTask() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return rq_.empty() ? nullptr : *rq_.begin();
  }

  // Attaches tasks to the run queue in batch.
  void AttachTasks(const std::vector<EasTask *> &tasks_to_attach)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Detaches at most `n` eligible tasks from this run queue and appends to the
  // vector of tasks. A task is eligible for detaching from its source RQ if
  // (i) affinity mask of `task` allows dst_cs->id to run it and (ii) channel
  // association succeeds with task struct's seqnum to dst_cs->channel. Returns
  // the number of tasks detached.
  int DetachTasks(const CpuState *dst_cs, int n,
                  std::vector<EasTask *> &detached_tasks)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Determines whether `task` can be migrated to `dst_cpu`. `task` should be on
  // this run queue. Similar to the upstream kernel implementation of
  // `can_migrate_task`.
  bool CanMigrateTask(EasTask *task, const CpuState *dst_cs);

  // Returns the exact size of the run queue.
  size_t Size() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) { return rq_.size(); }

  // Returns the last known size of the run queue, without acquiring any lock.
  // The returned size might be stale if read from a context other than the
  // agent that owns the queue.
  size_t LocklessSize() const {
    return rq_size_.load(std::memory_order_relaxed);
  }

  bool IsEmpty() const ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
    return rq_.empty();
  }

  // Needs to be called everytime we touch the rq or update a current task's
  // vruntime.
  void UpdateMinVruntime(CpuState *cs) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  // Protects this runqueue and the state of any task assoicated with the rq.
  mutable absl::Mutex mu_;

private:
  // Inserts a task into the backing runqueue.
  // Preconditons: task->vruntime has been set to a logical value.
  void InsertTaskIntoRq(EasTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_);

  absl::Duration min_vruntime_ ABSL_GUARDED_BY(mu_);

  // Unlike in-kernel Eas, we want to have this properties per run-queue instead
  // of system wide.
  absl::Duration min_preemption_granularity_ ABSL_GUARDED_BY(mu_);
  absl::Duration latency_ ABSL_GUARDED_BY(mu_);

  // We use a set as the backing data structure as, according to the
  // C++ standard, it is backed by a red-black tree, which is the backing
  // data structure in Eas in the the kernel. While opaque, using an std::
  // container is easiest way to use a red-black tree short of writing or
  // importing our own.
  std::set<EasTask *, decltype(&EasTask::Less)> rq_ ABSL_GUARDED_BY(mu_);
  // Used for lockless reads of rq size.
  std::atomic<size_t> rq_size_{0};
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