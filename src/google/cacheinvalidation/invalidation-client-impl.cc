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

#include "google/cacheinvalidation/invalidation-client-impl.h"

#include <string>

#include "google/cacheinvalidation/log-macro.h"
#include "google/cacheinvalidation/stl-namespace.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::string;

const char* InvalidationClientImpl::INVALIDATE_ALL_OBJECT_NAME = "ALL";

void InvalidationClientImpl::PeriodicTask() {
  MutexLock m(&lock_);

  // Check for session data to send.
  bool have_session_data = session_manager_.HasDataToSend();

  // Check for registrations to send.
  bool have_registration_data =
      registration_manager_.DoPeriodicRegistrationCheck();

  // Check to see if we need to send a heartbeat or poll.
  bool should_heartbeat_or_poll = network_manager_.HasDataToSend();

  // If there's no session data to send, and we don't have a session, then we
  // can't send anything.
  if (!have_session_data && !session_manager_.HasSession()) {
    TLOG(INFO_LEVEL,
         "Not sending data since no session and session request in-flight");
  } else if (have_session_data || have_registration_data ||
             should_heartbeat_or_poll) {
    network_manager_.OutboundDataReady();
  }

  // Reschedule the periodic task. The following lines MUST run, or the Ticl
  // will stop working so don't use 'return' statements in this function.
  TimeDelta smeared_delay = SmearDelay(
      config_.periodic_task_interval, config_.smear_factor, &random_);
  resources_->ScheduleWithDelay(
      smeared_delay,
      NewPermanentCallback(this, &InvalidationClientImpl::PeriodicTask));
}

void InvalidationClientImpl::Register(
    const ObjectId& oid, RegistrationCallback* callback) {
  MutexLock m(&lock_);
  TLOG(INFO_LEVEL, "Received register for %d/%s", oid.source(),
       oid.name().string_value().c_str());
  registration_manager_.Register(oid, callback);
}

void InvalidationClientImpl::Unregister(
    const ObjectId& oid, RegistrationCallback* callback) {
  MutexLock m(&lock_);
  TLOG(INFO_LEVEL, "Received unregister for %d/%s", oid.source(),
       oid.name().string_value().c_str());
  registration_manager_.Unregister(oid, callback);
}

void InvalidationClientImpl::HandleNewSession() {
  string client_uniquifier = session_manager_.client_uniquifier();

  TLOG(INFO_LEVEL, "Received new session: %s", client_uniquifier.c_str());

  registration_manager_.RemoveAllOperations();

  // Tell the listener we acquired a session and that its registrations were
  // removed.
  resources_->ScheduleImmediately(
      NewPermanentCallback(
          this, &InvalidationClientImpl::InformListenerOfNewSession));

  // In case we have any pending registrations...
  network_manager_.OutboundDataReady();
}

void InvalidationClientImpl::HandleLostSession() {
  resources_->ScheduleImmediately(
      NewPermanentCallback(
          // Tell the listener we lost our session.
          listener_, &InvalidationListener::SessionStatusChanged, false));
}

void InvalidationClientImpl::HandleObjectControl(
    const ServerToClientMessage& bundle) {
  // Handle registration response.
  for (int i = 0; i < bundle.registration_result_size(); ++i) {
    ProcessRegistrationUpdateResult(bundle.registration_result(i));
  }
  // Process invalidations.
  for (int i = 0; i < bundle.invalidation_size(); ++i) {
    ProcessInvalidation(bundle.invalidation(i));
  }
}

void InvalidationClientImpl::InformListenerOfNewSession() {
  listener_->SessionStatusChanged(true);
  listener_->AllRegistrationsLost(NewPermanentCallback(&DoNothing));
}

void InvalidationClientImpl::HandleInboundMessage(const string& message) {
  MutexLock m(&lock_);

  ServerToClientMessage bundle;
  bundle.ParseFromString(message);

  MessageAction action = session_manager_.ProcessMessage(bundle);

  TLOG(INFO_LEVEL, "Classified inbound message as %d (session token = %s)",
       action, bundle.session_token().c_str());
  switch (action) {
    case IGNORE_MESSAGE:
      TLOG(INFO_LEVEL, "Ignored last received message");
      return;
    case ACQUIRE_SESSION:
      HandleNewSession();
      break;
    case LOSE_SESSION:
      HandleLostSession();
      break;
    case PROCESS_OBJECT_CONTROL:
      HandleObjectControl(bundle);
      break;
    default:
      // Can't happen.
      TLOG(INFO_LEVEL,
           "Unknown message action: %d", action);
      return;  // Don't process the new polling/heartbeat intervals.
  }

  // Let the network manager acquire new polling and heartbeat intervals.  All
  // cases that reach here verified that the message was addressed to this
  // client.
  network_manager_.HandleInboundMessage(bundle);
}

