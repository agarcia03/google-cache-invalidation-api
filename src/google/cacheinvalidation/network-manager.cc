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
#include "google/cacheinvalidation/compiler-specific.h"
#include "google/cacheinvalidation/invalidation-client-impl.h"
#include "google/cacheinvalidation/log-macro.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/string_util.h"
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
                    ALLOW_THIS_IN_INITIALIZER_LIST(this),
                    &NetworkManager::DoInformOutboundListener)),
      has_outbound_data_(false),
      outbound_listener_(NULL),
      config_(config),
      next_poll_(Time() - TimeDelta::FromHours(1)),
      next_heartbeat_(Time() - TimeDelta::FromHours(1)),
      poll_delay_(config.initial_polling_interval),
      heartbeat_delay_(config.initial_heartbeat_interval),
      message_number_(0),
      random_(resources->current_time().ToInternalValue()) {
}

void NetworkManager::OutboundDataReady() {
  if (!has_outbound_data_) {
    has_outbound_data_ = true;
    if (outbound_listener_ != NULL) {
      InformOutboundListener();
    }
  }
}

void NetworkManager::ScheduleHeartbeat() {
  Time now = resources_->current_time();
  next_heartbeat_ = now + InvalidationClientImpl::SmearDelay(
      heartbeat_delay_, config_.smear_factor, &random_);
  TLOG(INFO_LEVEL, "Next heartbeat at %d", next_heartbeat_.ToInternalValue());
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
    resources_->ScheduleOnListenerThread(
        NewPermanentCallback(outbound_listener_, run_function, endpoint_));
  }
}

void NetworkManager::AddHeartbeat(ClientToServerMessage* message) {
  CHECK(message->message_type() ==
        ClientToServerMessage_MessageType_TYPE_OBJECT_CONTROL);
  Time now = resources_->current_time();
  if (NeedsHeartbeat()) {
    // Heartbeat required.
    message->set_action(ClientToServerMessage_Action_HEARTBEAT);
    ScheduleHeartbeat();
  }
}

void NetworkManager::FinalizeOutboundMessage(ClientToServerMessage* message) {
  ++message_number_;
  message->set_message_id(StringPrintf("%d", message_number_));
  // Set the protocol version that we want to use.
  VersionManager::GetLatestProtocolVersion(message->mutable_protocol_version());
  // Set the client version.
  VersionManager::GetClientVersion(message->mutable_client_version());
  // Set a timestamp on the message.  Internal time is in microseconds, so
  // divide to get milliseconds.
  message->set_timestamp(resources_->current_time().ToInternalValue() /
                         Time::kMicrosecondsPerMillisecond);
  has_outbound_data_ = false;
}

void NetworkManager::HandleInboundMessage(const ServerToClientMessage& bundle) {
  // Update the heartbeat interval.
  if (bundle.has_next_heartbeat_interval_ms()) {
    int new_heartbeat_interval_ms = bundle.next_heartbeat_interval_ms();
    // Don't accept intervals of 0 or less -- that has to be bad data.
    if (new_heartbeat_interval_ms > 0) {
      TimeDelta new_heartbeat_interval =
          TimeDelta::FromMilliseconds(new_heartbeat_interval_ms);
      if (heartbeat_delay_ != new_heartbeat_interval) {
        TLOG(INFO_LEVEL, "Accepting new heartbeat interval of %d ms",
             new_heartbeat_interval_ms);
        heartbeat_delay_ =
            TimeDelta::FromMilliseconds(new_heartbeat_interval_ms);
        // Schedule the next heartbeat using the new delay.
        ScheduleHeartbeat();
      } else {
        // Heartbeat interval is unchanged: do nothing.
      }
    } else {
      TLOG(INFO_LEVEL, "Ignoring bad server-provided heartbeat delay of %d ms",
           new_heartbeat_interval_ms);
    }
  }
}

}  // namespace invalidation
