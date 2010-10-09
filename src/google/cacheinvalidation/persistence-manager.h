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

#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/stl-namespace.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::queue;

struct PendingRecord {
  string payload;
  StorageCallback* callback;

  PendingRecord(const string& p, StorageCallback* cb)
      : payload(p), callback(cb) {}
};

class PersistenceManager {
 public:
  PersistenceManager(SystemResources* resources)
      : write_in_progress_(false),
        resources_(resources) {}

  ~PersistenceManager();

  void WriteState(const string& state, StorageCallback* callback) {
    CHECK(IsCallbackRepeatable(callback));
    pending_writes_.push(PendingRecord(state, callback));
  }

  void DoPeriodicCheck();

 private:
  void HandleWriteCompletion(StorageCallback* callback, bool result) {
    write_in_progress_ = false;
    callback->Run(result);
    delete callback;
  }

  queue<PendingRecord> pending_writes_;
  bool write_in_progress_;
  SystemResources* resources_;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_PERSISTENCE_MANAGER_H_
