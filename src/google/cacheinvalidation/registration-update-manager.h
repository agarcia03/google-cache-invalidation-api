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
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/types.pb.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::map;

class ClientConfig;
class SystemResources;

/* Contains the last time an operation message was sent, and the operation's
 * callback.
 */
struct PendingOperationInfo {
  // The pending operation.
  RegistrationUpdate operation;

  // Whether we have an outstanding (not timed-out) message to the server
  // requesting this operation.
  bool is_sent;

  // The time that we sent a request with this operation.
  Time sent_time;

  // The number of times we have attempted this registration.
  int attempt_count;

  // The callback to invoke when this operation completes.
  RegistrationCallback* callback;

  PendingOperationInfo() {}
  PendingOperationInfo(const RegistrationUpdate& op,
                       RegistrationCallback* cb)
      : operation(op), is_sent(false), attempt_count(1), callback(cb) {}
};

// Possible states in which an object may be.
enum RegistrationState {
  RegistrationState_NO_INFO = 0,
  RegistrationState_REG_PENDING = 1,
  RegistrationState_REG_CONFIRMED = 2,
  RegistrationState_UNREG_PENDING = 3,
  RegistrationState_UNREG_CONFIRMED = 4
};


/* Keeps track of pending and confirmed registration update operations for a
 * given Invalidation Client Library.  This class is not thread-safe, so the
 * Ticl is responsible for synchronization.
 *
 * This is an internal helper class for InvalidationClientImpl.
 */
class RegistrationUpdateManager {
 public:
  // Visible for testing only.
  RegistrationUpdateManager(SystemResources* resources,
                            const ClientConfig& config);

  ~RegistrationUpdateManager();

  /* Initiates registration on the given object_id, invoking the callback with
   * the server's eventual response.  Will retry indefinitely until the server
   * responds.  Takes ownership of the callback, which must be
   * repeatable and unique.
   */
  void Register(const ObjectId& object_id,
                RegistrationCallback* callback) {
    UpdateRegistration(object_id, RegistrationUpdate_Type_REGISTER, callback);
  }

  /* Initiates unregistration on the given object_id, invoking the callback with
   * the server's eventual response.  Will retry indefinitely until the server
   * responds.  Takes ownership of the callback, which must be
   * repeatable and unique.
   */
  void Unregister(const ObjectId& object_id,
                  RegistrationCallback* callback) {
    UpdateRegistration(object_id, RegistrationUpdate_Type_UNREGISTER, callback);
  }

  /* Returns the registration state of an object. */
  RegistrationState GetObjectState(const ObjectId& object_id);

  /* For each pending registration update that was not already sent out
   * recently, adds an update request to the given message.  Returns the number
   * of operations added.
   */
  int AddOutboundRegistrationUpdates(ClientToServerMessage* message);

  /* Given the result of a registration update, finds and invokes the associated
   * callback.  Also removes the operation from the pending operations map, and
   * if the result was successful, adds it to the maps of confirmed operations.
   */
  void ProcessRegistrationUpdateResult(const RegistrationUpdateResult& result);

  /* Performs the actual work of registering or unregistering for invalidations
   * on a specific object.  The callback will be invoked when the server
   * responds to the request.  Takes ownership of the callback, which
   * must be repeatable and unique.
   */
  void UpdateRegistration(const ObjectId& object_id,
                          RegistrationUpdate_Type op_type,
                          RegistrationCallback* callback);

  /* Implements the periodic check of registrations (called by the top-level,
   * half-second periodic task in the Ticl) by doing two things:
   *
   * 1) Checks for timed-out (un)registrations. If any are found, schedules
   *    their callbacks to be run and removes the records from the pending_ops_
   *    map.
   *
   * 2) Checks for unsent registrations. If any are found, returns true,
   *    indicating that it has data to send to the server.
   */
  bool DoPeriodicRegistrationCheck();

  /* Handles a timed-out registration operation. If attempts remain, tries
   * again; else, aborts the operation.  Returns whether there is any data to be
   * sent to the server.
   */
  bool HandleTimedOutRegistration(PendingOperationInfo* op_info);

  /* Aborts all pending (un)registrations and invokes their callbacks with
   * TRANSIENT_FAILURE. Removes all confirmed registrations. Called by the Ticl
   * on UNKNOWN_SESSION or INVALID_CLIENT.
   */
  void RemoveAllOperations();

  /* Aborts a pending (un)registration. Removes it from the pending_ops_ map and
   * invokes the callback with TRANSIENT_FAILURE.
   */
  void AbortPending(const PendingOperationInfo& op_info);

  // Logging, scheduling, persistence, etc.
  SystemResources* resources_;
  // Ticl configuration parameters.
  ClientConfig config_;
  // Sequence number to use for the next registration operation.
  uint64 current_op_seq_num_;
  // Set of confirmed registration update operations. The map keys are
  // serialized object ids, and the value values are RegistrationUpdates.
  map<string, RegistrationUpdate> confirmed_ops_;
  // Pending registrations and unregistrations. The map keys are serialized
  // object ids, and the values are PendingOperationInfo structs.
  map<string, PendingOperationInfo> pending_ops_;

  friend class InvalidationClientImpl;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_REGISTRATION_UPDATE_MANAGER_H_
