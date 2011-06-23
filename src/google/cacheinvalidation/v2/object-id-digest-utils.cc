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

// Digest-related utilities for object ids.

#include "google/cacheinvalidation/v2/object-id-digest-utils.h"

namespace invalidation {

template<typename T>
string ObjectIdDigestUtils::GetDigest(
    map<string, T> registrations, DigestFunction* digest_fn) {
  digest_fn->Reset();
  for (map<string, ObjectIdP>::iterator iter = registrations.begin();
       iter != registrations.end(); ++iter) {
    digest_fn->Update(iter->first);
  }
  return digest_fn->GetDigest();
}

string ObjectIdDigestUtils::GetDigest(
    const ObjectIdP& object_id, DigestFunction* digest_fn) {
  digest_fn->Reset();
  int32 source = object_id.source();
  string buffer(4, 0);

  // Little endian number for type followed by bytes.
  buffer[0] = source & 0xff;
  buffer[1] = (source >> 8) & 0xff;
  buffer[2] = (source >> 16) & 0xff;
  buffer[3] = (source >> 24) & 0xff;

  digest_fn->Update(buffer);
  digest_fn->Update(object_id.name());
  return digest_fn->GetDigest();
}

}  // namespace invalidation
