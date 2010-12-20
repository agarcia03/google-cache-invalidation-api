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
#include "google/cacheinvalidation/version-manager.h"

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

  // We lost our client id.
  LOSE_CLIENT_ID,

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
  enum State {
    State_NO_UNIQUIFIER_OR_SESSION,  // has neither uniqifier nor session token
    State_UNIQUIFIER_ONLY,  // has uniquifier but no session token
    State_UNIQUIFIER_AND_SESSION  // has uniquifier and session token
    // Note: a client cannot have a session token without a uniquifier
  };

  SessionManager(const ClientConfig& config, ClientType client_type,
                 const string& app_client_id, SystemResources* resources,
                 const string& uniquifier, const string& session_token)
      : config_(config),
        client_type_(client_type),
        app_client_id_(app_client_id),
        nonce_(-1),
        last_send_time_(Time() - TimeDelta::FromHours(1)),
        session_attempt_count_(0),
        resources_(resources),
        uniquifier_(uniquifier),
        session_token_(session_token),
        shutdown_(false),
        // Version manager doesn't need client info for session manager.
        version_manager_("") {
    AddSupportedProtocolVersions();
    UpdateState();
  }

  // Updates the field that tracks this object's state.
  void UpdateState();

  /* Returns whether the session manager has a session. */
  bool HasSession() {
    return !session_token_.empty();
  }

  // Returns true iff the message is intended for this client.  The actual
  // verification is message-type dependent. E.g., for an assign-client-id
  // message, a nonce check is involved, whereas for an object control message,
  // the session token is checked.
  bool IsMessageIntendedForClient(const ServerToClientMessage& message);

  // Adds content to the message relating to client id and session.
  void AddSessionAction(ClientToServerMessage* message);

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

  // Takes actions and updates internal state in response to the loss of a
  // client id.
  void DoLoseClientId();

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

  /* Returns whether the session manager has data to send. */
  bool HasDataToSend();

  /* Informs the session manager that the client is shutting down.  Any
   * subsequent outbound messages will be of type SHUTDOWN.
   */
  void Shutdown() {
    shutdown_ = true;
  }

  /* Registers versions of the protocol that this client implementation
   * understands.
   */
  void AddSupportedProtocolVersions() {
    version_manager_.AddSupportedProtocolVersion(0);
    version_manager_.AddSupportedProtocolVersion(1);
  }

  const string& client_uniquifier() const {
    return uniquifier_;
  }

  const string& session_token() {
    return session_token_;
  }

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

  /* The number of times we've sent a request for a client id or session without
   * getting a successful response.  We only try a fixed number of times before
   * giving up.
   */
  int session_attempt_count_;

  /* System resources (just used for logging here). */
  SystemResources * const resources_;

  /* The current state of the session manager. */
  State state_;

  /* The client's id, or {@code null} if unassigned. */
  string uniquifier_;

  /* The client's session id, or {@code null} if unassigned. */
  string session_token_;

  /* Whether this client has been shut down. */
  bool shutdown_;

  /* Tracks versions supported by this client. */
  VersionManager version_manager_;

  /* The maximum number of times we'll request a session (without a successful
   * response) before giving up.
   */
  static const int kMaxSessionAttempts;

  /* The amount of time to wait (in minutes) before waking up after giving up on
   * requesting a new session (currently 1 hour).
   */
  static const int kWakeUpAfterGiveUpIntervalMinutes;

 public:
  static int getMaxSessionAttemptsForTest() {
    return kMaxSessionAttempts;
  }

  static TimeDelta getWakeUpAfterGiveUpIntervalForTest() {
    return TimeDelta::FromMinutes(kWakeUpAfterGiveUpIntervalMinutes);
  }

  friend class InvalidationClientImpl;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_SESSION_MANAGER_H_
