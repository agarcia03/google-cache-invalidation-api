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

#include "google/cacheinvalidation/registration-update-manager.h"

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/invalidation-client-impl.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/log-macro.h"

namespace invalidation {

// RegistrationInfo definitions.

RegistrationInfo::RegistrationInfo(RegistrationUpdateManager* reg_manager,
                                   const ObjectId& object_id)
    : reg_manager_(reg_manager),
      resources_(reg_manager_->resources_),
      object_id_(object_id),
      latest_known_server_state_(RegistrationUpdate_Type_UNREGISTER) {}

void RegistrationInfo::ProcessRegistrationUpdateResult(
    const RegistrationUpdateResult& result) {
  if (!IsResultValid(result)) {
    // Ignore invalid messages.
    return;
  }
  const RegistrationUpdate& op = result.operation();

  // If this operation has a sequence number older than what we already know,
  // ignore it.
  if ((latest_known_server_seqno_.get() != NULL) &&
      (*latest_known_server_seqno_ >= op.sequence_number())) {
    TLOG(INFO_LEVEL, "ignoring operation on %s with old seqno %lld: ",
         GetObjectName(op.object_id()), op.sequence_number());
    return;
  }

  const Status& status = result.status();
  if (status.code() == Status_Code_SUCCESS) {
    bool matched_previous_state = (latest_known_server_state_ == op.type());

    if (IsInProgress()) {
      if (op.type() == *pending_state_) {
        // We received a message that matched our desired state, so we can
        // consider the operation to be no longer pending.
        TLOG(INFO_LEVEL, "Server message discharges pending on %s: %d",
             GetObjectName(object_id_), op.type());
        pending_state_.reset(NULL);
      } else {
        CHECK(matched_previous_state);
      }
    } else {
      // There's nothing pending: if the known server state is changing, then
      // inform the listener of the change.
      if (!matched_previous_state) {
        TLOG(INFO_LEVEL,
             "Message changes known state for %s to %d; invoking listener",
             GetObjectName(object_id_), op.type());
        InvokeStateChangedCallback(GetStateFromOpType(op.type()),
                                   UnknownHint());
      } else {
        TLOG(INFO_LEVEL,
             "Got message but object %s already in state %d; invoking listener",
             GetObjectName(object_id_), op.type());
      }
    }

    // Update our latest server state from the message.
    TLOG(INFO_LEVEL, "Accepting new state for %s at seqno %d",
         GetObjectName(op.object_id()), op.sequence_number());
    latest_known_server_state_ = op.type();
    latest_known_server_seqno_.reset(new int64(op.sequence_number()));
  } else if (IsInProgress() && (op.sequence_number() == *pending_seqno_)) {
    // Failure case: clear the pending state and inform the listener of the
    // change.
    TLOG(INFO_LEVEL, "Received error with matching seqno for %s",
         GetObjectName(object_id_));
    pending_state_.reset();
    bool is_transient = (status.code() == Status_Code_TRANSIENT_FAILURE);
    UnknownHint unknown_hint(is_transient, status.description());
    InvokeStateChangedCallback(RegistrationState_UNKNOWN, unknown_hint);
  }
}

void RegistrationInfo::ProcessApplicationRequest(
    RegistrationUpdate_Type op_type) {
  if (IsInProgress()) {
    if (GetInProgressType() == op_type) {
      // Already pending for this type, so nothing to do.
      CHECK(latest_known_server_state_ != op_type);
      return;
    } else {
      // We had something pending for the other operation type, but the latest
      // known server state was for the desired type.  So, we can just clear the
      // pending operation and return.  If later the pending operation completes
      // and we switch to the wrong type, we'll tell the app and it can correct
      // things.
      CHECK(latest_known_server_state_ == op_type);
      pending_state_.reset();
      send_time_.reset();
      return;
    }
  } else {
    // Nothing pending.  Nothing to do if the latest known state is what the
    // application is requesting.
    if (latest_known_server_state_ != op_type) {
      // We need to issue a request;
      pending_state_.reset(new RegistrationUpdate_Type(op_type));
      send_time_.reset();
      pending_seqno_.reset(new int64(reg_manager_->current_op_seqno_++));
    }
  }
}

void RegistrationInfo::CheckTimeout(Time now, TimeDelta deadline) {
  // If we don't have anything in progress, we can't time out.
  // TLOG(INFO_LEVEL, "checking timeout");
  if (!IsInProgress()) {
    return;
  }

  // If we're not yet sent, we can't time out.
  if (send_time_.get() == NULL) {
    TLOG(INFO_LEVEL, "%s not timed out since not sent",
         GetObjectName(object_id_));
    return;
  }

  // If we're before the deadline, we haven't timed out.
  if (now < *send_time_ + deadline) {
    TLOG(INFO_LEVEL, "%s not timed out since deadline not exceeded",
         GetObjectName(object_id_));
  }

  // We've timed out.
  UnknownHint unknown_hint(true, "Timed out");
  InvokeStateChangedCallback(RegistrationState_UNKNOWN, unknown_hint);
  pending_state_.reset();
  send_time_.reset();
}

bool RegistrationInfo::HasDataToSend() {
  bool has_data =
      IsInProgress() &&
      (send_time_.get() == NULL) &&
      (*pending_seqno_ <= reg_manager_->maximum_op_seqno_inclusive_);
  if (has_data) {
    CHECK(*pending_state_ != latest_known_server_state_);
  }
  return has_data;
}

void RegistrationInfo::TakeData(ClientToServerMessage* message, Time now) {
  CHECK(HasDataToSend());
  TLOG(INFO_LEVEL, "Sending registration message for %s, desired = %d",
       GetObjectName(object_id_), *pending_state_);
  RegistrationUpdate* op_to_send = message->add_register_operation();
  op_to_send->mutable_object_id()->CopyFrom(object_id_);
  op_to_send->set_type(*pending_state_);
  op_to_send->set_sequence_number(*pending_seqno_);
  send_time_.reset(new Time(now));
}

void RegistrationInfo::InvokeStateChangedCallback(
    RegistrationState new_state, const UnknownHint& unknown_hint) {
  resources_->ScheduleOnListenerThread(
      NewPermanentCallback(
          reg_manager_->listener_,
          &InvalidationListener::RegistrationStateChanged,
          object_id_,
          new_state,
          unknown_hint));
}

bool RegistrationInfo::IsResultValid(const RegistrationUpdateResult& result) {
  // Note: no condition checked in this function should ever be false.  If one
  // of these checks fails, it indicates a serious protocol or server-side bug.
  if (!result.has_status()) {
    // Ignore the message if it didn't contain a valid status.
    TLOG(WARNING_LEVEL, "ignoring status-less registration result");
    return false;
  }
  const Status& status = result.status();
  if (!status.has_code()) {
    // Ignore the message if the status doesn't have a code.
    TLOG(WARNING_LEVEL, "ignoring registration result without status code");
    return false;
  }
  if (!result.has_operation()) {
    // Ignore the message if the result doesn't have an operation.
    TLOG(WARNING_LEVEL, "ignoring registration result without operation");
    return false;
  }
  const RegistrationUpdate& op = result.operation();
  if (op.sequence_number() >= reg_manager_->current_op_seqno_) {
    // Ignore the message if the sequence number is beyond what we could have
    // issued.
    TLOG(SEVERE_LEVEL,
         "Got message with seqno beyond what we could have issued; ignoring");
    return false;
  }
  if (op.sequence_number() < RegistrationUpdateManager::kFirstSequenceNumber) {
    TLOG(SEVERE_LEVEL,
         "Got message with seqno before first seqno; ignoring");
    return false;
  }
  // Check omitted.
  return true;
}

RegState RegistrationInfo::GetRegistrationState() {
  if (IsInProgress()) {
    return (*pending_state_ == RegistrationUpdate_Type_REGISTER) ?
        RegState_REG_PENDING :
        RegState_UNREG_PENDING;
  }
  switch (latest_known_server_state_) {
    case RegistrationUpdate_Type_REGISTER:
      return RegState_REGISTERED;
    case RegistrationUpdate_Type_UNREGISTER:
      return RegState_UNREGISTERED;
    default:
      CHECK(false);  // Unknown state -- crash.
  }
}

void RegistrationInfo::CheckSequenceNumber() {
  if (latest_known_server_seqno_.get() != NULL) {
    reg_manager_->CheckSequenceNumber(object_id_, *latest_known_server_seqno_);
  }
  if (IsInProgress()) {
    reg_manager_->CheckSequenceNumber(object_id_, *pending_seqno_);
  }
}

// RegistrationInfoStore definitions

RegistrationInfoStore::RegistrationInfoStore(
    RegistrationUpdateManager* reg_manager)
    : reg_manager_(reg_manager),
      resources_(reg_manager->resources_) {}


void RegistrationInfoStore::ProcessRegistrationUpdateResult(
    const RegistrationUpdateResult& result) {
  const ObjectId& object_id = result.operation().object_id();
  EnsureRecordPresent(object_id);
  string serialized;
  object_id.SerializeToString(&serialized);
  registration_state_[serialized].ProcessRegistrationUpdateResult(result);
}

void RegistrationInfoStore::ProcessApplicationRequest(
    const ObjectId& object_id, RegistrationUpdate_Type op_type) {
  EnsureRecordPresent(object_id);
  string serialized;
  object_id.SerializeToString(&serialized);
  registration_state_[serialized].ProcessApplicationRequest(op_type);
}

void RegistrationInfoStore::Reset() {
  TLOG(INFO_LEVEL, "Resetting all registration state");
  registration_state_.clear();
}

bool RegistrationInfoStore::HasServerStateForChecks() {
  for (map<string, RegistrationInfo>::iterator iter =
           registration_state_.begin();
       iter != registration_state_.end();
       ++iter) {
    if (iter->second.latest_known_server_seqno_.get() != NULL) {
      return true;
    }
  }
  return false;
}

bool RegistrationInfoStore::HasDataToSend() {
  for (map<string, RegistrationInfo>::iterator iter =
           registration_state_.begin();
       iter != registration_state_.end();
       ++iter) {
    if (iter->second.HasDataToSend()) {
      return true;
    }
  }
  return false;
}

int RegistrationInfoStore::TakeData(ClientToServerMessage* message) {
  int registrations_added = 0;
  for (map<string, RegistrationInfo>::iterator iter =
           registration_state_.begin();
       iter != registration_state_.end();
       ++iter) {
    RegistrationInfo& reg_info = iter->second;
    if (reg_info.HasDataToSend()) {
      reg_info.TakeData(message, resources_->current_time());
      ++registrations_added;
    }
    if (registrations_added ==
        reg_manager_->config_.max_registrations_per_message) {
      break;
    }
  }
  return registrations_added;
}

void RegistrationInfoStore::CheckTimedOutRegistrations() {
  for (map<string, RegistrationInfo>::iterator iter =
           registration_state_.begin();
       iter != registration_state_.end();
       ++iter) {
    RegistrationInfo& reg_info = iter->second;
    reg_info.CheckTimeout(resources_->current_time(),
                          reg_manager_->config_.registration_timeout);
  }
}

void RegistrationInfoStore::CheckSequenceNumbers() {
  for (map<string, RegistrationInfo>::iterator iter =
           registration_state_.begin();
       iter != registration_state_.end();
       ++iter) {
    RegistrationInfo& reg_info = iter->second;
    reg_info.CheckSequenceNumber();
    if ((reg_info.pending_seqno_.get() != NULL) &&
        (*reg_info.pending_seqno_ >
         reg_manager_->maximum_op_seqno_inclusive_)) {
      CHECK(reg_info.send_time_.get() == NULL);
    }
  }
}

void RegistrationInfoStore::CheckNoPendingOpsSent() {
  for (map<string, RegistrationInfo>::iterator iter =
           registration_state_.begin();
       iter != registration_state_.end();
       ++iter) {
    RegistrationInfo& reg_info = iter->second;
    if (reg_info.IsInProgress() &&
        (*reg_info.pending_seqno_ <=
         reg_manager_->maximum_op_seqno_inclusive_)) {
      CHECK(reg_info.HasDataToSend());
    }
  }
}

RegState RegistrationInfoStore::GetRegistrationState(
    const ObjectId& object_id) {
  string serialized;
  object_id.SerializeToString(&serialized);
  map<string, RegistrationInfo>::iterator iter =
      registration_state_.find(serialized);
  if (iter == registration_state_.end()) {
    return RegState_UNREGISTERED;
  }
  return iter->second.GetRegistrationState();
}

void RegistrationInfoStore::EnsureRecordPresent(const ObjectId& object_id) {
  string serialized;
  object_id.SerializeToString(&serialized);
  map<string, RegistrationInfo>::iterator iter =
      registration_state_.find(serialized);
  if (iter == registration_state_.end()) {
    registration_state_[serialized] = RegistrationInfo(reg_manager_, object_id);
  }
}

// SyncState definitions.

SyncState::SyncState(RegistrationUpdateManager* reg_manager)
    : reg_manager_(reg_manager),
      request_send_time_(reg_manager_->resources_->current_time()),
      num_expected_registrations_(-1) {}

bool SyncState::IsSyncComplete() {
  int num_registrations = reg_manager_->GetNumConfirmedRegistrations();
  bool have_enough_registrations = (num_expected_registrations_ != -1) &&
      (num_expected_registrations_ <= num_registrations);
  return have_enough_registrations || IsTimedOut();
}

bool SyncState::IsTimedOut() {
  return reg_manager_->resources_->current_time() >=
      reg_manager_->config_.registration_sync_timeout + request_send_time_;
}

// RegistrationUpdateManager definitions.

RegistrationUpdateManager::RegistrationUpdateManager(
    SystemResources* resources, const ClientConfig& config,
    int64 current_op_seqno, InvalidationListener* listener)
    : state_(State_LIMBO),
      resources_(resources),
      listener_(listener),
      current_op_seqno_(current_op_seqno),
      maximum_op_seqno_inclusive_(current_op_seqno_ - 1),
      config_(config),
      registration_info_store_(this) {}

RegistrationUpdateManager::~RegistrationUpdateManager() {
}

void RegistrationUpdateManager::EnterState(State new_state) {
  CheckRep();
  switch (new_state) {
    case State_LIMBO:
      // Abort any existing sync operation.
      sync_state_.reset();

      // Abort all pending operations and clear state.
      registration_info_store_.Reset();
      break;

    case State_SYNC_NOT_STARTED:
      CHECK(state_ == State_LIMBO);
      break;

    case State_SYNC_STARTED:
      CHECK(state_ == State_SYNC_NOT_STARTED);
      CHECK(!registration_info_store_.HasServerStateForChecks());
      sync_state_.reset(new SyncState(this));
      break;

    case State_SYNCED: {
      bool is_short_circuit_sync_completion =
          (state_ == State_SYNC_NOT_STARTED) &&
          (current_op_seqno_ == kFirstSequenceNumber);
      bool is_normal_sync_completion =
          (state_ == State_SYNC_STARTED) && sync_state_->IsSyncComplete();
      CHECK(is_normal_sync_completion || is_short_circuit_sync_completion);
      sync_state_.reset();
      break;
    }

    default:
      CHECK(false);
      break;
  }
  state_ = new_state;
  CheckRep();
}

void RegistrationUpdateManager::CheckRep() {
  registration_info_store_.CheckSequenceNumbers();
  switch (state_) {
    case State_LIMBO:
      CHECK(!registration_info_store_.HasServerStateForChecks());
      // Fall through.
    case State_SYNC_NOT_STARTED:
      CHECK(sync_state_.get() == NULL);
      registration_info_store_.CheckNoPendingOpsSent();
      break;

    case State_SYNC_STARTED:
      CHECK(sync_state_.get() != NULL);
      registration_info_store_.CheckNoPendingOpsSent();
      break;

    case State_SYNCED:
      CHECK(sync_state_.get() == NULL);
      break;

    default:
      CHECK(false);  // Illegal state.
  }
}

void RegistrationUpdateManager::CheckSequenceNumber(const ObjectId& object_id,
                                                    int64 sequence_number) {
  CHECK(sequence_number >= kFirstSequenceNumber);
  CHECK(sequence_number < current_op_seqno_);
}

int RegistrationUpdateManager::GetNumConfirmedRegistrations() {
  int count = 0;
  for (map<string, RegistrationInfo>::iterator iter =
           registration_info_store_.registration_state_.begin();
       iter != registration_info_store_.registration_state_.end();
       ++iter) {
    if (iter->second.IsLatestKnownServerStateRegistration()) {
      ++count;
    }
  }
  return count;
}

void RegistrationUpdateManager::HandleLostSession() {
  // Approach: regardless of whether or not we have sequence numbers available,
  // simply go directly to the State_LIMBO state, since we can no longer
  // meaningfully issue registration requests to the server.  Abort all pending
  // requests, since we know we'll ignore the responses, as they'll have the old
  // session token.  Additionally, clear the confirmed-registrations list, since
  // those registrations are no longer valid in the absence of a session.
  CheckRep();
  CHECK(state_ != State_LIMBO);
  EnterState(State_LIMBO);
  CheckRep();
}

void RegistrationUpdateManager::HandleLostClientId() {
  // Approach: we'll put the manager in a state appropriate to a newly-created
  // client.  I.e., we'll abort all of our pending registrations and discard all
  // of our confirmed registrations, since both of them were issued on behalf of
  // a client that no longer exists.  We'll then enter the State_LIMBO state,
  // since we won't be able to issue any registrations until we obtain a new
  // client id and session.

  // Enter State_LIMBO, discard all registrations (pending or confirmed).
  CheckRep();
  EnterState(State_LIMBO);

  // Reset sequence numbers.
  current_op_seqno_ = kFirstSequenceNumber;
  maximum_op_seqno_inclusive_ = config_.seqno_block_size;
  CheckRep();
}

void RegistrationUpdateManager::HandleNewSession() {
  // Approach: first, ensure that we're in the State_LIMBO.  This reduces
  // acquiring a new session when the existing session was still valid to the
  // problem of acquiring a new session after the old session was invalidated by
  // the server.
  //
  // Then, we handle acquiring a new session after invalidation of the old one
  // as follows.  If this is the first session ever acquired by this client, we
  // go directly to State_SYNCED, since there can be no registrations to sync
  // from the server. Otherwise, we go to State_SYNC_NOT_STARTED, since there
  // might be registrations to sync, and we have not yet sent the message.

  // Reduce to reacquire-after-invalidated case.  It's important to check that
  // we're not already in State_LIMBO. Otherwise, we could:
  // 1) Lose a session and tell the app registrations-removed.
  // 2) App re-issues registrations, and we queue them.
  // 3) We get a new session and abort all the pending (queued) operations.
  CheckRep();
  if (state_ != State_LIMBO) {
    HandleLostSession();
  }

  // TODO(ghc): [misc] Consider moving this to SYNCED transition.
  resources_->ScheduleOnListenerThread(
      NewPermanentCallback(
          listener_,
          &InvalidationListener::AllRegistrationsLost,
          NewPermanentCallback(&DoNothing)));

  // Need to sync.
  BeginSync();
  CheckRep();
}

int RegistrationUpdateManager::AddOutboundData(ClientToServerMessage* message) {
  CheckRep();
  int num_registrations_added = 0;

  switch (state_) {
    case State_LIMBO:
      TLOG(INFO_LEVEL, "No data to send since in state %d", state_);
      break;

    case State_SYNC_NOT_STARTED:
      message->set_message_type(
          ClientToServerMessage_MessageType_TYPE_REGISTRATION_SYNC);
      EnterState(State_SYNC_STARTED);
      TLOG(INFO_LEVEL, "Setting message type to TYPE_REGISTRATION_SYNC");
      break;

    case State_SYNCED:
      num_registrations_added = registration_info_store_.TakeData(message);
      TLOG(INFO_LEVEL, "Adding %d registrations in from State_SYNCED",
           num_registrations_added);
      // Fall through.
    case State_SYNC_STARTED:
      // We need to set the message type to OBJECT_CONTROL even if we're not
      // SYNCED, since the network manager might be trying to send a heartbeat.
      message->set_message_type(
          ClientToServerMessage_MessageType_TYPE_OBJECT_CONTROL);
      break;
  }

  CheckRep();
  return num_registrations_added;
}

void RegistrationUpdateManager::ProcessInboundMessage(
    const ServerToClientMessage& message) {
  CheckRep();
  CHECK(message.message_type() ==
        ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  CHECK(state_ != State_LIMBO);
  for (int i = 0; i < message.registration_result_size(); ++i) {
    registration_info_store_.ProcessRegistrationUpdateResult(
        message.registration_result(i));
  }
  if (message.has_num_total_registrations()) {
    if (state_ == State_SYNC_STARTED) {
      sync_state_->set_num_expected_registrations(
          message.num_total_registrations());
    }
  }
  CheckRep();
}

bool RegistrationUpdateManager::DoPeriodicRegistrationCheck() {
  // Approach: we know that we definitely do not have data to send in two cases:
  // 1) We are in state LIMBO. In this case, we can do nothing since we have no
  //    session.
  // 2) We are in state SYNC_STARTED.
  //
  // If we are LIMBO, we simply return false. If we are SYNC_STARTED, we return
  // false after possibly transitioning to SYNCED, if the sync has finished.
  // Otherwise, we must be in either the SYNC_NOT_STARTED or SYNCED states.
  //
  // 1) SYNC_NOT_STARTED. In this case, we have data to send -- the sync message
  //    itself.
  //
  // 2) SYNCED. In this case, we have data to send if we have registrations
  //    ready to send.
  CheckRep();
  bool result;
  switch (state_) {
    case State_LIMBO:
      // Never have anything to send.
      result = false;
      break;

    case State_SYNC_STARTED:
      // Never have anything to send, but we need to check if the sync has timed
      // out.
      CHECK(sync_state_.get() != NULL);
      if (sync_state_->IsSyncComplete()) {
        EnterState(State_SYNCED);

        // Since we entered the SYNCED state, we'll evaluate its has-data
        // condition.
        result = SyncedStateHasDataToSend();
      } else {
        // We didn't enter the SYNCED state, so we have no data to send.
        result = false;
      }
      break;

    case State_SYNC_NOT_STARTED:
      CHECK(current_op_seqno_ > kFirstSequenceNumber);
      // Always have something to send (sync message).
      TLOG(INFO_LEVEL, "Signaling data to send for SYNC_NOT_STARTED");
      result = true;
      break;

    case State_SYNCED:
      registration_info_store_.CheckTimedOutRegistrations();
      result = SyncedStateHasDataToSend();
      break;

    default:
      CHECK(false);
  }
  CheckRep();
  return result;
}

void RegistrationUpdateManager::BeginSync() {
  EnterState(State_SYNC_NOT_STARTED);
  if (current_op_seqno_ == kFirstSequenceNumber) {
    // If sequence number is FIRST_SEQUENCE_NUMBER, then we can't have any
    // pending registrations queued to be sent, so we know we can return false
    // here.
    EnterState(State_SYNCED);
    CHECK(!SyncedStateHasDataToSend());
  }
}

}  // namespace invalidation
