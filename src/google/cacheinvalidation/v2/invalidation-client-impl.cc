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

#include "google/cacheinvalidation/v2/invalidation-client-impl.h"

#include <sstream>

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/random.h"
#include "google/cacheinvalidation/v2/client_test_internal.pb.h"
#include "google/cacheinvalidation/v2/invalidation-client-util.h"
#include "google/cacheinvalidation/v2/log-macro.h"
#include "google/cacheinvalidation/v2/persistence-utils.h"
#include "google/cacheinvalidation/v2/proto-converter.h"
#include "google/cacheinvalidation/v2/proto-helpers.h"
#include "google/cacheinvalidation/v2/sha1-digest-function.h"
#include "google/cacheinvalidation/v2/smearer.h"
#include "google/cacheinvalidation/v2/string_util.h"

namespace invalidation {

using ::ipc::invalidation::RegistrationManagerStateP;

/* Modifies configParams to contain the list of configuration parameter
 * names and their values.
 */
void InvalidationClientImpl::Config::GetConfigParams(
    vector<pair<string, int> >* config_params) {
  config_params->push_back(
      make_pair("networkTimeoutDelay",
                network_timeout_delay.InMilliseconds()));
  config_params->push_back(
      make_pair("writeRetryDelay", write_retry_delay.InMilliseconds()));
  config_params->push_back(
      make_pair("heartbeatInterval", heartbeat_interval.InMilliseconds()));
  config_params->push_back(
      make_pair("perfCounterDelay", perf_counter_delay.InMilliseconds()));
  config_params->push_back(
      make_pair("maxExponentialBackoffFactor", max_exponential_backoff_factor));
  protocol_handler_config.GetConfigParams(config_params);
}

string InvalidationClientImpl::Config::ToString() {
  std::stringstream stream;
  stream << "network delay: " << network_timeout_delay.InMilliseconds()
         << ", write retry delay: " << write_retry_delay.InMilliseconds()
         << ", heartbeat: " << heartbeat_interval.InMilliseconds();
  return stream.str();
}

InvalidationClientImpl::InvalidationClientImpl(
    SystemResources* resources, int client_type, const string& client_name,
    Config config, const string& application_name,
    InvalidationListener* listener)
    : resources_(resources),
      internal_scheduler_(resources->internal_scheduler()),
      logger_(resources->logger()),
      statistics_(new Statistics()),
      listener_(new CheckingInvalidationListener(
          listener, statistics_.get(), internal_scheduler_,
          resources_->listener_scheduler(), logger_)),
      config_(config),
      client_type_(client_type),
      digest_fn_(new Sha1DigestFunction()),
      registration_manager_(logger_, statistics_.get(), digest_fn_.get()),
      msg_validator_(new TiclMessageValidator(logger_)),
      protocol_handler_(config.protocol_handler_config, resources,
                        statistics_.get(), application_name, this,
                        msg_validator_.get()),
      operation_scheduler_(logger_, internal_scheduler_),
      token_exponential_backoff_(
          new Random(InvalidationClientUtil::GetCurrentTimeMs(
              resources->internal_scheduler())),
          config.max_exponential_backoff_factor *
              config.network_timeout_delay,
          config.network_timeout_delay),
      reg_sync_heartbeat_exponential_backoff_(
          new Random(InvalidationClientUtil::GetCurrentTimeMs(
              resources->internal_scheduler())),
          config.max_exponential_backoff_factor *
              config.network_timeout_delay,
          config.network_timeout_delay),
      persistence_exponential_backoff_(
          new Random(InvalidationClientUtil::GetCurrentTimeMs(
              resources->internal_scheduler())),
          config.max_exponential_backoff_factor *
              config.write_retry_delay,
          config.write_retry_delay),
      smearer_(new Random(InvalidationClientUtil::GetCurrentTimeMs(
          resources->internal_scheduler()))),
      heartbeat_task_(
          NewPermanentCallback(this, &InvalidationClientImpl::HeartbeatTask)),
      timeout_task_(
          NewPermanentCallback(
              this, &InvalidationClientImpl::CheckNetworkTimeouts)) {
  application_client_id_.set_client_name(client_name);
  operation_scheduler_.SetOperation(
      config.network_timeout_delay, timeout_task_.get(), "[timeout task]");
  operation_scheduler_.SetOperation(
      config.heartbeat_interval, heartbeat_task_.get(), "[heartbeat task]");
  TLOG(logger_, INFO, "Created client: %s", ToString().c_str());
}

void InvalidationClientImpl::Start() {
  // Initialize the nonce so that we can maintain the invariant that exactly
  // one of "nonce" and "clientToken" is non-null.
  set_nonce(IntToString(
      internal_scheduler_->GetCurrentTime().ToInternalValue()));

  TLOG(logger_, INFO, "Starting with C++ config: %s",
       config_.ToString().c_str());

  // Read the state blob and then schedule startInternal once the value is
  // there.
  ScheduleStartAfterReadingStateBlob();
}

void InvalidationClientImpl::StartInternal(const string& serialized_state) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";

