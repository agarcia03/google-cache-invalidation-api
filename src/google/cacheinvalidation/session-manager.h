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

#ifndef GOOGLE_CACHEINVALIDATION_SESSION_MANAGER_H_
#define GOOGLE_CACHEINVALIDATION_SESSION_MANAGER_H_

#include <string>

#include "base/basictypes.h"
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/stl-namespace.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::string;

class SystemResources;

/* Possible categories for inbound messages. */
enum MessageAction {
  // We acquired a new session token from this message.  The Ticl should repeat
  // any registration operations that may have been lost.
  ACQUIRE_SESSION,

  // We lost our session.
  LOSE_SESSION,

  // This message's session token matches the one we had.  The Ticl should
  // continue processing content from this message.
  PROCESS_OBJECT_CONTROL,

  // The session manager believes the message contains no useful information for
  // the Ticl.  For example, the message may be malformed, or the session token
  // may be absent or old.
  IGNORE_MESSAGE,
};

/* Manages client and session life cycles for InvalidationClientImpl.
 * This class is not thread-safe, so the Ticl must perform its own
 * synchronization.
 *
 * This is an internal helper class for InvalidationClientImpl.
 */
class SessionManager {
 private:
  SessionManager(const ClientConfig& config, ClientType client_type,
                 const string& app_client_id, SystemResources* resources)
      : config_(config),
        client_type_(client_type),
        app_client_id_(app_client_id),
        nonce_(-1),
        last_send_time_(Time() - TimeDelta::FromHours(1)),
        resources_(resources),
        uniquifier_(""),
        session_token_("") {}

  /* Constructs a session manager with a specified client id. */
  SessionManager(const ClientConfig& config, ClientType client_type,
                 const string& app_client_id, SystemResources* resources,
                 const string& client_internal_id)
      : config_(config),
        client_type_(client_type),
        app_client_id_(app_client_id),
        nonce_(-1),
        last_send_time_(Time() - TimeDelta::FromHours(1)),
        resources_(resources),
        uniquifier_(client_internal_id),
        session_token_("") {}

  /* If the client currently has no client id, sets the ASSIGN_CLIENT_ID action
   * in the given message, along with the client type, app client id, and a
   * nonce with which the reply will be matched.  If the client has an id but no
   * valid session, sets the UPDATE_SESSION action in the given message.
   * Returns whether it believes the current session to be valid, in which case
   * the Ticl may add invalidation acknowledgments and registration updates to
   * the message.
   */
  bool AddSessionAction(ClientToServerMessage* message);

  /* Consumes a message received from the server. If the message is a
   * session-related message (i.e., has type TYPE_ASSIGN_CLIENT_ID,
   * TYPE_UPDATE_SESSION, TYPE_INVALIDATE_CLIENT_ID, or
   * TYPE_INVALIDATE_SESSION), processes it.  Additionally, the return value
   * indicates what actions the main Ticl class should take as a result of
   * receiving this message.
   */
  MessageAction ProcessMessage(const ServerToClientMessage& message);

  /* Processes an ASSIGN_CLIENT_ID message.
   * REQUIRES: the message be of type ASSIGN_CLIENT_ID.
   */
  MessageAction ProcessAssignClientId(const ServerToClientMessage& message);

  /* Processes an UPDATE_SESSION message.
   * REQUIRES: the message be of type UPDATE_SESSION.
   */
  MessageAction ProcessUpdateSession(const ServerToClientMessage& message);

  /* Processes an INVALIDATE_CLIENT_ID message.
   * REQUIRES: the message be of type INVALIDATE_CLIENT_ID.
   */
  MessageAction ProcessInvalidateClientId(const ServerToClientMessage& message);

  /* Processes an INVALIDATE_SESSION message.
   * REQUIRES: the message be of type INVALIDATE_SESSION.
   */
  MessageAction ProcessInvalidateSession(const ServerToClientMessage& message);

  /* Determines whether the Ticl should process an OBJECT_CONTROL message.  This
   * is done by verifying that the session token in the message matches the
   * session token currently held by the session manager.
   *
   * REQUIRES: the message be of type OBJECT_CONTROL.
   */
  MessageAction CheckObjectControlMessage(const ServerToClientMessage& message);

  const string& client_uniquifier() const {
    return uniquifier_;
  }

  const string& session_token() {
    return session_token_;
  }

  /* Returns whether the session manager has a session. */
  bool HasSession() {
    return !session_token_.empty();
  }

  /* Returns whether the Ticl has data to send. */
  bool HasDataToSend() {
    return !HasSession() &&
        (resources_->current_time() >
         last_send_time_ + config_.registration_timeout);
  }

  /* Clears the is_new_client flag if ready_client_id is equal to the current
   * client id.
   */
  void CompleteInitializationForClient(const string& ready_client_id);

  /* Configuration parameters. */
  ClientConfig config_;

  /* The type of client application. */
  const ClientType client_type_;

  /* An application-assigned id for the client. */
  const string app_client_id_;

  /* A nonce to match client id-assignment responses. */
  int64 nonce_;

  /* The last time we sent a message requesting a client id or session. */
  Time last_send_time_;

  /* System resources (just used for logging here). */
  SystemResources * const resources_;

  /* The client's id, or {@code null} if unassigned. */
  string uniquifier_;

  /* The client's session id, or {@code null} if unassigned. */
  string session_token_;

  friend class InvalidationClientImpl;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_SESSION_MANAGER_H_
