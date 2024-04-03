// Copyright 2022 Google LLC
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "schedulers/eas/eas_scheduler.h"

#include <sys/timerfd.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/numeric/int128.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "lib/agent.h"
#include "lib/logging.h"
#include "lib/topology.h"

#include "schedulers/eas/energy_worker.h"

#define DPRINT_EAS(level, message)                                             \
  do {                                                                         \
    if (ABSL_PREDICT_TRUE(verbose() < level))                                  \
      break;                                                                   \
    absl::FPrintF(stderr, "DEAS: [%.6f] cpu %d: %s\n",                         \
                  absl::ToDoubleSeconds(MonotonicNow() - start),               \
                  sched_getcpu(), message);                                    \
  } while (0)

// TODO: Remove this flag after we test idle load balancing
// thoroughly.
ABSL_FLAG(bool, experimental_enable_idle_load_balancing, true,
          "Experimental flag to enable idle load balancing.");

namespace ghost {

void PrintDebugTaskMessage(std::string message_name, CpuState *cs,
                           EasTask *task) {
  DPRINT_EAS(2, absl::StrFormat("[%s]: %s with state %s, %scurrent",
                                message_name, task->gtid.describe(),
                                absl::FormatStreamed(task->task_state),
                                (cs && cs->current == task) ? "" : "!"));
}

EasScheduler::EasScheduler(Enclave *enclave, CpuList cpulist,
                           std::shared_ptr<TaskAllocator<EasTask>> allocator,
                           absl::Duration min_granularity,
                           absl::Duration latency)
    : BasicDispatchScheduler(enclave, std::move(cpulist), std::move(allocator)),
      min_granularity_(min_granularity), latency_(latency),
      idle_load_balancing_(
          absl::GetFlag(FLAGS_experimental_enable_idle_load_balancing)) {
  for (const Cpu &cpu : cpus()) {
    CpuState *cs = cpu_state(cpu);
    cs->id = cpu.id();

    // EasRq has a default constructor, meaning these parameters will initially
    // be set to 0, so set them to the correct value.
    {
      absl::MutexLock l(&cs->run_queue.mu_);
      cs->run_queue.SetMinGranularity(min_granularity_);
      cs->run_queue.SetLatency(latency_);
    }

    cs->channel = enclave->MakeChannel(GHOST_MAX_QUEUE_ELEMS, cpu.numa_node(),
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
    absl::FPrintF(stderr, "%-12s%-8d%-8d%-8d\n", gtid.describe(),
                  static_cast<uint32_t>(task->task_state.GetState()),
                  static_cast<uint32_t>(task->task_state.GetOnRq()), task->cpu);
    return true;
  });
}

void EasScheduler::DumpState(const Cpu &cpu, int flags) {
  if (flags & Scheduler::kDumpAllTasks) {
    DumpAllTasks();
  }

  CpuState *cs = cpu_state(cpu);
  EasRq *rq = &cs->run_queue;

  {
    absl::MutexLock l(&cs->run_queue.mu_);
    if (!(flags & Scheduler::kDumpStateEmptyRQ) && !cs->current &&
        cs->run_queue.IsEmpty()) {
      return;
    }

    const EasTask *current = cs->current;
    // TODO: Convert the rest of the FPrintFs and GHOST_DPRINTs to
    // DPRINT_EAS.
    absl::FPrintF(stderr, "SchedState[%d]: %s rq_l=%lu\n", cpu.id(),
                  current ? current->gtid.describe() : "none", rq->Size());
  }
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

  // Enable tick msg delivery here instead of setting AgentConfig.tick_config_
  // because the agent subscribing the default channel (mostly the
  // channel/agent for the front CPU in the enclave) can get CpuTick messages
  // for another CPU in the enclave while this function is trying to associate
  // each agent to its corresponding channel.
  enclave()->SetDeliverTicks(true);
}

// The in kernel SelectTaskRq attempts to do the following:
// - If sched_energy_enabled(), find an energy efficient CPU (not applicable to
// us)
// - If the affine flag is set, walks up the sched domain tree to see if we can
// find a cpu in the same domain as our previous cpu, but that will allow us to
// run sonner
// - If the above two fail, then we find the idlest cpu within the highest level
// sched domain assuming the sd_flag is on
// - If the above fails, we try to find the most idle core inside the same LLC
// assuming WF_TTWU is set
// - Otherwise fallback to the old cpu
// Our EAS agent has no notion of energy efficiency or scheduling domaims. So,
// we can simplify our algorithm to:
// - Check if the current CPU is idle, if so, place it there (this avoids a
// ping)
// - Otherwise, check if our prev_cpu is idle.
// - Otherwise, try to find an idle CPU in the L3 sibiling list of our prev_cpu
// - Otherwise, just use the least utilized CPU
// In general, there are many, many, many heuristic in kernel EAS, so I tried to
// just grab the general idea and translate it to ghost code. In the future, we
// will probably end up tweaking this code.
// TODO: We probably want to favor placing in a L3 cache sibling even if
// there is no idle sibling. To do this, we can introduce a load_bias variable,
// where we consider < load_bias load to be idle.
// TODO: Collect some data about placing on this cpu if idle vs an idle
// L3 sibling.
// TODO: Once we add nice values and possibly a cgroup interface, we
// need to update our load calculating logic from .Size() to something more
// robust.
// NOTE: This is inherently racy, since we only synchronize on individual rq's
// we are not guaranteed to see a consistent view of rq loads.
Cpu EasScheduler::SelectTaskRq(EasTask *task) {
  PrintDebugTaskMessage("SelectTaskRq", nullptr, task);

  uint64_t min_load = UINT64_MAX;
  Cpu min_load_cpu = topology()->cpu(MyCpu());

  // Get the intersection of the CPUs in this enclave and the CPU affinity
  // stored for this task.
  CpuList eligible_cpus = cpus();
  eligible_cpus.Intersection(task->cpu_affinity);
  if (eligible_cpus.Empty()) {
    DPRINT_EAS(3, absl::StrFormat("[%s]: No CPUs eligible for this task.",
                                  task->gtid.describe()));
  }

  // Updates the min cpu load variables and returns true if empty.
  auto update_min = [&min_load, &min_load_cpu,
                     &eligible_cpus](uint64_t this_load, const Cpu &this_cpu) {
    if (eligible_cpus.IsSet(this_cpu)) {
      if (min_load >= this_load) {
        min_load = this_load;
        min_load_cpu = this_cpu;
      }
      return this_load == 0;
    }
    return false;
  };

  // Check if this cpu is empty.
  // NOTE: placing on this cpu is safe as it is in cpus() by virtue of
  // us recieving a message on its queue
  const Cpu this_cpu = topology()->cpu(MyCpu());
  CpuState *cs = cpu_state(this_cpu);
  {
    absl::MutexLock l(&cs->run_queue.mu_);
    if (update_min(cs->run_queue.Size(), this_cpu)) {
      return this_cpu;
    }
  }

  // Check our prev cpu and its siblings
  // NOTE: placing on this cpu is safe as it is in cpus() by virtue of
  // it being a valid cpu beforehand.
  if (task->cpu >= 0) {
    // Check if prev cpu is empty.
    const Cpu prev_cpu = topology()->cpu(task->cpu);
    cs = cpu_state(prev_cpu);
    {
      absl::MutexLock l(&cs->run_queue.mu_);
      if (update_min(cs->run_queue.Size(), prev_cpu)) {
        return prev_cpu;
      }
    }

    // Check if we can find an idle l3 sibling.
    for (const Cpu &cpu : prev_cpu.l3_siblings()) {
      // We can't schedule on this cpu.
      if (!cpus().IsSet(cpu))
        continue;
      cs = cpu_state(cpu);
      {
        absl::MutexLock l(&cs->run_queue.mu_);
        if (update_min(cs->run_queue.Size(), cpu)) {
          return cpu;
        }
      }
    }
  }

  // Check if we can find any idle cpu.
  for (const Cpu &cpu : cpus()) {
    cs = cpu_state(cpu);
    {
      absl::MutexLock l(&cs->run_queue.mu_);
      if (update_min(cs->run_queue.Size(), cpu)) {
        return cpu;
      }
    }
  }

  // We couldn't find an idle cpu, so just use the least loaded one.
  return min_load_cpu;
}

void EasScheduler::StartMigrateTask(EasTask *task) {
  CpuState *cs = cpu_state_of(task);
  cs->run_queue.mu_.AssertHeld();

  cs->run_queue.DequeueTask(task);
  task->task_state.SetOnRq(EasTaskState::OnRq::kMigrating);
  cs->migration_queue.EnqueueTask(task);
}

void EasScheduler::StartMigrateCurrTask() {
  int my_cpu = MyCpu();
  CpuState *cs = &cpu_states_[my_cpu];
  cs->run_queue.mu_.AssertHeld();

  EasTask *task = cs->current;
  CHECK_EQ(task->cpu, my_cpu);

  cs->current = nullptr;
  task->task_state.SetState(EasTaskState::State::kRunnable);
  StartMigrateTask(task);
}

bool EasScheduler::Migrate(EasTask *task, Cpu cpu, BarrierToken seqnum) {
  // The task is not visible to anyone except the agent currently proccessing
  // the task as the only way to get to migrate is if the task is not
  // currently on a rq, so it would be impossible for anyone else to touch the
  // task.
  //
  // In the future, when we add load balancing or work stealing, it still would
  // only be possible to "see" the task once it is on a rq as there is not a
  // place where we modify task state outside of getting a pointer to it from an
  // rq. Since it isn't on the rq yet, there is not anyway for anything else to
  // modify the task state at the same time.
  //
  // Even if we got a task_departed message at the same time as we are executing
  // Migrate, there is no channel associated with the task yet, so it'll go to
  // the default channel, which is proccessed by the agent executing Migrate.
  CpuState *cs = cpu_state(cpu);
  const Channel *channel = cs->channel.get();

  // Short-circuit if we are trying to migrate to the same cpu.
  if (task->cpu == cpu.id()) {
    absl::MutexLock l(&cs->run_queue.mu_);
    if (task->task_state.IsRunnable()) {
      cs->run_queue.EnqueueTask(task);
    }

    return true;
  }

  // There is a dangerous interleaving where we hang inside AssociateTask, after
  // changing the task's queue from the current CPU A to the target CPU B (the
  // one we are migrating to). Once this happens, we will recieve messages on
  // the new queue. Then, we recieve a TaskDeparted messaged, which deletes the
  // task on another CPU. This leads to a use-after-free bug on the task in
  // question. To avoid those, lock the entire reference to task.
  {
    absl::MutexLock l(&cs->run_queue.mu_);
    if (!channel->AssociateTask(task->gtid, seqnum, /*status=*/nullptr)) {
      GHOST_DPRINT(3, stderr,
                   "Could not associate task %s to cpu %d. This is only "
                   "correct if a TaskDeparted message follows.",
                   task->gtid.describe(), cpu.id());
      return false;
    }

    GHOST_DPRINT(3, stderr, "Migrating task %s to cpu %d",
                 task->gtid.describe(), cpu.id());
    task->cpu = cpu.id();

    if (task->task_state.IsRunnable()) {
      cs->run_queue.EnqueueTask(task);
    }
  }

  // Get the agent's attention so it notices the new task.
  PingCpu(cpu);

  return true;
}

void EasScheduler::MigrateTasks(CpuState *cs) {
  // In MigrateTasks, this agent iterates over the tasks in the migration queue
  // and removes tasks whose migrations succeed. If a task fails to migrate,
  // mostly due to new messages for that task, the task will not be removed
  // from the migration queue and this agent will try to migrate it after the
  // next draining loop.
  if (ABSL_PREDICT_TRUE(cs->migration_queue.IsEmpty())) {
    return;
  }

  cs->migration_queue.DequeueTaskIf([this](const EasMq::MigrationArg &arg) {
    EasTask *task = arg.task;

    CHECK_NE(task, nullptr);
    CHECK(task->task_state.OnRqMigrating()) << task->gtid.describe();

    Cpu cpu =
        arg.dst_cpu < 0 ? SelectTaskRq(task) : topology()->cpu(arg.dst_cpu);

    return Migrate(task, cpu, task->seqnum);
  });
}

void EasScheduler::TaskNew(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_new *payload =
      static_cast<const ghost_msg_payload_task_new *>(msg.payload());

  PrintDebugTaskMessage("TaskNew", nullptr, task);

  CpuList cpu_affinity = MachineTopology()->EmptyCpuList();
  if (GhostHelper()->SchedGetAffinity(task->gtid, cpu_affinity) != 0) {
    // SchedGetAffinity can fail if the task does not exist at this point
    // (ESRCH). One example of such condition is: (i) the task enters ghOSt,
    // (ii) another task moves the task out of ghOSt via `sched_setscheduler`,
    // (iii) the task dies and then (iv) this agent handles the TASK_NEW
    // message.
    DPRINT_EAS(3, absl::StrFormat(
                      "[%s]: Cannot retrieve the CPU mask. Returned errno: %d.",
                      task->gtid.describe(), errno));
    // Fall back to having all the CPUs eligible.
    cpu_affinity = cpus();
  }

  task->cpu = MyCpu();
  task->cpu_affinity = cpu_affinity;
  task->seqnum = msg.seqnum();

  CHECK_GE(payload->nice, EasScheduler::kMinNice);
  CHECK_LE(payload->nice, EasScheduler::kMaxNice);

  task->nice = payload->nice;
  task->weight =
      EasScheduler::kNiceToWeight[task->nice - EasScheduler::kMinNice];
  task->inverse_weight =
      EasScheduler::kNiceToInverseWeight[task->nice - EasScheduler::kMinNice];

  if (payload->runnable) {
    CpuState *cs = cpu_state_of(task);
    cs->run_queue.mu_.AssertHeld();
    task->task_state.SetState(EasTaskState::State::kRunnable);
    task->task_state.SetOnRq(EasTaskState::OnRq::kMigrating);
    cs->migration_queue.EnqueueTask(task);
  } else {
    // Wait until task becomes runnable to avoid race between migration
    // and MSG_TASK_WAKEUP showing up on the default channel.
  }

  energy_state.add_task(task->gtid.id());
}

void EasScheduler::TaskRunnable(EasTask *task, const Message &msg) {
  CpuState *cs = &cpu_states_[task->cpu];
  PrintDebugTaskMessage("TaskRunnable", cs, task);
  cs->run_queue.mu_.AssertHeld();

  // If this is our current task, then we will defer its proccessing until
  // PickNextTask. Otherwise, use the normal wakeup logic.
  if (task->cpu >= 0) {
    if (cs->current == task) {
      cs->current = nullptr;
    }
  }

  task->task_state.SetState(EasTaskState::State::kRunnable);
  task->task_state.SetOnRq(EasTaskState::OnRq::kMigrating);

  cs->migration_queue.EnqueueTask(task);
}

// Disable thread safety analysis as this function is called with rq lock held
// but it's hard for the compiler to infer. Without this annotation, the
// compiler raises safety analysis error.
void EasScheduler::HandleTaskDone(EasTask *task, bool from_switchto)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  CpuState *cs = cpu_state_of(task);
  cs->run_queue.mu_.AssertHeld();

