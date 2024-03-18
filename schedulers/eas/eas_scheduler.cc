#include "schedulers/eas/eas_scheduler.h"

namespace ghost {

EasScheduler::EasScheduler(Enclave *enclave, CpuList cpulist,
                           std::shared_ptr<TaskAllocator<EasTask>> allocator)
    : BasicDispatchScheduler(enclave, std::move(cpulist),
                             std::move(allocator)) {
  for (const Cpu &cpu : cpus()) {
    int node = 0;
    CpuState *cs = cpu_state(cpu);
    cs->channel = enclave->MakeChannel(GHOST_MAX_QUEUE_ELEMS, node,
                                       MachineTopology()->ToCpuList({cpu}));
    // This channel pointer is valid for the lifetime of EasScheduler
    if (!default_channel_) {
      default_channel_ = cs->channel.get();
    }
  }
}

void EasScheduler::DumpAllTasks() {
  fprintf(stderr, "task        state   cpu\n");
  allocator()->ForEachTask([](Gtid gtid, const EasTask *task) {
    absl::FPrintF(stderr, "%-12s%-8d%-8d%c%c\n", gtid.describe(),
                  task->run_state, task->cpu, task->preempted ? 'P' : '-',
                  task->prio_boost ? 'B' : '-');
    return true;
  });
}

void EasScheduler::DumpState(const Cpu &cpu, int flags) {
  if (flags & Scheduler::kDumpAllTasks) {
    DumpAllTasks();
  }

  CpuState *cs = cpu_state(cpu);
  if (!(flags & Scheduler::kDumpStateEmptyRQ) && !cs->current &&
      cs->run_queue.Empty()) {
    return;
  }

  const EasTask *current = cs->current;
  const EasRq *rq = &cs->run_queue;
  absl::FPrintF(stderr, "SchedState[%d]: %s rq_l=%lu\n", cpu.id(),
                current ? current->gtid.describe() : "none", rq->Size());
}

void EasScheduler::EnclaveReady() {
  for (const Cpu &cpu : cpus()) {
    CpuState *cs = cpu_state(cpu);
    Agent *agent = enclave()->GetAgent(cpu);

    // AssociateTask may fail if agent barrier is stale.
    while (!cs->channel->AssociateTask(agent->gtid(), agent->barrier(),
                                       /*status=*/nullptr)) {
      CHECK_EQ(errno, ESTALE);
    }
  }
}

// TODO: maybe use the CPU with least number of tasks
Cpu EasScheduler::AssignCpu(EasTask *task) {
  static auto begin = cpus().begin();
  static auto end = cpus().end();
  static auto next = end;

  if (next == end) {
    next = begin;
  }
  return next++;
}

void EasScheduler::Migrate(EasTask *task, Cpu cpu, BarrierToken seqnum) {
  CHECK_EQ(task->run_state, EasTask::RunState::kRunnable);
  CHECK_EQ(task->cpu, -1);

  CpuState *cs = cpu_state(cpu);
  const Channel *channel = cs->channel.get();
  CHECK(channel->AssociateTask(task->gtid, seqnum, /*status=*/nullptr));

  GHOST_DPRINT(3, stderr, "Migrating task %s to cpu %d", task->gtid.describe(),
               cpu.id());
  task->cpu = cpu.id();

  // Make task visible in the new runqueue *after* changing the association
  // (otherwise the task can get oncpu while producing into the old queue).
  cs->run_queue.EnqueueTask(task);

  // Get the agent's attention so it notices the new task.
  enclave()->GetAgent(cpu)->Ping();
}

void EasScheduler::TaskNew(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_new *payload =
      static_cast<const ghost_msg_payload_task_new *>(msg.payload());

  task->seqnum = msg.seqnum();

  if (payload->runnable) {
    task->run_state = EasTask::RunState::kRunnable;
    Cpu cpu = AssignCpu(task);
    Migrate(task, cpu, msg.seqnum());
  } else {
    task->run_state = EasTask::RunState::kBlocked;
  }
}

void EasScheduler::TaskRunnable(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_wakeup *payload =
      static_cast<const ghost_msg_payload_task_wakeup *>(msg.payload());

  CHECK(task->blocked());
  task->run_state = EasTask::RunState::kRunnable;

  // A non-deferrable wakeup gets the same preference as a preempted task.
  // This is because it may be holding locks or resources needed by other
  // tasks to make progress.
  // task->prio_boost = !payload->deferrable;

  if (task->cpu < 0) {
    // There cannot be any more messages pending for this task after a
    // MSG_TASK_WAKEUP (until the agent puts it oncpu) so it's safe to
    // migrate.
    Cpu cpu = AssignCpu(task);
    Migrate(task, cpu, msg.seqnum());
  } else {
    CpuState *cs = cpu_state_of(task);
    cs->run_queue.Enqueue(task);
  }
}

void EasScheduler::TaskDeparted(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_departed *payload =
      static_cast<const ghost_msg_payload_task_departed *>(msg.payload());

  if (task->oncpu() || payload->from_switchto) {
    TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);
  } else if (task->queued()) {
    CpuState *cs = cpu_state_of(task);
    cs->run_queue.Erase(task);
  } else {
    CHECK(task->blocked());
  }

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }

  allocator()->FreeTask(task);
}

