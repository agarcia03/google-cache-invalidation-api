/*
 * Copyright 2011 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.google.ipc.invalidation.ticl;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.external.client.SystemResources.Scheduler;
import com.google.ipc.invalidation.util.Smearer;
import com.google.ipc.invalidation.util.TypedUtil;

import java.util.HashMap;
import java.util.Map;
import java.util.Random;

/**
 * Class to schedule future operations such that if one has already been scheduled for the same
 * operation, another one is not scheduled.
 *
 */
class OperationScheduler {

  /** Information about an operation. */
  private static class OperationScheduleInfo {
    private final int delayMs;
    private boolean hasBeenScheduled;

    OperationScheduleInfo(int delayMs) {
      this.delayMs = delayMs;
    }
  }

  /** Operations that can be scheduled - key is the actual runnable being scheduled. */
  private final Map<Runnable, OperationScheduleInfo> operations =
      new HashMap<Runnable, OperationScheduleInfo>();
  private final Logger logger;
  private final Scheduler scheduler;
  private final Smearer smearer = new Smearer(new Random());

  OperationScheduler(Logger logger, Scheduler scheduler) {
    this.logger = logger;
    this.scheduler = scheduler;
  }

  /**
   * Informs the scheduler about a new operation that can be scheduled.
   * <p>
   * REQUIRES: has not previously been called for {@code opType}.
   *
   * @param delayMs delay to use when scheduling
   * @param operation implementation of the operation
   */
  void setOperation(int delayMs, Runnable operation) {
    Preconditions.checkState(!TypedUtil.containsKey(operations, operation),
        "operation %s already set", operation);
    Preconditions.checkArgument(delayMs > 0, "delayMs must be positive: %s", delayMs);
    Preconditions.checkNotNull(operation);
    logger.fine("Set %s with delay %s", operation, delayMs);
    operations.put(operation, new OperationScheduleInfo(delayMs));
  }

  /**
   * Changes the existing delay for {@code operation} to be {@code delayMs}.
   * Must be called before the Ticl is started.
   * <p>
   * REQUIRES: an entry for {@code operation} already exists.
   */
  void changeDelayForTest(Runnable operation, int delayMs) {
    OperationScheduleInfo opInfo = TypedUtil.mapGet(operations, operation);
    Preconditions.checkNotNull(opInfo);
    logger.info("Changing delay for %s to be %s ms", operation, delayMs);
    operations.put(operation, new OperationScheduleInfo(delayMs));
  }

  /**
   * Schedules the operation represented by {@code operation}. If the operation is already pending,
   * does nothing.
   * <p>
   * REQUIRES: {@link #setOperation(int, Runnable)} has previously been called for this
   * {@code operation}.
   */
  void schedule(final Runnable operation) {
    final OperationScheduleInfo opInfo =
        Preconditions.checkNotNull(TypedUtil.mapGet(operations, operation));

    // Schedule an event if one has not been already scheduled.
    if (!opInfo.hasBeenScheduled) {
      int delayMs = smearer.getSmearedDelay(opInfo.delayMs);
      logger.fine("Scheduling %s with a delay %s, Now = %s", operation, delayMs,
          scheduler.getCurrentTimeMs());
      opInfo.hasBeenScheduled = true;
      scheduler.schedule(delayMs, new Runnable() {
        @Override
        public void run() {
          opInfo.hasBeenScheduled = false;
          operation.run();
        }
      });
    }
  }
}