  // Remove any pending migration on this task.
  cs->migration_queue.DequeueTask(task);

  // We might pair the state transition with pulling task of its rq, so lock
  // it. If we don't, we run the risk of the following race: CPU 1:
  // TaskRunnable(T1) CPU 1: T1->state = runnable CPU 5: TaskDeparted(T1) CPU
  // 5: rq->erase(T1) - bad because T1 has not been inserted into the rq yet.
  EasTaskState::State prev_state = task->task_state.GetState();
  task->task_state.SetState(EasTaskState::State::kDone);

  if ((prev_state == EasTaskState::State::kRunning || from_switchto) ||
      prev_state == EasTaskState::State::kRunnable ||
      prev_state == EasTaskState::State::kBlocked) {
    if (cs->current != task) {
      // Remove from the rq and free it.
      cs->run_queue.DequeueTask(task);
      allocator()->FreeTask(task);
      cs->run_queue.UpdateMinVruntime(cs);
    }
    // if cs->current == task, then we will take care of it in PickNextTask.
  } else {
    // Our assertion in ->task_state.Set(), should keep this from every
    // happening.
    DPRINT_EAS(1, absl::StrFormat(
                      "TaskDeparted/Dead cases were not exhaustive, got %s",
                      absl::FormatStreamed(EasTaskState::State(prev_state))));
  }