  // Initialize the session manager using the persisted client token.
  PersistentTiclState persistent_state;
  bool deserialized = false;
  if (!serialized_state.empty()) {
    deserialized = PersistenceUtils::DeserializeState(
        logger_, serialized_state, digest_fn_.get(), &persistent_state);
  }

  if (!serialized_state.empty() && !deserialized) {
    // In this case, we'll proceed as if we had no persistent state -- i.e.,
    // obtain a new client id from the server.
    statistics_->RecordError(
        Statistics::ClientErrorType_PERSISTENT_DESERIALIZATION_FAILURE);
    TLOG(logger_, SEVERE, "Failed deserializing persistent state: %s",
         ProtoHelpers::ToString(serialized_state).c_str());
  }
  if (deserialized) {
    // If we have persistent state, use the previously-stored token and send a
    // heartbeat to let the server know that we've restarted, since we may have
    // been marked offline.
    //
    // In the common case, the server will already have all of our
    // registrations, but we won't know for sure until we've gotten its summary.
    // We'll ask the application for all of its registrations, but to avoid
    // making the registrar redo the work of performing registrations that
    // probably already exist, we'll suppress sending them to the registrar.
    TLOG(logger_, INFO, "Restarting from persistent state: %s",
         ProtoHelpers::ToString(
             persistent_state.client_token()).c_str());
    set_nonce("");
    set_client_token(persistent_state.client_token());
    should_send_registrations_ = false;
    SendInfoMessageToServer(false, true);

    // We need to ensure that heartbeats are sent, regardless of whether we
    // start fresh or from persistent state.  The line below ensures that they
    // are scheduled in the persistent startup case.  For the other case, the
    // task is scheduled when we acquire a token.
    operation_scheduler_.Schedule(heartbeat_task_.get());
  } else {
    // If we had no persistent state or couldn't deserialize the state that we
    // had, start fresh.  Request a new client identifier.
    //
    // The server can't possibly have our registrations, so whatever we get
    // from the application we should send to the registrar.
    TLOG(logger_, INFO, "Starting with no previous state");
    should_send_registrations_ = true;
    ScheduleAcquireToken("Startup");
  }
  // InvalidationListener.Ready() is called when the ticl has acquired a
  // new token.
}

void InvalidationClientImpl::Stop() {
  internal_scheduler_->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(this, &InvalidationClientImpl::StopInternal));
}

void InvalidationClientImpl::StopInternal() {
  TLOG(logger_, WARNING, "Ticl being stopped: %s", ToString().c_str());
  if (ticl_state_.IsStarted()) {
    ticl_state_.Stop();
  }
}

void InvalidationClientImpl::Register(const ObjectId& object_id) {
  vector<ObjectId> object_ids;
  object_ids.push_back(object_id);
  PerformRegisterOperations(object_ids, RegistrationP_OpType_REGISTER);
}

void InvalidationClientImpl::Unregister(const ObjectId& object_id) {
  vector<ObjectId> object_ids;
  object_ids.push_back(object_id);
  PerformRegisterOperations(object_ids, RegistrationP_OpType_UNREGISTER);
}

