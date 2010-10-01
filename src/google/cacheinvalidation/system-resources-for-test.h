// Copyright 2010 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// An implementation of SystemResources for unit testing (in a single-threaded
// environment).  Multiplexes two logical threads (work queues) onto a single
// real thread.

#ifndef GOOGLE_CACHEINVALIDATION_SYSTEM_RESOURCES_FOR_TEST_H_
#define GOOGLE_CACHEINVALIDATION_SYSTEM_RESOURCES_FOR_TEST_H_

#include <cstdarg>
#include <queue>
#include <string>
#include <utility>

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/string_util.h"
#include "google/cacheinvalidation/time.h"

namespace invalidation {

// An entry in the work queue.  Ensures that tasks don't run until their
// scheduled time, and for a given time, they run in the order in which they
// were enqueued.
struct TaskEntry {
  TaskEntry(Time time, bool immediate, int64 id, Closure* task)
      : time(time), immediate(immediate), id(id), task(task) {}

  bool operator<(const TaskEntry& other) const {
    // Priority queue returns *largest* element first.
    return (time > other.time) ||
        ((time == other.time) && (id > other.id));
  }
  Time time;  // the time at which to run
  bool immediate;  // whether the task was scheduled "immediately"
  int64 id;  // the order in which this task was enqueued
  Closure* task;  // the task to be run
};

class SystemResourcesForTest : public SystemResources {
 public:
  SystemResourcesForTest()
      : current_id_(0), started_(false), stopped_(false),
        running_internal_(false) {}

  ~SystemResourcesForTest() {
    StopScheduler();
  }

  virtual Time current_time() {
    return current_time_;
  }

  virtual void StartScheduler() {
    started_ = true;
  }

  virtual void StopScheduler() {
    stopped_ = true;
    while (!work_queue_.empty()) {
      TaskEntry top_elt = work_queue_.top();
      work_queue_.pop();
      // If the task has expired or was scheduled with ScheduleImmediately(),
      // run it.
      if (top_elt.immediate || (top_elt.time <= current_time())) {
        top_elt.task->Run();
      }
      delete top_elt.task;
    }
    while (!listener_work_queue_.empty()) {
      // All listener tasks were to run immediately, so run them all.
      Closure* task = listener_work_queue_.front();
      listener_work_queue_.pop();
      task->Run();
      delete task;
    }
  }

  virtual void ScheduleImmediately(Closure* task) {
    CHECK(IsCallbackRepeatable(task));
    CHECK(started_);
    if (!stopped_) {
      work_queue_.push(TaskEntry(current_time(), true, current_id_++, task));
    } else {
      delete task;
    }
  }

  virtual void ScheduleWithDelay(TimeDelta delay, Closure* task) {
    CHECK(IsCallbackRepeatable(task));
    CHECK(started_);
    if (!stopped_) {
      work_queue_.push(TaskEntry(current_time() + delay, false, current_id_++,
                                 task));
    } else {
      delete task;
    }
  }

  virtual void ScheduleOnListenerThread(Closure* task) {
    CHECK(IsCallbackRepeatable(task));
    CHECK(started_);
    if (!stopped_) {
      listener_work_queue_.push(task);
    } else {
      delete task;
    }
  }

  virtual bool IsRunningOnInternalThread() {
    return running_internal_;
  }

  virtual void Log(LogLevel level, const char* file, int line,
                   const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    string result;
    StringAppendV(&result, format, ap);
    LogMessage(file, line).stream() << result;
    va_end(ap);
  }

  virtual void WriteState(const string& state, StorageCallback* callback) {
    // TODO(ghc): Implement.
  }

  void SetTime(Time new_time) {
    current_time_ = new_time;
  }

  void ModifyTime(TimeDelta delta_time) {
    current_time_ += delta_time;
  }

  // Runs all the work in the queue that should be executed by the current time.
  // Note that tasks run may enqueue additional immediate tasks, and this call
  // won't return until they've completed as well.  While these tasks are
  // running, the running_internal_ flag is set, so IsRunningOnInternalThread()
  // will return true.
  void RunReadyTasks() {
    running_internal_ = true;
    while (RunNextTask()) {
      continue;
    }
    running_internal_ = false;
  }

  // Runs all queued listener tasks.
  void RunListenerTasks() {
    while (!listener_work_queue_.empty()) {
      Closure* task = listener_work_queue_.front();
      listener_work_queue_.pop();
      task->Run();
      delete task;
    }
  }

 private:
  // Attempts to run a task, returning true is there was a task to run.
  bool RunNextTask() {
    if (!work_queue_.empty()) {
      // The queue is not empty, so get the first task and see if its scheduled
      // execution time has passed.
      TaskEntry top_elt = work_queue_.top();
      if (top_elt.time <= current_time()) {
        // The task is scheduled to run in the past or present, so remove it
        // from the queue and run the task.
        work_queue_.pop();
        top_elt.task->Run();
        delete top_elt.task;
        return true;
      }
    }
    return false;
  }

  // The current time, which may be set by the test.
  Time current_time_;

  // The id number of the next task.
  uint64 current_id_;

  // Whether or not the scheduler has been started.
  bool started_;

  // Whether or not the scheduler has been stopped.
  bool stopped_;

  // Whether or not we're currently running internal tasks from the internal
  // queue.
  bool running_internal_;

  // A priority queue on which the actual tasks are enqueued.
  std::priority_queue<TaskEntry> work_queue_;

  // A simple queue for the listener tasks.
  std::queue<Closure*> listener_work_queue_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_SYSTEM_RESOURCES_FOR_TEST_H_
