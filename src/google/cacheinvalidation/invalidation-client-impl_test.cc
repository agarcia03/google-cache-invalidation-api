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

#include <queue>

#include "base/logging.h"
#include "base/scoped_ptr.h"
#include "google/cacheinvalidation/internal.pb.h"
#include "google/cacheinvalidation/invalidation-client-impl.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/system-resources-for-test.h"
#include "gtest/gtest.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::make_pair;
using INVALIDATION_STL_NAMESPACE::pair;
using INVALIDATION_STL_NAMESPACE::vector;

/* A listener for testing. */
class TestListener : public InvalidationListener {
 public:
  TestListener() : invalidate_all_count_(0) {}

  virtual void Invalidate(const Invalidation& invalidation, Closure* callback) {
    CHECK(IsCallbackRepeatable(callback));
    invalidations_.push_back(make_pair(invalidation, callback));
  }

  virtual void InvalidateAll(Closure* callback) {
    CHECK(IsCallbackRepeatable(callback));
    ++invalidate_all_count_;
    callback->Run();
    delete callback;
  }

  virtual void AllRegistrationsLost(Closure* callback) {
    CHECK(IsCallbackRepeatable(callback));
    ASSERT_TRUE(false);  // fail
  }

  virtual void RegistrationLost(const ObjectId& objectId, Closure* callback) {
    CHECK(IsCallbackRepeatable(callback));
    removed_registrations_.push_back(objectId);
    callback->Run();
    delete callback;
  }

  /* The number of InvalidateAll() calls it's received. */
  int invalidate_all_count_;

  /* The individual invalidations received, with their callbacks. */
  vector<pair<Invalidation, Closure*> > invalidations_;

  /* Individual registration removals the Ticl has informed us about. */
  vector<ObjectId> removed_registrations_;
};

class InvalidationClientImplTest : public testing::Test {
 public:
  /* A name for the application. */
  static const char* APP_NAME;

  /* Fake data for a session token. */
  static const char* OPAQUE_DATA;

  /* A status object indicating success. */
  Status success_status_;

  /* An object id. */
  ObjectId object_id1_;

  /* An object id. */
  ObjectId object_id2_;

  /* A sample version. */
  static const int64 VERSION;

  /* System resources for testing. */
  scoped_ptr<SystemResourcesForTest> resources_;

  /* Test listener. */
  scoped_ptr<TestListener> listener_;

  /* The invalidation client being tested. */
  scoped_ptr<InvalidationClient> ticl_;

  /* A field that's set when the Ticl informs us about an outgoing message.
   */
  bool outbound_message_ready_;

  /* Listens for outbound messages from the Ticl. */
  void HandleOutboundMessageReady(NetworkEndpoint* endpoint) {
    outbound_message_ready_ = true;
  }

  scoped_ptr<NetworkCallback> network_listener_;

  /* The client id that we've assigned for the Ticl. */
  ClientUniquifier client_id_;

  /* The session token we've assigned for the Ticl. */
  string session_token_;

  /* A register operation. */
  RegistrationUpdate reg_op1_;

  /* A register operation. */
  RegistrationUpdate reg_op2_;

  /* Registration responses we've received. */
  vector<RegistrationUpdateResult> reg_results_;

  /* A registration callback that writes its result to reg_results_. */
  void HandleRegistrationResult(const RegistrationUpdateResult& result) {
    reg_results_.push_back(result);
  }

  scoped_ptr<RegistrationCallback > callback_;

  /* Checks that client's message contains a proper id-assignment request. */
  void CheckAssignClientIdRequest(
      const ClientToServerMessage& message, ClientExternalId* result) {
    // Check that the message contains an "assign client id" action.
    ASSERT_TRUE(message.has_action());
    ASSERT_EQ(message.action(), ClientToServerMessage_Action_ASSIGN_CLIENT_ID);

    // Check that it does not contain a session token or any registration
    // operations or invalidation acknowledgments.
    ASSERT_FALSE(message.has_session_token());
    ASSERT_EQ(message.acked_invalidation_size(), 0);
    ASSERT_EQ(message.register_operation_size(), 0);

    // Check that it contains the fields of an external id.
    ASSERT_TRUE(message.has_client_type());
    ASSERT_EQ(message.client_type().type(), ClientType_Type_CALENDAR);
    ASSERT_TRUE(message.has_app_client_id());
    ASSERT_EQ(message.app_client_id().string_value(), APP_NAME);

    // Check that the client did not specify values for the server-supplied
    // fields.
    ASSERT_FALSE(message.has_session_token());

    result->mutable_client_type()->CopyFrom(message.client_type());
    result->mutable_app_client_id()->CopyFrom(message.app_client_id());
  }