void InvalidationClientImpl::PerformRegisterOperations(
    const vector<ObjectId>& object_ids, RegistrationP::OpType reg_op_type) {
  CHECK(!object_ids.empty()) << "Must specify some object id";

  CHECK(ticl_state_.IsStarted() || ticl_state_.IsStopped()) <<
      "Cannot call " << reg_op_type << " for object " <<
      " when the Ticl has not been started. If start has been " <<
      "called, caller must wait for InvalidationListener.Ready";
  if (ticl_state_.IsStopped()) {
    // The Ticl has been stopped. This might be some old registration op
    // coming in. Just ignore instead of crashing.
    TLOG(logger_, WARNING, "Ticl stopped: register (%d) of %d objects ignored.",
         reg_op_type, object_ids.size());
    return;
  }

  internal_scheduler_->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          this, &InvalidationClientImpl::PerformRegisterOperationsInternal,
          object_ids, reg_op_type));
}

void InvalidationClientImpl::PerformRegisterOperationsInternal(
    const vector<ObjectId>& object_ids, RegistrationP::OpType reg_op_type) {
  vector<ObjectIdP> object_id_protos;
  for (size_t i = 0; i < object_ids.size(); ++i) {
    const ObjectId& object_id = object_ids[i];
    ObjectIdP object_id_proto;
    ProtoConverter::ConvertToObjectIdProto(object_id, &object_id_proto);
    Statistics::IncomingOperationType op_type =
        (reg_op_type == RegistrationP_OpType_REGISTER) ?
        Statistics::IncomingOperationType_REGISTRATION :
        Statistics::IncomingOperationType_UNREGISTRATION;
    statistics_->RecordIncomingOperation(op_type);
    TLOG(logger_, INFO, "Register %s, %d",
         ProtoHelpers::ToString(object_id_proto).c_str(), reg_op_type);
    object_id_protos.push_back(object_id_proto);
  }

  // Update the registration manager state, then have the protocol client send a
  // message.
  registration_manager_.PerformOperations(object_id_protos, reg_op_type);

  // Check whether we should suppress sending registrations because we don't
  // yet know the server's summary.
  if (should_send_registrations_) {
    protocol_handler_.SendRegistrations(object_id_protos, reg_op_type);
  }
  operation_scheduler_.Schedule(timeout_task_.get());
}

void InvalidationClientImpl::Acknowledge(const AckHandle& acknowledge_handle) {
  if (acknowledge_handle.IsNoOp()) {
    // Nothing to do. We do not increment statistics here since this is a no op
    // handle and statistics can only be acccessed on the scheduler thread.
    return;
  }

  internal_scheduler_->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          this, &InvalidationClientImpl::AcknowledgeInternal,
          acknowledge_handle));
}

void InvalidationClientImpl::AcknowledgeInternal(
    const AckHandle& acknowledge_handle) {
  // Validate the ack handle.

  // 1. Parse the ack handle first.
  AckHandleP ack_handle;
  ack_handle.ParseFromString(acknowledge_handle.handle_data());
  if (!ack_handle.IsInitialized()) {
    TLOG(logger_, WARNING, "Bad ack handle : %s",
         ProtoHelpers::ToString(
             acknowledge_handle.handle_data()).c_str());
    statistics_->RecordError(
        Statistics::ClientErrorType_ACKNOWLEDGE_HANDLE_FAILURE);
    return;
  }

  // 2. Validate ack handle - it should have a valid invalidation.
  if (!ack_handle.has_invalidation()
      || !msg_validator_->IsValid(ack_handle.invalidation())) {
    TLOG(logger_, WARNING, "Incorrect ack handle: %s",
         ProtoHelpers::ToString(ack_handle).c_str());
    statistics_->RecordError(
        Statistics::ClientErrorType_ACKNOWLEDGE_HANDLE_FAILURE);
    return;
  }

  // Currently, only invalidations have non-trivial ack handle.
  const InvalidationP& invalidation = ack_handle.invalidation();
  statistics_->RecordIncomingOperation(
      Statistics::IncomingOperationType_ACKNOWLEDGE);
  protocol_handler_.SendInvalidationAck(invalidation);
}

string InvalidationClientImpl::ToString() {
  return StringPrintf("Client: %s, %s",
                      ProtoHelpers::ToString(application_client_id_).c_str(),
                      ProtoHelpers::ToString(client_token_).c_str());
}

string InvalidationClientImpl::GetClientToken() {
  CHECK(client_token_.empty() || nonce_.empty());
  TLOG(logger_, FINE, "Return client token = %s",
       ProtoHelpers::ToString(client_token_).c_str());
  return client_token_;
}

