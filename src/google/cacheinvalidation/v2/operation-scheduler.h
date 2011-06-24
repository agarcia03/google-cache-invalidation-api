// Copyright 2011 Google Inc.
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

// Class to schedule future operations such that if one has already been
// scheduled for the same operation, another one is not scheduled.

#ifndef GOOGLE_CACHEINVALIDATION_V2_OPERATION_SCHEDULER_H_
#define GOOGLE_CACHEINVALIDATION_V2_OPERATION_SCHEDULER_H_

#include "google/cacheinvalidation/v2/hash_map.h"
#include "google/cacheinvalidation/v2/invalidation-client-util.h"
#include "google/cacheinvalidation/v2/smearer.h"
#include "google/cacheinvalidation/v2/system-resources.h"

namespace invalidation {

/* Computes hashes of closures (uses the address). */
struct ClosureHashFunction {
  size_t operator()(Closure* closure) const {
    return (size_t) closure;
  }
};

/* Information about an operation. */
struct OperationScheduleInfo {
 public:
  TimeDelta delay;
  bool has_been_scheduled;

  OperationScheduleInfo() {}

  explicit OperationScheduleInfo(TimeDelta init_delay)
      : delay(init_delay), has_been_scheduled(false) {}

  OperationScheduleInfo& operator=(const OperationScheduleInfo& other) {
    delay = other.delay;
    has_been_scheduled = other.has_been_scheduled;
    return *this;
  }
};

class OperationScheduler {
 public:
  OperationScheduler(Logger* logger, Scheduler* scheduler)
      : logger_(logger), scheduler_(scheduler),
        smearer_(
            new Random(InvalidationClientUtil::GetCurrentTimeMs(scheduler))) {}

  /* Informs the scheduler about a new operation that can be scheduled.
   *
   * REQUIRES: has not previously been called for op_type.
   *
   * delay - delay to use when scheduling
   * operation - implementation of the operation
   */
  void SetOperation(TimeDelta delay, Closure* operation);

  /* Changes the existing delay for operation to be delay.
   *
   * REQUIRES: an entry for operation already exists.
   */
  void ChangeDelayForTest(Closure* operation, TimeDelta delay);

  /* Scheduled the operation represented by op_type. If the operation is already
   * pending, does nothing.
   *
   * REQUIRES: SetOperation(int, Closure) has previously been called for
   * this operation.
   */
  void Schedule(Closure* operation);

 private:
  /* Runs the given closure and then sets info->has_been_scheduled to false. */
  static void RunAndClearScheduled(
      Closure* closure, OperationScheduleInfo* info);

  /* Operations that can be scheduled - key is the actual closure being
   * scheduled.
   */
  hash_map<Closure*, OperationScheduleInfo, ClosureHashFunction> operations_;
  Logger* logger_;
  Scheduler* scheduler_;

  /* A smearer to make sure that delays are randomized a little bit. */
  Smearer smearer_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_OPERATION_SCHEDULER_H_