  void TestInitialization() {
    // Start up the Ticl, connect a network listener, and let it do its
    // initialization.
    ticl_->network_endpoint()->RegisterOutboundListener(
        network_listener_.get());
    resources_->RunReadyTasks();

    // Check that it has a message to send, and pull the message.
    ASSERT_TRUE(outbound_message_ready_);
    outbound_message_ready_ = false;
    string serialized;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    ClientToServerMessage message;
    message.ParseFromString(serialized);

    // Check that the message is a proper request for client id assignment.
    ClientExternalId external_id;
    CheckAssignClientIdRequest(message, &external_id);

    // Construct a full client id.
    client_id_.set_high_bits(4);
    client_id_.set_low_bits(7);

    // Also construct an initial session token.
    session_token_ = OPAQUE_DATA;

    // Construct a response with the client id and session token.
    ServerToClientMessage response;
    string client_id_str;
    client_id_.SerializeToString(&client_id_str);
    response.mutable_client_type()->set_type(external_id.client_type().type());
    response.mutable_app_client_id()->set_string_value(
        external_id.app_client_id().string_value());
    response.set_nonce(message.nonce());
    response.set_client_id(client_id_str);
    response.set_session_token(session_token_);
    response.mutable_status()->set_code(Status_Code_SUCCESS);

    response.SerializeToString(&serialized);

    // Give the message to the Ticl, and let it handle it.
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();

    // Check that it didn't give the app an InvalidateAll.
    ASSERT_EQ(listener_->invalidate_all_count_, 0);

    // Pull another message from the Ticl.
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    message.ParseFromString(serialized);

    // Check that it has the right session token, and that it's polling
    // invalidations.
    ASSERT_TRUE(message.has_session_token());
    ASSERT_EQ(message.session_token(), session_token_);
    ASSERT_TRUE(message.has_action());
    ASSERT_EQ(message.action(),
              ClientToServerMessage_Action_POLL_INVALIDATIONS);
  }

  /* Requests that the Ticl (un)register for two objects.  Checks that the
   * message it sends contains the correct information about these
   * (un)registrations.
   */
  void MakeAndCheckRegistrations(bool is_register) {
    void (InvalidationClient::*operation)(const ObjectId&,
                                          RegistrationCallback*) =
        is_register ?
        &InvalidationClient::Register : &InvalidationClient::Unregister;

    // Explicitness hack here to work around broken callback
    // implementations.
    void (RegistrationCallback::*run_function)(
        const RegistrationUpdateResult&) = &RegistrationCallback::Run;

    // Ask the Ticl to register for two objects.
    (ticl_.get()->*operation)(
        object_id1_,
        NewPermanentCallback(callback_.get(), run_function));
    (ticl_.get()->*operation)(
        object_id2_,
        NewPermanentCallback(callback_.get(), run_function));
    resources_->RunReadyTasks();

    RegistrationUpdate_Type operation_type = is_register ?
        RegistrationUpdate_Type_REGISTER : RegistrationUpdate_Type_UNREGISTER;

    // Pull a message, and check that it has the right session token and
    // registration update messages.
    ClientToServerMessage message;
    string serialized;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    message.ParseFromString(serialized);
    ASSERT_TRUE(message.has_session_token());
    ASSERT_EQ(message.session_token(), session_token_);
    ASSERT_EQ(message.register_operation_size(), 2);
    reg_op1_.Clear();
    reg_op1_.mutable_object_id()->CopyFrom(object_id1_);
    reg_op1_.set_sequence_number(1);
    reg_op1_.set_type(operation_type);
    reg_op2_.mutable_object_id()->CopyFrom(object_id2_);
    reg_op2_.set_sequence_number(2);
    reg_op2_.set_type(operation_type);

    string serialized2;
    reg_op1_.SerializeToString(&serialized);
    message.register_operation(0).SerializeToString(&serialized2);
    ASSERT_EQ(serialized, serialized2);
    reg_op2_.SerializeToString(&serialized);
    message.register_operation(1).SerializeToString(&serialized2);
    ASSERT_EQ(serialized, serialized2);

    // Check that the Ticl has not responded to the app about either of the
    // operations yet.
    ASSERT_TRUE(reg_results_.empty());
  }

