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

// Implementation of the invalidation client library.

#ifndef GOOGLE_CACHEINVALIDATION_INVALIDATION_CLIENT_IMPL_H_
#define GOOGLE_CACHEINVALIDATION_INVALIDATION_CLIENT_IMPL_H_

#include <map>
#include <vector>

#include "base/scoped_ptr.h"
#include "google/cacheinvalidation/compiler-specific.h"
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/mutex.h"
#include "google/cacheinvalidation/network-manager.h"
#include "google/cacheinvalidation/persistence-manager.h"
#include "google/cacheinvalidation/persistence-utils.h"
#include "google/cacheinvalidation/random.h"
#include "google/cacheinvalidation/registration-update-manager.h"
#include "google/cacheinvalidation/session-manager.h"
#include "google/cacheinvalidation/stl-namespace.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::map;
using INVALIDATION_STL_NAMESPACE::vector;

/**
 * Implementation of the Invalidation Client Library (Ticl).
 */
class InvalidationClientImpl : public InvalidationClient, NetworkEndpoint {
 public:
  /* Constructs an InvalidationClientImpl with the given system resources,
   * client type, and application name.  It will deliver invalidations to
   * the given listener.
   */
  InvalidationClientImpl(SystemResources* resources,
                         const ClientType& client_type,
                         const string& app_name,
                         const ClientConfig& config,
                         InvalidationListener* listener);

  static const char* INVALIDATE_ALL_OBJECT_NAME;

  // Methods called by the application. ////////////////////////////////////////

  // Inherited from InvalidationClient:

  virtual void start(const string& serialized_state);

  virtual void Register(const ObjectId& oid);

  virtual void Unregister(const ObjectId& oid);

  virtual void PermanentShutdown();

  virtual NetworkEndpoint* network_endpoint() {
    return this;
  }

  virtual void GetClientUniquifier(string* uniquifier) {
    CHECK(!resources_->IsRunningOnInternalThread());
    MutexLock m(&lock_);
    *uniquifier = session_manager_->client_uniquifier();
  }

  // Inherited from NetworkEndpoint:

  virtual void TakeOutboundMessage(string* message);

  virtual void HandleInboundMessage(const string& bundle);

  virtual void AdviseNetworkStatus(bool online) {
    CHECK(!resources_->IsRunningOnInternalThread());
  }

  virtual void RegisterOutboundListener(
      NetworkCallback* outbound_message_ready);

  RegState GetRegistrationStateForTest(const ObjectId& object_id) {
    return registration_manager_->GetRegistrationState(object_id);
  }

  State GetRegistrationManagerStateForTest() {
    return registration_manager_->GetStateForTest();
  }

  /**
   * Generates a "smeared" delay. The returned smeared delay must be baseDelay
   * +/- (baseDelay * smearFactor).
   */
  // Visible for testing.
  static TimeDelta SmearDelay(TimeDelta base_delay, double smear_factor,
                              Random* random);

 private:
  // Internal methods:

  /* Persists a state blob that allocates config_.seqno_block_size sequence
   * numbers.  If the write is successful, reinitializes the registration
   * manager with the new block of sequence numbers.  This function manufactures
   * a callback (to invoke HandleSeqnoWritebackResult), which it passes to the
   * WriteState() function.
   */
  void AllocateNewSequenceNumbers(const TiclState& persistent_state);

  /* Handles the result of write performed on restart to allocate a new block of
   * sequence numbers.  If 'success' is false, the client forgets its persisted
   * state and starts fresh; if it's true, the client is allowed to begin
   * sending registration requests with sequence numbers up to the new maximum.
   */
  void HandleSeqnoWritebackResult(int64 maximum_op_seqno, bool success);

  /* Handles the result of a write performed on receipt of a new session.  This
   * write is best-effort, so 'success' is only used for logging.
   */
  void HandleBestEffortWrite(bool success);

  /* Checks for messages that need to be sent, operations to time out, etc. */
  void PeriodicTask();

  /* Handles a response from the server that involves getting a new session. */
  void HandleNewSession();

  /* Handles a lost-session event. */
  void HandleLostSession();

  /* Handles an OBJECT_CONTROL message. */
  void HandleObjectControl(const ServerToClientMessage& bundle);

  // Handlers for server-to-client messages. ///////////////////////////////////

  /* Handles an invalidation. */
  void ProcessInvalidation(const Invalidation& invalidation);

  /* Adds the given {@code invalidation} to the list of pending outgoing
   * invalidations.
   */
  void AcknowledgeInvalidation(const Invalidation& invalidation);

  /* Asynchronously adds the given {@code invalidation} to the list of pending
   * outgoing invalidations.
   */
  void ScheduleAcknowledgeInvalidation(const Invalidation& invalidation);

  /* Forgets any client id and session the client may currently have. */
  void ForgetClientId();

  /* Ensures that the client has been started. */
  void EnsureStarted();

  /* Various system resources needed by the Ticl (storage, CPU, logging). */
  SystemResources* resources_;

  /* Type of the client. */
  const ClientType client_type_;

  /* Application id of the client. */
  const string app_name_;

  /* The listener that will be notified of changes to objects. */
  InvalidationListener* listener_;

  /* Configuration parameters. */
  ClientConfig config_;

  /* Keeps track of pending and confirmed registrations. */
  scoped_ptr<RegistrationUpdateManager> registration_manager_;

  /* Manages push heartbeats and polling. */
  NetworkManager network_manager_;

  /* Manages client ids and session tokens. */
  scoped_ptr<SessionManager> session_manager_;

  /* Wraps resources_->WriteState() to ensure sequential access. */
  PersistenceManager persistence_manager_;

  /* Invalidation acknowledgments waiting to be delivered to the server. */
  vector<Invalidation> pending_invalidation_acks_;

  /* Whether we're waiting for the initial seqno write-back to complete.  While
   * this is true, the Ticl will not accept any messages from the server, and it
   * will not issue any registrations or inform the network listener that it has
   * data to send.
   */
  bool awaiting_seqno_writeback_;

  /* Whether the client has been started. */
  bool is_started_;

  /* Random number generator for smearing periodic intervals. */
  Random random_;

  /* A lock to protect this object's state. */
  Mutex lock_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_INVALIDATION_CLIENT_IMPL_H_
