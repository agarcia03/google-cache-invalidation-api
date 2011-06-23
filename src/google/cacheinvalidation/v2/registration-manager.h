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

// Object to track desired client registrations. This class belongs to caller
// (e.g., InvalidationClientImpl) and is not thread-safe - the caller has to use
// this class in a thread-safe manner.

#ifndef GOOGLE_CACHEINVALIDATION_V2_REGISTRATION_MANAGER_H_
#define GOOGLE_CACHEINVALIDATION_V2_REGISTRATION_MANAGER_H_

#include "google/cacheinvalidation/v2/system-resources.h"
#include "google/cacheinvalidation/v2/client-protocol-namespace-fix.h"
#include "google/cacheinvalidation/v2/digest-function.h"
#include "google/cacheinvalidation/v2/digest-store.h"
#include "google/cacheinvalidation/v2/statistics.h"

namespace invalidation {

class RegistrationManager {
 public:
  RegistrationManager(Logger* logger, Statistics* statistics,
                      DigestFunction* digest_function);

  /* Sets the digest store to be digest_store for testing purposes.
   *
   * REQUIRES: This method is called before the Ticl has done any operations on
   * this object.
   */
  void SetDigestStoreForTest(DigestStore<ObjectIdP>* digest_store) {
    desired_registrations_.reset(digest_store);
    GetRegistrationSummary(&last_known_server_summary_);
  }

  void GetRegisteredObjectsForTest(vector<ObjectIdP>* registrations) {
    desired_registrations_->GetElements(kEmptyPrefix, 0, registrations);
  }

  /* (Un)registers for object_id. */
  void PerformOperations(const vector<ObjectIdP>& object_ids,
                         RegistrationP::OpType reg_op_type);

  /* Initializes a registration subtree for registrations where the digest of
   * the object id begins with the prefix digest_prefix of prefix_len bits. This
   * method may also return objects whose digest prefix does not match
   * digest_prefix.
   */
  void GetRegistrations(const string& digest_prefix, int prefix_len,
                        RegistrationSubtree* builder);

  /* Handles registration operation statuses from the server.
   *
   * Arguments:
   * registration_statuses - a list of local-processing status codes, one per
   *     element of registration_statuses.
   */
  void HandleRegistrationStatus(
      const proto2::RepeatedPtrField<RegistrationStatus>& registration_statuses,
      vector<bool>* result);

  /* Removes all the registrations in this manager and returns the list. */
  void RemoveRegisteredObjects(vector<ObjectIdP>* result) {
    desired_registrations_->RemoveAll(result);
  }

  //
  // Digest-related methods
  //

  /* Returns a summary of the desired registrations. */
  void GetRegistrationSummary(RegistrationSummary* summary);

  /* Informs the manager of a new registration state summary from the server. */
  void InformServerRegistrationSummary(const RegistrationSummary& reg_summary) {
    last_known_server_summary_.CopyFrom(reg_summary);
  }

  /* Returns whether the local registration state and server state agree, based
   * on the last received server summary (from InformServerRegistrationSummary).
   */
  bool IsStateInSyncWithServer() {
    RegistrationSummary summary;
    GetRegistrationSummary(&summary);
    return (last_known_server_summary_.num_registrations() ==
            summary.num_registrations()) &&
        (last_known_server_summary_.registration_digest() ==
         summary.registration_digest());
  }

  string ToString();

  // Empty hash prefix.
  static const char* kEmptyPrefix;

 private:
  /* The set of regisrations that the application has requested for. */
  scoped_ptr<DigestStore<ObjectIdP> > desired_registrations_;

  /* Statistics objects to track number of sent messages, etc. */
  Statistics* statistics_;

  /* Latest known server registration state summary. */
  RegistrationSummary last_known_server_summary_;

  Logger* logger_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_V2_REGISTRATION_MANAGER_H_
