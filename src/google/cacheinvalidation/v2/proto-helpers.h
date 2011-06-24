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
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_PROTO_HELPERS_H_