void InvalidationClientImpl::HandleTokenChanged(
    const ServerMessageHeader& header, const string& new_token) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";

  // If the client token was valid, we have already checked in protocol
  // handler.  Otherwise, we need to check for the nonce, i.e., if we have a
  // nonce, the message must carry the same nonce.
  if (!nonce_.empty()) {
    if (header.token == nonce_) {
      TLOG(logger_, INFO, "Accepting server message with matching nonce: %s",
           ProtoHelpers::ToString(nonce_).c_str());
      set_nonce("");
    } else {
      statistics_->RecordError(Statistics::ClientErrorType_NONCE_MISMATCH);
      TLOG(logger_, INFO,
           "Rejecting server message with mismatched nonce: %s, %s",
           ProtoHelpers::ToString(nonce_).c_str(),
           ProtoHelpers::ToString(header.token).c_str());
      return;
    }
  }

  // The message is for us. Process it.
  HandleIncomingHeader(header);

  if (new_token.empty()) {
    TLOG(logger_, INFO, "Destroying existing token: %s",
         ProtoHelpers::ToString(client_token_).c_str());
    ScheduleAcquireToken("Destroy");
  } else {
    // We just received a new token. Start the regular heartbeats now.
    operation_scheduler_.Schedule(heartbeat_task_.get());
    set_nonce("");
    set_client_token(new_token);
    WriteStateBlob();
    TLOG(logger_, INFO, "New token assigned at client: %s, Old = %s",
         ProtoHelpers::ToString(new_token).c_str(),
         ProtoHelpers::ToString(client_token_).c_str());
  }
}

void InvalidationClientImpl::ScheduleAcquireToken(const string& debug_string) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  set_client_token("");

  // Schedule the token acquisition while respecting exponential backoff.
  internal_scheduler_->Schedule(token_exponential_backoff_.GetNextDelay(),
      NewPermanentCallback(
          this,
          &InvalidationClientImpl::AcquireToken, debug_string));
}

void InvalidationClientImpl::HandleInvalidations(
    const ServerMessageHeader& header,
    const RepeatedPtrField<InvalidationP>& invalidations) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  HandleIncomingHeader(header);

  for (int i = 0; i < invalidations.size(); ++i) {
    const InvalidationP& invalidation = invalidations.Get(i);
    AckHandleP ack_handle_proto;
    ack_handle_proto.mutable_invalidation()->CopyFrom(invalidation);
    string serialized;
    ack_handle_proto.SerializeToString(&serialized);
    AckHandle ack_handle(serialized);
    if (ProtoConverter::IsAllObjectIdP(invalidation.object_id())) {
      TLOG(logger_, INFO, "Issuing invalidate all");
      listener_->InvalidateAll(this, ack_handle);
    } else {
      // Regular object. Could be unknown version or not.
      Invalidation inv;
      ProtoConverter::ConvertFromInvalidationProto(invalidation, &inv);
      TLOG(logger_, INFO, "Issuing invalidate: %s",
           ProtoHelpers::ToString(invalidation).c_str());
      if (invalidation.is_known_version()) {
        listener_->Invalidate(this, inv, ack_handle);
      } else {
        // Unknown version
        listener_->InvalidateUnknownVersion(this, inv.object_id(), ack_handle);
      }
    }
  }
}

void InvalidationClientImpl::HandleRegistrationStatus(
    const ServerMessageHeader& header,
    const RepeatedPtrField<RegistrationStatus>& reg_status_list) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  HandleIncomingHeader(header);

  vector<bool> local_processing_statuses;
  registration_manager_.HandleRegistrationStatus(
      reg_status_list, &local_processing_statuses);
  CHECK(local_processing_statuses.size() ==
        static_cast<size_t>(reg_status_list.size())) <<
      "Not all registration statuses were processed";

  // Inform app about the success or failure of each registration based
  // on what the registration manager has indicated.
  for (int i = 0; i < reg_status_list.size(); ++i) {
    const RegistrationStatus& reg_status = reg_status_list.Get(i);
    bool was_success = local_processing_statuses[i];
    TLOG(logger_, FINE, "Process reg status: %s",
         ProtoHelpers::ToString(reg_status).c_str());

    ObjectId object_id;
    ProtoConverter::ConvertFromObjectIdProto(
        reg_status.registration().object_id(), &object_id);
    if (was_success) {
      InvalidationListener::RegistrationState reg_state =
          ConvertOpTypeToRegState(reg_status);
      listener_->InformRegistrationStatus(this, object_id, reg_state);
    } else {
      bool is_permanent =
          (reg_status.status().code() == StatusP_Code_PERMANENT_FAILURE);
      listener_->InformRegistrationFailure(
          this, object_id, !is_permanent, reg_status.status().description());
    }
  }
}

