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

#include "google/cacheinvalidation/v2/client-protocol-namespace-fix.h"
#include "google/cacheinvalidation/v2/hash_map.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"

namespace invalidation {

using ::google::protobuf::Message;
using ::google::protobuf::TextFormat;

// Hash functions for various protocol messages.
struct ProtoHash {
  size_t operator()(const ObjectIdP& object_id) const {
    size_t accum = 1;
    const string& object_name = object_id.name();
    for (size_t i = 0; i < object_name.length(); ++i) {
      accum = accum * 31 + object_name.data()[i];
    }
    return accum ^ object_id.source();
  }

  size_t operator()(const InvalidationP& invalidation) const {
    return (*this)(invalidation.object_id()) ^ invalidation.version() ^
        invalidation.is_known_version();
  }

  size_t operator()(const RegistrationSubtree& reg_subtree) const {
    RepeatedPtrField<ObjectIdP> objects = reg_subtree.registered_object();
    RepeatedPtrField<ObjectIdP>::const_iterator iter;
    size_t accum = 0;
    for (iter = objects.begin(); iter != objects.end(); ++iter) {
      accum = (accum * 31) + (*this)(*iter);
    }
    return accum;
  }
};

// Equality operators for various protocol messages.
struct ProtoEq {
  bool operator()(const ObjectIdP& object_id1,
                  const ObjectIdP& object_id2) const {
    return (object_id1.source() == object_id2.source()) &&
        (object_id1.name() == object_id2.name());
  }

  bool operator()(const InvalidationP& inv1,
                  const InvalidationP& inv2) const {
    return (*this)(inv1.object_id(), inv2.object_id()) &&
        (inv1.version() == inv2.version()) &&
        (inv1.is_known_version() == inv2.is_known_version()) &&
        (inv1.payload() == inv2.payload());
  }

  bool operator()(const RegistrationSubtree& reg_subtree1,
                  const RegistrationSubtree& reg_subtree2) const {
    RepeatedPtrField<ObjectIdP> objects1 = reg_subtree1.registered_object();
    RepeatedPtrField<ObjectIdP> objects2 = reg_subtree2.registered_object();
    if (objects1.size() != objects2.size()) {
      return false;
    }
    RepeatedPtrField<ObjectIdP>::const_iterator iter1, iter2;
    for (iter1 = objects1.begin(), iter2 = objects2.begin();
         iter1 != objects1.end(); ++iter1, ++iter2) {
      if (!(*this)(*iter1, *iter2)) {
        return false;
      }
    }
    return true;
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
