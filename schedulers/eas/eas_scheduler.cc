#include "schedulers/eas/eas_scheduler.h"

namespace ghost {

EasScheduler::EasScheduler(Enclave* enclave, CpuList cpulist,
                           std::shared_ptr<TaskAllocator<EasTask>> allocator)
    : BasicDispatchScheduler(enclave, std::move(cpulist),
                             std::move(allocator)) {
  for (const Cpu& cpu : cpus()) {
    int node = 0;
    CpuState* cs = cpu_state(cpu);
    cs->channel = enclave->MakeChannel(GHOST_MAX_QUEUE_ELEMS, node,
                                       MachineTopology()->ToCpuList({cpu}));
    // This channel pointer is valid for the lifetime of EasScheduler
    if (!default_channel_) {
      default_channel_ = cs->channel.get();
    }
  }
}

void EasRq::Enqueue(EasTask *task) {
  CHECK_GE(task->cpu, 0);
  CHECK_EQ(task->run_state, EasTask::RunState::kRunnable);

  task->run_state = EasTask::RunState::kQueued;

  absl::MutexLock lock(&mu_);
  rq_.push(task);
}

EasTask *EasRq::Dequeue() {
  absl::MutexLock lock(&mu_);
  if (rq_.empty())
    return nullptr;

  EasTask *task = rq_.top();
  rq_.pop();
  CHECK(task->queued());
  task->run_state = EasTask::RunState::kRunnable;
  return task;
}

void EasRq::Erase(EasTask *task) {
  CHECK_EQ(task->run_state, EasTask::RunState::kQueued);
  absl::MutexLock lock(&mu_);
  size_t size = rq_.size();
  if (size > 0) {
    // Now search for 'task' from the beginning of the runqueue.
    rq_.remove(task);
    return;
  }
  CHECK(false);
}

std::unique_ptr<EasScheduler> MultiThreadedEasScheduler(Enclave *enclave,
                                                        CpuList cpulist) {
  auto allocator = std::make_shared<ThreadSafeMallocTaskAllocator<EasTask>>();
  auto scheduler = std::make_unique<EasScheduler>(enclave, std::move(cpulist),
                                                  std::move(allocator));
  return scheduler;
}

void EasAgent::AgentThread() {
  gtid().assign_name("Agent:" + std::to_string(cpu().id()));
  if (verbose() > 1) {
    printf("Agent tid:=%d\n", gtid().tid());
  }
  SignalReady();
  WaitForEnclaveReady();

  PeriodicEdge debug_out(absl::Seconds(1));

  while (!Finished() || !scheduler_->Empty(cpu())) {
    scheduler_->Schedule(cpu(), status_word());

    if (verbose() && debug_out.Edge()) {
      static const int flags = verbose() > 1 ? Scheduler::kDumpStateEmptyRQ : 0;
      if (scheduler_->debug_runqueue_) {
        scheduler_->debug_runqueue_ = false;
        scheduler_->DumpState(cpu(), Scheduler::kDumpAllTasks);
      } else {
        scheduler_->DumpState(cpu(), flags);
      }
    }
  }
}
}