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

// Simple, map-based implementation of DigestStore.

#include "google/cacheinvalidation/v2/simple-registration-store.h"

#include "google/cacheinvalidation/v2/object-id-digest-utils.h"

namespace invalidation {

void SimpleRegistrationStore::Add(const ObjectIdP& oid) {
  registrations_[ObjectIdDigestUtils::GetDigest(oid, digest_function_)] = oid;
  RecomputeDigest();
}

void SimpleRegistrationStore::Add(const vector<ObjectIdP>& oids) {
  for (size_t i = 0; i < oids.size(); ++i) {
    const ObjectIdP& oid = oids[i];
    registrations_[ObjectIdDigestUtils::GetDigest(oid, digest_function_)] = oid;
  }
  RecomputeDigest();
}

void SimpleRegistrationStore::Remove(const ObjectIdP& oid) {
  registrations_.erase(ObjectIdDigestUtils::GetDigest(oid, digest_function_));
  RecomputeDigest();
}

void SimpleRegistrationStore::Remove(const vector<ObjectIdP>& oids) {
  for (size_t i = 0; i < oids.size(); ++i) {
    const ObjectIdP& oid = oids[i];
    registrations_.erase(ObjectIdDigestUtils::GetDigest(oid, digest_function_));
  }
  RecomputeDigest();
}

void SimpleRegistrationStore::RemoveAll(vector<ObjectIdP>* oids) {
  for (map<string, ObjectIdP>::const_iterator iter = registrations_.begin();
       iter != registrations_.end(); ++iter) {
    oids->push_back(iter->second);
  }
  registrations_.clear();
  RecomputeDigest();
}

bool SimpleRegistrationStore::Contains(const ObjectIdP& oid) {
  return registrations_.find(
      ObjectIdDigestUtils::GetDigest(oid, digest_function_)) !=
      registrations_.end();
}

void SimpleRegistrationStore::GetElements(
    const string& oid_digest_prefix, int prefix_len,
    vector<ObjectIdP>* result) {
  // We always return all the registrations and let the Ticl sort it out.
  for (map<string, ObjectIdP>::iterator iter = registrations_.begin();
       iter != registrations_.end(); ++iter) {
    result->push_back(iter->second);
  }
}

void SimpleRegistrationStore::RecomputeDigest() {
  digest_ = ObjectIdDigestUtils::GetDigest(
      registrations_, digest_function_);
}

}  // namespace invalidation
