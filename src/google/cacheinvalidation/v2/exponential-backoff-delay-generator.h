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

// Class that generates successive intervals for random exponential backoff.
// Class tracks a "high water mark" which is doubled each time getNextDelay is
// called; each call to getNextDelay returns a value uniformly randomly
// distributed between 0 (inclusive) and the high water mark (exclusive). Note
// that this class does not dictate the time units for which the delay is
// computed.

#ifndef GOOGLE_CACHEINVALIDATION_V2_EXPONENTIAL_BACKOFF_DELAY_GENERATOR_H_
#define GOOGLE_CACHEINVALIDATION_V2_EXPONENTIAL_BACKOFF_DELAY_GENERATOR_H_

#include "google/cacheinvalidation/scoped_ptr.h"
#include "google/cacheinvalidation/random.h"
#include "google/cacheinvalidation/v2/logging.h"
#include "google/cacheinvalidation/v2/time.h"

namespace invalidation {

class ExponentialBackoffDelayGenerator {
 public:
  /* Creates a generator with the given maximum and initial delays.
   * random is owned by this after the call.
   */
  ExponentialBackoffDelayGenerator(Random* random, TimeDelta max_delay,
                                   TimeDelta initial_max_delay) :
    max_delay_(max_delay), random_(random) {
    CHECK(max_delay > TimeDelta()) << "max delay must be positive";
    CHECK(random_ != NULL);
    Reset(initial_max_delay);
  }

  /* Resets the exponential backoff generator to start delays at the given
   * delay.
   */
  void Reset(TimeDelta delay) {
    CHECK(delay > TimeDelta()) << "initial delay must be positive";
    CHECK(delay <= max_delay_) << "initial delay cannot be more than max delay";
    current_max_delay_ = delay;
  }

  /* Gets the next delay interval to use. */
  TimeDelta GetNextDelay();

 private:
  /* Maximum allowed delay time. */
  TimeDelta max_delay_;

  /* Next delay time to use. */
  TimeDelta current_max_delay_;

  scoped_ptr<Random> random_;
};
}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_EXPONENTIAL_BACKOFF_DELAY_GENERATOR_H_
