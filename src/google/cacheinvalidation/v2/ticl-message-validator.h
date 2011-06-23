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

// Validator for v2 protocol messages.
//
// TODO: Fill in the implementation for this class.

#ifndef GOOGLE_CACHEINVALIDATION_V2_TICL_MESSAGE_VALIDATOR_H_
#define GOOGLE_CACHEINVALIDATION_V2_TICL_MESSAGE_VALIDATOR_H_

#include "google/cacheinvalidation/v2/client-protocol-namespace-fix.h"

namespace invalidation {

class TiclMessageValidator {
 public:
  /* Returns whether client_message is valid. */
  bool IsValid(const ClientToServerMessage& client_message) {
    return true;
  }

  /* Returns whether server_message is valid. */
  bool IsValid(const ServerToClientMessage& server_message) {
    return true;
  }

  /* Returns whether invalidation is valid. */
  bool IsValid(const InvalidationP& invalidation) {
    return true;
  }
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_TICL_MESSAGE_VALIDATOR_H_
