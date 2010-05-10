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

NetworkManager::NetworkManager(
    NetworkEndpoint* endpoint, SystemResources* resources,
    const ClientConfig& config)
    : endpoint_(endpoint),
      resources_(resources),
      // Set the throttler up with rate limits defined by the config.
      throttle_(config.rate_limits, resources,
                NewPermanentCallback(
                    this, &NetworkManager::DoInformOutboundListener)),
      has_outbound_data_(false),
      outbound_listener_(NULL),
      last_poll_(Time() - TimeDelta::FromHours(1)),
      last_send_(Time() - TimeDelta::FromHours(1)),
      poll_delay_(config.initial_polling_interval),
      heartbeat_delay_(config.initial_heartbeat_interval) {
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
  throttle_.Fire();
}

void NetworkManager::DoInformOutboundListener() {
  // Explicitness hack here to work around broken callback
  // implementations.
  void (NetworkCallback::*run_function)(NetworkEndpoint* const&) =
      &NetworkCallback::Run;

  // This call may have gotten deferred by the throttler, so check again that we
  // have outbound data before scheduling the ping (the app could have pulled a
  // bundle of its own accord in the mean time).
  if (has_outbound_data_) {
    TLOG(INFO_LEVEL, "scheduling outbound listener");
    resources_->ScheduleImmediately(
        NewPermanentCallback(outbound_listener_, run_function, endpoint_));
  }
}

void NetworkManager::HandleOutboundMessage(ClientToServerMessage* message,
                                           bool is_object_control) {
  Time now = resources_->current_time();
  if (is_object_control && (now >= last_poll_ + poll_delay_)) {
    // If we should poll for invalidations, do so.
    TLOG(INFO_LEVEL, "Adding POLL_INVALIDATIONS action to outbound message");
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