  void TestRegistration(bool is_register) {
    // Do setup and initiate registrations.
    TestInitialization();
    outbound_message_ready_ = false;
    MakeAndCheckRegistrations(is_register);

    // Construct responses and let the Ticl process them.
    ServerToClientMessage response;
    RegistrationUpdateResult* result1 = response.add_registration_result();
    result1->mutable_operation()->CopyFrom(reg_op1_);
    result1->mutable_status()->set_code(Status_Code_SUCCESS);
    RegistrationUpdateResult* result2 = response.add_registration_result();
    result2->mutable_operation()->CopyFrom(reg_op2_);
    result2->mutable_status()->set_code(Status_Code_SUCCESS);
    response.mutable_status()->set_code(Status_Code_SUCCESS);
    response.set_session_token(session_token_);
    string serialized;
    response.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();

    // Check that the registration callback was invoked.
    ASSERT_EQ(reg_results_.size(), 2);
    string serialized2;
    reg_results_[0].SerializeToString(&serialized);
    result1->SerializeToString(&serialized2);
    ASSERT_EQ(serialized, serialized2);
    reg_results_[1].SerializeToString(&serialized);
    result2->SerializeToString(&serialized2);
    ASSERT_EQ(serialized, serialized2);

    // Advance the clock a lot, run everything, and make sure it's not trying to
    // resend.
    resources_->ModifyTime(TimeDelta::FromMilliseconds(
        ClientConfig::DEFAULT_REGISTRATION_TIMEOUT_MS));
    resources_->RunReadyTasks();
    ClientToServerMessage message;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    message.ParseFromString(serialized);
    ASSERT_EQ(message.register_operation_size(), 0);
  }

  void TestSessionSwitch() {
    TestRegistration(true);

    // Clear the "outbound message ready" flag, so we can check below that the
    // invalid session status causes it to be set.
    outbound_message_ready_ = false;

    // Tell the Ticl its session is invalid.
    ServerToClientMessage message;
    message.set_session_token(session_token_);
    message.mutable_status()->set_code(Status_Code_INVALID_SESSION);
    string serialized;
    message.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();

    // Check that the Ticl has pinged the client to indicate it has a request.
    ASSERT_TRUE(outbound_message_ready_);

    // Pull a message from the Ticl and check that it requests a new session.
    ClientToServerMessage request;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    request.ParseFromString(serialized);
    ASSERT_TRUE(request.has_action());
    ASSERT_EQ(request.action(), ClientToServerMessage_Action_UPDATE_SESSION);
    ASSERT_TRUE(request.has_client_id());
    ClientUniquifier uniq;
    uniq.ParseFromString(request.client_id());
    ASSERT_EQ(uniq.low_bits(), client_id_.low_bits());
    ASSERT_EQ(uniq.high_bits(), client_id_.high_bits());

    // Give it a new session token and tell it one of its registrations was
    // lost.
    session_token_ = "NEW_OPAQUE_DATA";
    message.Clear();
    client_id_.SerializeToString(&serialized);
    message.set_client_id(serialized);
    message.set_session_token(session_token_);
    message.mutable_status()->set_code(Status_Code_SUCCESS);
    message.set_last_operation_sequence_number(1);
    message.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();

    // Pull a message and check that the Ticl is repeating the registration.
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    request.ParseFromString(serialized);
    ASSERT_TRUE(request.has_session_token());
    ASSERT_EQ(request.session_token(), session_token_);
    ASSERT_EQ(request.register_operation_size(), 1);
    string serialized2;
    request.register_operation(0).SerializeToString(&serialized);
    reg_op2_.SerializeToString(&serialized2);
    ASSERT_EQ(serialized, serialized2);
  }

  virtual void SetUp() {
    object_id1_.Clear();
    object_id1_.set_source(ObjectId_Source_CALENDAR);
    object_id1_.mutable_name()->set_string_value("someone@example.com");
    object_id2_.Clear();
    object_id2_.set_source(ObjectId_Source_CONTACTS);
    object_id2_.mutable_name()->set_string_value("someone@example.com");
    resources_.reset(new SystemResourcesForTest());
    resources_->ModifyTime(TimeDelta::FromSeconds(1000000));
    resources_->StartScheduler();
    listener_.reset(new TestListener());
    network_listener_.reset(
        NewPermanentCallback(
            this, &InvalidationClientImplTest::HandleOutboundMessageReady));
    callback_.reset(
        NewPermanentCallback(
            this, &InvalidationClientImplTest::HandleRegistrationResult));
    ClientConfig ticl_config;
    ClientType client_type;
    client_type.set_type(ClientType_Type_CALENDAR);
    ticl_.reset(new InvalidationClientImpl(
        resources_.get(), client_type, APP_NAME, listener_.get(), ticl_config));
    reg_results_.clear();
  }

  virtual void TearDown() {
    resources_->StopScheduler();
  }
};

