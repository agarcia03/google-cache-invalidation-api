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

#ifndef GOOGLE_CACHEINVALIDATION_V2_OBJECT_ID_DIGEST_UTILS_H_
#define GOOGLE_CACHEINVALIDATION_V2_OBJECT_ID_DIGEST_UTILS_H_

#include <map>

#include "google/cacheinvalidation/v2/client-protocol-namespace-fix.h"
#include "google/cacheinvalidation/v2/digest-function.h"

namespace invalidation {

class ObjectIdDigestUtils {
 public:
  /* Returns the digest of the set of keys in the given map. */
  template<typename T>
  static string GetDigest(
      map<string, T> registrations, DigestFunction* digest_fn);

  /* Returns the digest of object_id using digest_fn. */
  static string GetDigest(
      const ObjectIdP& object_id, DigestFunction* digest_fn);
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_OBJECT_ID_DIGEST_UTILS_H_
