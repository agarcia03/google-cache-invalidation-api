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

// Statistics for the Ticl, e.g., number of registration calls, number of token
// mismatches, etc.

#include "google/cacheinvalidation/v2/statistics.h"

namespace invalidation {

const char* Statistics::SentMessageType_names[] = {
  "info",
  "initialize",
  "invalidation_ack",
  "registration",
  "registration_sync",
  "total",
};

const char* Statistics::ReceivedMessageType_names[] = {
  "info_request",
  "invalidation",
  "registration_status",
  "registration_sync_request",
  "invalidation_token_control",
  "total",
};

const char* Statistics::IncomingOperationType_names[] = {
  "acknowledge",
  "registration",
  "unregistration",
};

const char* Statistics::ListenerEventType_names[] = {
  "inform_error",
  "inform_registration_failure",
  "inform_registration_status",
  "invalidate",
  "invalidate_all",
  "invalidate_unknown",
  "reissue_registrations",
};

const char* Statistics::ClientErrorType_names[] = {
  "acknowledge_handle_failure",
  "incoming_message_failure",
  "outgoing_message_failure",
  "persistent_deserialization_failure",
  "persistent_read_failure",
  "persistent_write_failure",
  "protocol_version_failure",
  "registration_discrepancy",
  "nonce_mismatch",
  "token_mismatch",
  "token_missing_failure",
  "token_transient_failure",
};

Statistics::Statistics() {
  InitializeMap(sent_message_types_, SentMessageType_MAX);
  InitializeMap(received_message_types_, ReceivedMessageType_MAX);
  InitializeMap(incoming_operation_types_, IncomingOperationType_MAX);
  InitializeMap(listener_event_types_, ListenerEventType_MAX);
  InitializeMap(client_error_types_, ClientErrorType_MAX);
}

void Statistics::GetNonZeroStatistics(
    vector<pair<string, int> >* performance_counters) {
  // Add the non-zero values from the different maps to performance_counters.
  FillWithNonZeroStatistics(
      sent_message_types_, SentMessageType_MAX, SentMessageType_names,
      "sent_message_type.", performance_counters);
  FillWithNonZeroStatistics(
      received_message_types_, ReceivedMessageType_MAX,
      ReceivedMessageType_names, "received_message_type.",
      performance_counters);
  FillWithNonZeroStatistics(
      incoming_operation_types_, IncomingOperationType_MAX,
      IncomingOperationType_names, "incoming_operation_type.",
      performance_counters);
  FillWithNonZeroStatistics(
      listener_event_types_, ListenerEventType_MAX, ListenerEventType_names,
      "listener_event_type.", performance_counters);
  FillWithNonZeroStatistics(
      client_error_types_, ClientErrorType_MAX, ClientErrorType_names,
      "client_error_type.", performance_counters);
}

/* Modifies result to contain those statistics from map whose value is > 0. */
void Statistics::FillWithNonZeroStatistics(
    int map[], int size, const char* names[], const char* prefix,
    vector<pair<string, int> >* destination) {
  for (int i = 0; i < size; ++i) {
    if (map[i] > 0) {
      destination->push_back(
          make_pair(StringPrintf("%s%s", prefix, names[i]), map[i]));
    }
  }
}

void Statistics::InitializeMap(int map[], int size) {
  for (int i = 0; i < size; ++i) {
    map[i] = 0;
  }
}

}  // namespace invalidation