const char* InvalidationClientImplTest::APP_NAME = "app_name";
const char* InvalidationClientImplTest::OPAQUE_DATA = "opaque_data";
const int64 InvalidationClientImplTest::VERSION = 5;

TEST_F(InvalidationClientImplTest, InitializationTest) {
  /* Test plan: start up a new Ticl.  Check that it requests to send a message
   * and that the message requests client id assignment with an appropriately
   * formed partial client id.  Respond with a full client id and session token.
   * Check that the Ticl's next step is to poll invalidations.
   */
  TestInitialization();
}

TEST_F(InvalidationClientImplTest, MismatchingClientIdIgnored) {
  /* Test plan: create a Ticl and pull a bundle from it, which will be
   * requesting a client id.  Respond with a client id, but for a mismatched app
   * client id.  Check that pulling a subsequent bundle results in another
   * assign-client-id action.
   */

  // Start up the Ticl, connect a network listener, and let it do its
  // initialization.
  ticl_->network_endpoint()->RegisterOutboundListener(network_listener_.get());
  resources_->RunReadyTasks();

  // Pull a message.
  ClientToServerMessage message;
  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the message is a proper request for client id assignment.
  ClientExternalId external_id;
  CheckAssignClientIdRequest(message, &external_id);

  // Construct a full client id.
  client_id_.set_high_bits(4);
  client_id_.set_low_bits(7);
  string client_id_str;
  client_id_.SerializeToString(&client_id_str);

  // Also construct an initial session token.
  session_token_ = OPAQUE_DATA;

  // Construct a response with a client id and session token but the wrong app
  // client id.
  ServerToClientMessage response;
  response.mutable_client_type()->CopyFrom(external_id.client_type());
  response.mutable_app_client_id()->set_string_value("wrong-app-client-id");
  response.set_client_id(client_id_str);
  response.set_session_token(session_token_);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);

  // Give the message to the Ticl, and let it handle it.
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message.
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the is still looking for a client id.
  CheckAssignClientIdRequest(message, &external_id);
}

TEST_F(InvalidationClientImplTest, PollingIntervalRespectedTest) {
  /* Test plan: get a client id and session, and consume the initial
   * poll-invalidations request.  Send a message reducing the polling interval
   * to 10s.  Check that we won't send a poll-invalidations until 10s in the
   * future.  Now increase the polling interval to 100s, and again check that we
   * won't send a poll-invalidations until 100s in the future.
   */

  // Handle setup.
  TestInitialization();

  // Respond to the client's poll with a new polling interval.
  ServerToClientMessage response;
  string serialized;
  response.set_session_token(session_token_);
  response.set_next_poll_interval_ms(10000);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Advance to 1 ms before the polling interval, and check that the Ticl does
  // not try to poll again.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(9999));
  ClientToServerMessage message;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  ASSERT_FALSE(message.has_action());

  // Advance the last ms and check that the Ticl does try to poll.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(1));
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.action(), ClientToServerMessage_Action_POLL_INVALIDATIONS);

  // Respond and increase the polling interval.
  response.Clear();
  response.set_session_token(session_token_);
  response.set_next_poll_interval_ms(100000);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Advance the time to just before the polling interval expires, and check
  // that no poll request is sent.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(99999));
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_FALSE(message.has_action());

  // Advance so that the polling interval is fully elapsed, and check that the
  // Ticl does poll.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(1));
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.action(), ClientToServerMessage_Action_POLL_INVALIDATIONS);
}

TEST_F(InvalidationClientImplTest, HeartbeatIntervalRespectedTest) {
  /* Test plan: get a client id and session, and consume the initial
   * poll-invalidations message.  Respond and increase heartbeat interval to
   * 80s.  Check that the outbound message listener doesn't get pinged until 80s
   * in the future.  Then send a message reducing the heartbeat interval to 10s.
   * Because of the way the heartbeat timer is implemented, we don't expect the
   * very next heartbeat to occur until 80s in the future, but subsequently it
   * should be 10s.
   */

  // Do setup.
  TestInitialization();

  // Respond with a new heartbeat interval (larger than the default).
  ServerToClientMessage response;
  response.set_session_token(session_token_);
  response.set_next_heartbeat_interval_ms(80000);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  string serialized;
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // Advance to just shy of the heartbeat interval, and check that the Ticl did
  // not nudge the application to send.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(79999));
  resources_->RunReadyTasks();
  ASSERT_FALSE(outbound_message_ready_);

  // Advance further, and check that it did nudge the application to send.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(1));
  resources_->RunReadyTasks();
  ASSERT_TRUE(outbound_message_ready_);

  // Shorten the heartbeat interval and repeat.
  response.Clear();
  response.set_session_token(session_token_);
  response.set_next_heartbeat_interval_ms(10000);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // Because the Ticl uses a single timer-task, the next heartbeat will still
  // happen after the longer interval.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(80000));
  resources_->RunReadyTasks();
  ASSERT_TRUE(outbound_message_ready_);
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // But subsequently, heartbeats should happen with the shorter interval.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(9999));
  resources_->RunReadyTasks();
  ASSERT_FALSE(outbound_message_ready_);
  resources_->ModifyTime(TimeDelta::FromMilliseconds(1));
  resources_->RunReadyTasks();
  ASSERT_TRUE(outbound_message_ready_);
}