void InvalidationClientImpl::ProcessRegistrationUpdateResult(
    const RegistrationUpdateResult& result) {
  registration_manager_.ProcessRegistrationUpdateResult(result);
}

/* Handles an invalidation. */
void InvalidationClientImpl::ProcessInvalidation(
    const Invalidation& invalidation) {
  Closure* callback =
      NewPermanentCallback(
          this, &InvalidationClientImpl::ScheduleAcknowledgeInvalidation,
          invalidation);

  const ObjectId& oid = invalidation.object_id();
  if ((oid.source() == ObjectId_Source_INTERNAL) &&
      (oid.name().string_value() == INVALIDATE_ALL_OBJECT_NAME)) {
    resources_->ScheduleImmediately(
        NewPermanentCallback(listener_, &InvalidationListener::InvalidateAll,
                             callback));
  } else {
    resources_->ScheduleImmediately(
        NewPermanentCallback(listener_, &InvalidationListener::Invalidate,
                             invalidation, callback));
  }
}

void InvalidationClientImpl::AcknowledgeInvalidation(
    const Invalidation& invalidation) {

  MutexLock m(&lock_);
  pending_invalidation_acks_.push_back(invalidation);
  network_manager_.OutboundDataReady();
}

void InvalidationClientImpl::ScheduleAcknowledgeInvalidation(
    const Invalidation& invalidation) {

  resources_->ScheduleImmediately(
      NewPermanentCallback(this,
                           &InvalidationClientImpl::AcknowledgeInvalidation,
                           invalidation));
}

void InvalidationClientImpl::RegisterOutboundListener(
    NetworkCallback* outbound_message_ready) {

  MutexLock m(&lock_);
  network_manager_.RegisterOutboundListener(outbound_message_ready);
}

void InvalidationClientImpl::TakeOutboundMessage(string* serialized) {
  MutexLock m(&lock_);

  ClientToServerMessage message;
  bool is_object_control = session_manager_.AddSessionAction(&message);
  network_manager_.HandleOutboundMessage(&message, is_object_control);
  if (is_object_control) {
    TLOG(INFO_LEVEL, "Adding data to outbound OBJECT_CONTROL message");

    // Add up to maxRegistrationsPerMessage registrations.
    int sent_count =
        registration_manager_.AddOutboundRegistrationUpdates(&message);
    int invalidation_acks_sent = 0;

    // Add any outbound invalidations, up to max_ops_per_message. We ack the
    // newest invalidations first (since we pop from the array), which is good,
    // because an invalidation for a newer version of an object subsumes an
    // older invalidation.
    while (!pending_invalidation_acks_.empty() &&
           sent_count + invalidation_acks_sent < config_.max_ops_per_message) {
      ++invalidation_acks_sent;
      Invalidation* inv = message.add_acked_invalidation();
      inv->CopyFrom(pending_invalidation_acks_.back());
      // If the invalidation contains a component stamp log, add a client stamp.
      if (inv->has_component_stamp_log()) {
        ComponentStamp* stamp = inv->mutable_component_stamp_log()->add_stamp();
        stamp->set_component("C");  // "C" -> Client.
        // Internal time value is in microseconds; stamp log should be in
        // millis.
        stamp->set_time(resources_->current_time().ToInternalValue() /
                        Time::kMicrosecondsPerMillisecond);
      }
      pending_invalidation_acks_.pop_back();
    }
  }
  message.SerializeToString(serialized);
}

TimeDelta InvalidationClientImpl::SmearDelay(
    TimeDelta base_delay, double smear_factor, Random* random) {
  CHECK(smear_factor >= 0.0);
  CHECK(smear_factor <= 1.0);
  // 2*r - 1 gives us a number in [-1, 1]
  double normalized_rand = random->RandDouble();
  double applied_smear = smear_factor * (2.0 * normalized_rand - 1.0);
  return TimeDelta::FromMicroseconds(
      static_cast<int64>(
          base_delay.InMicroseconds() * (applied_smear + 1.0)));
}

}  // namespace invalidation
