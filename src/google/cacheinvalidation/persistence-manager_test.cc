// Copyright 2010 Google Inc.  All rights reserved.
// Author: akalin@google.com (Fred Akalin)
//
// Tests the PersistenceManager.

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/gmock.h"
#include "google/cacheinvalidation/googletest.h"
#include "google/cacheinvalidation/scoped_ptr.h"
#include "google/cacheinvalidation/persistence-manager.h"
#include "google/cacheinvalidation/system-resources-for-test.h"

namespace invalidation {

class MockStorageCallback {
 public:
  MOCK_METHOD1(StorageCallback, void(bool));
};

class PersistenceManagerTest : public testing::Test {
 protected:
  SystemResourcesForTest resources_;

  MockStorageCallback mock_storage_callback_;

  scoped_ptr<PersistenceManager> persistence_manager_;

  virtual void SetUp() {
    resources_.StartScheduler();
    persistence_manager_.reset(new PersistenceManager(&resources_));
  }

  virtual void TearDown() {
    // Pump task queue before resetting persistence_manager_.
    resources_.RunReadyTasks();
    persistence_manager_.reset();
    resources_.StopScheduler();
  }

  StorageCallback* NewStorageCallback() {
    return NewPermanentCallback(
        &mock_storage_callback_, &MockStorageCallback::StorageCallback);
  }
};

TEST_F(PersistenceManagerTest, WriteState) {
  /* Test plan: Call WriteState() a bunch of times but only call
   * DoPeriodicCheck() a few times.  The storage callback should be
   * called the same number of times as DoPeriodicCheck() was and all
   * the other ones should be dropped.
   */

  const int kNumSuccessCalls = 5;
  EXPECT_CALL(mock_storage_callback_, StorageCallback(true)).
      Times(kNumSuccessCalls);

  for (int i = 0; i < kNumSuccessCalls; ++i) {
    persistence_manager_->WriteState("written-state", NewStorageCallback());
  }

  const int kNumDroppedCalls = 3;

  for (int i = 0; i < kNumDroppedCalls; ++i) {
    persistence_manager_->WriteState("dropped-state", NewStorageCallback());
  }

  for (int i = 0; i < kNumSuccessCalls; ++i) {
    persistence_manager_->DoPeriodicCheck();
    resources_.RunReadyTasks();
  }
}

}  // namespace invalidation