  energy_state.remove_task(task->gtid.id());
}

void EasScheduler::TaskDeparted(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_departed *payload =
      static_cast<const ghost_msg_payload_task_departed *>(msg.payload());
  CpuState *cs = cpu_state_of(task);
  PrintDebugTaskMessage("TaskDeparted", cs, task);
  cs->run_queue.mu_.AssertHeld();

  HandleTaskDone(task, payload->from_switchto);

  if (payload->from_switchto) {
    Cpu cpu = topology()->cpu(payload->cpu);
    PingCpu(cpu);
  }
}

void EasScheduler::TaskDead(EasTask *task, const Message &msg) {
  CpuState *cs = cpu_state_of(task);
  PrintDebugTaskMessage("TaskDead", cs, task);
  cs->run_queue.mu_.AssertHeld();

  HandleTaskDone(task, false);
}

void EasScheduler::TaskYield(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_yield *payload =
      static_cast<const ghost_msg_payload_task_yield *>(msg.payload());
  Cpu cpu = topology()->cpu(MyCpu());
  CpuState *cs = cpu_state(cpu);
  PrintDebugTaskMessage("TaskYield", cs, task);
  cs->run_queue.mu_.AssertHeld();

  // If this task is not from a switchto chain, it should be the current task on
  // this CPU.
  if (!payload->from_switchto) {
    CHECK_EQ(cs->current, task);
  }

  // The task should be in kDequeued state because only a currently running
  // task can yield.
  CHECK(task->task_state.OnRqDequeued());

  // Updates the task state accordingly. This is safe because this task should
  // be associated with this CPU's agent and protected by this CPU's RQ lock.
  PutPrevTask(task);

  // This task was the last task in a switchto chain on a remote CPU. We should
  // ping the remote CPU to schedule a new task.
  if (payload->cpu != cpu.id()) {
    CHECK(payload->from_switchto);
    PingCpu(topology()->cpu(payload->cpu));
  }
}

