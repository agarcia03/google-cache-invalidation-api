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

// Tests the persistence utils.

#include "google/cacheinvalidation/persistence-utils.h"
#include "google/cacheinvalidation/googletest.h"

namespace invalidation {

class PersistenceUtilsTest : public testing::Test {
 public:
  PersistenceUtilsTest() {}

  void SetUp() {
  }

  void TearDown() {
  }

  void CreateState(int64 max_seqno, TiclState* state) {
    state->set_uniquifier("bogus-uniquifier");
    state->set_session_token("bogus-session-token");
    state->set_sequence_number_limit(max_seqno);
  }
};

TEST_F(PersistenceUtilsTest, RoundTrip) {
  TiclState state;
  CreateState(47, &state);

  // Serialize the state.
  string serialized;
  SerializeState(state, &serialized);
  ASSERT_TRUE(!serialized.empty());

  // Deserialize it.
  TiclState roundtrip_state;
  ASSERT_TRUE(DeserializeState(serialized, &roundtrip_state));
  ASSERT_EQ(state.uniquifier(), roundtrip_state.uniquifier());
  ASSERT_EQ(state.session_token(), roundtrip_state.session_token());
  ASSERT_EQ(state.sequence_number_limit(),
            roundtrip_state.sequence_number_limit());
}

TEST_F(PersistenceUtilsTest, InvalidMacDetected) {
  // Make a couple of different state blobs.
  TiclState state1;
  TiclState state2;
  CreateState(47, &state1);
  CreateState(48, &state2);
  string serialized1;
  string serialized2;
  SerializeState(state1, &serialized1);
  SerializeState(state2, &serialized2);
  StateBlob blob1;
  StateBlob blob2;
  blob1.ParseFromString(serialized1);
  blob2.ParseFromString(serialized2);

  // Take the content from blob1 and the digest from blob2.
  StateBlob bad_blob;
  bad_blob.mutable_ticl_state()->CopyFrom(blob2.ticl_state());
  bad_blob.set_authentication_code(blob1.authentication_code());
  string bad_serialized;
  bad_blob.SerializeToString(&bad_serialized);

  // Check that it won't deserialize.
  ASSERT_FALSE(DeserializeState(bad_serialized, NULL));
}

TEST_F(PersistenceUtilsTest, EmptyStringInvalid) {
  string empty_string;
  ASSERT_FALSE(DeserializeState(empty_string, NULL));
}

}  // namespace invalidation