TEST_F(InvalidationClientImplTest, InitializationInTwoSteps) {
  /* Test plan: start up a new Ticl.  Check that it sends a request for client
   * id assignment.  Respond with a client id but not a session token.  Check
   * that the Ticl sends another request asking for a session token.  Respond
   * with one.  Check that it then polls invalidations.
   */

  // Start up the Ticl, connect a network listener, and let it do its
  // initialization.
  ticl_->network_endpoint()->RegisterOutboundListener(network_listener_.get());
  resources_->RunReadyTasks();

  // Check that it has a message to send, and pull the message.
  ASSERT_TRUE(outbound_message_ready_);
  outbound_message_ready_ = false;
  ClientToServerMessage message;
  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the message is a proper request for client id assignment.
  ClientExternalId external_id;
  CheckAssignClientIdRequest(message, &external_id);

  // Construct a client id, but not a session token, and respond with it.
  client_id_.set_high_bits(4);
  client_id_.set_low_bits(7);
  ServerToClientMessage response;
  string client_id_str;
  client_id_.SerializeToString(&client_id_str);
  response.mutable_client_type()->set_type(external_id.client_type().type());
  response.mutable_app_client_id()->set_string_value(
      external_id.app_client_id().string_value());
  response.set_nonce(message.nonce());
  response.set_client_id(client_id_str);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that the Ticl has a new message to send, and that it's asking for a
  // session.
  ASSERT_TRUE(outbound_message_ready_);
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_TRUE(message.has_action());
  ASSERT_EQ(message.action(), ClientToServerMessage_Action_UPDATE_SESSION);
  ASSERT_TRUE(message.has_client_id());
  ClientUniquifier actual_client_id;
  actual_client_id.ParseFromString(message.client_id());
  ASSERT_EQ(actual_client_id.low_bits(), client_id_.low_bits());
  ASSERT_EQ(actual_client_id.high_bits(), client_id_.high_bits());
  ASSERT_FALSE(message.has_session_token());

  // Construct a session token and respond with it.
  session_token_ = OPAQUE_DATA;
  response.Clear();
  response.set_session_token(session_token_);
  response.set_client_id(client_id_str);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that the Ticl now polls for invalidations, using the correct session
  // token.
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_TRUE(message.has_session_token());
  ASSERT_EQ(message.session_token(), session_token_);
  ASSERT_EQ(message.action(), ClientToServerMessage_Action_POLL_INVALIDATIONS);
}

TEST_F(InvalidationClientImplTest, MismatchingSessionUpdateIgnored) {
  /* Test plan: start up a new Ticl.  Check that it sends a request for client
   * id assignment.  Respond with a client id but not a session token.  Check
   * that the Ticl sends another request asking for a session token.  Respond
   * with one, but with the wrong client id.  Check that it requests a session
   * again.
   */

  // Start up the Ticl, connect a network listener, and let it do its
  // initialization.
  ticl_->network_endpoint()->RegisterOutboundListener(network_listener_.get());
  resources_->RunReadyTasks();

  // Check that it has a message to send, and pull the message.
  ASSERT_TRUE(outbound_message_ready_);
  outbound_message_ready_ = false;
  ClientToServerMessage message;
  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the message is a proper request for client id assignment.
  ClientExternalId external_id;
  CheckAssignClientIdRequest(message, &external_id);

  // Construct a client id, but not a session token, and respond with it.
  client_id_.set_high_bits(4);
  client_id_.set_low_bits(7);
  string client_id_str;
  client_id_.SerializeToString(&client_id_str);

  ServerToClientMessage response;
  response.mutable_client_type()->CopyFrom(external_id.client_type());
  response.mutable_app_client_id()->CopyFrom(external_id.app_client_id());
  response.set_nonce(message.nonce());
  response.set_client_id(client_id_str);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that the Ticl has a new message to send, and that it's asking for a
  // session.
  ASSERT_TRUE(outbound_message_ready_);
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_TRUE(message.has_action());
  ASSERT_EQ(message.action(), ClientToServerMessage_Action_UPDATE_SESSION);
  ASSERT_TRUE(message.has_client_id());

  ASSERT_EQ(client_id_str, message.client_id());
  ASSERT_FALSE(message.has_session_token());

  // Construct a session token and respond with it.
  session_token_ = OPAQUE_DATA;
  ClientUniquifier bad_client_id;
  bad_client_id.set_high_bits(3);
  bad_client_id.set_low_bits(8);
  string bad_client_id_str;
  bad_client_id.SerializeToString(&bad_client_id_str);

  response.Clear();
  response.set_session_token(session_token_);
  response.set_client_id(bad_client_id_str);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that the Ticl now polls for invalidations, using the correct session
  // token.
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_FALSE(message.has_session_token());
  ASSERT_EQ(message.action(), ClientToServerMessage_Action_UPDATE_SESSION);
}