void EasScheduler::TaskBlocked(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_blocked *payload =
      static_cast<const ghost_msg_payload_task_blocked *>(msg.payload());
  Cpu cpu = topology()->cpu(MyCpu());
  CpuState *cs = cpu_state(cpu);
  PrintDebugTaskMessage("TaskBlocked", cs, task);
  cs->run_queue.mu_.AssertHeld();

  // If this task is not from a switchto chain, it should be the current task on
  // this CPU.
  if (!payload->from_switchto) {
    CHECK_EQ(cs->current, task);
  }

  // Updates the task state accordingly. This is safe because this task should
  // be associated with this CPU's agent and protected by this CPU's RQ lock.
  if (cs->current == task) {
    cs->current = nullptr;
  }

  task->task_state.SetState(EasTaskState::State::kBlocked);
  // No need to update OnRq state to kDequeued because the task should already
  // be in kDequeued state because only a currently running task can block and
  // it should be in kDequeued state.
  CHECK(task->task_state.OnRqDequeued());

  // This task was the last task in a switchto chain on a remote CPU. We should
  // ping the remote CPU to schedule a new task.
  if (payload->cpu != cpu.id()) {
    CHECK(payload->from_switchto);
    PingCpu(topology()->cpu(payload->cpu));
  }
}

void EasScheduler::TaskPreempted(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_preempt *payload =
      static_cast<const ghost_msg_payload_task_preempt *>(msg.payload());
  Cpu cpu = topology()->cpu(MyCpu());
  CpuState *cs = cpu_state(cpu);
  PrintDebugTaskMessage("TaskPreempted", cs, task);
  cs->run_queue.mu_.AssertHeld();

  // If this task is not from a switchto chain, it should be the current task on
  // this CPU.
  if (!payload->from_switchto) {
    CHECK_EQ(cs->current, task);
  }

  // The task should be in kDequeued state because only a currently running
  // task can be preempted.
  CHECK(task->task_state.OnRqDequeued());

  // Updates the task state accordingly. This is safe because this task should
  // be associated with this CPU's agent and protected by this CPU's RQ lock.
  PutPrevTask(task);

  // This task was the last task in a switchto chain on a remote CPU. We should
  // ping the remote CPU to schedule a new task.
  if (payload->cpu != cpu.id()) {
    CHECK(payload->from_switchto);
    PingCpu(topology()->cpu(payload->cpu));
  }
}

void EasScheduler::TaskSwitchto(EasTask *task, const Message &msg) {
  CpuState *cs = cpu_state_of(task);
  PrintDebugTaskMessage("TaskSwitchTo", cs, task);
  cs->run_queue.mu_.AssertHeld();

  CHECK_EQ(cs->current, task);
  task->task_state.SetState(EasTaskState::State::kBlocked);
  // No need to update OnRq state to kDequeued because the task should be on
  // CPU and therefore in kDequeued state.
  CHECK(task->task_state.OnRqDequeued());
  cs->current = nullptr;
}

// Disable thread safety analysis as this function is called with rq lock held
// but it's hard for the compiler to infer. Without this annotation, the
// compiler raises safety analysis error.
void EasScheduler::CheckPreemptTick(const Cpu &cpu)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  CpuState *cs = cpu_state(cpu);
  cs->run_queue.mu_.AssertHeld();

  if (cs->current) {
    // If we were on cpu, check if we have run for longer than
    // Granularity(). If so, force picking another task via setting current
    // to nullptr.
    if (absl::Nanoseconds(cs->current->status_word.runtime() -
                          cs->current->runtime_at_first_pick_ns) >
        cs->run_queue.MinPreemptionGranularity()) {
      cs->preempt_curr = true;
    }
  }
}

void EasScheduler::PutPrevTask(EasTask *task) {
  CpuState *cs = &cpu_states_[MyCpu()];
  cs->run_queue.mu_.AssertHeld();

  CHECK_NE(task, nullptr);

  // If this task is currently running, kick it off-cpu.
  if (cs->current == task) {
    cs->current = nullptr;
  }

  // We have a notable deviation from the upstream's behavior here. In upstream,
  // put_prev_task does not update the state, while we update the state here.
  task->task_state.SetState(EasTaskState::State::kRunnable);

  // Task affinity no longer allows this CPU to run the task. We should migrate
  // this task.
  if (!task->cpu_affinity.IsSet(task->cpu)) {
    StartMigrateTask(task);
  } else { // Otherwise just add the task into this CPU's run queue.
    cs->run_queue.PutPrevTask(task);
  }
}

void EasScheduler::CpuTick(const Message &msg) {
  const ghost_msg_payload_cpu_tick *payload =
      static_cast<const ghost_msg_payload_cpu_tick *>(msg.payload());
  Cpu cpu = topology()->cpu(payload->cpu);
  CpuState *cs = cpu_state(cpu);
  cs->run_queue.mu_.AssertHeld();

  // We do not actually need any logic in CpuTick for preemption. Since
  // CpuTick messages wake up the agent, EasSchedule will eventually be
  // called, which contains the logic for figuring out if we should run the
  // task that was running before we got preempted the agent or if we should
  // reach into our rb tree.
  CheckPreemptTick(cpu);
}

