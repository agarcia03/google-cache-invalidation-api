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
#include "google/cacheinvalidation/scoped_ptr.h"
#include "google/cacheinvalidation/types.pb.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::string;
using INVALIDATION_STL_NAMESPACE::vector;

// A class to represent a unique object id that an application can register or
// unregister for.
class ObjectId {
 public:
  // Creates an object id for the given source and id name.
  ObjectId(ObjectSource_Type source, const string& name) {
    Init(source, name);
  }

  // Makes a copy of "from".
  ObjectId(const ObjectId& from) {
    Init(from.source(), from.name());
  }

  const ObjectSource_Type source() const {
    return source_;
  }

  const string& name() const {
    return name_;
  }

 private:

  // Initializes the state of the object  with the given
  // source and name.
  void Init(ObjectSource_Type source, const string& name) {
    source_ = source;
    name_ = name;
  }

  // The property that hosts the object.
  ObjectSource_Type source_;

  // The name/unique id for the object.
  string name_;
};


// A class to represent an invalidation for a given object/version and an
// optional payload.
class Invalidation {
 public:
  //
  // Creates an invalidation for the given object, version} and
  // optional/nullable payload} and optional/nullable component_stamp_log.
  // Ownership of payload and component_stamp_log is retained by the caller.
  Invalidation(const ObjectId& object_id, int64 version,
               const string* payload,
               const ComponentStampLog* component_stamp_log)  {
    Init(object_id, version, payload, component_stamp_log);
  }

  Invalidation(const Invalidation& inv) {
    Init(*inv.object_id_.get(), inv.version_, inv.payload(),
         inv.component_stamp_log());
  }

  const ObjectId& object_id() const {
    return *object_id_.get();
  }

  int64 version() const {
    return version_;
  }

  // Returns the payload contained in this invalidation, if any.
  // If no payload exists, returns NULL. Ownership of the
  // returned result is retained by this.
  const string* payload() const {
    return payload_.get();
  }

  // Returns the component stamp log contained in this invalidation, if any.
  // If no component stamp log exists, returns NULL.  Ownership of the returned
  // result is retained by this.
  const ComponentStampLog* component_stamp_log() const {
    return component_stamp_log_.get();
  }

 private:
  // Initializes the Invalidation obhect with the given fields.
  void Init(const ObjectId& object_id, int64 version,
            const string* payload,
            const ComponentStampLog* component_stamp_log) {
    version_ = version;
    object_id_.reset(new ObjectId(object_id));

    // Set the payload field only if any payload is present. Else leave it as
    // NULL (in the scoped_ptr).
    if (payload != NULL) {
      payload_.reset(new string(*payload));
    }
    // Set the component_stamp_log field only if any stamp log is present. Else
    // leave it as NULL (in the scoped_ptr).

    if (component_stamp_log != NULL) {
      component_stamp_log_.reset(new ComponentStampLog(*component_stamp_log));
    }
  }

  // The object being invalidated/updated.
  scoped_ptr<ObjectId> object_id_;

  // The new version of the object.
  int64 version_;

  // Optional payload for the object.
  scoped_ptr<string> payload_;

  // Optional stamp log.
  scoped_ptr<ComponentStampLog> component_stamp_log_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_INVALIDATION_TYPES_H_
