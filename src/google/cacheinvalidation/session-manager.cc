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
  if (uniquifier_.empty()) {
    // If we need a client id, make a request that will get a client id.

    // Generate a nonce if we haven't already done so.
    if (nonce_ == -1) {
      nonce_ = resources_->current_time().ToInternalValue();
    }
    message->mutable_client_type()->CopyFrom(client_type_);
    message->mutable_app_client_id()->set_string_value(app_client_id_);
    message->set_nonce(nonce_);
    message->set_action(ClientToServerMessage_Action_ASSIGN_CLIENT_ID);
    return false;
  }
  if (session_token_.empty()) {
    // Else, if we need a session, make a request that will get a session
    // token.
    message->set_client_id(uniquifier_);
    message->set_action(ClientToServerMessage_Action_UPDATE_SESSION);
    return false;
  }
  message->set_session_token(session_token_);
  return true;
}

MessageSessionStatus SessionManager::ClassifyMessage(
    const ServerToClientMessage& bundle) {

  // Check that the message has a status and, if necessary, a client id.
  if (!(HandleStatusCode(bundle) && CheckClientId(bundle))) {
    return IGNORE_MESSAGE;
  }
  // Check whether the message contains a session token.
  if (!bundle.has_session_token()) {
    // If the message contained no session token, then ignore it.
    TLOG(WARNING_LEVEL, "ignoring message with missing session token");

    return IGNORE_MESSAGE;
  }
  if ((session_token_.empty()) && bundle.has_client_id() &&
      (uniquifier_ == bundle.client_id())) {
    // If we don't have a session token, and the message is properly addressed
    // to this client, take the token from the message, and indicate to the
    // caller that we have a new session.
    session_token_ = bundle.session_token();
    return NEW_SESSION;
  }
  if (bundle.session_token() != session_token_) {
    // If the message's token doesn't match ours, then ignore the message.
    TLOG(WARNING_LEVEL, "message ignored because my session token (%s) "
         "did not match the one in the message: (%s)", session_token_.c_str(),
         bundle.session_token().c_str());
    return IGNORE_MESSAGE;
  }

  // Otherwise the message contains a token matching the one we have.
  return EXISTING_SESSION;
}

bool SessionManager::CheckClientId(const ServerToClientMessage& bundle) {
  // We don't have a client id yet, so look for a valid one in the bundle.
  if (uniquifier_.empty()) {
    bool client_type_matches = bundle.has_client_type() &&
        (client_type_.type() == bundle.client_type().type());
    bool app_client_id_matches = bundle.has_app_client_id() &&
        (app_client_id_ == bundle.app_client_id().string_value());
    bool nonce_matches = bundle.has_nonce() && (nonce_ == bundle.nonce());
    if (bundle.has_client_id() && client_type_matches &&
        app_client_id_matches && nonce_matches) {
      // We have a fresh client id now.
      uniquifier_ = bundle.client_id();
      nonce_ = -1;
    } else {
      TLOG(WARNING_LEVEL,
           "ignoring message with missing client id or mismatched nonce");
      return false;
    }
  }
  return true;
}

bool SessionManager::HandleStatusCode(const ServerToClientMessage& bundle) {
  // Check status, handle session expiration and GC.
  if (!(bundle.has_status() && bundle.status().has_code())) {
    // No status code in the message.  Give up.
    TLOG(WARNING_LEVEL, "message contained no status code");
    return false;
  }
  switch (bundle.status().code()) {
    case Status_Code_UNKNOWN_CLIENT:
      // Make sure the message is actually addressed to this client before
      // blowing away the client id and session.
      if (bundle.has_client_id() && (bundle.client_id() == uniquifier_)) {
        // No record of client at the server (e.g., GC'd).
        // Session is also invalid.
        TLOG(INFO_LEVEL,
             "received UNKNOWN_CLIENT status: clearing id, session");
        uniquifier_.clear();
        session_token_.clear();
      }
      return false;

    case Status_Code_INVALID_SESSION:
      // Make sure the message actually refers to our current session before
      // blowing it away.
      if (bundle.has_session_token() &&
          (bundle.session_token() == session_token_)) {
        // Session is invalid.  Need to get a new session.
        TLOG(INFO_LEVEL, "received INVALID_SESSION status: clearing session");
        session_token_.clear();
      }
      return false;

    case Status_Code_SUCCESS:
      return true;

    default:
      TLOG(WARNING_LEVEL, "unexpected status code: %d", bundle.status().code());
      return false;
  }
}

}  // namespace invalidation
