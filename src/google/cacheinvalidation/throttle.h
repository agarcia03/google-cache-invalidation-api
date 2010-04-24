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

#ifndef GOOGLE_CACHEINVALIDATION_THROTTLE_H_
#define GOOGLE_CACHEINVALIDATION_THROTTLE_H_

#include <deque>
#include <vector>

#include "base/scoped_ptr.h"
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/time.h"

namespace invalidation {

// Provides an abstraction for multi-level rate-limiting.  For example, the
// default limits state that no more than one message should be sent per second,
// or six per minute.  Rate-limiting is implemented by maintaining a buffer of
// recent messages, which is as large as the highest 'count' property.  Note:
// this means the object consumes space proportional to the _largest_ 'count'.
class Throttle {
 public:
  // Constructs a throttler to enforce the given rate limits for the given
  // listener, using the given system resources.  Ownership of resources is
  // retained by the caller, but the throttle takes ownership of the listener.
  Throttle(const vector<RateLimit>& rate_limits, SystemResources* resources,
           Closure* listener);

  // If calling the listener would not violate the rate limits, does so.
  // Otherwise, schedules a timer to do so as soon as doing so would not violate
  // the rate limits, unless such a timer is already set, in which case does
  // nothing.  I.e., once the rate limit is reached, additional calls are not
  // queued.
  void Fire();

 private:
  // Retries a call to Fire() after some delay.
  void RetryFire() {
    timer_scheduled_ = false;
    Fire();
  }

  // Rate limits to be enforced by this object.
  vector<RateLimit> rate_limits_;

  // System resources for reading the current time and scheduling tasks that
  // need to be delayed.
  SystemResources* resources_;

  // The closure whose calls are throttled.
  scoped_ptr<Closure> listener_;

  // Whether we've already scheduled a deferred call.
  bool timer_scheduled_;

  // A buffer of recent events, so we can determine the length of the interval
  // in which we made the most recent K events.
  deque<Time> recent_event_times_;

  // The maximum size of the recent_event_times_ buffer.
  int max_recent_events_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_THROTTLE_H_