TEST_F(InvalidationClientImplTest, Registration) {
  /* Test plan: get a client id and session.  Register for an object.  Check
   * that the Ticl sends an appropriate registration request.  Respond with a
   * successful status.  Check that the registration callback is invoked with an
   * appropriate result, and that the Ticl does not resend the request.
   */
  TestRegistration(true);
}

TEST_F(InvalidationClientImplTest, Unegistration) {
  /* Test plan: get a client id and session.  Unregister for an object.  Check
   * that the Ticl sends an appropriate unregistration request.  Respond with a
   * successful status.  Check that the unregistration callback is invoked with
   * an appropriate result, and that the Ticl does not resend the request.
   */
  TestRegistration(false);
}

TEST_F(InvalidationClientImplTest, OrphanedRegistration) {
  /* Test plan: get a client id and session.  Register for an object.  Check
   * that the Ticl sends an appropriate registration request.  Don't respond;
   * just check that the callbacks aren't leaked.
   */
  TestInitialization();
  outbound_message_ready_ = false;
  MakeAndCheckRegistrations(true);
}

TEST_F(InvalidationClientImplTest, RegistrationRetried) {
  /* Test plan: get a client id and session.  Register for an object.  Check
   * that the Ticl sends a registration request.  Advance the clock without
   * responding to the request.  Check that the Ticl resends the request.
   * Repeat the last step to ensure that retrying happens more than once.
   * Finally, respond and check that the callback was invoked with an
   * appropriate result.
   */
  TestInitialization();
  outbound_message_ready_ = false;
  MakeAndCheckRegistrations(true);

  // Advance the clock without responding and make sure the Ticl resends the
  // request.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(
      ClientConfig::DEFAULT_REGISTRATION_TIMEOUT_MS));
  resources_->RunReadyTasks();
  ClientToServerMessage message;
  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.register_operation_size(), 2);

  string serialized2;
  reg_op1_.SerializeToString(&serialized);
  message.register_operation(0).SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);
  reg_op2_.SerializeToString(&serialized);
  message.register_operation(1).SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Ack one of the registrations.
  ServerToClientMessage response;
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_session_token(session_token_);
  RegistrationUpdateResult* result = response.add_registration_result();
  result->mutable_operation()->CopyFrom(reg_op2_);
  result->mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);

  // Deliver the ack and check that the registration callback is invoked.
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ASSERT_EQ(reg_results_.size(), 1);

  result->SerializeToString(&serialized);
  reg_results_[0].SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Advance the clock again, and check that (only) the unacked operation is
  // retried again.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(
      ClientConfig::DEFAULT_REGISTRATION_TIMEOUT_MS));
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  // regOps = message.getRegisterOperationList();
  ASSERT_EQ(message.register_operation_size(), 1);
  message.register_operation(0).SerializeToString(&serialized);
  reg_op1_.SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Now ack the other registration.
  response.Clear();
  result = response.add_registration_result();
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_session_token(session_token_);
  result->mutable_operation()->CopyFrom(reg_op1_);
  result->mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);

  // Check that the reg. callback was invoked for the second ack.
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ASSERT_EQ(reg_results_.size(), 2);

  result->SerializeToString(&serialized);
  reg_results_[1].SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);
}