void EasScheduler::TaskDead(EasTask *task, const Message &msg) {
  CHECK(task->blocked());
  allocator()->FreeTask(task);
}

void EasScheduler::TaskYield(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_yield *payload =
      static_cast<const ghost_msg_payload_task_yield *>(msg.payload());

  TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);

  CpuState *cs = cpu_state_of(task);
  cs->run_queue.Enqueue(task);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void EasScheduler::TaskBlocked(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_blocked *payload =
      static_cast<const ghost_msg_payload_task_blocked *>(msg.payload());

  TaskOffCpu(task, /*blocked=*/true, payload->from_switchto);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void EasScheduler::TaskPreempted(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_preempt *payload =
      static_cast<const ghost_msg_payload_task_preempt *>(msg.payload());

  TaskOffCpu(task, /*blocked=*/false, payload->from_switchto);

  // task->prio_boost = true;
  CpuState *cs = cpu_state_of(task);
  cs->run_queue.Enqueue(task);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    enclave()->GetAgent(cpu)->Ping();
  }
}

void EasScheduler::TaskSwitchto(EasTask *task, const Message &msg) {
  TaskOffCpu(task, /*blocked=*/true, /*from_switchto=*/false);
}

void EasScheduler::TaskOffCpu(EasTask *task, bool blocked, bool from_switchto) {
  GHOST_DPRINT(3, stderr, "Task %s offcpu %d", task->gtid.describe(),
               task->cpu);
  CpuState *cs = cpu_state_of(task);

  if (task->oncpu()) {
    CHECK_EQ(cs->current, task);
    cs->current = nullptr;
  } else {
    CHECK(from_switchto);
    CHECK_EQ(task->run_state, EasTask::RunState::kBlocked);
  }

  task->run_state =
      blocked ? EasTask::RunState::kBlocked : EasTask::RunState::kRunnable;
}

void EasScheduler::TaskOnCpu(EasTask *task, Cpu cpu) {
  CpuState *cs = cpu_state(cpu);
  cs->current = task;

  GHOST_DPRINT(3, stderr, "Task %s oncpu %d", task->gtid.describe(), cpu.id());

  task->run_state = EasTask::RunState::kOnCpu;
  task->cpu = cpu.id();
}

void EasScheduler::EasSchedule(const Cpu &cpu, BarrierToken agent_barrier) {
  CpuState *cs = cpu_state(cpu);
  EasTask *next = nullptr;
  // if (!prio_boost) {
  next = cs->current;
  if (!next) {
    next = cs->run_queue.DequeueTask();
  }
  // }

  RunRequest *req = enclave()->GetRunRequest(cpu);
  if (next) {

    while (next->status_word.on_cpu()) {
      Pause();
    }

    req->Open({
        .target = next->gtid,
        .target_barrier = next->seqnum,
        .agent_barrier = agent_barrier,
        .commit_flags = COMMIT_AT_TXN_COMMIT,
    });

    uint64_t before_runtime = next->status_word.runtime();
    if (req->Commit()) {
      GHOST_DPRINT(3, stderr, "EasSchedule %s on cpu %d ",
                   next ? next->gtid.describe() : "idling", cpu.id());

      // Txn commit succeeded and 'next' is oncpu.
      TaskOnCpu(next, cpu);
      uint64_t runtime = next->status_word.runtime() - before_runtime;
      next->vruntime += absl::Nanoseconds(static_cast<uint64_t>(runtime));

    } else {
      GHOST_DPRINT(3, stderr, "EasSchedule: commit failed (state=%d)",
                   req->state());

      if (next == cs->current) {
        TaskOffCpu(next, /*blocked=*/false, /*from_switchto=*/false);
      }

      // Txn commit failed so push 'next' to the front of runqueue.
      // next->prio_boost = true;
      cs->run_queue.Enqueue(next);
    }
  } else {
    int flags = 0;
    if (cs->current || !cs->run_queue.Empty()) {
      flags = RTLA_ON_IDLE;
    }
    req->LocalYield(agent_barrier, flags);
  }
}

