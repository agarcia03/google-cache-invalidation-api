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

#include <string>

#include "google/cacheinvalidation/proto-converter.h"
#include "google/cacheinvalidation/scoped_ptr.h"
#include "google/cacheinvalidation/stl-namespace.h"

namespace invalidation {

void ConvertFromObjectIdProto(const ObjectIdP& object_id, ObjectId* result) {
  // Just extract the components and call the constructor while
  // casting the enum.
  result->Init((ObjectSource_Type) object_id.source(),
               object_id.name().string_value());
}

void ConvertToObjectIdProto(const ObjectId& object_id, ObjectIdP* result) {
  // Just extract the components and call the constructor while
  // casting the enum.
  result->mutable_name()->set_string_value(object_id.name());
  ObjectIdP_Source proto_source = (ObjectIdP_Source) object_id.source();
  result->set_source(proto_source);
}

void ConvertFromInvalidationProto(
    const InvalidationP& invalidation, Invalidation* result) {
  ObjectId object_id;
  ConvertFromObjectIdProto(invalidation.object_id(), &object_id);

  // Initialize the optional payload and component stamp
  // log fields if they are present. Else set them to NULL.
  const string* payload = invalidation.has_payload() ?
      &invalidation.payload().string_value() : NULL;
  const ComponentStampLog* component_stamp_log =
      invalidation.has_component_stamp_log() ?
          &invalidation.component_stamp_log() : NULL;

  result->Init(object_id, invalidation.version(), payload, component_stamp_log);
}

void ConvertToInvalidationProto(
    const Invalidation& invalidation, InvalidationP* result) {
  ObjectIdP object_id_proto;
  ConvertToObjectIdProto(invalidation.object_id(), &object_id_proto);

  result->set_version(invalidation.version());
  result->mutable_object_id()->CopyFrom(object_id_proto);

  // Initialize the optional payload and component stamp
  // log fields if they are present. Else let them be unset in the proto.
  if (invalidation.has_payload()) {
    StringOrBytesP payload_proto;
    payload_proto.set_string_value(invalidation.payload());
    result->mutable_payload()->CopyFrom(payload_proto);
  }

  if (invalidation.has_component_stamp_log()) {
    result->mutable_component_stamp_log()->CopyFrom(
        invalidation.component_stamp_log());
  }
}

}  // namespace invalidation
