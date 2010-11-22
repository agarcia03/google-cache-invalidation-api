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

// Tests the cache invalidation client's registration manager.

#include "google/cacheinvalidation/googletest.h"
#include "google/cacheinvalidation/registration-update-manager.h"
#include "google/cacheinvalidation/system-resources-for-test.h"

namespace invalidation {

class RegistrationUpdateManagerTest : public testing::Test {
 public:
  RegistrationUpdateManagerTest() {}

  void SetUp() {
    ClientConfig config;
    resources_.reset(new SystemResourcesForTest());
    resources_->StartScheduler();
    registration_manager_.reset(
        new RegistrationUpdateManager(resources_.get(), config));
  }

  void TearDown() {
    registration_manager_.reset();
    resources_.reset();
  }

  scoped_ptr<SystemResourcesForTest> resources_;
  scoped_ptr<RegistrationUpdateManager> registration_manager_;
  Status_Code last_status_;
};

void GenericRegCallback(RegistrationUpdateResult* box,
                        SystemResources* resources,
                        const RegistrationUpdateResult& result) {
  CHECK(!resources->IsRunningOnInternalThread());
  *box = result;
}

/* Test plan: start in the NO_INFO state for an object. Register for an object
 * and verify that it transitions to the REG_PENDING state. Register again for
 * the same object and verify that the first callback is invoked with
 * STALE_OPERATION. Provide a success response and verify that the second
 * callback is invoked with SUCCESS.
 */
TEST_F(RegistrationUpdateManagerTest, NoInfoToRegPending) {
  ObjectIdP object_id;
  object_id.mutable_name()->set_string_value("testObject");
  object_id.set_source(ObjectIdP_Source_CHROME_SYNC);

  RegistrationUpdateResult result;
  registration_manager_->Register(
      object_id, NewPermanentCallback(&GenericRegCallback, &result,
                                      resources_.get()));
  resources_->RunReadyTasks();

  // Verify in state pending.
  ASSERT_EQ(RegistrationState_REG_PENDING,
            registration_manager_->GetObjectState(object_id));
  ASSERT_TRUE(!result.has_status());  // Callback not yet invoked.

  // Register again and check first callback invoked.
  RegistrationUpdateResult second_result;
  registration_manager_->Register(
      object_id, NewPermanentCallback(&GenericRegCallback, &second_result,
                                      resources_.get()));
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_EQ(Status_Code_STALE_OPERATION, result.status().code());
  ASSERT_EQ(RegistrationState_REG_PENDING,
            registration_manager_->GetObjectState(object_id));

  // Check message to be send;
  ClientToServerMessage c2s_message;
  registration_manager_->AddOutboundRegistrationUpdates(&c2s_message);
  ASSERT_EQ(1, c2s_message.register_operation_size());
  const RegistrationUpdate& reg_op = c2s_message.register_operation(0);
  RegistrationUpdateResult reg_op_response;
  reg_op_response.mutable_operation()->CopyFrom(reg_op);
  reg_op_response.mutable_status()->set_code(Status_Code_SUCCESS);

  // Supply the response to the registration manager and verify that the second
  // callback is invoked.
  registration_manager_->ProcessRegistrationUpdateResult(reg_op_response);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_TRUE(second_result.has_status());
  ASSERT_EQ(Status_Code_SUCCESS, second_result.status().code());

  // Check that the object is now in the REG_CONFIRMED state.
  ASSERT_EQ(RegistrationState_REG_CONFIRMED,
            registration_manager_->GetObjectState(object_id));

  // Register one more time and verify that we immedaitely get REG_CONFIRMED and
  // no additional messages are sent.
  RegistrationUpdateResult third_result;
  registration_manager_->Register(
      object_id, NewPermanentCallback(&GenericRegCallback, &third_result,
                                      resources_.get()));
  resources_->RunReadyTasks();
  ASSERT_EQ(Status_Code_SUCCESS, third_result.status().code());
  ASSERT_EQ(RegistrationState_REG_CONFIRMED,
            registration_manager_->GetObjectState(object_id));

  // Check no message to be sent.
  c2s_message.Clear();
  registration_manager_->AddOutboundRegistrationUpdates(&c2s_message);
  ASSERT_EQ(0, c2s_message.register_operation_size());

  // Finally, do an unregister and verify a transition to UNREG_PENDING.
  RegistrationUpdateResult dummy_result;
  registration_manager_->Unregister(
      object_id, NewPermanentCallback(&GenericRegCallback, &dummy_result,
                                      resources_.get()));
  ASSERT_EQ(RegistrationState_UNREG_PENDING,
            registration_manager_->GetObjectState(object_id));
  resources_->StopScheduler();
}

}  // namespace invalidation
