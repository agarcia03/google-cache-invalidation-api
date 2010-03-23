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

class SystemResources;
class ClientConfig;

/* Contains the last time an operation message was sent, and the operation's
 * callback.
 */
struct PendingOperationInfo {
  // The pending operation.
  RegistrationUpdate operation;

  // The last time that we sent a request with this operation.
  Time last_sent;

  // The callback to invoke when this operation completes.
  RegistrationCallback* callback;

  PendingOperationInfo() {}
  PendingOperationInfo(const RegistrationUpdate& op,
                       RegistrationCallback* cb)
      : operation(op), callback(cb) {}
};

/* Keeps track of pending and confirmed registration update operations for a
 * given Invalidation Client Library.  This class is not thread-safe, so the
 * Ticl is responsible for synchronization.
 *
 * This is an internal helper class for InvalidationClientImpl.
 */
class RegistrationUpdateManager {
 private:
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

  /* For each pending registration update that was not already sent out
   * recently, adds an update request to the given message.
   */
  void AddOutboundRegistrationUpdates(ClientToServerMessage* message);

  /* Forces all operations with a sequence number greater than
   * last_confirmed_seq_num to be retried, using the given callback for each
   * operation.  The callback must therefore be "permanent" (capable of being
   * invoked any number of times), and the caller retains ownership of it.
   * Note: we don't currently have a way of making this do anything meaningful
   * for any value of last_confirmed_seq_num other than 0.
   */
  void RepeatLostOperations(
      uint64 last_confirmed_seq_num, RegistrationCallback* callback);

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

  // Logging, scheduling, persistence, etc.
  SystemResources* resources_;
  // Sequence number to use for the next registration operation.
  uint64 current_op_seq_num_;
  // Time after which to resend unacknowledged registrations.
  TimeDelta registration_timeout_;
  // Operations confirmed by the server, keyed by sequence number.
  map<uint64, RegistrationUpdate> confirmed_ops_by_seq_num_;
  // Operations awaiting a response from the server, keyed by sequence number.
  map<uint64, PendingOperationInfo> pending_ops_;

  friend class InvalidationClientImpl;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_REGISTRATION_UPDATE_MANAGER_H_
