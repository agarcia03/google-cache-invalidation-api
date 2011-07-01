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

// Helper utilities for dealing with protocol buffers.

#ifndef GOOGLE_CACHEINVALIDATION_V2_PROTO_HELPERS_H_
#define GOOGLE_CACHEINVALIDATION_V2_PROTO_HELPERS_H_

#include <sstream>
#include <string>

#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/v2/client-protocol-namespace-fix.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::string;
using ::google::protobuf::Message;
using ::google::protobuf::RepeatedPtrField;
using ::google::protobuf::TextFormat;

// Functor to compare various protocol messages.
struct ProtoCompareLess {
  bool operator()(const ObjectIdP& object_id1,
                  const ObjectIdP& object_id2) const {
    // If the sources differ, then the one with the smaller source is the
    // smaller object id.
    int source_diff = object_id1.source() - object_id2.source();
    if (source_diff != 0) {
      return source_diff < 0;
    }
    // Otherwise, the one with the smaller name is the smaller object id.
    return object_id1.name().compare(object_id2.name()) < 0;
  }

  bool operator()(const InvalidationP& inv1,
                  const InvalidationP& inv2) const {
    const ProtoCompareLess& compare_less_than = *this;
    // If the object ids differ, then the one with the smaller object id is the
    // smaller invalidation.
    if (compare_less_than(inv1.object_id(), inv2.object_id())) {
      return true;
    }
    if (compare_less_than(inv2.object_id(), inv1.object_id())) {
      return false;
    }

    // Otherwise, the object ids are the same, so we need to look at the
    // versions.

    // We define an unknown version to be less than a known version.
    int64 known_version_diff = inv1.is_known_version() - inv2.is_known_version();
    if (known_version_diff != 0) {
      return known_version_diff < 0;
    }

    // Otherwise, they're both known both unknown, so the one with the smaller
    // version is the smaller invalidation.
    return inv1.version() < inv2.version();
  }

  bool operator()(const RegistrationSubtree& reg_subtree1,
                  const RegistrationSubtree& reg_subtree2) const {
    const RepeatedPtrField<ObjectIdP>& objects1 =
        reg_subtree1.registered_object();
    const RepeatedPtrField<ObjectIdP>& objects2 =
        reg_subtree2.registered_object();
    // If they have different numbers of objects, the one with fewer is smaller.
    if (objects1.size() != objects2.size()) {
      return objects1.size() < objects2.size();
    }
    // Otherwise, compare the object ids in order.
    RepeatedPtrField<ObjectIdP>::const_iterator iter1, iter2;
    const ProtoCompareLess& compare_less_than = *this;
    for (iter1 = objects1.begin(), iter2 = objects2.begin();
         iter1 != objects1.end(); ++iter1, ++iter2) {
      if (compare_less_than(*iter1, *iter2)) {
        return true;
      }
      if (compare_less_than(*iter2, *iter1)) {
        return false;
      }
    }
    // The registration subtrees are the same.
    return false;
  }
};

// Other protocol message utilities.
class ProtoHelpers {
 public:
  // Converts a message to text format.
  static string ToString(const Message& message) {
    string result;
    TextFormat::PrintToString(message, &result);
    return result;
  }

  static string ToString(const RegistrationSummary& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "num_registrations: " << message.num_registrations() << ", ";
    ss << "registration_digest: " << message.registration_digest();
    ss << " }";
    return ss.str();
  }

  static string ToString(const ObjectIdP& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "source: " << message.source() << ", ";
    ss << "name: " << message.name();
    ss << " }";
    return ss.str();
  }

  static string ToString(const AckHandleP& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "invalidation: " << ToString(message.invalidation());
    ss << " }";
    return ss.str();
  }

  static string ToString(const ApplicationClientIdP& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "client_name: " << message.client_name();
    ss << " }";
    return ss.str();
  }

  static string ToString(const StatusP& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "code: " << message.code() << ", ";
    ss << "description: " << message.description();
    ss << " }";
    return ss.str();
  }

  static string ToString(const InvalidationP& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "object_id: " << ToString(message.object_id()) << ", ";
    ss << "is_known_version: " << message.is_known_version() << ", ";
    ss << "version: " << message.version() << ", ";
    if (message.has_payload()) {
      ss << "payload: " << message.payload();
    }
    ss << " }";
    return ss.str();
  }

  static string ToString(const RegistrationP& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "object_id: " << ToString(message.object_id()) << ", ";
    ss << "op_type: " << message.op_type();
    ss << " }";
    return ss.str();
  }

  static string ToString(const RegistrationStatus& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "registration: " << ToString(message.registration()) << ", ";
    ss << "status: " << ToString(message.status());
    ss << " }";
    return ss.str();
  }

