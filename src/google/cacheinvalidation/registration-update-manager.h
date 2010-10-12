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

#ifndef GOOGLE_CACHEINVALIDATION_REGISTRATION_UPDATE_MANAGER_H_
#define GOOGLE_CACHEINVALIDATION_REGISTRATION_UPDATE_MANAGER_H_

#include <map>

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/hash_map.h"
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/scoped_ptr.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/types.pb.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::map;

class SystemResources;

// Possible states in which an object may be.
enum RegState {
  RegState_UNREGISTERED = 0,
  RegState_REG_PENDING = 1,
  RegState_REGISTERED = 2,
  RegState_UNREG_PENDING = 3
};

// High-level states the registration manager can be in.
enum State {
  State_LIMBO = 0,  // No session.
  State_SYNC_NOT_STARTED = 1,  // Have session, but need to send sync message.
  State_SYNC_STARTED = 2,  // Sent reg sync message, awaiting replies / timeout.
  State_SYNCED = 3  // Received regs from server, processing client regs.
};

class RegistrationUpdateManager;

// Record of the registration state of an object.
class RegistrationInfo {
 public:
  RegistrationInfo() {}

  RegistrationInfo(RegistrationUpdateManager* reg_manager,
                   const ObjectId& object_id);

  RegistrationInfo(const RegistrationInfo& reg_info) {
    *this = reg_info;
  }

  // Putting these objects in a hash_map requires an assignment operator.  The
  // default doesn't work, since there's no assignment operator or copy ctor for
  // scoped_ptr's (see comment below).
  RegistrationInfo& operator=(const RegistrationInfo& reg_info) {
    reg_manager_ = reg_info.reg_manager_;
    resources_ = reg_info.resources_;
    object_id_ = reg_info.object_id_;
    latest_known_server_state_ = reg_info.latest_known_server_state_;

    // We use scoped_ptr in several places to simulate optional / nullable
    // values.  These values cannot be copied implicitly.  We need to allocate
    // space for and make copies of their contents.
    if (reg_info.pending_state_.get() != NULL) {
      pending_state_.reset(
          new RegistrationUpdate_Type(*reg_info.pending_state_));
    } else {
      pending_state_.reset();
    }
    if (reg_info.send_time_.get() != NULL) {
      send_time_.reset(new Time(*reg_info.send_time_));
    } else {
      send_time_.reset();
    }
    if (reg_info.pending_seqno_.get() != NULL) {
      pending_seqno_.reset(new int64(*reg_info.pending_seqno_));
    } else {
      pending_seqno_.reset();
    }
    return *this;
  }

  // Updates registration state and invokes callbacks as apprpriate for the
  // receipt of the result from the server.
  void ProcessRegistrationUpdateResult(const RegistrationUpdateResult& result);

  // Updates state and invokes callbacks as appropriate for an application
  // request of the given operation.
  void ProcessApplicationRequest(RegistrationUpdate_Type op_type);

  // Checks to see if an in-progress operation has timed out, if one exists.  If
  // so, attempts a retry or invokes a callback with TRANSIENT_FAILURE if no
  // retries remain.
  void CheckTimeout(Time now, TimeDelta deadline);

  // Returns whether there is any data to be sent for the object whose
  // registration state we're tracking.
  bool HasDataToSend();

  // If appropriate, modifies message to include a request to put the object
  void TakeData(ClientToServerMessage* message, Time now);

 private:
  RegistrationState GetStateFromOpType(RegistrationUpdate_Type op_type) {
    return (op_type == RegistrationUpdate_Type_REGISTER) ?
        RegistrationState_REGISTERED :
        RegistrationState_UNREGISTERED;
  }

  // Invokes the RegistrationStateChanged() callback on object_id_ and
  // new_state_.
  void InvokeStateChangedCallback(RegistrationState new_state_,
                                  const UnknownHint& unknown_hint);

  // Returns whether an operation is in-progress.
  bool IsInProgress() {
    return pending_state_.get() != NULL;
  }

  // Returns the type of operation in-progress.
  // REQUIRES: IsInProgress().
  RegistrationUpdate_Type GetInProgressType() {
    CHECK(IsInProgress());
    return *pending_state_;
  }

  // Returns whether the latest known server state is a registration.
  bool IsLatestKnownServerStateRegistration() {
    return latest_known_server_state_ == RegistrationUpdate_Type_REGISTER;
  }

  // Returns whether result is a valid message and can be processed.
  bool IsResultValid(const RegistrationUpdateResult& result);

