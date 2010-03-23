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

const int ClientConfig::DEFAULT_REGISTRATION_TIMEOUT_MS = 60000;  // one minute
const char* InvalidationClientImpl::INVALIDATE_ALL_OBJECT_NAME = "ALL";

void InvalidationClientImpl::Register(
    const ObjectId& oid, RegistrationCallback* callback) {
  MutexLock m(&lock_);
  TLOG(INFO_LEVEL, "Received register for %d/%s", oid.source(),
       oid.name().string_value().c_str());
  registration_manager_.Register(oid, callback);
  network_manager_.OutboundDataReady();
}

void InvalidationClientImpl::Unregister(
    const ObjectId& oid, RegistrationCallback* callback) {
  MutexLock m(&lock_);
  TLOG(INFO_LEVEL, "Received unregister for %d/%s", oid.source(),
       oid.name().string_value().c_str());
  registration_manager_.Unregister(oid, callback);
  network_manager_.OutboundDataReady();
}

void InvalidationClientImpl::HandleRepeatedOperationResult(
    const RegistrationUpdateResult& result) {

  // If the re-registration attempt failed, inform the listener that the
  // registration was removed.
  if ((result.operation().type() == RegistrationUpdate_Type_REGISTER) &&
      (result.status().code() != Status_Code_SUCCESS)) {
    listener_->RegistrationLost(
        result.operation().object_id(),
        // No need to do anything when the application acks, since we're not
        // currently persisting registration state.
        NewPermanentCallback(&DoNothing));
  }
}

void InvalidationClientImpl::HandleNewSession(
    const ServerToClientMessage& bundle) {
  string client_uniquifier = session_manager_.client_uniquifier();

  uint64 last_confirmed_sequence_number =
      bundle.last_operation_sequence_number();

  TLOG(INFO_LEVEL, "HandleNewSession %s", client_uniquifier.c_str());

  registration_manager_.RepeatLostOperations(
      last_confirmed_sequence_number, repeated_op_callback_.get());

  // In case we have any pending registrations...
  network_manager_.OutboundDataReady();
}

void InvalidationClientImpl::HandleInboundMessage(const string& message) {
  MutexLock m(&lock_);

  ServerToClientMessage bundle;
  bundle.ParseFromString(message);

  MessageSessionStatus session_status =
      session_manager_.ClassifyMessage(bundle);

  TLOG(INFO_LEVEL, "Classified inbound message as %d (session token = %s)",
       session_status, bundle.session_token().c_str());
  switch (session_status) {
    case NEW_SESSION:
      HandleNewSession(bundle);

      // Fall through intentionally.
    case EXISTING_SESSION:
      // Handle registration response.
      for (int i = 0; i < bundle.registration_result_size(); ++i) {
        ProcessRegistrationUpdateResult(bundle.registration_result(i));
      }
      // Process invalidations.
      for (int i = 0; i < bundle.invalidation_size(); ++i) {
        ProcessInvalidation(bundle.invalidation(i));
      }
      network_manager_.HandleInboundMessage(bundle);
      break;
    case IGNORE_MESSAGE:
      TLOG(INFO_LEVEL, "Ignored last received message");
      break;
    default:
      TLOG(INFO_LEVEL,
           "Ignored last received message (default classification)");
      break;
  }

  // If, for whatever reason, we don't have a session token, ping the app so
  // we can send an outbound message requesting a session.
  if (session_manager_.session_token() == "") {
    network_manager_.OutboundDataReady();
  }
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
  bool have_session = session_manager_.AddSessionAction(&message);
  network_manager_.HandleOutboundMessage(&message, have_session);
  if (have_session) {
    // Add all registration updates that weren't sent recently, plus all
    // invalidation acks.
    registration_manager_.AddOutboundRegistrationUpdates(&message);
    for (int i = 0; i < pending_invalidation_acks_.size(); ++i) {
      Invalidation* inv = message.add_acked_invalidation();
      inv->CopyFrom(pending_invalidation_acks_[i]);
    }
    pending_invalidation_acks_.clear();
  }
  message.SerializeToString(serialized);
}

}  // namespace invalidation
