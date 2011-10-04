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

package com.google.ipc.invalidation.util;

import com.google.common.base.Preconditions;

import java.util.Random;

/**
 * Class that generates successive intervals for random exponential backoff. Class tracks a
 * "high water mark" which is doubled each time {@code getNextDelay} is called; each call to
 * {@code getNextDelay} returns a value uniformly randomly distributed between 0 (inclusive) and the
 * high water mark (exclusive). Note that this class does not dictate the time units for which the
 * delay is computed.
 *
 */
public class ExponentialBackoffDelayGenerator {

  /** Maximum allowed delay time. */
  private final int maxDelay;

  /** Initial allowed delay time.*/
  private final int initialMaxDelay;

  /** Next delay time to use. */
  private int currentMaxDelay;

  /** If the first call to {@code getNextDelay} has been made after reset. */
  private boolean inRetryMode;

  private final Random random;

  /** Creates a generator with the given maximum and initial delays. */
  public ExponentialBackoffDelayGenerator(Random random, int maxDelay, int initialMaxDelay) {
    Preconditions.checkArgument(maxDelay > 0, "max delay must be positive");
    this.random = Preconditions.checkNotNull(random);
    this.maxDelay = maxDelay;
    this.initialMaxDelay = initialMaxDelay;
    Preconditions.checkArgument(initialMaxDelay > 0, "initial delay must be positive");
    Preconditions.checkArgument(initialMaxDelay <= maxDelay,
        "initial delay cannot be more than max delay");
    reset();
  }

  /** Resets the exponential backoff generator to start delays at the initial delay. */
  public void reset() {
    this.currentMaxDelay = initialMaxDelay;
    this.inRetryMode = false;
  }

  /** Gets the next delay interval to use. */
  public int getNextDelay() {
    int delay = 0;  // After a reset, the delay is 0.
    if (inRetryMode) {

      // Generate the delay.
      delay = (int) (random.nextDouble() * currentMaxDelay);

      // Adjust the max for the next run.
      if (currentMaxDelay <= maxDelay) { // Guard against overflow.
        currentMaxDelay *= 2;
        if (currentMaxDelay > maxDelay) {
          currentMaxDelay = maxDelay;
        }
      }
    }
    inRetryMode = true;
    return delay;
  }
}
