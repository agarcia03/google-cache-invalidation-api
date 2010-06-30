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
#include "google/cacheinvalidation/registration-update-manager.h"
#include "google/cacheinvalidation/session-manager.h"
#include "google/cacheinvalidation/stl-namespace.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::map;
using INVALIDATION_STL_NAMESPACE::vector;

// A pending operation, the time it was attempted, and the callback to invoke
// when it finishes.
struct PendingOperation {
  PendingOperation() : callback(NULL) {}
  RegistrationUpdate::Type op_type;
  uint64 time;
  RegistrationCallback* callback;
};

/**
 * Implementation of the Invalidation Client Library (Ticl).
 */
class InvalidationClientImpl : public InvalidationClient, NetworkEndpoint {
 public:
  /* Constructs an InvalidationClientImpl with the given system resources,
   * client type, and application name.  It will deliver invalidations to the
   * given listener.
   */
  InvalidationClientImpl(SystemResources* resources,
                         const ClientType& client_type,
                         const string& app_name,
                         InvalidationListener* listener,
                         const ClientConfig& config)
      : config_(config),
        resources_(resources),
        listener_(listener),
        registration_manager_(resources, config),
        network_manager_(ALLOW_THIS_IN_INITIALIZER_LIST(this),
                         resources, config),
        session_manager_(config, client_type, app_name, resources),
        random_seed_(resources->current_time().ToInternalValue()) {
    resources->ScheduleImmediately(
        NewPermanentCallback(this, &InvalidationClientImpl::PeriodicTask));
  }

  static const char* INVALIDATE_ALL_OBJECT_NAME;

  // Methods called by the application. ////////////////////////////////////////

  // Inherited from InvalidationClient:

  virtual void Register(const ObjectId& oid,
                        RegistrationCallback* callback);

  virtual void Unregister(const ObjectId& oid,
                          RegistrationCallback* callback);

  virtual NetworkEndpoint* network_endpoint() {
    return this;
  }

  virtual void GetClientUniquifier(string* uniquifier) const {
    *uniquifier = session_manager_.client_uniquifier();
  }

  // Inherited from NetworkEndpoint:

  virtual void TakeOutboundMessage(string* message);

  virtual void HandleInboundMessage(const string& bundle);

  virtual void AdviseNetworkStatus(bool online) {
  }

  virtual void RegisterOutboundListener(
      NetworkCallback* outbound_message_ready);

  /**
   * Generates a "smeared" delay. The returned smeared delay must be baseDelay
   * +/- (baseDelay * smearFactor).
   */
  // Visible for testing.
  static TimeDelta SmearDelay(TimeDelta base_delay, double smear_factor,
                              unsigned int* random_seed);

 private:
  // Internal methods:

  /* Checks for messages that need to be sent, operations to time out, etc. */
  void PeriodicTask();

  /* Handles a response from the server that involves getting a new session. */
  void HandleNewSession();

  /* Handles a lost-session event. */
  void HandleLostSession();

  /* Handles an OBJECT_CONTROL message. */
  void HandleObjectControl(const ServerToClientMessage& bundle);

  /* Informs the application that it has a new session and that its
   * registrations have been removed.
   */
  void InformListenerOfNewSession();

  // Handlers for server-to-client messages. ///////////////////////////////////

  /* Handles a response from the NFE regarding an attempt to perform an
   * operation of {@code opType} on {@code objectId}.
   */
  void ProcessRegistrationUpdateResult(const RegistrationUpdateResult& result);

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

  /* Configuration parameters. */
  ClientConfig config_;

  /* Various system resources needed by the Ticl (storage, CPU, logging). */
  SystemResources* resources_;

  /* The listener that will be notified of changes to objects. */
  InvalidationListener* listener_;

  /* Keeps track of pending and confirmed registrations. */
  RegistrationUpdateManager registration_manager_;

  /* Manages push heartbeats and polling. */
  NetworkManager network_manager_;

  /* Manages client ids and session tokens. */
  SessionManager session_manager_;

  /* Invalidation acknowledgments waiting to be delivered to the server. */
  vector<Invalidation> pending_invalidation_acks_;

  /* Seed to generate random numbers for smearing. */
  unsigned int random_seed_;

  /* A lock to protect this object's state. */
  Mutex lock_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_INVALIDATION_CLIENT_IMPL_H_
