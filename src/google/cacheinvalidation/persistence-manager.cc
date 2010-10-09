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

#include "google/cacheinvalidation/persistence-manager.h"

#include "google/cacheinvalidation/log-macro.h"

namespace invalidation {

PersistenceManager::~PersistenceManager() {
  while (!pending_writes_.empty()) {
    PendingRecord pending_record = pending_writes_.front();
    pending_writes_.pop();
    pending_record.callback->Run(false);
    delete pending_record.callback;
  }
}

void PersistenceManager::DoPeriodicCheck() {
  if (!pending_writes_.empty() && !write_in_progress_) {
    PendingRecord pending_record = pending_writes_.front();
    TLOG(INFO_LEVEL, "Issuing write");
    pending_writes_.pop();
    write_in_progress_ = true;
    resources_->WriteState(
        pending_record.payload,
        NewPermanentCallback(
            this,
            &PersistenceManager::HandleWriteCompletion,
            pending_record.callback));
  }
}

}  // namespace invalidation