//-----------------------------------------------------------------------------
// Load Balance
//-----------------------------------------------------------------------------

inline void EasScheduler::AttachTasks(struct LoadBalanceEnv &env) {
  absl::MutexLock l(&env.dst_cs->run_queue.mu_);

  env.dst_cs->run_queue.AttachTasks(env.tasks);
  env.imbalance -= env.tasks.size();
}

inline int EasScheduler::DetachTasks(struct LoadBalanceEnv &env) {
  absl::MutexLock l(&env.src_cs->run_queue.mu_);

  env.src_cs->run_queue.DetachTasks(env.dst_cs, env.imbalance, env.tasks);

  return env.tasks.size();
}

inline int EasScheduler::CalculateImbalance(LoadBalanceEnv &env) {
  // Migrate up to half the tasks src_cpu has more then dst_cpu.
  int src_tasks = env.src_cs->run_queue.LocklessSize();
  int dst_tasks = env.dst_cs->run_queue.LocklessSize();
  int excess = src_tasks - dst_tasks;

  env.imbalance = 0;
  if (excess >= 2) {
    env.imbalance =
        std::min(kMaxTasksToLoadBalance, static_cast<size_t>(excess / 2));
    env.tasks.reserve(env.imbalance);
  }

  return env.imbalance;
}

inline int EasScheduler::FindBusiestQueue() {
  // TODO: Add more logic for better selection of busiest CPU.
  // Upstream handles more cases, in this simplistic implementation we
  // balance only the number of runnable tasks.

  int busiest_runnable_nr = 0;
  int busiest_cpu = 0;
  for (const Cpu &cpu : cpus()) {
    int src_cpu_runnable_nr = cpu_state(cpu)->run_queue.LocklessSize();

    if (src_cpu_runnable_nr <= busiest_runnable_nr)
      continue;

    busiest_runnable_nr = src_cpu_runnable_nr;
    busiest_cpu = cpu.id();
  }

  return busiest_cpu;
}

inline bool EasScheduler::ShouldWeBalance(LoadBalanceEnv &env) {
  // Allow any newly idle CPU to do the newly idle load balance.
  if (env.idle == CpuIdleType::kCpuNewlyIdle) {
    return env.dst_cs->LocklessRqEmpty();
  }

  // Load balance runs from the first idle CPU or if there are no idle CPUs then
  // the first CPU in the enclave.
  int dst_cpu = cpus().Front().id();
  for (const Cpu &cpu : cpus()) {
    CpuState *dst_cs = cpu_state(cpu);
    if (dst_cs->LocklessRqEmpty()) {
      dst_cpu = cpu.id();
      break;
    }
  }

  return dst_cpu == MyCpu();
}

inline int EasScheduler::LoadBalance(CpuState *cs, CpuIdleType idle_type) {
  struct LoadBalanceEnv env;
  int my_cpu = MyCpu();

  env.idle = idle_type;
  env.dst_cs = &cpu_states_[my_cpu];
  if (!ShouldWeBalance(env)) {
    return 0;
  }

  int busiest_cpu = FindBusiestQueue();
  if (busiest_cpu == my_cpu) {
    return 0;
  }

  env.src_cs = &cpu_states_[busiest_cpu];
  if (!CalculateImbalance(env)) {
    return 0;
  }

  int moved_tasks_cnt = DetachTasks(env);
  if (moved_tasks_cnt) {
    AttachTasks(env);
  }

  return moved_tasks_cnt;
}

inline EasTask *EasScheduler::NewIdleBalance(CpuState *cs) {
  int load_balanced = LoadBalance(cs, CpuIdleType::kCpuNewlyIdle);
  if (load_balanced <= 0) {
    return nullptr;
  }

  absl::MutexLock lock(&cs->run_queue.mu_);
  return cs->run_queue.PickNextTask(nullptr, allocator(), cs);
}

//-----------------------------------------------------------------------------
// Schedule
//-----------------------------------------------------------------------------

