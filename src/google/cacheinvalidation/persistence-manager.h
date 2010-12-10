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

#ifndef GOOGLE_CACHEINVALIDATION_PERSISTENCE_MANAGER_H_
#define GOOGLE_CACHEINVALIDATION_PERSISTENCE_MANAGER_H_

#include <queue>
#include <string>

#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/stl-namespace.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::queue;

// Contains the data and callback associated with a pending write.
struct PendingRecord {
  // The data to be written.
  string payload;
  // The callback to be invoked when the write completes.
  StorageCallback* callback;

  // Constructs a new pending record with the given payload and callback.
  PendingRecord(const string& p, StorageCallback* cb)
      : payload(p), callback(cb) {}
};

// Enforces sequential access to the persistent storage system.
class PersistenceManager {
 public:
  // Creates a persistence manager that wraps the given resources.
  explicit PersistenceManager(SystemResources* resources)
      : write_in_progress_(false),
        resources_(resources) {}

  // Frees callbacks from queued writes.
  ~PersistenceManager();

  // Enqueues a write.  Takes ownership of callback.  callback is not
  // guaranteed to be called (e.g., if the PersistenceManager is
  // destroyed before the write completes).
  void WriteState(const string& state, StorageCallback* callback) {
    CHECK(IsCallbackRepeatable(callback));
    pending_writes_.push(PendingRecord(state, callback));
  }

  // Issues a write to the persistent store if there isn't already one in
  // progress.
  void DoPeriodicCheck();

 private:
  // Runs the callback with the given result, deletes it, and records that there
  // is no longer a write in progress.
  void HandleWriteCompletion(StorageCallback* callback, bool result) {
    write_in_progress_ = false;
    callback->Run(result);
    delete callback;
  }

  // Writes that have been queued and not yet processed.
  queue<PendingRecord> pending_writes_;
  // Whether we have a write in progress or not.
  bool write_in_progress_;
  // System resources that perform the actual writes.
  SystemResources* resources_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_PERSISTENCE_MANAGER_H_
