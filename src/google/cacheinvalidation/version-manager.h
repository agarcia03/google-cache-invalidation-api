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

// An abstraction to manage the version of this client and the protocol versions
// it supports.

#ifndef GOOGLE_CACHEINVALIDATION_VERSION_MANAGER_H_
#define GOOGLE_CACHEINVALIDATION_VERSION_MANAGER_H_

#include <set>

#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/stl-namespace.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::set;

class VersionManager {
 public:
  VersionManager() {}

  // Indicates to the manager that it supports the given major protocol version
  // number.
  void AddSupportedProtocolVersion(int32 major_number);

  // Returns whether the protocol version specified by the given message is
  // supported.
  bool ProtocolVersionSupported(const ServerToClientMessage& message);

  // Stores the version of this client implementation in client_version.
  static void GetClientVersion(ClientVersion* client_version);

  // Stores the latest protocol version that this client understands in
  // protocol_version.
  static void GetLatestProtocolVersion(ProtocolVersion* protocol_version);

 private:
  // The set of major versions supported by this client.
  set<int32> supported_major_versions_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_VERSION_MANAGER_H_