void InvalidationClientImpl::HandleRegistrationSyncRequest(
    const ServerMessageHeader& header) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  // Send all the registrations in the reg sync message.
  HandleIncomingHeader(header);

  // Generate a single subtree for all the registrations.
  RegistrationSubtree subtree;
  registration_manager_.GetRegistrations("", 0, &subtree);
  protocol_handler_.SendRegistrationSyncSubtree(subtree);
}

void InvalidationClientImpl::HandleInfoMessage(
    const ServerMessageHeader& header,
    const RepeatedField<int>& info_types) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  HandleIncomingHeader(header);
  bool must_send_performance_counters = false;
  for (int i = 0; i < info_types.size(); ++i) {
    must_send_performance_counters =
        (info_types.Get(i) ==
         InfoRequestMessage_InfoType_GET_PERFORMANCE_COUNTERS);
    if (must_send_performance_counters) {
      break;
    }
  }
  SendInfoMessageToServer(must_send_performance_counters,
                          !registration_manager_.IsStateInSyncWithServer());
}

void InvalidationClientImpl::HandleErrorMessage(
      const ServerMessageHeader& header,
      const ErrorMessage::Code code,
      const string& description) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  HandleIncomingHeader(header);

  // If it is an auth failure, we shut down the ticl.
  TLOG(logger_, SEVERE, "Received error message: %s, %s, %s",
         header.ToString().c_str(), ProtoHelpers::ToString(code).c_str(),
         description.c_str());

  // Translate the code to error reason.
  int reason;
  switch (code) {
    case ErrorMessage_Code_AUTH_FAILURE:
      reason = ErrorReason::AUTH_FAILURE;
      break;
    case ErrorMessage_Code_UNKNOWN_FAILURE:
      reason = ErrorReason::UNKNOWN_FAILURE;
      break;
    default:
      reason = ErrorReason::UNKNOWN_FAILURE;
      break;
  }
  // Issue an informError to the application.
  ErrorInfo error_info(reason, false, description, ErrorContext());
  listener_->InformError(this, error_info);

  // If this is an auth failure, remove registrations and stop the Ticl.
  // Otherwise do nothing.
  if (code != ErrorMessage_Code_AUTH_FAILURE) {
    return;
  }

  // If there are any registrations, remove them and issue registration
  // failure.
  vector<ObjectIdP> desired_registrations;
  registration_manager_.RemoveRegisteredObjects(&desired_registrations);
  TLOG(logger_, WARNING, "Issuing failure for %d objects",
       desired_registrations.size());
  for (size_t i = 0; i < desired_registrations.size(); ++i) {
    ObjectId object_id;
    ProtoConverter::ConvertFromObjectIdProto(
        desired_registrations[i], &object_id);
    listener_->InformRegistrationFailure(
        this, object_id, false, "Auth error");
  }

  // Schedule the stop on the listener work queue so that it happens after the
  // inform registration failure calls above
  resources_->listener_scheduler()->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(this, &InvalidationClientImpl::Stop));
}

void InvalidationClientImpl::GetRegistrationManagerStateAsSerializedProto(
    string* result) {
  RegistrationManagerStateP reg_state;
  registration_manager_.GetClientSummary(reg_state.mutable_client_summary());
  registration_manager_.GetServerSummary(reg_state.mutable_server_summary());
  vector<ObjectIdP> registered_objects;
  registration_manager_.GetRegisteredObjectsForTest(&registered_objects);
  for (size_t i = 0; i < registered_objects.size(); ++i) {
    reg_state.add_registered_objects()->CopyFrom(registered_objects[i]);
  }
  reg_state.SerializeToString(result);
}