void EasScheduler::EasSchedule(const Cpu &cpu, BarrierToken agent_barrier,
                               bool prio_boost) {
  RunRequest *req = enclave()->GetRunRequest(cpu);
  CpuState *cs = cpu_state(cpu);

  EasTask *prev = cs->current;

  if (prio_boost) {
    // If we are currently running a task, we need to put it back onto the
    // queue.
    if (prev) {
      absl::MutexLock l(&cs->run_queue.mu_);
      switch (prev->task_state.GetState()) {
      case EasTaskState::State::kNumStates:
        CHECK(false);
        break;
      case EasTaskState::State::kBlocked:
        break;
      case EasTaskState::State::kDone:
        cs->run_queue.DequeueTask(prev);
        allocator()->FreeTask(prev);
        break;
      case EasTaskState::State::kRunnable:
        // This case exclusively handles a task yield:
        // - TaskYield: task->state goes from kRunning -> kRunnable
        // - PickNextTask: we need to put the task back in the rq.
        cs->run_queue.PutPrevTask(prev);
        break;
      case EasTaskState::State::kRunning:
        cs->run_queue.PutPrevTask(prev);
        prev->task_state.SetState(EasTaskState::State::kRunnable);
        break;
      }

      cs->preempt_curr = false;
      cs->current = nullptr;
      cs->run_queue.UpdateMinVruntime(cs);
    }
    // If we are prio_boost'ed, then we are temporarily running at a higher
    // priority than (kernel) EAS. The purpose of this is so that we can
    // reconcile our state with the fact that any task we wanted to be running
    // on the CPU will no longer be running. In our case, since we only sync
    // up our CpuState in PickNextTask, we can simply RTLA yield. This works
    // because:
    // - We get prio_boosted
    // - We rtla yield
    // - eventually the cpu goes idle
    // - we go directly back into the scheduling loop (without consuming any
    // new messages as none will be generated).
    req->LocalYield(agent_barrier, RTLA_ON_IDLE);
    return;
  }

  cs->run_queue.mu_.Lock();
  EasTask *next = cs->run_queue.PickNextTask(prev, allocator(), cs);
  cs->run_queue.mu_.Unlock();

  if (!next && idle_load_balancing_) {
    next = NewIdleBalance(cs);
  }

  cs->current = next;

  if (next) {
    DPRINT_EAS(3, absl::StrFormat("[%s]: Picked via PickNextTask",
                                  next->gtid.describe()));

    req->Open({
        .target = next->gtid,
        .target_barrier = next->seqnum,
        .agent_barrier = agent_barrier,
        .commit_flags = COMMIT_AT_TXN_COMMIT,
    });

    // Although unlikely it's possible for an oncpu task to enter ghOSt on
    // any cpu. In this case there is a race window between producing the
    // MSG_TASK_NEW and getting off that cpu (a race that is exacerbated
    // by EAS dropping the rq->lock in PNT). During this window an agent
    // can observe the MSG_TASK_NEW on the default queue and because the
    // task is runnable it becomes a candidate to be put oncpu immediately.
    //
    // In this case we wait for `next` to fully get offcpu before trying
    // to Commit().
    while (next->status_word.on_cpu()) {
      Pause();
    }

    uint64_t before_runtime = next->status_word.runtime();
    if (req->Commit()) {
      GHOST_DPRINT(3, stderr, "Task %s oncpu %d", next->gtid.describe(),
                   cpu.id());
      // Update task's vruntime, which is the physical runtime multiplied by
      // the inverse of the weight for the task's nice value. We additionally
      // divide the product by 2^22 (right shift by 22 bits) to make a nice
      // value 0's vruntime equal to the wall runtime. This is because the
      // pre-computed weight values are scaled up by 2^10 (the load weight for
      // nice value = 0 becomes 1024). The weight values then get inverted
      // (which turns scale-up to scale-down) and scaled up by 2^32 to
      // pre-compute their inverse weights, leaving us the final scale up of
      // 2^22.
      //
      // i.e., vruntime = wall_runtime / (precomputed_weight / 2^10)
      //         = wall_runtime * 2^10 / precomputed_weight
      //         = wall_runtime * 2^10 / (2^32 / precomputed_inverse_weight)
      //         = wall_runtime * precomputed_inverse_weight / 2^22
      uint64_t runtime = next->status_word.runtime() - before_runtime;
      int energy_score = energy_state.score(next->gtid.id());
      uint32_t energy_inverse_weight = EasScheduler::kNiceToInverseWeight[energy_score - EasScheduler::kMinNice];
      next->vruntime += absl::Nanoseconds(static_cast<uint64_t>(
          static_cast<absl::uint128>(energy_inverse_weight) * static_cast<absl::uint128>(next->inverse_weight) * runtime >> 22));
    } else {
      GHOST_DPRINT(3, stderr, "EasSchedule: commit failed (state=%d)",
                   req->state());
      // If our transaction failed, it is because our agent was stale.
      // Processing the remaining messages will bring our view up to date.
      // Since only the last state of cs->current matters, it is okay to keep
      // cs->current as what was picked by PickNextTask.
    }
  } else {
    req->LocalYield(agent_barrier, 0);
  }
}

void EasScheduler::Schedule(const Cpu &cpu, const StatusWord &agent_sw) {
  BarrierToken agent_barrier = agent_sw.barrier();
  CpuState *cs = cpu_state(cpu);

  GHOST_DPRINT(3, stderr, "Schedule: agent_barrier[%d] = %d\n", cpu.id(),
               agent_barrier);

  Message msg;
  {
    absl::MutexLock l(&cs->run_queue.mu_);
    while (!(msg = Peek(cs->channel.get())).empty()) {
      DispatchMessage(msg);
      Consume(cs->channel.get(), msg);
    }
  }
  MigrateTasks(cs);
  EasSchedule(cpu, agent_barrier, agent_sw.boosted_priority());
}

void EasScheduler::PingCpu(const Cpu &cpu) {
  Agent *agent = enclave()->GetAgent(cpu);
  if (agent) {
    agent->Ping();
  }
}

// Disable thread safety analysis as this function is called with rq lock held
// but it's hard for the compiler to infer. Without this annotation, the
// compiler raises safety analysis error.
void EasScheduler::TaskAffinityChanged(EasTask *task, const Message &msg)
    ABSL_NO_THREAD_SAFETY_ANALYSIS {
  const ghost_msg_payload_task_affinity_changed *payload =
      static_cast<const ghost_msg_payload_task_affinity_changed *>(
          msg.payload());

  // Make sure to remove the task from the current cpu.
  CpuState *cs = cpu_state_of(task);
  cs->run_queue.mu_.AssertHeld();

  CHECK_EQ(task->gtid.id(), payload->gtid);

  CpuList cpu_affinity = MachineTopology()->EmptyCpuList();
  if (GhostHelper()->SchedGetAffinity(task->gtid, cpu_affinity) != 0) {
    DPRINT_EAS(3, absl::StrFormat("[%s]: Cannot retrieve the CPU mask.",
                                  task->gtid.describe()));
    cpu_affinity = cpus();
  }

  task->cpu_affinity = cpu_affinity;

  // Short-circuit if the current CPU is an eligible CPU. In this case, we do
  // not need to do anything here.
  if (task->cpu_affinity.IsSet(task->cpu)) {
    return;
  }

  // The only case we consider here is if the task's state is runnable and it is
  // in a wrong CPU's queue. If the task is currently running and kicked off-cpu
  // by this affinity change, affinity will be honored at TaskPreempted. If the
  // task is blocked, affinity will be honored at wake-up routine. If this task
  // yields, its affinity will be examined at TaskYield.
  if (task->task_state.IsRunnable() && !task->task_state.OnRqMigrating()) {
    StartMigrateTask(task);
  }
}