  // Returns the registration state for the associated object.
  RegState GetRegistrationState();

  // Verifies that sequence numbers in this record are valid.
  void CheckSequenceNumber();

  const char* GetObjectName(const ObjectId& object_id) {
    return object_id.name().string_value().c_str();
  }

  // The registration manager to which this state object belongs.
  RegistrationUpdateManager* reg_manager_;

  // System resources for logging, etc.
  SystemResources* resources_;

  // The object id whose registration state we're tracking.
  ObjectId object_id_;

  // Latest state at the server that we know (registered or unregistered).
  RegistrationUpdate_Type latest_known_server_state_;

  // Sequence number associated with latest server state, or NULL if we've
  // received no explicit state from the server.
  scoped_ptr<int64> latest_known_server_seqno_;

  // The state desired by the client application, or NULL if there is no
  // operation pending.
  scoped_ptr<RegistrationUpdate_Type> pending_state_;

  // The time, if any, at which a message was sent requesting this operation.
  scoped_ptr<Time> send_time_;

  // The sequence number, if any, of the pending operation.
  scoped_ptr<int64> pending_seqno_;

  friend class RegistrationInfoStore;
  friend class RegistrationUpdateManager;
};

// Class to manage RegistrationInfo records representing the total registration
// state known to a manager.
class RegistrationInfoStore {
 public:
  // Constructs a registration info store associated with the given reg_manager.
  // The reg manager will own this object.
  RegistrationInfoStore(RegistrationUpdateManager* reg_manager);

  // Handles a registration result received from the server.
  void ProcessRegistrationUpdateResult(const RegistrationUpdateResult& result);

  // Handles a request from the client application to put the given object_id
  // into the given registration state (indicated by op_type).
  void ProcessApplicationRequest(const ObjectId& object_id,
                                 RegistrationUpdate_Type op_type);

  // Clears the registration state map.
  void Reset();

  // Returns whether the store has received any state from the server.
  bool HasServerStateForChecks();

  // Returns whether any (un)registrations are ready to be sent to the server.
  bool HasDataToSend();

  // Adds outbound registration messages to the given message.  Returns the
  // number of registrations added.
  int32 TakeData(ClientToServerMessage* message);

  // Checks for timed-out registrations, either invoking callbacks or
  // arranging for retries as needed.
  void CheckTimedOutRegistrations();

  // Validates sequence numbers in the store.
  void CheckSequenceNumbers();

  // Verifies that no pending operations have been sent, for checks.
  void CheckNoPendingOpsSent();

  // Returns the registration state for object_id.
  RegState GetRegistrationState(const ObjectId& object_id);

 private:
  // Ensures that a record for object_id is present in the registration_state_
  // map.  If none was previously present, adds a default record.
  void EnsureRecordPresent(const ObjectId& object_id);

  // The registration update manager to which this store belongs.
  RegistrationUpdateManager* reg_manager_;

  // System resources for logging, etc.
  SystemResources* resources_;

  // Map from serialized object id to associated record.
  map<string, RegistrationInfo> registration_state_;

  friend class RegistrationUpdateManager;
};

// Represents the state of an on-going registration synchronization operation.
class SyncState {
 public:
  SyncState(RegistrationUpdateManager* reg_manager);

  // Returns whether the sync process has completed, either due to receipt of
  // all messages or timeout.
  bool IsSyncComplete();

  void set_num_expected_registrations(int num_expected_registrations) {
    num_expected_registrations_ = num_expected_registrations;
  }

 private:
  // Returns whether the sync process has timed out -- i.e., it has started and
  // too much time has passed.
  bool IsTimedOut();

  // The registration manager to which this synchronization state pertains.
  RegistrationUpdateManager* reg_manager_;

  // The time at which we sent the request to sync registrations.
  Time request_send_time_;

  // The total number of registrations we expect from the server.
  int32 num_expected_registrations_;
};

/* Keeps track of pending and confirmed registration update operations for a
 * given Invalidation Client Library.  This class is not thread-safe, so the
 * Ticl is responsible for synchronization.
 *
 * This is an internal helper class for InvalidationClientImpl.
 */
class RegistrationUpdateManager {
 public:
  RegistrationUpdateManager(SystemResources* resources,
                            const ClientConfig& config,
                            int64 current_op_seqno,
                            InvalidationListener* listener);

  ~RegistrationUpdateManager();

  // Handles a lost session.
  void HandleLostSession();

  // Handles a lost client id.
  void HandleLostClientId();

  // Handles a new session.
  void HandleNewSession();

