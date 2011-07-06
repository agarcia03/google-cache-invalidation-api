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

  /** Next delay time to use. */
  private int currentMaxDelay;

  private final Random random;

  /** Creates a generator with the given maximum and initial delays. */
  public ExponentialBackoffDelayGenerator(Random random, int maxDelay, int initialMaxDelay) {
    Preconditions.checkArgument(maxDelay > 0, "max delay must be positive");
    this.random = Preconditions.checkNotNull(random);
    this.maxDelay = maxDelay;
    reset(initialMaxDelay);
  }

  /** Resets the exponential backoff generator to start delays at the given delay. */
  public void reset(int delay) {
    Preconditions.checkArgument(delay > 0, "initial delay must be positive");
    Preconditions.checkArgument(delay <= maxDelay, "initial delay cannot be more than max delay");
    this.currentMaxDelay = delay;
  }

  /** Gets the next delay interval to use. */
  public int getNextDelay() {

    // Generate the delay.
    int delay = (int) random.nextDouble() * currentMaxDelay;

    // Adjust the max for the next run.
    if (currentMaxDelay <= maxDelay) { // Guard against overflow.
      currentMaxDelay *= 2;
      if (currentMaxDelay > maxDelay) {
        currentMaxDelay = maxDelay;
      }
    }
    return delay;
  }
}