void EasScheduler::Schedule(const Cpu &cpu, const StatusWord &agent_sw) {
  BarrierToken agent_barrier = agent_sw.barrier();
  CpuState *cs = cpu_state(cpu);

  GHOST_DPRINT(3, stderr, "Schedule: agent_barrier[%d] = %d\n", cpu.id(),
               agent_barrier);

  Message msg;
  while (!(msg = Peek(cs->channel.get())).empty()) {
    DispatchMessage(msg);
    Consume(cs->channel.get(), msg);
  }

  EasSchedule(cpu, agent_barrier); // agent_sw.boosted_priority()
}

CfsRq::CfsRq() : min_vruntime_(absl::ZeroDuration()), rq_(&CfsTask::Less) {}

void CfsRq::EnqueueTask(CfsTask *task) {
  CHECK_GE(task->cpu, 0);

  DPRINT_CFS(2, absl::StrFormat("[%s]: Enqueing task", task->gtid.describe()));

  // We never want to enqueue a new task with a smaller vruntime that we have
  // currently. We also never want to have a task's vruntime go backwards,
  // so we take the max of our current min vruntime and the tasks current one.
  // Until load balancing is implented, this should just evaluate to
  // min_vruntime_.
  // TODO: come up with more logical way of handling new tasks with
  // existing vruntimes (e.g. migration from another rq).
  task->vruntime = std::max(min_vruntime_, task->vruntime);
  InsertTaskIntoRq(task);
}

void CfsRq::PutPrevTask(CfsTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  CHECK_GE(task->cpu, 0);

  DPRINT_CFS(2,
             absl::StrFormat("[%s]: Putting prev task", task->gtid.describe()));

  InsertTaskIntoRq(task);
}

CfsTask *CfsRq::PickNextTask(CfsTask *prev, TaskAllocator<CfsTask> *allocator,
                             CpuState *cs) {
  // Check if we can just keep running the current task.
  if (prev && prev->task_state.IsRunning() && !cs->preempt_curr) {
    return prev;
  }

  // Past here, we will return a new task to run, so reset our preemption flag.
  cs->preempt_curr = false;

  // Check what happened to our previously running task and reconcile our
  // runqueue. No scheduling decision is made here unless our prev task still
  // wants to be oncpu, then we check if it needs to be preempted or not. If
  // it does not, we just transact prev if it does, then we go through to
  // PickNextTask.
  if (prev) {
    switch (prev->task_state.GetState()) {
    case CfsTaskState::State::kNumStates:
      CHECK(false);
      break;
    case CfsTaskState::State::kBlocked:
      break;
    case CfsTaskState::State::kDone:
      DequeueTask(prev);
      allocator->FreeTask(prev);
      break;
    case CfsTaskState::State::kRunnable:
      PutPrevTask(prev);
      break;
    case CfsTaskState::State::kRunning:
      // We had the preempt curr flag set, so we need to put our current task
      // back into the rq.
      PutPrevTask(prev);
      prev->task_state.SetState(CfsTaskState::State::kRunnable);
      break;
    }
  }

  // First, we reconcile our CpuState with the messaging relating to prev.
  if (IsEmpty()) {
    UpdateMinVruntime(cs);
    return nullptr;
  }

  CfsTask *task = LeftmostRqTask();
  DequeueTask(task);
  task->task_state.SetState(CfsTaskState::State::kRunning);
  task->runtime_at_first_pick_ns = task->status_word.runtime();

  // min_vruntime is used for Enqueing new tasks. We want to place them at
  // at least the current moment in time. Placing them before min_vruntime,
  // would give them an inordinate amount of runtime on the CPU as they would
  // need to catch up to other tasks that have accummulated a large runtime.
  // For easy access, we cache the value.
  UpdateMinVruntime(cs);
  return task;
}

void CfsRq::DequeueTask(CfsTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  DPRINT_CFS(2, absl::StrFormat("[%s]: Erasing task", task->gtid.describe()));
  if (rq_.erase(task)) {
    task->task_state.SetOnRq(CfsTaskState::OnRq::kDequeued);
    rq_size_.store(rq_.size(), std::memory_order_relaxed);
    return;
  }

  // TODO: Figure out the case where we call DequeueTask, but the task is not
  // actually in the rq. This seems to sporadically happen when processing a
  // TaskDeparted message. In reality, this is harmless as adding a check for
  // is my task in the rq currently would be equivalent.
  // DPRINT_CFS(
  //     1, absl::StrFormat(
  //            "[%s] Attempted to remove task with state %d while not in rq",
  //            task->gtid.describe(), task->task_state.Get()));
  // CHECK(false);
}

