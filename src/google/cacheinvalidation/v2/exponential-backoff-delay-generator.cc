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

#include "google/cacheinvalidation/v2/exponential-backoff-delay-generator.h"

namespace invalidation {

TimeDelta ExponentialBackoffDelayGenerator::GetNextDelay() {
  TimeDelta delay = TimeDelta();  // After a reset, delay is zero.
  if (in_retry_mode) {
    delay = random_->RandDouble() * current_max_delay_;

    // Adjust the max for the next run.
    if (current_max_delay_ <= max_delay_) {  // Guard against overflow.
      current_max_delay_ *= 2;
      if (current_max_delay_ > max_delay_) {
        current_max_delay_ = max_delay_;
      }
    }
  }
  in_retry_mode = true;
  return delay;
}
}
