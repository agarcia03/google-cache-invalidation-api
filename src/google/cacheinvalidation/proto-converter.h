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

// Utility routines to convert between protocol buffers and external types
// exposed by the TICL to applications.

#ifndef GOOGLE_CACHEINVALIDATION_PROTO_CONVERTER_H_
#define GOOGLE_CACHEINVALIDATION_PROTO_CONVERTER_H_

#include "google/cacheinvalidation/invalidation-types.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::string;

// Initializes an ObjectId from a raw protocol buffer representation.  Note that
// even if the object_id.source() is not convertible (due to enum value in
// ObjectIdP_Source missing in ObjectSource_Type's enum, result is still
// initialized (with an enum value corresponding to no enum in
// ObjectSource_Type).
void ConvertFromObjectIdProto(const ObjectIdP& object_id, ObjectId* result);

// Initializes an object id protocol buffer from an ObjectId.  Note that even if
// the object_id.source() is not convertible (due to enum value in
// ObjectSource_Type missing in ObjectId_Source's enum, result is still
// initialized (with an enum value corresponding to no enum in
// ObjectIdP_Source).
void ConvertToObjectIdProto(const ObjectId& object_id, ObjectIdP* result);

// Initializes an Invalidation from a raw protocol buffer representation.  See
// discussion in ConvertFromObjectIdProto for cases where the object id in
// "invalidation" has the wrong enum value.
void ConvertFromInvalidationProto(
    const InvalidationP& invalidation, Invalidation* result);

// Initializes an invalidation protocol buffer from an Invalidation. See
// discussion in ConvertToObjectIdProto for cases where the object id in
// "invalidation" has the wrong enum value).
void ConvertToInvalidationProto(
    const Invalidation& invalidation, InvalidationP* result);

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_PROTO_CONVERTER_H_