  // Returns the registration state of the given object.
  RegState GetRegistrationState(const ObjectId& object_id) {
    CheckRep();
    return registration_info_store_.GetRegistrationState(object_id);
  }

  // For each pending registration update that was not aready sent out recently,
  // adds a message to the given message (up to
  // config.max_registrations_per_message).
  int AddOutboundData(ClientToServerMessage* message);

  // Handles a message from the server.
  void ProcessInboundMessage(const ServerToClientMessage& message);

  // Performs the actual work of registering or unregistering (as indicated by
  // op_type) for invalidations on the given object id.
  void UpdateRegistration(const ObjectId& object_id,
                          RegistrationUpdate_Type op_type) {
    // If we're in LIMBO, then we silently ignore registrations, since we know
    // that:
    // 1) We can't send them now.
    // 2) We'll issue registrations-removed when we leave LIMBO, which will
    //    cause the application to re-register everything anyway.
    if (state_ == State_LIMBO) {
      return;
    }
    registration_info_store_.ProcessApplicationRequest(object_id, op_type);
  }

  // Initiates registration on the given object_id.
  void Register(const ObjectId& object_id) {
    CheckRep();
    UpdateRegistration(object_id, RegistrationUpdate_Type_REGISTER);
    CheckRep();
  }

  // Initiates unregistration on the given object_id.
  void Unregister(const ObjectId& object_id) {
    CheckRep();
    UpdateRegistration(object_id, RegistrationUpdate_Type_UNREGISTER);
    CheckRep();
  }

  /* Implements the periodic check of registrations (called by the top-level,
   * half-second periodic task in the Ticl) by doing two things:
   *
   * 1) Checks for timed-out (un)registrations.
   *
   * 2) Checks for unsent registrations. If any are found, returns true,
   *    indicating that it has data to send to the server.
   */
  bool DoPeriodicRegistrationCheck();

  // Updates the maximum sequence number.
  void UpdateMaximumSeqno(int64 new_maximum_seqno_inclusive) {
    CHECK(new_maximum_seqno_inclusive > maximum_op_seqno_inclusive_);
    maximum_op_seqno_inclusive_ = new_maximum_seqno_inclusive;
  }

  State GetStateForTest() {
    CheckRep();
    return state_;
  }

  int64 maximum_op_seqno_inclusive() {
    CheckRep();
    return maximum_op_seqno_inclusive_;
  }

  int64 current_op_seqno() {
    CheckRep();
    return current_op_seqno_;
  }

 private:
  // Checks invariants for the current state, checks that it's legal to make a
  // transition to new_state, performs the transition, and checks invariants for
  // the new state.
  void EnterState(State new_state);

  // Performs checks of invariants on the internal state representation.
  void CheckRep();

  // Checks that sequence_number is in [kFirstSequenceNumber,
  // maximum_op_seqno_inclusive_].
  void CheckSequenceNumber(const ObjectId& object_id, int64 sequence_number);

  // Returns the number of objects from which we've received explicit
  // notification from the server about registration state.
  int GetNumConfirmedRegistrations();

  // Starts the registration sync process.  If our current sequence number is
  // kFirstSequenceNumber, then we know we can't have issued any registrations,
  // and we transition directly to State_SYNCED.  Otherwise, we transition to
  // State_SYNC_NOT_STARTED.
  void BeginSync();

  // Returns whether there is any data to send, given that the manager is in
  // State_SYNCED.
  bool SyncedStateHasDataToSend() {
    CHECK(state_ == State_SYNCED);
    return registration_info_store_.HasDataToSend();
  }

  // The registration manager's current state.
  State state_;

  // Logging, scheduling, persistence, etc.
  SystemResources* resources_;

  // Listener on which to invoke callbacks indicating changes in registration
  // state.
  InvalidationListener* listener_;

  // Sequence number to use for the next registration operation.
  int64 current_op_seqno_;

  // Maximum sequence number we're allowed to use.
  int64 maximum_op_seqno_inclusive_;

  // Ticl configuration parameters.
  ClientConfig config_;

  // State of the current registration synchronization operation.  Non-null iff
  // state_ == State_SYNC_STARTED.
  scoped_ptr<SyncState> sync_state_;

  // Registration info store
  RegistrationInfoStore registration_info_store_;

  // The lowest sequence number allowed.
  static const int64 kFirstSequenceNumber = 1;

  friend class InvalidationClientImpl;
  friend class RegistrationInfo;
  friend class RegistrationInfoStore;
  friend class SyncState;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_REGISTRATION_UPDATE_MANAGER_H_
