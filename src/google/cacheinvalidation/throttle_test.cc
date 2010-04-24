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

#include "google/cacheinvalidation/googletest.h"
#include "google/cacheinvalidation/system-resources-for-test.h"
#include "google/cacheinvalidation/throttle.h"

namespace invalidation {

// Tests the throttle.
class ThrottleTest : public testing::Test {
 public:
  ThrottleTest() : call_count_(0) {}

  // Increments the call count.
  void IncrementCounter() {
    ++call_count_;
  }

  // Increments the call count and maintains and checks state to ensure that
  // rate limits are being observed.
  void CheckRateLimits() {
    ++call_count_;
    Time now = resources_->current_time();
    ASSERT_TRUE(now - last_send_ >= TimeDelta::FromSeconds(1));
    last_send_ = now;
    Time min_time = start_time_ + TimeDelta::FromMinutes((call_count_ - 1) / 6);
    ASSERT_TRUE(min_time <= now);
  }

  void SetUp() {
    resources_.reset(new SystemResourcesForTest());
    start_time_ = resources_->current_time();
    call_count_ = 0;
    last_send_ = Time() - TimeDelta::FromHours(1);
  }

  int call_count_;
  Time start_time_;
  Time last_send_;
  scoped_ptr<SystemResourcesForTest> resources_;
};

/* Make a throttler similar to what we expect the Ticl to use and check that it
 * behaves as expected when called at a number of specific times.
 */
TEST_F(ThrottleTest, ThrottlingScripted) {
  resources_->StartScheduler();
  Closure* listener =
      NewPermanentCallback(this, &ThrottleTest::IncrementCounter);

  vector<RateLimit> rate_limits;
  rate_limits.push_back(RateLimit(TimeDelta::FromSeconds(1), 1));
  rate_limits.push_back(RateLimit(TimeDelta::FromMinutes(1), 6));

  scoped_ptr<Throttle> throttle(
      new Throttle(rate_limits, resources_.get(), listener));

  // --> Time is initially 0.

  // The first time we fire(), it should call right away.
  throttle->Fire();
  resources_->RunReadyTasks();
  ASSERT_EQ(1, call_count_);

  // However, if we now fire() a bunch more times within the small interval,
  // there should be no more calls to the listener ...
  for (int i = 0; i < 10; ++i) {
    resources_->ModifyTime(TimeDelta::FromMilliseconds(80));
    throttle->Fire();
    resources_->RunReadyTasks();
    ASSERT_EQ(1, call_count_);
  }

  // --> Time is now 0.8s.

  // ... until the interval passes, at which time it should be called once more.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(200));

  // --> Time is now 1s.
  resources_->RunReadyTasks();
  ASSERT_EQ(2, call_count_);

  // However, the prior fire() calls don't get queued up, so no more calls to
  // the listener will occur unless we fire() again.
  resources_->ModifyTime(TimeDelta::FromSeconds(2));
  resources_->RunReadyTasks();
  ASSERT_EQ(2, call_count_);

  // --> Time is now 3s.

  // At this point, we've fired twice within a few seconds.  We can fire four
  // more times within a minute until we get throttled.
  for (int i = 0; i < 4; ++i) {
    throttle->Fire();
    ASSERT_EQ(3 + i, call_count_);
    resources_->ModifyTime(TimeDelta::FromSeconds(3));
    resources_->RunReadyTasks();
    ASSERT_EQ(3 + i, call_count_);
  }

  // --> Time is now 15s.

  // Now we've sent six times.  If we fire again, nothing should happen.
  throttle->Fire();
  resources_->RunReadyTasks();
  ASSERT_EQ(6, call_count_);

  // Now if we fire slowly, we still shouldn't make calls, since we'd violate
  // the larger rate limit interval.
  for (int i = 0; i < 20; ++i) {
    resources_->ModifyTime(TimeDelta::FromSeconds(2));
    throttle->Fire();
    resources_->RunReadyTasks();
    ASSERT_EQ(6, call_count_);
  }

  // --> Time is now 55s.
  resources_->ModifyTime(TimeDelta::FromSeconds(5));

  // --> Time is now 60s.
  resources_->RunReadyTasks();
  ASSERT_EQ(7, call_count_);

  resources_->StopScheduler();
}

/* Test that if we keep calling fire() every millisecond, we never violate the
 * rate limits, and the expected number of total events is allowed through.
 */
TEST_F(ThrottleTest, ThrottlingStorm) {
  resources_->StartScheduler();
  Closure* listener =
      NewPermanentCallback(this, &ThrottleTest::CheckRateLimits);

  vector<RateLimit> rate_limits;
  rate_limits.push_back(RateLimit(TimeDelta::FromSeconds(1), 1));
  rate_limits.push_back(RateLimit(TimeDelta::FromMinutes(1), 6));

  // Throttler allowing one call per second and six per minute.
  scoped_ptr<Throttle> throttle(
      new Throttle(rate_limits, resources_.get(), listener));

  // For five minutes, call Fire() every ten milliseconds, and make sure the
  // rate limits are respected.
  for (int i = 0; i < 30000; ++i) {
    throttle->Fire();
    resources_->ModifyTime(TimeDelta::FromMilliseconds(10));
    resources_->RunReadyTasks();
  }
  ASSERT_EQ(31, call_count_);
}

}  // namespace invalidation
