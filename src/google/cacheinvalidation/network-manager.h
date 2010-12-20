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

#ifndef GOOGLE_CACHEINVALIDATION_NETWORK_MANAGER_H_
#define GOOGLE_CACHEINVALIDATION_NETWORK_MANAGER_H_

#include <string>

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/random.h"
#include "google/cacheinvalidation/throttle.h"
#include "google/cacheinvalidation/time.h"
#include "google/cacheinvalidation/types.pb.h"
#include "google/cacheinvalidation/version-manager.h"

namespace invalidation {

struct ClientConfig;
class InvalidationClientImpl;
class NetworkEndpoint;
class SystemResources;

using INVALIDATION_STL_NAMESPACE::string;

/* Keeps track of whether there is outbound data to be sent and informs the
 * application when appropriate.  Handles heartbeats and updates the heartbeat
 * interval when requested by the server.
 *
 * This is an internal helper class for InvalidationClientImpl.
 */
class NetworkManager {
 private:
  /* Constructs a network manager with the given endpoint, resources, and
   * configuration parameters.
   */
  NetworkManager(NetworkEndpoint* endpoint, SystemResources* resources,
                 const string& client_info, const ClientConfig& config);

  // Adds a heartbeat action to the message, if it's time to do so.
  void AddHeartbeat(ClientToServerMessage* message);

  // Adds message id, protocol version, and client version to the message, and
  // clears the flag indicating it has data to send.
  void FinalizeOutboundMessage(ClientToServerMessage* message);

  // Updates the heartbeat interval if this is present in the bundle.
  void HandleInboundMessage(const ServerToClientMessage& bundle);

  // Reschedules a heartbeat because it knows the server has performed one
  // implicitly.
  void RecordImplicitHeartbeat() {
    ScheduleHeartbeat();
  }

  /* Returns whether a heartbeat needs to be sent -- i.e., where the current
   * time is greater than the next_heartbeat_.
   */
  bool NeedsHeartbeat() {
    return resources_->current_time() >= next_heartbeat_;
  }

  /* Returns whether a heartbeat task should be performed, i.e., whether
   * enough time has elapsed since the last communication with the server.
   */
  bool HasDataToSend() {
    return NeedsHeartbeat();
  }

  /* Indicates that the Ticl has data it's ready to send to the server.  If a
   * network listener has been registered and it hasn't been informed about
   * outbound data since it last pulled a message, let it know.
   */
  void OutboundDataReady();

  /* Schedules the next heartbeat by setting nextHeartbeatMs_ to the current
   * time plus a smeared delay.
   */
  void ScheduleHeartbeat();

  /* Registers a listener to be notified when outbound data becomes available.
   * If there is outbound data already waiting to be send, notifies it
   * immediately.
   */
  void RegisterOutboundListener(
      NetworkCallback* outbound_message_ready);

  /* Calls DoInformOutboundListener() through a throttler. */
  void InformOutboundListener();

  /* Schedules a task to inform the network listener immediately that the Ticl
   * has outbound data waiting to be sent.
   */
  void DoInformOutboundListener();

  /* The network endpoint through which the application and Ticl communicate.
   */
  NetworkEndpoint* endpoint_;

  /* System resources (for scheduling and logging). */
  SystemResources* resources_;

  /* A rate-limiter for calls to inform the network listenr that we have data to
   * send.
   */
  Throttle throttle_;

  /* Whether or not we have useful data for the server. */
  bool has_outbound_data_;

  /* A callback to call when an outbound message is ready, or null. */
  NetworkCallback* outbound_listener_;

  /* Ticl configuration parameters. */
  ClientConfig config_;

  /* The next time we should send a heartbeat to the server.
   */
  Time next_heartbeat_;

  /* How long we should wait before sending a message (assuming no additional
   * message content to send).
   */
  TimeDelta heartbeat_delay_;

  /* A message number used to identify an outgoing message. Incremented on every
   * message and used to set the "message_id" field of ClientToServerMessage.
   */
  int message_number_;

  /* Random number generator for smearing. */
  Random random_;

  /* Keeps track of supported protocol and client versions. */
  VersionManager version_manager_;

  /* The maximum delay for the timer that checks whether to send a heartbeat.
   */
  static const int MAX_TIMER_DELAY_MS;

  friend class InvalidationClientImpl;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_NETWORK_MANAGER_H_
