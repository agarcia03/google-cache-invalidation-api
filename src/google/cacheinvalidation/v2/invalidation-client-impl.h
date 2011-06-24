// Copyright 2011 Google Inc.
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

// Implementation of the Invalidation Client Library (Ticl).

#ifndef GOOGLE_CACHEINVALIDATION_V2_INVALIDATION_CLIENT_IMPL_H_
#define GOOGLE_CACHEINVALIDATION_V2_INVALIDATION_CLIENT_IMPL_H_

#include <string>
#include <utility>

#include "google/cacheinvalidation/v2/invalidation-client.h"
#include "google/cacheinvalidation/v2/invalidation-listener.h"
#include "google/cacheinvalidation/v2/checking-invalidation-listener.h"
#include "google/cacheinvalidation/v2/client-protocol-namespace-fix.h"
#include "google/cacheinvalidation/v2/digest-function.h"
#include "google/cacheinvalidation/v2/digest-store.h"
#include "google/cacheinvalidation/v2/protocol-handler.h"
#include "google/cacheinvalidation/v2/registration-manager.h"
#include "google/cacheinvalidation/v2/run-state.h"

namespace invalidation {

class InvalidationClientImpl : public InvalidationClient,
                               public ProtocolListener {
 public:
  struct Config {
    Config() : network_timeout_delay(TimeDelta::FromMinutes(1)),
               write_retry_delay(TimeDelta::FromSeconds(10)),
               heartbeat_interval(TimeDelta::FromMinutes(20)),
               perf_counter_delay(TimeDelta::FromHours(6)) {}

    /* The delay after which a network message sent to the server is considered
     * timed out.
     */
    TimeDelta network_timeout_delay;

    /* Retry delay for a persistent write if it fails. */
    TimeDelta write_retry_delay;

    /* Delay for sending heartbeats to the server. */
    TimeDelta heartbeat_interval;

    /* Delay after which performance counters are sent to the server. */
    TimeDelta perf_counter_delay;

    /* Configuration for the protocol client to control batching etc. */
    ProtocolHandler::Config protocol_handler_config;

    /* Modifies configParams to contain the list of configuration parameter
     * names and their values.
     */
    void GetConfigParams(vector<pair<string, int> >* config_params);

    string ToString();
  };

  /* Constructs a client.
   *
   * Arguments:
   * resources - resources to use during execution
   * client_type - client type code
   * client_name - application identifier for the client
   * config - configuration for the client
   * listener - application callback
   */
  InvalidationClientImpl(
      SystemResources* resources, int client_type, const string& client_name,
      Config config, const string& application_name,
      InvalidationListener* listener);

  /* Stores the client id that is used for squelching invalidations on the
   * server side.
   */
  void GetApplicationClientIdForTest(string* result) {
    application_client_id_.SerializeToString(result);
  }

  void GetClientTokenForTest(string* result) {
    *result = client_token_;
  }

  // Getters for testing.  No transfer of ownership occurs in any of these
  // methods.

  /* Returns the listener that was registered by the caller. */
  InvalidationListener* GetInvalidationListenerForTest() {
    return listener_->delegate();
  }

  /* Returns the system resources. */
  SystemResources* GetResourcesForTest() {
    return resources_;
  }

  /* Returns the performance counters/statistics. */
  Statistics* GetStatisticsForTest() {
    return statistics_.get();
  }

  /* Returns the digest function used for computing digests for object
   * registrations.
   */
  DigestFunction* GetDigestFunctionForTest() {
    return digest_fn_.get();
  }

  /* Changes the existing delay for the network timeout delay in the operation
   * scheduler to be delay_ms.
   */
  void ChangeNetworkTimeoutDelayForTest(TimeDelta delay) {
    operation_scheduler_.ChangeDelayForTest(timeout_task_.get(), delay);
  }

  /* Changes the existing delay for the heartbeat delay in the operation
   * scheduler to be delay_ms.
   */
  void ChangeHeartbeatDelayForTest(TimeDelta delay) {
    operation_scheduler_.ChangeDelayForTest(heartbeat_task_.get(), delay);
  }

  /* Returns the next time a message is allowed to be sent to the server (could
   * be in the past).
   */
  int64 GetNextMessageSendTimeMsForTest() {
    return protocol_handler_.GetNextMessageSendTimeMsForTest();
  }

  /* Sets the digest store to be digest_store for testing purposes.
   *
   * REQUIRES: This method is called before the Ticl has been started.
   */
  void SetDigestStoreForTest(DigestStore<ObjectIdP>* digest_store) {
    CHECK(!resources_->IsStarted());
    registration_manager_.SetDigestStoreForTest(digest_store);
  }

  virtual void Start();

  virtual void Stop();

  virtual void Register(const ObjectId& object_id);

  virtual void Unregister(const ObjectId& object_id);

  virtual void Register(const vector<ObjectId>& object_ids) {
    PerformRegisterOperations(object_ids, RegistrationP_OpType_REGISTER);
  }

  virtual void Unregister(const vector<ObjectId>& object_ids) {
    PerformRegisterOperations(object_ids, RegistrationP_OpType_UNREGISTER);
  }

  /* Implementation of (un)registration.
   *
   * Arguments:
   * object_ids - object ids on which to operate
   * reg_op_type - whether to register or unregister
   */
  virtual void PerformRegisterOperations(
      const vector<ObjectId>& object_ids, RegistrationP::OpType reg_op_type);

  void PerformRegisterOperationsInternal(
      const vector<ObjectId>& object_ids, RegistrationP::OpType reg_op_type);

  virtual void Acknowledge(const AckHandle& acknowledge_handle);

  string ToString();

  //
  // Protocol listener methods
  //

  /* Returns the current client token. */
  virtual string GetClientToken();

  virtual void HandleTokenChanged(
      const ServerMessageHeader& header, const string& new_token,
      const StatusP& status);

  virtual void HandleInvalidations(
      const ServerMessageHeader& header,
      const RepeatedPtrField<InvalidationP>& invalidations);

  virtual void HandleRegistrationStatus(
      const ServerMessageHeader& header,
      const RepeatedPtrField<RegistrationStatus>& reg_status_list);

  virtual void HandleRegistrationSyncRequest(
      const ServerMessageHeader& header);

  virtual void HandleInfoMessage(
      const ServerMessageHeader& header,
      const RepeatedField<int>& info_types);

  virtual void GetRegistrationSummary(RegistrationSummary* summary) {
    registration_manager_.GetRegistrationSummary(summary);
  }

  /* Gets registration manager state as a serialized RegistrationManagerState.
   */
  void GetRegistrationManagerStateAsSerializedProto(string* result);

  /* Gets statistics as a serialized InfoMessage. */
  void GetStatisticsAsSerializedProto(string* result);

  /* The single key used to write all the Ticl state. */
  static const char* kClientTokenKey;

 private:
  //
  // Private methods.
  //

  /* Implementation of start on the internal thread with the persistent
   * serialized_state if any. Starts the TICL protocol and makes the TICL ready
   * to received registration, invalidations, etc
   */
  void StartInternal(const string& serialized_state);

  void StopInternal();

  void AcknowledgeInternal(const AckHandle& acknowledge_handle);

  void LoseToken();

  /* Requests a new client identifier from the server.
   *
   * REQUIRES: no token currently be held.
   *
   * Arguments:
   * debug_string - information to identify the caller
   */
  void AcquireToken(const string& debug_string);

  /* Function called to check for timed-out network messages. */
  void CheckNetworkTimeouts();

  /* Processes the header on a server message by updating the latest known
   * server time and informing the registration manager of a new summary.
   *
   * REQUIRES: nonce be empty.
   */
  void ProcessServerHeader(const ServerMessageHeader& header);

  /* Sends an info message to the server. If mustSendPerformanceCounters is
   * true, the performance counters are sent regardless of when they were sent
   * earlier.
   */
  void SendInfoMessageToServer(bool mustSendPerformanceCounters);

  /* Writes the Ticl state to persistent storage. */
  void WriteStateBlob();

  /* Sets the nonce to new_nonce.
   *
   * REQUIRES: new_nonce be empty or client_token_ be empty.  The goal is to
   * ensure that a nonce is never set unless there is no client token, unless
   * the nonce is being cleared.
   */
  void set_nonce(const string& new_nonce);

  /* Sets the client_token_ to new_client_token.
   *
   * REQUIRES: new_client_token be empty or nonce_ be empty.  The goal is to
   * ensure that a token is never set unless there is no nonce, unless the token
   * is being cleared.
   */
  void set_client_token(const string& new_client_token);

  /* Handles the result of a request to write to persistent storage. */
  void WriteCallback(Status status);

  /* Reads the Ticl state from persistent storage (if any) and calls
   * startInternal.
   */
  void ScheduleStartAfterReadingStateBlob();

  /* Handles the result of a request to read from persistent storage. */
  void ReadCallback(pair<Status, string> read_result);

  /* Ensures that a heartbeat message is sent periodically. */
  void HeartbeatTask();

  /* Converts an operation type reg_status to a
   * InvalidationListener::RegistrationState.
   */
  static InvalidationListener::RegistrationState ConvertOpTypeToRegState(
      RegistrationStatus reg_status);

 private:
  /* Resources for the Ticl. */
  SystemResources* resources_;  // Owned by application.

  /* Reference into the resources object for cleaner code. All Ticl code must be
   * scheduled on this scheduler.
   */
  Scheduler* internal_scheduler_;

  /* Logger reference into the resources object for cleaner code. */
  Logger* logger_;

  /* Statistics objects to track number of sent messages, etc. */
  scoped_ptr<Statistics> statistics_;

  /* Application callback interface. */
  scoped_ptr<CheckingInvalidationListener> listener_;

  /* Configuration for this instance. */
  Config config_;

  /* The client type code as assigned by the notification system's backend. */
  int client_type_;

  /* Application identifier for this client. */
  ApplicationClientIdP application_client_id_;

  /* The function for computing the registration and persistence state digests.
   */
  scoped_ptr<DigestFunction> digest_fn_;

  /* Object maintaining the registration state for this client. */
  RegistrationManager registration_manager_;

  /* Used to validate messages */
  scoped_ptr<TiclMessageValidator> msg_validator_;

  /* Object handling low-level wire format interactions. */
  ProtocolHandler protocol_handler_;

  /* Object to schedule future events. */
  OperationScheduler operation_scheduler_;

  /* The state of the Ticl whether it has started or not. */
  RunState ticl_state_;

  /* Last time performance counters were sent to the server. */
  Time last_performance_send_time_;

  /* Current client token known from the server. */
  string client_token_;

  // After the client starts, exactly one of nonce and clientToken is non-null.

  /* If not empty, nonce for pending identifier request. */
  string nonce_;

  /* A task for periodic heartbeats. */
  scoped_ptr<Closure> heartbeat_task_;

  /* A task to periodically check network timeouts. */
  scoped_ptr<Closure> timeout_task_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_INVALIDATION_CLIENT_IMPL_H_
