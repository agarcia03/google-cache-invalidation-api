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

// Various types used by the applications using the invalidation client
// library.

#ifndef GOOGLE_CACHEINVALIDATION_INVALIDATION_TYPES_H_
#define GOOGLE_CACHEINVALIDATION_INVALIDATION_TYPES_H_

#include <cstddef>
#include <string>
#include <vector>

#include "google/cacheinvalidation/internal.pb.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/scoped_ptr.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/types.pb.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::string;
using INVALIDATION_STL_NAMESPACE::vector;

// A class to represent a unique object id that an application can register or
// unregister for.
class ObjectId {
 public:
  // Constructs a default (uninitialized) ObjectId instance.
  ObjectId() : is_initialized_(false) {}

  // Creates an object id for the given source and id name.
  ObjectId(ObjectSource_Type source, const string& name)
      : is_initialized_(false) {
    Init(source, name);
  }

  // Makes a copy of "from".
  ObjectId(const ObjectId& from) : is_initialized_(false) {
    CHECK(from.is_initialized_);
    Init(from.source(), from.name());
  }

  // Initializes the state of the object with the given source and name.
  void Init(ObjectSource_Type source, const string& name) {
    CHECK(!is_initialized_);  // Disallow re-initialization.
    is_initialized_ = true;
    source_ = source;
    name_ = name;
  }

  const ObjectSource_Type source() const {
    CHECK(is_initialized_);
    return source_;
  }

  const string& name() const {
    CHECK(is_initialized_);
    return name_;
  }

 private:
  // Prevent implicit definition / use of assignment operator.
  ObjectId& operator=(const ObjectId& other);

  // Whether this object has been initialized.
  bool is_initialized_;

  // The property that hosts the object.
  ObjectSource_Type source_;

  // The name/unique id for the object.
  string name_;
};


// A class to represent an invalidation for a given object/version and an
// optional payload.
class Invalidation {
 public:
  // Constructs a default (uninitialized) invalidation instance.
  Invalidation() : is_initialized_(false) {}

  // Creates an invalidation for the given object, version} and
  // optional/nullable payload} and optional/nullable component_stamp_log.
  // Ownership of payload and component_stamp_log is retained by the caller.
  Invalidation(const ObjectId& object_id, int64 version,
               const string* payload,
               const ComponentStampLog* component_stamp_log)
      : is_initialized_(false) {
    Init(object_id, version, payload, component_stamp_log);
  }

  Invalidation(const Invalidation& inv) : is_initialized_(false) {
    CHECK(inv.is_initialized_);
    Init(inv.object_id_, inv.version_, inv.payload_.get(),
         inv.component_stamp_log_.get());
  }

  const ObjectId& object_id() const {
    CHECK(is_initialized_);
    return object_id_;
  }

  int64 version() const {
    CHECK(is_initialized_);
    return version_;
  }

  bool has_payload() const {
    CHECK(is_initialized_);
    return payload_.get() != NULL;
  }

  // Returns the payload contained in this invalidation.
  // REQUIRES: has_payload().
  const string& payload() const {
    CHECK(is_initialized_);
    CHECK(has_payload());
    return *payload_.get();
  }

  bool has_component_stamp_log() const {
    CHECK(is_initialized_);
    return component_stamp_log_.get() != NULL;
  }

  // Returns the component stamp log contained in this invalidation.
  // REQUIRES: has_component_stamp_log().
  const ComponentStampLog& component_stamp_log() const {
    CHECK(is_initialized_);
    CHECK(has_component_stamp_log());
    return *component_stamp_log_.get();
  }

  // Initializes the Invalidation obhect with the given fields.
  void Init(const ObjectId& object_id, int64 version,
            const string* payload,
            const ComponentStampLog* component_stamp_log) {
    CHECK(!is_initialized_);  // Disallow re-initialization.
    is_initialized_ = true;
    object_id_.Init(object_id.source(), object_id.name());
    version_ = version;

    // Set the payload field only if any payload is present. Else leave it as
    // NULL (in the scoped_ptr).
    if (payload != NULL) {
      payload_.reset(new string(*payload));
    }

    // Set the component_stamp_log field only if any stamp log is present. Else
    // leave it as NULL (in the scoped_ptr).
    if (component_stamp_log != NULL) {
      component_stamp_log_.reset(new ComponentStampLog);
      component_stamp_log_->CopyFrom(*component_stamp_log);
    }
  }

 private:
  // Prevent implicit definition / use of assignment operator.
  Invalidation& operator=(const Invalidation& inv);

  // Whether this invalidation is initialized.
  bool is_initialized_;

  // The object being invalidated/updated.
  ObjectId object_id_;

  // The new version of the object.
  int64 version_;

  // Optional payload for the object.
  scoped_ptr<string> payload_;

  // Optional stamp log.
  scoped_ptr<ComponentStampLog> component_stamp_log_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_INVALIDATION_TYPES_H_
