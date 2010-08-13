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

#include "google/cacheinvalidation/session-manager.h"

#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/log-macro.h"
#include "google/cacheinvalidation/types.pb.h"

namespace invalidation {

bool SessionManager::AddSessionAction(ClientToServerMessage* message) {
  if (shutdown_) {
    // If we're shutting down, just send a message with TYPE_SHUTDOWN.
    message->set_message_type(ClientToServerMessage_MessageType_TYPE_SHUTDOWN);
    message->set_client_uniquifier(uniquifier_);
    message->set_session_token(session_token_);
    return false;
  }
  if (uniquifier_.empty()) {
    // If we need a client id, make a request that will get a client id.
    // Sending message TYPE_ASSIGN_CLIENT_ID.

    // Generate a nonce if we haven't already done so.
    if (nonce_ == -1) {
      nonce_ = resources_->current_time().ToInternalValue();
    }
    message->mutable_client_type()->CopyFrom(client_type_);
    message->mutable_app_client_id()->set_string_value(app_client_id_);
    message->set_nonce(nonce_);
    message->set_action(ClientToServerMessage_Action_ASSIGN_CLIENT_ID);
    message->set_message_type(
        ClientToServerMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);
    last_send_time_ = resources_->current_time();
    ++session_attempt_count_;
    return false;
  }
  if (session_token_.empty()) {
    // Else, if we need a session, make a request that will get a session
    // token.
    // Sending message TYPE_UPDATE_SESSION.
    message->set_client_uniquifier(uniquifier_);
    message->set_action(ClientToServerMessage_Action_UPDATE_SESSION);
    message->set_message_type(
        ClientToServerMessage_MessageType_TYPE_UPDATE_SESSION);
    last_send_time_ = resources_->current_time();
    ++session_attempt_count_;
    return false;
  }
  // Sending TYPE_OBJECT_CONTROL.
  message->set_message_type(
      ClientToServerMessage_MessageType_TYPE_OBJECT_CONTROL);
  message->set_session_token(session_token_);
  return true;
}

MessageAction SessionManager::ProcessMessage(
    const ServerToClientMessage& message) {

  if (!message.has_message_type()) {
    TLOG(WARNING_LEVEL, "Ignoring message with no type");
    return IGNORE_MESSAGE;
  }

  if (!version_manager_.ProtocolVersionSupported(message)) {
    TLOG(WARNING_LEVEL, "Ignoring message with unsupported version");
    return IGNORE_MESSAGE;
  }

  ServerToClientMessage_MessageType msg_type = message.message_type();
  TLOG(INFO_LEVEL, "Process message with type %d", msg_type);
  switch (msg_type) {
    case ServerToClientMessage_MessageType_TYPE_ASSIGN_CLIENT_ID:
      return ProcessAssignClientId(message);

    case ServerToClientMessage_MessageType_TYPE_UPDATE_SESSION:
      return ProcessUpdateSession(message);

    case ServerToClientMessage_MessageType_TYPE_INVALIDATE_CLIENT_ID:
      return ProcessInvalidateClientId(message);

    case ServerToClientMessage_MessageType_TYPE_INVALIDATE_SESSION:
      return ProcessInvalidateSession(message);

    case ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL:
      // Delegate should-process check.
      return CheckObjectControlMessage(message);

    default:
      // Unrecognizable message.
      TLOG(WARNING_LEVEL, "Unknown message type: %d", msg_type);
      return IGNORE_MESSAGE;
  }
}

MessageAction SessionManager::ProcessAssignClientId(
    const ServerToClientMessage& message) {
  // SUCCESS status required by spec.
  // TODO: handle PERMANENT_FAILURE.
  if (message.status().code() != Status_Code_SUCCESS) {
    TLOG(WARNING_LEVEL,
         "Ignoring assign-client-id message with non-success response: %d",
         message.status().code());
    return IGNORE_MESSAGE;
  }

  // Ignore the message if we have an id.
  if (!uniquifier_.empty()) {
    TLOG(INFO_LEVEL, "Ignoring assign-client-id message: Ticl has an id");
    return IGNORE_MESSAGE;
  }

  // Ignore the message if we don't have a nonce -- presence of a nonce
  // indicates that we're expecting an id.
  if (nonce_ == -1) {
    TLOG(INFO_LEVEL, "Ignoring assign-client-id message: Ticl has no nonce");
    return IGNORE_MESSAGE;
  }

  // Ignore the message if the message does not have a nonce in it.
  if (!message.has_nonce()) {
    TLOG(WARNING_LEVEL,
         "Ignoring purported assign-client-id message with no nonce");
    return IGNORE_MESSAGE;
  }

  // Ignore the message if its nonce does not match our nonce.
  if (nonce_ != message.nonce()) {
    TLOG(INFO_LEVEL,
         "Ignoring assign-client-id message with non-matching nonce: "
         "%lld vs %lld", nonce_, message.nonce());
    return IGNORE_MESSAGE;
  }

  // Ignore the message if it does not have a client id and session token.
  if (!(message.has_session_token() && message.has_client_uniquifier())) {
    TLOG(WARNING_LEVEL, "Ignoring purported assign-client-id with a missing "
         "client id or session");
    return IGNORE_MESSAGE;
  }

  // Ignore the message if it has an empty client id or session token.
  // This check prevents us from accepting obviously-wrong data.
  if (message.session_token().empty() || message.client_uniquifier().empty()) {
    TLOG(WARNING_LEVEL, "Ignoring purported assign-client-id with a empty "
         "client id or session");
    return IGNORE_MESSAGE;
  }

  // Ignore the message if its client type and app client id do not match ours.
  bool client_type_matches =
      message.client_type().type() == client_type_.type();
  bool app_client_id_matches =
      message.app_client_id().string_value() == app_client_id_;
  if (!(client_type_matches && app_client_id_matches)) {
    TLOG(INFO_LEVEL,
         "Ignoring assign-client-id message with non-matching client type or "
         "app-client id");
    return IGNORE_MESSAGE;
  }

  // Message passes verification. Acquire the client id and session token
  // from the message. Clear our nonce.
  TLOG(INFO_LEVEL, "Accepting assign-client-id request");
  session_token_ = message.session_token();
  uniquifier_ = message.client_uniquifier();
  nonce_ = -1;

  // Reset the count of unsuccessful session acquisition attempts.
  session_attempt_count_ = 0;

  return ACQUIRE_SESSION;
}

MessageAction SessionManager::ProcessUpdateSession(
    const ServerToClientMessage& message) {
  // SUCCESS status required by spec.
  if (message.status().code() != Status_Code_SUCCESS) {
    TLOG(WARNING_LEVEL,
         "Ignoring update-session message with non-success response");
    return IGNORE_MESSAGE;
  }

  // If we don't have a client id, we can't accept a new session.
  if (uniquifier_.empty()) {
    TLOG(INFO_LEVEL, "Ignoring update-session since Ticl has no client id");
    return IGNORE_MESSAGE;
  }

  // If the message does not have a client id, we can't process it.
  if (!message.has_client_uniquifier()) {
    TLOG(WARNING_LEVEL, "Ignoring purported update-session with no client id");
    return IGNORE_MESSAGE;
  }

  // If the message does not have a session, we can't process it.
  // We check sessionToken == '' to avoid taking an obviously-wrong token.
  if (!message.has_session_token() || message.session_token().empty()) {
    TLOG(WARNING_LEVEL, "Ignoring purported update-session with no session");
    return IGNORE_MESSAGE;
  }

  // We accept the new session if the client id in the message matches our own.
  if (message.client_uniquifier() == uniquifier_) {
    TLOG(INFO_LEVEL, "Accepting new session %s replacing old session %s",
         message.session_token().c_str(), session_token_.c_str());
    session_token_ = message.session_token();
    // Reset the count of unsuccessful session acquisition attempts.
    session_attempt_count_ = 0;
    return ACQUIRE_SESSION;
  }
  return IGNORE_MESSAGE;
}

MessageAction SessionManager::ProcessInvalidateClientId(
    const ServerToClientMessage& message) {
  // UNKNOWN_CLIENT status required by spec.
  if (message.status().code() != Status_Code_UNKNOWN_CLIENT) {
    TLOG(WARNING_LEVEL,
         "Ignoring invalidate-client-id msg with non-UNKNOWN_CLIENT response");
    return IGNORE_MESSAGE;
  }

  // We cannot invalidate our client id if we do not have one.
  if (uniquifier_.empty()) {
    TLOG(INFO_LEVEL,
         "Ignoring invalidate-client-id message since the Ticl has no id");
    return IGNORE_MESSAGE;
  }

  // Invalidate our client id if the client id in the message matches ours.
  if (uniquifier_ == message.client_uniquifier()) {
    TLOG(INFO_LEVEL, "Client id invalidated");
    uniquifier_.clear();
    session_token_.clear();
    // Set the "last send time" into the far past so we'll be allowed to send an
    // assign-client-id request at least once.
    last_send_time_ = Time() - TimeDelta::FromHours(1);
    return LOSE_SESSION;
  } else {
    TLOG(INFO_LEVEL, "Ignoring invalidate-client with mis-matching client id");
    return IGNORE_MESSAGE;
  }
}

MessageAction SessionManager::ProcessInvalidateSession(
    const ServerToClientMessage& message) {
  // INVALID_SESSION status required by spec.
  if (message.status().code() != Status_Code_INVALID_SESSION) {
    TLOG(WARNING_LEVEL,
         "Ignoring invalidate-session msg with non-INVALID_SESSION response");
    return IGNORE_MESSAGE;
  }

  // If we do not have a session, we cannot invalidate it.
  if (session_token_.empty()) {
    TLOG(INFO_LEVEL,
         "Ignoring invalide-session message since Ticl has no session");
    return IGNORE_MESSAGE;
  }

  // If the message does not have a session, we cannot invalidate ours.
  if (!message.has_session_token()) {
    TLOG(WARNING_LEVEL,
         "Ignoring purported invalidate-session message with no session token");
    return IGNORE_MESSAGE;
  }

  // Invalidate our session token if it matches the one in the message.
  if (session_token_ == message.session_token()) {
    TLOG(INFO_LEVEL, "Invalidating session: %s", session_token_.c_str());
    session_token_.clear();
    // Set the "last send time" into the far past so we'll be allowed to send an
    // update-session request at least once.
    last_send_time_ = Time() - TimeDelta::FromHours(1);
    return LOSE_SESSION;
  }
  return IGNORE_MESSAGE;
}

MessageAction SessionManager::CheckObjectControlMessage(
    const ServerToClientMessage& message) {
  // SUCCESS status required by spec.
  if (message.status().code() != Status_Code_SUCCESS) {
    TLOG(WARNING_LEVEL,
         "Ignoring object-control message with non-success response");
    return IGNORE_MESSAGE;
  }

  // If we don't have a valid session and client, we cannot process the
  // message. Technically, sessionToken != null implies clientId != null, but
  // we'll be defensive and check both.
  if (session_token_.empty() || uniquifier_.empty()) {
    TLOG(INFO_LEVEL,
         "Ignoring OBJECT_CONTROL message since Ticl does not have a session");
    return IGNORE_MESSAGE;
  }

  // If the message doesn't have a session, we cannot process it.
  if (!message.has_session_token()) {
    TLOG(WARNING_LEVEL, "Received purported OBJECT_CONTROL message with no "
         "session token; ignoring");
    return IGNORE_MESSAGE;
  }

  // We have a session and the message has a session. We process the message if
  // the sessions match.
  if (session_token_ == message.session_token()) {
    return PROCESS_OBJECT_CONTROL;
  } else {
    TLOG(INFO_LEVEL, "session token mismatch: %s vs %s", session_token_.c_str(),
         message.session_token().c_str());
    return IGNORE_MESSAGE;
  }
}

bool SessionManager::HasDataToSend() {
  // If we haven't sent anything in a very long time, reset the session attempt
  // counter.
  Time now = resources_->current_time();
  if (now - last_send_time_ >
      TimeDelta::FromMinutes(kWakeUpAfterGiveUpIntervalMinutes)) {
    session_attempt_count_ = 0;
  }
  // The session manager only needs to send data if we don't have a session.
  // It's only allowed to do so if it (a) hasn't just sent a request, and (b)
  // hasn't tried more than kMaxSessionAttempts times without getting a
  // successful response.
  return !HasSession() &&
      (now > last_send_time_ + config_.registration_timeout) &&
      (session_attempt_count_ < kMaxSessionAttempts);
}

const int SessionManager::kMaxSessionAttempts = 5;
const int SessionManager::kWakeUpAfterGiveUpIntervalMinutes = 3 * 60;

}  // namespace invalidation
