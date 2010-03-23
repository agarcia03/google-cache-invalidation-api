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
enum MessageSessionStatus {
  // We acquired a new session token from this message.  The Ticl should repeat
  // any registration operations that may have been lost.
  NEW_SESSION,

  // This message's session token matches the one we had.  The Ticl should
  // continue processing content from this message.
  EXISTING_SESSION,

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
  SessionManager(ClientType client_type, const string& app_client_id,
                 SystemResources* resources)
      : client_type_(client_type), app_client_id_(app_client_id), nonce_(-1),
        resources_(resources), uniquifier_(""), session_token_("") {}

  /* Constructs a session manager with a specified client id. */
  SessionManager(ClientType client_type, const string& app_client_id,
                 SystemResources* resources, const string& client_internal_id)
      : client_type_(client_type), app_client_id_(app_client_id),
        nonce_(-1), resources_(resources), uniquifier_(client_internal_id),
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

  /* Extracts client id and session information from a message.  Returns a
   * status code indicating whether the message matches our current session, and
   * if so, whether that session is new or old.
   */
  MessageSessionStatus ClassifyMessage(const ServerToClientMessage& bundle);

  const string& client_uniquifier() {
    return uniquifier_;
  }

  const string& session_token() {
    return session_token_;
  }

  /* Clears the is_new_client flag if ready_client_id is equal to the current
   * client id.
   */
  void CompleteInitializationForClient(const string& ready_client_id);

  /* If we have a client id, returns {@code true}.  Otherwise, checks whether
   * the message has assigned us one: if so, acquires it and returns {@code
   * true}; else returns {@code false}.
   */
  bool CheckClientId(const ServerToClientMessage& bundle);

  /* If the bundle's status code is SUCCESS, returns true.  If it's
   * UNKNOWN_CLIENT or INVALID_SESSION (and properly addressed to this client),
   * deletes the session token (and client id, if appropriate), and returns
   * false.  Otherwise, the message's status code is missing or unexpected, so
   * the method returns false without taking action.  The return value indicates
   * whether the Ticl should attempt to continue processing the message.
   */
  bool HandleStatusCode(const ServerToClientMessage& bundle);

  /* The type of client application. */
  const ClientType client_type_;

  /* An application-assigned id for the client. */
  const string app_client_id_;

  /* A nonce to match client id-assignment responses. */
  int64 nonce_;

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