void EasScheduler::TaskPriorityChanged(EasTask *task, const Message &msg) {
  const ghost_msg_payload_task_priority_changed *payload =
      static_cast<const ghost_msg_payload_task_priority_changed *>(
          msg.payload());

  CpuState *cs = cpu_state_of(task);
  cs->run_queue.mu_.AssertHeld();

  CHECK_EQ(task->gtid.id(), payload->gtid);
  CHECK_GE(payload->nice, EasScheduler::kMinNice);
  CHECK_LE(payload->nice, EasScheduler::kMaxNice);

  task->nice = payload->nice;
  task->weight =
      EasScheduler::kNiceToWeight[task->nice - EasScheduler::kMinNice];
  task->inverse_weight =
      EasScheduler::kNiceToInverseWeight[task->nice - EasScheduler::kMinNice];
}

#ifndef NDEBUG
void EasTaskState::AssertValidTransition(State next) {
  State curr = state_;
  uint64_t valid_states = GetStateTransitionMap().at(next);

  // Check if next is actually a valid state to come from.
  if ((valid_states & (1 << static_cast<uint32_t>(curr))) == 0) {
    DPRINT_EAS(1, absl::StrFormat("[%s]: Cannot go from %s -> %s",
                                  absl::FormatStreamed(task_name_),
                                  absl::FormatStreamed(curr),
                                  absl::FormatStreamed(next)));
    DPRINT_EAS(1, absl::StrFormat("[%s]: Valid transitions -> %s are:",
                                  absl::FormatStreamed(task_name_),
                                  absl::FormatStreamed(next)));

    // Extract all the valid from states.
    for (uint32_t i = 0;
         i < static_cast<uint32_t>(EasTaskState::State::kNumStates); ++i) {
      if ((valid_states & (1 << static_cast<uint32_t>(i))) != 0) {
        DPRINT_EAS(1, absl::StrFormat(
                          "%s", absl::FormatStreamed(EasTaskState::State(i))));
      }
    }

    DPRINT_EAS(1, absl::StrFormat("[%s]: State trace:", task_name_));
    state_trace_.ForEach([this](const FullState &s) {
      DPRINT_EAS(1, absl::StrFormat("[%s]: (%s, %s)", task_name_,
                                    absl::FormatStreamed(s.state),
                                    absl::FormatStreamed(s.on_rq)));
    });

    // We want to crash since we tranisitioned to an invalid state.
    CHECK(false);
  }
}

void EasTaskState::AssertValidTransition(OnRq next) {
  OnRq curr = on_rq_;
  uint64_t valid_states = GetOnRqTransitionMap().at(next);

  // Check if next is actually a valid state to come from.
  if ((valid_states & (1 << static_cast<uint32_t>(curr))) == 0) {
    DPRINT_EAS(1, absl::StrFormat("[%s]: Cannot go from %s -> %s",
                                  absl::FormatStreamed(task_name_),
                                  absl::FormatStreamed(curr),
                                  absl::FormatStreamed(next)));
    DPRINT_EAS(1, absl::StrFormat("[%s]: Valid transitions -> %s are:",
                                  absl::FormatStreamed(task_name_),
                                  absl::FormatStreamed(next)));

    // Extract all the valid from states.
    for (uint32_t i = 0;
         i < static_cast<uint32_t>(EasTaskState::OnRq::kNumStates); ++i) {
      if ((valid_states & (1 << static_cast<uint32_t>(i))) != 0) {
        DPRINT_EAS(1, absl::StrFormat(
                          "%s", absl::FormatStreamed(EasTaskState::OnRq(i))));
      }
    }

    DPRINT_EAS(1, absl::StrFormat("[%s]: State trace:", task_name_));
    state_trace_.ForEach([this](const FullState &s) {
      DPRINT_EAS(1, absl::StrFormat("[%s]: (%s, %s)", task_name_,
                                    absl::FormatStreamed(s.state),
                                    absl::FormatStreamed(s.on_rq)));
    });

    // We want to crash since we tranisitioned to an invalid state.
    CHECK(false);
  }
}

#endif // !NDEBUG

EasRq::EasRq() : min_vruntime_(absl::ZeroDuration()), rq_(&EasTask::Less) {}

void EasRq::EnqueueTask(EasTask *task) {
  CHECK_GE(task->cpu, 0);

  DPRINT_EAS(2, absl::StrFormat("[%s]: Enqueing task", task->gtid.describe()));

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

void EasRq::PutPrevTask(EasTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  CHECK_GE(task->cpu, 0);

  DPRINT_EAS(2,
             absl::StrFormat("[%s]: Putting prev task", task->gtid.describe()));

  InsertTaskIntoRq(task);
}

EasTask *EasRq::PickNextTask(EasTask *prev, TaskAllocator<EasTask> *allocator,
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
    case EasTaskState::State::kNumStates:
      CHECK(false);
      break;
    case EasTaskState::State::kBlocked:
      break;
    case EasTaskState::State::kDone:
      DequeueTask(prev);
      allocator->FreeTask(prev);
      break;
    case EasTaskState::State::kRunnable:
      PutPrevTask(prev);
      break;
    case EasTaskState::State::kRunning:
      // We had the preempt curr flag set, so we need to put our current task
      // back into the rq.
      PutPrevTask(prev);
      prev->task_state.SetState(EasTaskState::State::kRunnable);
      break;
    }
  }

  // First, we reconcile our CpuState with the messaging relating to prev.
  if (IsEmpty()) {
    UpdateMinVruntime(cs);
    return nullptr;
  }

  EasTask *task = LeftmostRqTask();
  DequeueTask(task);
  task->task_state.SetState(EasTaskState::State::kRunning);
  task->runtime_at_first_pick_ns = task->status_word.runtime();

  // min_vruntime is used for Enqueing new tasks. We want to place them at
  // at least the current moment in time. Placing them before min_vruntime,
  // would give them an inordinate amount of runtime on the CPU as they would
  // need to catch up to other tasks that have accummulated a large runtime.
  // For easy access, we cache the value.
  UpdateMinVruntime(cs);
  return task;
}