void InvalidationClientImpl::GetStatisticsAsSerializedProto(
    string* result) {
  vector<pair<string, int> > properties;
  statistics_->GetNonZeroStatistics(&properties);
  InfoMessage info_message;
  for (size_t i = 0; i < properties.size(); ++i) {
    PropertyRecord* record = info_message.add_performance_counter();
    record->set_name(properties[i].first);
    record->set_value(properties[i].second);
  }
  info_message.SerializeToString(result);
}

void InvalidationClientImpl::AcquireToken(const string& debug_string) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";

  // If token is still not assigned (as expected), sends a request. Otherwise,
  // ignore.
  if (client_token_.empty()) {
    // Allocate a nonce and send a message requesting a new token.
    set_nonce(IntToString(
        internal_scheduler_->GetCurrentTime().ToInternalValue()));
    protocol_handler_.SendInitializeMessage(
        client_type_, application_client_id_, nonce_, debug_string);

    // Schedule a timeout to retry if we don't receive a response.
    operation_scheduler_.Schedule(timeout_task_.get());
  }
}

void InvalidationClientImpl::CheckNetworkTimeouts() {
  /*
   * Timeouts can happen for two reasons:
   * 1) Request to obtain an token does not receive a reply.
   * 2) Registration state is not in sync with the server.
   *
   * We simply check for both conditions and taken corrective action when
   * needed.
   */
  // If we have no token, send a message for one.
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  if (client_token_.empty()) {
    TLOG(logger_, INFO, "Request for token timed out");
    ScheduleAcquireToken("Network timeout");
    return;
  }

  // Simply send an info message to ensure syncing happens.
  if (!registration_manager_.IsStateInSyncWithServer()) {
    TLOG(logger_, INFO, "Registration state not in sync with server: %s",
         registration_manager_.ToString().c_str());

    // Send the info message while respecting exponential backoff.
    internal_scheduler_->Schedule(token_exponential_backoff_.GetNextDelay(),
        NewPermanentCallback(this, &InvalidationClientImpl::SendHeartbeatSync));
  }
}

void InvalidationClientImpl::SendHeartbeatSync() {
  if (registration_manager_.IsStateInSyncWithServer()) {
    TLOG(logger_, INFO, "Not sending message since state is now in sync");
  } else {
    SendInfoMessageToServer(false, true /* request server summary */);

    // Schedule a timeout after sending the message to make sure that we get
    // into sync.
    operation_scheduler_.Schedule(timeout_task_.get());
  }
}

void InvalidationClientImpl::HandleIncomingHeader(
    const ServerMessageHeader& header) {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  CHECK(nonce_.empty()) <<
      "Cannot process server header " << header.ToString() <<
      " with non-empty nonce " << nonce_;

  if (header.registration_summary.has_num_registrations()) {
    // We've received a summary from the server, so if we were suppressing
    // registrations, we should now allow them to go to the registrar.
    should_send_registrations_ = true;
    registration_manager_.InformServerRegistrationSummary(
        header.registration_summary);
  }

  // Check and reset the exponential back off for the reg sync-based heartbeats
  // on receipt of a message.
  if (registration_manager_.IsStateInSyncWithServer()) {
    reg_sync_heartbeat_exponential_backoff_.Reset();
  }
}

void InvalidationClientImpl::SendInfoMessageToServer(
    bool must_send_performance_counters, bool request_server_summary) {
  TLOG(logger_, INFO, "Sending info message to server");
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";

  // Make sure that you have the latest registration summary.
  Time next_performance_send_time =
      last_performance_send_time_ + config_.perf_counter_delay;
  vector<pair<string, int> > performance_counters;
  vector<pair<string, int> > config_params;
  if (must_send_performance_counters ||
      (next_performance_send_time <
       internal_scheduler_->GetCurrentTime())) {
    statistics_->GetNonZeroStatistics(&performance_counters);
    config_.GetConfigParams(&config_params);
    last_performance_send_time_ = internal_scheduler_->GetCurrentTime();
  }

  protocol_handler_.SendInfoMessage(
      performance_counters, config_params, request_server_summary);
}