  static string ToString(const ClientHeader& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "protocol_version: " << "<ToString() not implemented>" << ", ";
    ss << "client_token: " << message.client_token() << ", ";
    ss << "registration_summary: "
       << ToString(message.registration_summary()) << ", ";
    ss << "client_time_ms: " << message.client_time_ms() << ", ";
    ss << "max_known_server_time_ms: " <<
        message.max_known_server_time_ms() << ", ";
    ss << "message_id: " << message.message_id();
    ss << " }";
    return ss.str();
  }

  static string ToString(const InitializeMessage& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "client_type: " << message.client_type() << ", ";
    ss << "nonce: " << message.nonce() << ", ";
    ss << "application_client_id: "
       << ToString(message.application_client_id()) << ", ";
    ss << "digest_serialization_type: " << message.digest_serialization_type();
    ss << " }";
    return ss.str();
  }

  static string ToString(const RegistrationMessage& message) {
    std::stringstream ss;
    ss << "{ ";
    for (int i = 0; i < message.registration_size(); ++i) {
      ss << "registration: " << ToString(message.registration(i)) << ", ";
    }
    ss << " }";
    return ss.str();
  }

  static string ToString(const ClientToServerMessage& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "header: " << ToString(message.header()) << ", ";
    if (message.has_initialize_message()) {
      ss << "initialize_message: "
         << ToString(message.initialize_message()) << ", ";
    }
    if (message.has_registration_message()) {
      ss << "registration_message: "
         << ToString(message.registration_message()) << ", ";
    }
    if (message.has_registration_sync_message()) {
      ss << "registration_sync_message: "
         << ToString(message.registration_sync_message()) << ", ";
    }
    if (message.has_invalidation_ack_message()) {
      ss << "invalidation_ack_message: "
         << ToString(message.invalidation_ack_message()) << ", ";
    }
    if (message.has_info_message()) {
      ss << "info_message: <ToString() unimplemented>" << ", ";
    }
    ss << " }";
    return ss.str();
  }

  static string ToString(const ServerHeader& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "protocol_version: " << "<ToString() not implemented>" << ", ";
    ss << "client_token: " << message.client_token() << ", ";
    ss << "registration_summary: "
       << ToString(message.registration_summary()) << ", ";
    ss << "server_time_ms: " << message.server_time_ms() << ", ";
    ss << "message_id: " << message.message_id();
    ss << " }";
    return ss.str();
  }

  static string ToString(const TokenControlMessage& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "new_token: " << message.new_token();
    ss << " }";
    return ss.str();
  }

  static string ToString(const RegistrationStatusMessage& message) {
    std::stringstream ss;
    ss << "{ ";
    for (int i = 0; i < message.registration_status_size(); ++i) {
      ss << "registration_status: "
         << ToString(message.registration_status(i)) << ", ";
    }
    ss << " }";
    return ss.str();
  }

  static string ToString(const ServerToClientMessage& message) {
    std::stringstream ss;
    ss << "{ ";
    ss << "header: " << ToString(message.header()) << ", ";
    if (message.has_token_control_message()) {
      ss << "token_control_message: "
         << ToString(message.token_control_message()) << ", ";
    }
    if (message.has_invalidation_message()) {
      ss << "invalidation_message: "
         << ToString(message.invalidation_message()) << ", ";
    }
    if (message.has_registration_status_message()) {
      ss << "registration_status_message: "
         << ToString(message.registration_status_message()) << ", ";
    }
    if (message.has_registration_sync_request_message()) {
      ss << "registration_sync_request_message: <present>";
    }
    if (message.has_info_request_message()) {
      ss << "info_request_message: <ToString() unimplemented>" << ", ";
    }
    ss << " }";
    return ss.str();
  }

  static string ToString(const InvalidationMessage& message) {
    std::stringstream ss;
    ss << "{ ";
    for (int i = 0; i < message.invalidation_size(); ++i) {
      ss << "invalidation: " << ToString(message.invalidation(i)) << ", ";
    }
    ss << " }";
    return ss.str();
  }

  static string ToString(const RegistrationSyncMessage& message) {
    std::stringstream ss;
    ss << "{ ";
    for (int i = 0; i < message.subtree_size(); ++i) {
      ss << "subtree: " << ToString(message.subtree(i)) << ", ";
    }
    ss << " }";
    return ss.str();
  }

  static string ToString(const RegistrationSubtree& message) {
    std::stringstream ss;
    ss << "{ ";
    for (int i = 0; i < message.registered_object_size(); ++i) {
      ss << "registered_object: "
         << ToString(message.registered_object(i)) << ", ";
    }
    ss << " }";
    return ss.str();
  }
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_PROTO_HELPERS_H_