void CfsRq::UpdateMinVruntime(CpuState *cs) {
  // We want to make sure min_vruntime_ is set to the min of curr's vruntime and
  // the vruntime of our leftmost node. We do this so that:
  // - if curr is immediately placed back into the rq, we don't go back in time
  // wrt vruntime
  // - if a new task is inserted into the rq, it doesn't get treated unfairly
  // wrt to curr
  CfsTask *leftmost = LeftmostRqTask();
  CfsTask *curr = cs->current;

  absl::Duration vruntime = min_vruntime_;

  // If our curr task should/is on the rq then it should be in contention
  // for the min vruntime.
  if (curr) {
    if (curr->task_state.IsRunnable() || curr->task_state.IsRunning()) {
      vruntime = curr->vruntime;
    } else {
      curr = nullptr;
    }
  }

  // non-empty rq
  if (leftmost) {
    if (!curr) {
      vruntime = leftmost->vruntime;
    } else {
      vruntime = std::min(vruntime, leftmost->vruntime);
    }
  }

  min_vruntime_ = std::max(min_vruntime_, vruntime);
}

void CfsRq::SetMinGranularity(absl::Duration t) {
  min_preemption_granularity_ = t;
}

void CfsRq::SetLatency(absl::Duration t) { latency_ = t; }

absl::Duration CfsRq::MinPreemptionGranularity() {
  // Get the number of tasks our cpu is handling. As we only call this to check
  // if cs->current should be pulled be preempted, the number of tasks
  // associated with the cpu is rq_.size() + 1;
  std::multiset<CfsTask *, bool (*)(CfsTask *, CfsTask *)>::size_type tasks =
      rq_.size() + 1;
  if (tasks * min_preemption_granularity_ > latency_) {
    // If we target latency_, each task will run for less than min_granularity
    // so we just return min_granularity_.
    return min_preemption_granularity_;
  }

  // We want ceil(latency_/num_tasks) here. If we take the floor (normal
  // integer division), then we might go below min_granularity in the edge
  // case.
  return (latency_ + absl::Nanoseconds(tasks - 1)) / tasks;
}

void CfsRq::InsertTaskIntoRq(CfsTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  task->task_state.SetOnRq(CfsTaskState::OnRq::kQueued);
  rq_.insert(task);
  rq_size_.store(rq_.size(), std::memory_order_relaxed);
  min_vruntime_ = (*rq_.begin())->vruntime;
  DPRINT_CFS(2, absl::StrFormat("[%s]: Inserted into run queue",
                                task->gtid.describe()));
}

void CfsRq::AttachTasks(const std::vector<CfsTask *> &tasks)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  for (CfsTask *task : tasks) {
    EnqueueTask(task);
  }
}

int CfsRq::DetachTasks(const CpuState *dst_cs, int n,
                       std::vector<CfsTask *> &tasks)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  int tasks_detached = 0;
  for (auto it = rq_.begin(); it != rq_.end();) {
    if (rq_.size() <= 1 || tasks_detached >= n) {
      break;
    }

    CfsTask *task = *it;
    CHECK_NE(task, nullptr);
    if (CanMigrateTask(task, dst_cs)) {
      tasks.push_back(task);
      tasks_detached++;

      task->cpu = dst_cs->id;
      task->task_state.SetOnRq(CfsTaskState::OnRq::kDequeued);
      it = rq_.erase(it);
      rq_size_.store(rq_.size(), std::memory_order_relaxed);
    } else {
      it++;
    }
  }

  return tasks_detached;
}

bool CfsRq::CanMigrateTask(CfsTask *task, const CpuState *dst_cs) {
  // TODO: not suppporting migration for now

  // uint32_t seqnum = task->seqnum.load();

  // int dst_cpu = dst_cs->id;
  // const Channel *channel = dst_cs->channel.get();

  // if (dst_cpu >= 0 && !task->cpu_affinity.IsSet(dst_cpu)) {
  //   return false;
  // }

  // if (channel != nullptr &&
  //     !channel->AssociateTask(task->gtid, seqnum, /*status=*/nullptr)) {
  //   return false;
  // }

  return true;
}

// void EasRq::Enqueue(EasTask *task) {
//   CHECK_GE(task->cpu, 0);
//   CHECK_EQ(task->run_state, EasTask::RunState::kRunnable);

//   task->run_state = EasTask::RunState::kQueued;

//   absl::MutexLock lock(&mu_);
//   task->vruntime = std::max((*rq_.begin())->vruntime, task->vruntime);
//   rq_.push(task);
// }

// EasTask *EasRq::Dequeue() {
//   absl::MutexLock lock(&mu_);
//   if (rq_.empty())
//     return nullptr;

//   EasTask *task = rq_.top();
//   rq_.pop();
//   CHECK(task->queued());
//   task->run_state = EasTask::RunState::kRunnable;
//   return task;
// }

// void EasRq::Erase(EasTask *task) {
//   CHECK_EQ(task->run_state, EasTask::RunState::kQueued);
//   absl::MutexLock lock(&mu_);
//   size_t size = rq_.size();
//   if (size > 0) {
//     // Now search for 'task' from the beginning of the runqueue.
//     rq_.remove(task);
//     return;
//   }
//   CHECK(false);
// }

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
} // namespace ghost