void InvalidationClientImpl::WriteStateBlob() {
  CHECK(internal_scheduler_->IsRunningOnThread()) << "Not on internal thread";
  CHECK(!client_token_.empty());
  PersistentTiclState state;
  state.set_client_token(client_token_);
  string serialized_state;
  PersistenceUtils::SerializeState(state, digest_fn_.get(), &serialized_state);
  resources_->storage()->WriteKey(
      kClientTokenKey, serialized_state,
      NewPermanentCallback(this, &InvalidationClientImpl::WriteCallback));
}

void InvalidationClientImpl::set_nonce(const string& new_nonce) {
  CHECK(new_nonce.empty() || client_token_.empty()) <<
      "Tried to set nonce with existing token " << client_token_;
  nonce_ = new_nonce;
}

void InvalidationClientImpl::set_client_token(const string& new_client_token) {
  CHECK(new_client_token.empty() || nonce_.empty()) <<
      "Tried to set token with existing nonce " << nonce_;

  // If the ticl has not been started and we are getting a new token (either
  // from persistence or from the server, start the ticl and inform the
  // application.
  bool finish_starting_ticl = !ticl_state_.IsStarted() &&
      client_token_.empty() && !new_client_token.empty();
  client_token_ = new_client_token;

  if (!new_client_token.empty()) {
    // Token control message succeeded - reset the network delay so that the
    // next time we acquire a token, the delay starts from the original value.
    token_exponential_backoff_.Reset();
  }

  if (finish_starting_ticl) {
    FinishStartingTiclAndInformListener();
  }
}

void InvalidationClientImpl::FinishStartingTiclAndInformListener() {
  CHECK(!ticl_state_.IsStarted());

  ticl_state_.Start();
  listener_->Ready(this);

  // We are not currently persisting our registration digest, so regardless of
  // whether or not we are restarting from persistent state, we need to query
  // the application for all of its registrations.
  listener_->ReissueRegistrations(this, RegistrationManager::kEmptyPrefix, 0);
  TLOG(logger_, INFO, "Ticl started: %s", ToString().c_str());
}

void InvalidationClientImpl::WriteCallback(Status status) {
  TLOG(logger_, INFO, "Write state completed: %s", status.message().c_str());
  if (!status.IsSuccess()) {
    // Retry with exponential backoff.
    statistics_->RecordError(
        Statistics::ClientErrorType_PERSISTENT_WRITE_FAILURE);
    internal_scheduler_->Schedule(
        persistence_exponential_backoff_.GetNextDelay(),
        NewPermanentCallback(this, &InvalidationClientImpl::WriteStateBlob));
  } else {
    // Write succeeded - reset the backoff delay.
    persistence_exponential_backoff_.Reset();
  }
}

void InvalidationClientImpl::ScheduleStartAfterReadingStateBlob() {
  resources_->storage()->ReadKey(
      kClientTokenKey,
      NewPermanentCallback(this, &InvalidationClientImpl::ReadCallback));
}

void InvalidationClientImpl::ReadCallback(
    pair<Status, string> read_result) {
  string serialized_state;
  if (read_result.first.IsSuccess()) {
    serialized_state = read_result.second;
  } else {
    statistics_->RecordError(
        Statistics::ClientErrorType_PERSISTENT_READ_FAILURE);
    TLOG(logger_, WARNING, "Could not read state blob: %s",
         read_result.first.message().c_str());
  }
  // Call start now.
  internal_scheduler_->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          this, &InvalidationClientImpl::StartInternal, serialized_state));
}

void InvalidationClientImpl::HeartbeatTask() {
  // Send info message.
  TLOG(logger_, INFO, "Sending heartbeat to server: %s", ToString().c_str());
  SendInfoMessageToServer(
      false, !registration_manager_.IsStateInSyncWithServer());
  operation_scheduler_.Schedule(heartbeat_task_.get());
}

InvalidationListener::RegistrationState
InvalidationClientImpl::ConvertOpTypeToRegState(RegistrationStatus reg_status) {
  InvalidationListener::RegistrationState reg_state =
      reg_status.registration().op_type() == RegistrationP_OpType_REGISTER ?
      InvalidationListener::REGISTERED :
      InvalidationListener::UNREGISTERED;
  return reg_state;
}

const char* InvalidationClientImpl::kClientTokenKey = "ClientToken";

}  // namespace invalidation
