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

#include "google/cacheinvalidation/version-manager.h"

namespace invalidation {

void VersionManager::AddSupportedProtocolVersion(int32 major_number) {
  supported_major_versions_.insert(major_number);
}

bool VersionManager::ProtocolVersionSupported(
    const ServerToClientMessage& message) {
  int32 major_number = 0;
  if (message.has_protocol_version()) {
    major_number = message.protocol_version().version().major_version();
  }
  return (supported_major_versions_.find(major_number) !=
          supported_major_versions_.end());
}

// Client version history.
// 1.0 first version to include client version field
// 1.1 adds PermanentShutdown()
void VersionManager::GetClientVersion(ClientVersion* client_version) {
  client_version->set_flavor(ClientVersion_Flavor_OPEN_SOURCE_CPP);
  client_version->mutable_version()->set_major_version(1);
  client_version->mutable_version()->set_minor_version(1);
}

void VersionManager::GetLatestProtocolVersion(
    ProtocolVersion* protocol_version) {
  protocol_version->mutable_version()->set_major_version(1);
  protocol_version->mutable_version()->set_minor_version(1);
}

}  // namespace invalidation