TEST_F(InvalidationClientImplTest, RegistrationFailure) {
  /* Test plan: get a client id and session.  Register for an object.  Check
   * that the Ticl sends an appropriate registration request.  Respond with an
   * error status.  Check that the registration callback is invoked with an
   * appropriate result, and that the Ticl does not resend the request.
   */

  // Do setup and initiate registrations.
  TestInitialization();
  outbound_message_ready_ = false;
  MakeAndCheckRegistrations(true);

  // Construct and deliver responses: one failure and one success.
  ServerToClientMessage response;
  RegistrationUpdateResult* result1 = response.add_registration_result();
  result1->mutable_operation()->CopyFrom(reg_op1_);
  result1->mutable_status()->set_code(Status_Code_OBJECT_UNKNOWN);
  result1->mutable_status()->set_description("Registration update failed");
  RegistrationUpdateResult* result2 = response.add_registration_result();
  result2->mutable_operation()->CopyFrom(reg_op2_);
  result2->mutable_status()->set_code(Status_Code_SUCCESS);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_session_token(session_token_);
  string serialized;
  response.SerializeToString(&serialized);

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that the registration callback was invoked.
  ASSERT_EQ(reg_results_.size(), 2);
  string serialized2;
  reg_results_[0].SerializeToString(&serialized);
  result1->SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  reg_results_[1].SerializeToString(&serialized);
  result2->SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Advance the clock a lot, run everything, and make sure it's not trying to
  // resend.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(
      ClientConfig::DEFAULT_REGISTRATION_TIMEOUT_MS));
  resources_->RunReadyTasks();
  ClientToServerMessage message;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.register_operation_size(), 0);
}

TEST_F(InvalidationClientImplTest, Invalidation) {
  /* Test plan: get a client id and session token, and register for an object.
   * Deliver an invalidation for that object.  Check that the listener's
   * invalidate() method gets called with the right invalidation.  Check that
   * the Ticl acks the invalidation, but only after the listener has acked it.
   */
  TestRegistration(true);

  // Deliver and invalidation for an object.
  ServerToClientMessage message;
  Invalidation* invalidation = message.add_invalidation();
  invalidation->mutable_object_id()->CopyFrom(object_id1_);
  invalidation->set_version(InvalidationClientImplTest::VERSION);
  message.set_session_token(session_token_);
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that the app (listener) was informed of the invalidation.
  ASSERT_EQ(listener_->invalidations_.size(), 1);
  pair<Invalidation, Closure*> tmp = listener_->invalidations_[0];
  string serialized2;
  tmp.first.SerializeToString(&serialized);
  invalidation->SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Check that the Ticl isn't acking the invalidation yet, since we haven't
  // called the callback.
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  ClientToServerMessage client_message;
  client_message.ParseFromString(serialized);
  ASSERT_EQ(client_message.acked_invalidation_size(), 0);
  outbound_message_ready_ = false;

  // Now run the callback, and check that the Ticl does ack the invalidation.
  tmp.second->Run();
  delete tmp.second;
  resources_->RunReadyTasks();
  ASSERT_TRUE(outbound_message_ready_);
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  client_message.ParseFromString(serialized);
  ASSERT_EQ(client_message.acked_invalidation_size(), 1);
  client_message.acked_invalidation(0).SerializeToString(&serialized);
  ASSERT_EQ(serialized, serialized2);
}

TEST_F(InvalidationClientImplTest, SessionSwitch) {
  /* Test plan: get client id and session.  Register for a couple of objects.
   * Send the Ticl an invalid-session message.  Check that the Ticl sends an
   * UpdateSession request, and respond with a new session token and last
   * sequence number of 1.  Check that the Ticl resends a registration request
   * for the second register operation.
   */
  TestSessionSwitch();
}

TEST_F(InvalidationClientImplTest, MismatchingInvalidSessionIgnored) {
  /* Test plan: get client id and session.  Register for a couple of objects.
   * Send the Ticl an invalid-session message with a mismatched session token.
   * Check that the Ticl ignores it.
   */
  TestRegistration(true);

  // Tell the Ticl its session is invalid.
  string bogus_session_token = "bogus-session-token";
  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_INVALID_SESSION);
  message.set_session_token(bogus_session_token);
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message from the Ticl and check that it doesn't request a new
  // session.
  ClientToServerMessage request;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  request.ParseFromString(serialized);
  ASSERT_FALSE(request.has_action());
}

TEST_F(InvalidationClientImplTest, SessionSwitchRegSucceeds) {
  /* Test plan: start with the session-switch test.  Respond with success to the
   * repeated registration request.  Check that the listener is not informed of
   * any removed registration.
   */
  TestSessionSwitch();

  // Tell the Ticl that its re-registration succeeded.
  ServerToClientMessage message;
  RegistrationUpdateResult* result = message.add_registration_result();
  result->mutable_operation()->CopyFrom(reg_op2_);
  result->mutable_status()->set_code(Status_Code_SUCCESS);
  message.set_session_token(session_token_);
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that it did not issue an InvalidateAll or tell the listener that the
  // registration was lost.
  ASSERT_TRUE(listener_->removed_registrations_.empty());
  ASSERT_EQ(listener_->invalidate_all_count_, 0);
}

