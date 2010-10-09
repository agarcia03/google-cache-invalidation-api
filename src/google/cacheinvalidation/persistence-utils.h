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

#ifndef GOOGLE_CACHEINVALIDATION_PERSISTENCE_UTILS_H_
#define GOOGLE_CACHEINVALIDATION_PERSISTENCE_UTILS_H_

#include "google/cacheinvalidation/ticl_persistence.pb.h"

namespace invalidation {

// Creates an envelope for state including a message digest and serializes the
// whole thing to out.
void SerializeState(const TiclState& state, string* out);

// Deserializes serialized, checks the message digest on the envelope, and if it
// validates, initializes state from it.  Returns whether deserialization was
// successful.
bool DeserializeState(const string& serialized, TiclState* state);

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_PERSISTENCE_UTILS_H_
