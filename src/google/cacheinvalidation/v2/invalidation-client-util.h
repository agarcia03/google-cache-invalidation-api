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

// Useful utility functions for the TICL

#ifndef GOOGLE_CACHEINVALIDATION_V2_INVALIDATION_CLIENT_UTIL_H_
#define GOOGLE_CACHEINVALIDATION_V2_INVALIDATION_CLIENT_UTIL_H_

#include "google/cacheinvalidation/v2/system-resources.h"
#include "google/cacheinvalidation/v2/time.h"

namespace invalidation {

class InvalidationClientUtil {
 public:
  /* Returns the time in milliseconds. */
  static int64 GetTimeInMillis(const Time& time) {
    return time.ToInternalValue() / Time::kMicrosecondsPerMillisecond;
  }

  /* Returns the current time in the scheduler's epoch in milliseconds. */
  static int64 GetCurrentTimeMs(Scheduler* scheduler) {
    return GetTimeInMillis(scheduler->GetCurrentTime());
  }
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_INVALIDATION_CLIENT_UTIL_H_