TEST_F(InvalidationClientImplTest, SessionSwitchRegFails) {
  /* Test plan: start with the session-switch test.  Respond with a failure to
   * the repeated registration request.  Check that the listener is informed of
   * the removed registration.
   */
  TestSessionSwitch();

  // Tell the Ticl that the re-registration failed.
  ServerToClientMessage message;
  RegistrationUpdateResult* result = message.add_registration_result();
  result->mutable_operation()->CopyFrom(reg_op2_);
  result->mutable_status()->set_code(Status_Code_OBJECT_DELETED);
  result->mutable_status()->set_description("Registration update");
  message.set_session_token(session_token_);
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that it informed the listener about the lost registration.
  ASSERT_EQ(listener_->removed_registrations_.size(), 1);
  string serialized2;
  listener_->removed_registrations_[0].SerializeToString(&serialized);
  object_id2_.SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);
  ASSERT_EQ(listener_->invalidate_all_count_, 0);
}

TEST_F(InvalidationClientImplTest, GarbageCollection) {
  /* Test plan: get a client id and session, and perform some registrations.
   * Send the Ticl a message indicating it has been garbage-collected.  Check
   * that the Ticl requests a new client id.  Respond with one, along with a
   * session.  Check that it repeats the register operations, and that it sends
   * an invalidateAll once the registrations have completed.
   */
  TestRegistration(true);

  // Tell the Ticl we don't recognize it.
  ServerToClientMessage message;
  message.Clear();
  message.mutable_status()->set_code(Status_Code_UNKNOWN_CLIENT);
  message.set_session_token(session_token_);
  string serialized;
  client_id_.SerializeToString(&serialized);
  message.set_client_id(serialized);
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message from it, and check that it's trying to assign a client id.
  ClientToServerMessage request;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  request.ParseFromString(serialized);
  ASSERT_TRUE(request.has_action());
  ASSERT_EQ(request.action(), ClientToServerMessage_Action_ASSIGN_CLIENT_ID);
  ClientExternalId external_id;
  CheckAssignClientIdRequest(request, &external_id);

  // Give it a new client id and session.
  client_id_.set_low_bits(4);
  client_id_.set_high_bits(7);
  client_id_.SerializeToString(&serialized);

  session_token_ = "new opaque data";
  ServerToClientMessage response;
  response.set_session_token(session_token_);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.mutable_client_type()->set_type(external_id.client_type().type());
  response.mutable_app_client_id()->set_string_value(
      external_id.app_client_id().string_value());
  response.set_nonce(request.nonce());
  response.set_client_id(serialized);
  response.SerializeToString(&serialized);

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that it is repeating all registrations and has not yet issued an
  // invalidateAll.
  ASSERT_EQ(listener_->invalidate_all_count_, 0);

  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  request.ParseFromString(serialized);
  ASSERT_EQ(request.register_operation_size(), 2);

  string serialized2;
  request.register_operation(0).SerializeToString(&serialized);
  reg_op1_.SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);
  request.register_operation(1).SerializeToString(&serialized);
  reg_op2_.SerializeToString(&serialized2);
  ASSERT_EQ(serialized, serialized2);

  // Give successful acks to the registrations.
  message.Clear();
  RegistrationUpdateResult* result1 = message.add_registration_result();
  result1->mutable_operation()->CopyFrom(reg_op1_);
  result1->mutable_status()->set_code(Status_Code_SUCCESS);
  RegistrationUpdateResult* result2 = message.add_registration_result();
  result2->mutable_operation()->CopyFrom(reg_op2_);
  result2->mutable_status()->set_code(Status_Code_SUCCESS);

  message.set_session_token(session_token_);
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Check that no InvalidateAll is issued once the registrations are completed.
  ASSERT_EQ(listener_->invalidate_all_count_, 0);
}

TEST_F(InvalidationClientImplTest, MismatchedUnknownClientIgnored) {
  /* Test plan: get a client id and session, and perform some registrations.
   * Send the Ticl a message indicating it has been garbage-collected, with a
   * mismatched client id.  Check that the Ticl ignores it.
   */
  TestRegistration(true);

  // Tell the Ticl we don't recognize it, but supply an incorrect client id.
  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_UNKNOWN_CLIENT);
  message.set_session_token(session_token_);
  message.set_client_id("bogus-client-id");
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message from it, and check that it's not trying to assign a client
  // id.
  ClientToServerMessage request;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  request.ParseFromString(serialized);
  ASSERT_FALSE(request.has_action());
}

}  // namespace invalidation
