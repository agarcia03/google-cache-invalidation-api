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

#include "google/cacheinvalidation/network-manager.h"

#include <algorithm>

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/invalidation-client-impl.h"
#include "google/cacheinvalidation/log-macro.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/time.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::min;

/* The maximum delay for the timer that checks whether to send a heartbeat.
 */
const int NetworkManager::MAX_TIMER_DELAY_MS = 5 * 1000;

NetworkManager::NetworkManager(
    NetworkEndpoint* endpoint, SystemResources* resources,
    const ClientConfig& config)
    : endpoint_(endpoint), resources_(resources), has_outbound_data_(false),
      outbound_listener_(NULL), last_poll_(Time()), last_send_(Time()),
      poll_delay_(config.initial_polling_interval),
      heartbeat_delay_(config.initial_heartbeat_interval) {

  // Schedule a task that sends a heartbeat if there's ever a period of
  // heartbeatDelay without any outbound network traffic.
  resources_->ScheduleImmediately(
      NewPermanentCallback(this, &NetworkManager::CheckHeartbeat));
}

void NetworkManager::CheckHeartbeat() {
  // High-level logic of this method: read the current time and check whether
  // the heartbeat delay has passed since the last time we asked the app to send
  // a message.  If so, indicate to the application that we have data for it to
  // send.  Otherwise, schedule another task for when we expect the heartbeat
  // delay to pass.
  Time now = resources_->current_time();
  TimeDelta delay;
  if (now >= last_send_ + heartbeat_delay_) {
    // If it's been long enough to schedule a heartbeat, do so.
    TLOG(INFO_LEVEL, "calling OutboundDataReady()");
    OutboundDataReady();
    // Assume that the application will pull a bundle and send right away, so
    // the next time to request a heartbeat will be the heartbeat delay.
    delay = heartbeat_delay_;
  } else {
    // Heartbeat delay hasn't quite passed, so reschedule for when we expect it
    // to pass.
    delay = last_send_ + heartbeat_delay_ - now;
  }
  // Always check at least every MAX_TIMER_DELAY_MS, in case the heartbeat
  // interval changes.
  if (delay > TimeDelta::FromMilliseconds(MAX_TIMER_DELAY_MS)) {
    delay = TimeDelta::FromMilliseconds(MAX_TIMER_DELAY_MS);
  }
  resources_->ScheduleWithDelay(
      delay, NewPermanentCallback(this, &NetworkManager::CheckHeartbeat));
}

void NetworkManager::OutboundDataReady() {
  if (!has_outbound_data_) {
    has_outbound_data_ = true;
    if (outbound_listener_ != NULL) {
      InformOutboundListener();
    }
  }
}

void NetworkManager::RegisterOutboundListener(
    NetworkCallback* outbound_message_ready) {
  CHECK(IsCallbackRepeatable(outbound_message_ready));
  outbound_listener_ = outbound_message_ready;
  if (has_outbound_data_) {
    InformOutboundListener();
  }
}

void NetworkManager::InformOutboundListener() {
  // Explicitness hack here to work around broken callback
  // implementations.
  void (NetworkCallback::*run_function)(NetworkEndpoint* const&) =
      &NetworkCallback::Run;

  TLOG(INFO_LEVEL, "scheduling outbound listener");
  resources_->ScheduleImmediately(
      NewPermanentCallback(outbound_listener_, run_function, endpoint_));
}

void NetworkManager::HandleOutboundMessage(ClientToServerMessage* message,
                                           bool have_session) {
  Time now = resources_->current_time();
  if (have_session && (now >= last_poll_ + poll_delay_)) {
    // If we should poll for invalidations, do so.
    message->set_action(ClientToServerMessage_Action_POLL_INVALIDATIONS);
    last_poll_ = now;
  }
  last_send_ = now;
  has_outbound_data_ = false;
}

void NetworkManager::HandleInboundMessage(const ServerToClientMessage& bundle) {
  // Update the heartbeat and polling delays.
  if (bundle.has_next_heartbeat_interval_ms()) {
    heartbeat_delay_ =
        TimeDelta::FromMilliseconds(bundle.next_heartbeat_interval_ms());
  }
  if (bundle.has_next_poll_interval_ms()) {
    poll_delay_ = TimeDelta::FromMilliseconds(bundle.next_poll_interval_ms());
  }

  // It doesn't make sense to poll more frequently than the heartbeat
  // interval.
  heartbeat_delay_ = min(heartbeat_delay_, poll_delay_);
}

}  // namespace invalidation
