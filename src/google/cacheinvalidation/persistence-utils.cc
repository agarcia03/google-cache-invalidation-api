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

#include "google/cacheinvalidation/persistence-utils.h"

#include "google/cacheinvalidation/md5.h"

namespace invalidation {

void SerializeState(const TiclState& state, string* out) {
  string serialized;
  string md5_digest;

  state.SerializeToString(&serialized);
  ComputeMd5Digest(serialized, &md5_digest);

  StateBlob state_blob;
  state_blob.mutable_ticl_state()->CopyFrom(state);
  state_blob.set_authentication_code(md5_digest);
  state_blob.SerializeToString(out);
}

bool DeserializeState(const string& serialized, TiclState* state) {
  StateBlob state_blob;

  state_blob.ParseFromString(serialized);
  if (state_blob.IsInitialized()) {
    string state_serialized;
    string md5_digest;
    state_blob.ticl_state().SerializeToString(&state_serialized);
    ComputeMd5Digest(state_serialized, &md5_digest);
    if (md5_digest == state_blob.authentication_code()) {
      state->CopyFrom(state_blob.ticl_state());
      return true;
    }
  }
  return false;
}

}  // namespace invalidation