void EasRq::DequeueTask(EasTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  DPRINT_EAS(2, absl::StrFormat("[%s]: Erasing task", task->gtid.describe()));
  if (rq_.erase(task)) {
    task->task_state.SetOnRq(EasTaskState::OnRq::kDequeued);
    rq_size_.store(rq_.size(), std::memory_order_relaxed);
    return;
  }

  // TODO: Figure out the case where we call DequeueTask, but the task is not
  // actually in the rq. This seems to sporadically happen when processing a
  // TaskDeparted message. In reality, this is harmless as adding a check for
  // is my task in the rq currently would be equivalent.
  // DPRINT_EAS(
  //     1, absl::StrFormat(
  //            "[%s] Attempted to remove task with state %d while not in rq",
  //            task->gtid.describe(), task->task_state.Get()));
  // CHECK(false);
}

void EasRq::UpdateMinVruntime(CpuState *cs) {
  // We want to make sure min_vruntime_ is set to the min of curr's vruntime and
  // the vruntime of our leftmost node. We do this so that:
  // - if curr is immediately placed back into the rq, we don't go back in time
  // wrt vruntime
  // - if a new task is inserted into the rq, it doesn't get treated unfairly
  // wrt to curr
  EasTask *leftmost = LeftmostRqTask();
  EasTask *curr = cs->current;

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

void EasRq::SetMinGranularity(absl::Duration t) {
  min_preemption_granularity_ = t;
}

void EasRq::SetLatency(absl::Duration t) { latency_ = t; }

absl::Duration EasRq::MinPreemptionGranularity() {
  // Get the number of tasks our cpu is handling. As we only call this to check
  // if cs->current should be pulled be preempted, the number of tasks
  // associated with the cpu is rq_.size() + 1;
  std::multiset<EasTask *, bool (*)(EasTask *, EasTask *)>::size_type tasks =
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

void EasRq::InsertTaskIntoRq(EasTask *task) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  task->task_state.SetOnRq(EasTaskState::OnRq::kQueued);
  rq_.insert(task);
  rq_size_.store(rq_.size(), std::memory_order_relaxed);
  min_vruntime_ = (*rq_.begin())->vruntime;
  DPRINT_EAS(2, absl::StrFormat("[%s]: Inserted into run queue",
                                task->gtid.describe()));
}

void EasRq::AttachTasks(const std::vector<EasTask *> &tasks)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  for (EasTask *task : tasks) {
    EnqueueTask(task);
  }
}

int EasRq::DetachTasks(const CpuState *dst_cs, int n,
                       std::vector<EasTask *> &tasks)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(mu_) {
  int tasks_detached = 0;
  for (auto it = rq_.begin(); it != rq_.end();) {
    if (rq_.size() <= 1 || tasks_detached >= n) {
      break;
    }

    EasTask *task = *it;
    CHECK_NE(task, nullptr);
    if (CanMigrateTask(task, dst_cs)) {
      tasks.push_back(task);
      tasks_detached++;

      task->cpu = dst_cs->id;
      task->task_state.SetOnRq(EasTaskState::OnRq::kDequeued);
      it = rq_.erase(it);
      rq_size_.store(rq_.size(), std::memory_order_relaxed);
    } else {
      it++;
    }
  }

  return tasks_detached;
}

bool EasRq::CanMigrateTask(EasTask *task, const CpuState *dst_cs) {
  uint32_t seqnum = task->seqnum.load();

  int dst_cpu = dst_cs->id;
  const Channel *channel = dst_cs->channel.get();

  if (dst_cpu >= 0 && !task->cpu_affinity.IsSet(dst_cpu)) {
    return false;
  }

  if (channel != nullptr &&
      !channel->AssociateTask(task->gtid, seqnum, /*status=*/nullptr)) {
    return false;
  }

  return true;
}

std::unique_ptr<EasScheduler>
MultiThreadedEasScheduler(Enclave *enclave, CpuList cpulist,
                          absl::Duration min_granularity,
                          absl::Duration latency) {
  auto allocator = std::make_shared<ThreadSafeMallocTaskAllocator<EasTask>>();
  auto scheduler = std::make_unique<EasScheduler>(enclave, std::move(cpulist),
                                                  std::move(allocator),
                                                  min_granularity, latency);
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

std::ostream &operator<<(std::ostream &os, EasTaskState::State state) {
  switch (state) {
  case EasTaskState::State::kBlocked:
    return os << "kBlocked";
  case EasTaskState::State::kDone:
    return os << "kDone";
  case EasTaskState::State::kRunning:
    return os << "kRunning";
  case EasTaskState::State::kRunnable:
    return os << "kRunnable";
  case EasTaskState::State::kNumStates:
    return os << "SENTINEL";
  }
}

std::ostream &operator<<(std::ostream &os, EasTaskState::OnRq state) {
  switch (state) {
  case EasTaskState::OnRq::kDequeued:
    return os << "kDequeued";
  case EasTaskState::OnRq::kQueued:
    return os << "kQueued";
  case EasTaskState::OnRq::kMigrating:
    return os << "kMigrating";
  case EasTaskState::OnRq::kNumStates:
    return os << "SENTINEL";
  }
}

std::ostream &operator<<(std::ostream &os, const EasTaskState &state) {
  return os << absl::StrFormat("(%s, %s)",
                               absl::FormatStreamed(state.GetState()),
                               absl::FormatStreamed(state.GetOnRq()));
}

} //  namespace ghost
