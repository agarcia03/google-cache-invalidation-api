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

#include "google/cacheinvalidation/gmock.h"
#include "google/cacheinvalidation/googletest.h"
#include "google/cacheinvalidation/invalidation-client-impl.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/proto-converter.h"
#include "google/cacheinvalidation/random.h"
#include "google/cacheinvalidation/scoped_ptr.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/system-resources-for-test.h"
#include "google/cacheinvalidation/version-manager.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::make_pair;
using INVALIDATION_STL_NAMESPACE::pair;
using INVALIDATION_STL_NAMESPACE::vector;

using ::testing::_;
using ::testing::AllOf;
using ::testing::InvokeArgument;
using ::testing::MakeMatcher;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::MatchResultListener;
using ::testing::Property;
using ::testing::SaveArg;
using ::testing::StrictMock;

class MockSystemResources : public SystemResourcesForTest {
 public:
  MOCK_METHOD2(WriteState, void(const string&, StorageCallback*));
};

/* A listener for testing. */
class MockListener : public InvalidationListener {
 public:
  MOCK_METHOD2(Invalidate, void(const Invalidation&, Closure*));
  MOCK_METHOD1(InvalidateAll, void(Closure*));
  MOCK_METHOD1(AllRegistrationsLost, void(Closure*));
  MOCK_METHOD3(RegistrationStateChanged,
               void(const ObjectId&, RegistrationState, const UnknownHint&));
  MOCK_METHOD1(SessionStatusChanged, void(bool));

  /* System resources, for checking that callbacks run on the right thread. */
  SystemResources* resources_;
};

static bool ObjectIdPsEqual(const ObjectIdP& object_id1,
                            const ObjectIdP& object_id2) {
  return (object_id1.source() == object_id2.source()) &&
        (object_id1.name().string_value() == object_id2.name().string_value());
}

static bool ObjectIdsEqual(const ObjectId& object_id1,
                           const ObjectId& object_id2) {
  return (object_id1.source() == object_id2.source()) &&
        (object_id1.name() == object_id2.name());
}

class ObjectIdEqMatcher : public MatcherInterface<const ObjectId&> {
 public:
  explicit ObjectIdEqMatcher(const ObjectId& object_id)
      : object_id_(object_id) {}

  virtual bool MatchAndExplain(const ObjectId& other_oid,
                               MatchResultListener* listener) const {
    return ObjectIdsEqual(object_id_, other_oid);
  }

  virtual void DescribeTo(::std::ostream* os) const {
    *os << "object ids equal";
  }

 private:
  ObjectId object_id_;
};

inline Matcher<const ObjectId&> ObjectIdEq(const ObjectId& object_id) {
  return MakeMatcher(new ObjectIdEqMatcher(object_id));
}

class InvalidationClientImplTest : public testing::Test {
 public:
  InvalidationClientImplTest() :
      // Calls to the outbound network listener are throttled to no more than
      // one per second, so sometimes we need to advance time by this much in
      // order for the next call to be made.
      fine_throttle_interval_(TimeDelta::FromSeconds(1)),
      default_registration_timeout_(TimeDelta::FromMinutes(1)) {}

  /* A name for the application. */
  static const char* APP_NAME;

  /* Fake client information for testing. */
  static const char* CLIENT_INFO;

  /* Fake data for a session token. */
  static const char* OPAQUE_DATA;

  /* A status object indicating success. */
  Status success_status_;

  /* An object id. */
  ObjectIdP object_id1_;

  /* An object id. */
  ObjectIdP object_id2_;

  /* A sample version. */
  static const int64 VERSION;

  /* System resources for testing. */
  scoped_ptr<MockSystemResources> resources_;

  /* Test listener. */
  scoped_ptr<MockListener> listener_;

  /* The invalidation client being tested. */
  scoped_ptr<InvalidationClientImpl> ticl_;

  /* A field that's set when the Ticl informs us about an outgoing message.
   */
  bool outbound_message_ready_;

  /* Listens for outbound messages from the Ticl. */
  void HandleOutboundMessageReady(NetworkEndpoint* const& endpoint) {
    ASSERT_FALSE(resources_->IsRunningOnInternalThread());
    outbound_message_ready_ = true;
  }

  scoped_ptr<NetworkCallback> network_listener_;

  /* The uniquifier that we've assigned for the client. */
  string client_uniquifier_;

  /* The session token we've assigned for the client. */
  string session_token_;

  /* A register operation. */
  RegistrationUpdate reg_op1_;

  /* A register operation. */
  RegistrationUpdate reg_op2_;

  /* Registration responses we've received. */
  vector<RegistrationUpdateResult> reg_results_;

  /* The throttler's smaller window size. */
  TimeDelta fine_throttle_interval_;

  /* The default registration timeout. */
  TimeDelta default_registration_timeout_;

  /* The last state the Ticl persisted. */
  string last_persisted_state_;

  /* Checks that client's message contains a proper id-assignment request. */
  void CheckAssignClientIdRequest(
      const ClientToServerMessage& message, ClientExternalIdP* result) {
    // Check that the message contains an "assign client id" action.
    ASSERT_TRUE(message.has_action());
    ASSERT_EQ(message.action(), ClientToServerMessage_Action_ASSIGN_CLIENT_ID);

    // Check that the message specifies the client's desired protocol version.
    ProtocolVersion expected_version;
    VersionManager::GetLatestProtocolVersion(&expected_version);
    ASSERT_EQ(message.protocol_version().version().major_version(),
              expected_version.version().major_version());
    ASSERT_EQ(message.protocol_version().version().minor_version(),
              expected_version.version().minor_version());

    // Check that the message specifies the client's version.
    ClientVersion expected_client_version;
    VersionManager version_manager(CLIENT_INFO);

    version_manager.GetClientVersion(&expected_client_version);
    ASSERT_EQ(message.client_version().version().major_version(),
              expected_client_version.version().major_version());
    ASSERT_EQ(message.client_version().version().minor_version(),
              expected_client_version.version().minor_version());
    ASSERT_EQ(message.client_version().flavor(),
              expected_client_version.flavor());
    ASSERT_EQ(message.client_version().client_info(), CLIENT_INFO);

    // Check that the message supplied a timestamp.
    ASSERT_EQ(message.timestamp(),
              resources_->current_time().ToInternalValue() /
              Time::kMicrosecondsPerMillisecond);

    // Check that the message contains an "assign client id" type.
    ASSERT_TRUE(message.has_message_type());
    ASSERT_EQ(message.message_type(),
              ClientToServerMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);

    // Check that it does not contain a session token or any registration
    // operations or invalidation acknowledgments.
    ASSERT_FALSE(message.has_session_token());
    ASSERT_EQ(message.acked_invalidation_size(), 0);
    ASSERT_EQ(message.register_operation_size(), 0);

    // Check that it contains the fields of an external id.
    ASSERT_TRUE(message.has_client_type());
    ASSERT_EQ(message.client_type().type(), ClientType_Type_CHROME_SYNC);
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
    ticl_->Start("");
    outbound_message_ready_ = false;
    ticl_->network_endpoint()->RegisterOutboundListener(
        network_listener_.get());
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();

    // Check that it has a message to send, and pull the message.
    ASSERT_TRUE(outbound_message_ready_);
    outbound_message_ready_ = false;
    string serialized;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    ClientToServerMessage message;
    message.ParseFromString(serialized);

    // Check that the message is a proper request for client id assignment.
    ClientExternalIdP external_id;
    CheckAssignClientIdRequest(message, &external_id);

    // Construct a uniquifier.
    client_uniquifier_ = "uniquifier";

    // Also construct an initial session token.
    session_token_ = OPAQUE_DATA;

    // Construct a response with the uniquifier and session token.
    ServerToClientMessage response;
    response.mutable_client_type()->set_type(external_id.client_type().type());
    response.mutable_app_client_id()->set_string_value(
        external_id.app_client_id().string_value());
    response.set_nonce(message.nonce());
    response.set_client_uniquifier(client_uniquifier_);
    response.set_session_token(session_token_);
    response.mutable_status()->set_code(Status_Code_SUCCESS);
    response.set_message_type(
        ServerToClientMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);

    response.SerializeToString(&serialized);

    EXPECT_CALL(*listener_, SessionStatusChanged(true));
    Closure* callback = NULL;
    EXPECT_CALL(*listener_, AllRegistrationsLost(_))
        .WillOnce(SaveArg<0>(&callback));

    // Give the message to the Ticl, and let it handle it.
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();

    ASSERT_TRUE(callback != NULL);
    callback->Run();
    delete callback;

    StorageCallback* storage_callback = NULL;
    EXPECT_CALL(*resources_, WriteState(_, _))
        .WillOnce(DoAll(SaveArg<0>(&last_persisted_state_),
                        SaveArg<1>(&storage_callback)));
    resources_->ModifyTime(TimeDelta::FromSeconds(1));
    resources_->RunReadyTasks();

    storage_callback->Run(true);
    delete storage_callback;
  }

  /* Requests that the Ticl (un)register for two objects.  Checks that the
   * message it sends contains the correct information about these
   * (un)registrations.
   */
  void MakeAndCheckRegistrations(bool is_register) {
    void (InvalidationClient::*operation)(const ObjectId&) =
        is_register ?
        &InvalidationClient::Register : &InvalidationClient::Unregister;

    // Ask the Ticl to register for two objects.
    outbound_message_ready_ = false;
    ObjectId oid1;
    ObjectId oid2;
    ConvertFromObjectIdProto(object_id1_, &oid1);
    ConvertFromObjectIdProto(object_id2_, &oid2);
    (ticl_.get()->*operation)(oid1);
    (ticl_.get()->*operation)(oid2);
    resources_->ModifyTime(fine_throttle_interval_);
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();
    ASSERT_TRUE(outbound_message_ready_);

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
    ASSERT_TRUE(message.has_message_type());
    ASSERT_EQ(message.message_type(),
              ClientToServerMessage_MessageType_TYPE_OBJECT_CONTROL);
    ASSERT_EQ(message.register_operation_size(), 2);
    reg_op1_.Clear();
    reg_op1_.mutable_object_id()->CopyFrom(object_id1_);
    reg_op1_.set_sequence_number(1);
    reg_op1_.set_type(operation_type);
    reg_op2_.mutable_object_id()->CopyFrom(object_id2_);
    reg_op2_.set_sequence_number(2);
    reg_op2_.set_type(operation_type);

    string serialized2, serialized_reg_op1, serialized_reg_op2;
    reg_op1_.SerializeToString(&serialized_reg_op1);
    message.register_operation(0).SerializeToString(&serialized);
    reg_op2_.SerializeToString(&serialized_reg_op2);
    message.register_operation(1).SerializeToString(&serialized2);
    ASSERT_TRUE(((serialized == serialized_reg_op1) &&
                 (serialized2 == serialized_reg_op2)) ||
                ((serialized == serialized_reg_op2) &&
                 (serialized2 == serialized_reg_op1)));

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
    response.set_message_type(
        ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
    string serialized;
    response.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();

    // Check that the registration state is REGISTERED.
    ASSERT_EQ(RegState_REGISTERED,
              ticl_->GetRegistrationStateForTest(object_id1_));
    ASSERT_EQ(RegState_REGISTERED,
              ticl_->GetRegistrationStateForTest(object_id2_));

    // Advance the clock a lot, run everything, and make sure it's not trying to
    // resend.
    resources_->ModifyTime(default_registration_timeout_);
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

    EXPECT_CALL(*listener_, SessionStatusChanged(false));

    // Tell the Ticl its session is invalid.
    ServerToClientMessage message;
    message.set_session_token(session_token_);
    message.mutable_status()->set_code(Status_Code_INVALID_SESSION);
    message.set_message_type(
        ServerToClientMessage_MessageType_TYPE_INVALIDATE_SESSION);
    string serialized;
    message.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->ModifyTime(fine_throttle_interval_);
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();

    // Check that the Ticl has pinged the client to indicate it has a request.
    ASSERT_TRUE(outbound_message_ready_);

    // Pull a message from the Ticl and check that it requests a new session.
    ClientToServerMessage request;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    request.ParseFromString(serialized);
    ASSERT_TRUE(request.has_action());
    ASSERT_EQ(request.action(), ClientToServerMessage_Action_UPDATE_SESSION);
    ASSERT_TRUE(request.has_message_type());
    ASSERT_EQ(request.message_type(),
              ClientToServerMessage_MessageType_TYPE_UPDATE_SESSION);
    ASSERT_TRUE(request.has_client_uniquifier());
    ASSERT_EQ(client_uniquifier_, request.client_uniquifier());

    // Give it a new session token.
    Closure* callback = NULL;
    EXPECT_CALL(*listener_, AllRegistrationsLost(_))
        .WillOnce(SaveArg<0>(&callback));
    EXPECT_CALL(*listener_, SessionStatusChanged(true));

    session_token_ = "NEW_OPAQUE_DATA";
    message.Clear();
    message.set_client_uniquifier(client_uniquifier_);
    message.set_session_token(session_token_);
    message.mutable_status()->set_code(Status_Code_SUCCESS);
    message.set_message_type(
        ServerToClientMessage_MessageType_TYPE_UPDATE_SESSION);
    message.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();
    callback->Run();
    delete callback;

    // Give it some time and check that it persists the state.
    StorageCallback* storage_callback = NULL;
    EXPECT_CALL(*resources_, WriteState(_, _))
        .WillOnce(DoAll(SaveArg<0>(&last_persisted_state_),
                        SaveArg<1>(&storage_callback)));
    resources_->ModifyTime(TimeDelta::FromSeconds(1));
    resources_->RunReadyTasks();

    storage_callback->Run(true);
    delete storage_callback;
  }

  void TestInvalidateAndReassignClientId() {
    // Tell the Ticl we don't recognize it.
    EXPECT_CALL(*listener_, SessionStatusChanged(false));
    ServerToClientMessage message;
    message.Clear();
    message.mutable_status()->set_code(Status_Code_UNKNOWN_CLIENT);
    message.set_session_token(session_token_);
    string serialized;
    message.set_client_uniquifier(client_uniquifier_);
    message.set_message_type(
        ServerToClientMessage_MessageType_TYPE_INVALIDATE_CLIENT_ID);
    message.SerializeToString(&serialized);
    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();

    // Pull a message from it, and check that it's trying to assign a client id.
    ClientToServerMessage request;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    request.ParseFromString(serialized);
    ASSERT_TRUE(request.has_action());
    ASSERT_EQ(request.action(), ClientToServerMessage_Action_ASSIGN_CLIENT_ID);
    ClientExternalIdP external_id;
    CheckAssignClientIdRequest(request, &external_id);

    // Give it a new uniquifier and session.
    string new_uniquifier_str = "newuniquifierstr";

    session_token_ = "new opaque data";
    ServerToClientMessage response;
    response.set_session_token(session_token_);
    response.mutable_status()->set_code(Status_Code_SUCCESS);
    response.mutable_client_type()->set_type(external_id.client_type().type());
    response.mutable_app_client_id()->set_string_value(
        external_id.app_client_id().string_value());
    response.set_nonce(request.nonce());
    response.set_client_uniquifier(new_uniquifier_str);
    response.set_message_type(
        ServerToClientMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);
    response.SerializeToString(&serialized);

    Closure* callback = NULL;
    EXPECT_CALL(*listener_, AllRegistrationsLost(_))
        .WillOnce(SaveArg<0>(&callback));
    EXPECT_CALL(*listener_, SessionStatusChanged(true));

    ticl_->network_endpoint()->HandleInboundMessage(serialized);
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();
    callback->Run();
    delete callback;
  }

  virtual void SetUp() {
    object_id1_.Clear();
    object_id1_.set_source(ObjectIdP_Source_CHROME_SYNC);
    object_id1_.mutable_name()->set_string_value("BOOKMARKS");
    object_id2_.Clear();
    object_id2_.set_source(ObjectIdP_Source_CHROME_SYNC);
    object_id2_.mutable_name()->set_string_value("HISTORY");
    resources_.reset(new StrictMock<MockSystemResources>());
    resources_->ModifyTime(TimeDelta::FromSeconds(1000000));
    resources_->StartScheduler();
    listener_.reset(new StrictMock<MockListener>());
    network_listener_.reset(
        NewPermanentCallback(
            this, &InvalidationClientImplTest::HandleOutboundMessageReady));
    ClientConfig ticl_config;
    ticl_config.smear_factor = 0.0;  // Disable smearing for determinism.
    ClientType client_type;
    client_type.set_type(ClientType_Type_CHROME_SYNC);
    ticl_.reset(new InvalidationClientImpl(
        resources_.get(), client_type, APP_NAME, CLIENT_INFO, ticl_config,
        listener_.get()));
    reg_results_.clear();
  }

  virtual void TearDown() {
    resources_->StopScheduler();
  }
};

const char* InvalidationClientImplTest::APP_NAME = "app_name";
const char* InvalidationClientImplTest::CLIENT_INFO = "unit test client";
const char* InvalidationClientImplTest::OPAQUE_DATA = "opaque_data";
const int64 InvalidationClientImplTest::VERSION = 5;

TEST_F(InvalidationClientImplTest, Initialization) {
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
  ticl_->Start("");
  ticl_->network_endpoint()->RegisterOutboundListener(network_listener_.get());
  resources_->RunReadyTasks();

  // Pull a message.
  ClientToServerMessage message;
  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the message is a proper request for client id assignment.
  ClientExternalIdP external_id;
  CheckAssignClientIdRequest(message, &external_id);

  // Fabricate a uniquifier and initial session token.
  client_uniquifier_ = "uniquifier";
  session_token_ = OPAQUE_DATA;

  // Construct a response with the uniquifier and session token but the wrong
  // app client id.
  ServerToClientMessage response;
  response.mutable_client_type()->CopyFrom(external_id.client_type());
  response.mutable_app_client_id()->set_string_value("wrong-app-client-id");
  response.set_client_uniquifier(client_uniquifier_);
  response.set_session_token(session_token_);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_ASSIGN_CLIENT_ID);

  // Give the message to the Ticl, and let it handle it.
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();

  // Pull a message.
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the Ticl is still looking for a client id.
  CheckAssignClientIdRequest(message, &external_id);
}

TEST_F(InvalidationClientImplTest, HeartbeatIntervalRespected) {
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
  int new_heartbeat_interval_ms = 300000;
  ServerToClientMessage response;
  response.set_session_token(session_token_);
  response.set_next_heartbeat_interval_ms(new_heartbeat_interval_ms);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // Advance to just shy of the heartbeat interval, and check that the Ticl did
  // not nudge the application to send.
  resources_->ModifyTime(
      TimeDelta::FromMilliseconds(new_heartbeat_interval_ms - 1));
  resources_->RunReadyTasks();
  ASSERT_FALSE(outbound_message_ready_);

  // Advance further, and check that it did nudge the application to send.
  resources_->ModifyTime(fine_throttle_interval_);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_TRUE(outbound_message_ready_);

  // Shorten the heartbeat interval and repeat.
  response.Clear();
  response.set_session_token(session_token_);
  response.set_next_heartbeat_interval_ms(10000);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // Because the Ticl uses a single timer-task, the next heartbeat will still
  // happen after the longer interval.
  // Periodic task executes after this since heartbeat interval is large.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(80000));
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_TRUE(outbound_message_ready_);
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  outbound_message_ready_ = false;

  // But subsequently, heartbeats should happen with the shorter interval.
  resources_->ModifyTime(TimeDelta::FromMilliseconds(9999));
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_FALSE(outbound_message_ready_);

  resources_->ModifyTime(fine_throttle_interval_);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();

  ASSERT_TRUE(outbound_message_ready_);
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
  // Start in the REGISTERED state so we actually have something to do.
  TestRegistration(true);
  ObjectId oid2;
  ConvertFromObjectIdProto(object_id2_, &oid2);
  ticl_->Unregister(oid2);
  resources_->ModifyTime(
      fine_throttle_interval_ + TimeDelta::FromMilliseconds(500));
  resources_->RunReadyTasks();

  // Pull a message, and check that it has the right session token and
  // registration update message.
  ClientToServerMessage message;
  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_TRUE(message.has_session_token());
  ASSERT_EQ(message.session_token(), session_token_);
  ASSERT_TRUE(message.has_message_type());
  ASSERT_EQ(message.message_type(),
            ClientToServerMessage_MessageType_TYPE_OBJECT_CONTROL);
  ASSERT_EQ(message.register_operation_size(), 1);
  const RegistrationUpdate& op = message.register_operation(0);
  ASSERT_TRUE(ObjectIdPsEqual(object_id2_, op.object_id()));
  ASSERT_EQ(RegistrationUpdate_Type_UNREGISTER, op.type());

  // Construct a response.
  ServerToClientMessage response;
  response.set_session_token(session_token_);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  RegistrationUpdateResult* result = response.add_registration_result();
  result->mutable_operation()->CopyFrom(op);
  result->mutable_status()->set_code(Status_Code_SUCCESS);
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
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
  result1->mutable_status()->set_code(Status_Code_PERMANENT_FAILURE);
  result1->mutable_status()->set_description("Registration update failed");
  RegistrationUpdateResult* result2 = response.add_registration_result();
  result2->mutable_operation()->CopyFrom(reg_op2_);
  result2->mutable_status()->set_code(Status_Code_SUCCESS);
  response.mutable_status()->set_code(Status_Code_SUCCESS);
  response.set_session_token(session_token_);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  response.SerializeToString(&serialized);

  ObjectId oid1;
  ConvertFromObjectIdProto(object_id1_, &oid1);
  EXPECT_CALL(
      *listener_,
      RegistrationStateChanged(
          ObjectIdEq(oid1), RegistrationState_UNKNOWN, _));

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();

  ASSERT_EQ(RegState_REGISTERED,
            ticl_->GetRegistrationStateForTest(object_id2_));

  // Advance the clock a lot, run everything, and make sure it's not trying to
  // resend.
  resources_->ModifyTime(default_registration_timeout_);
  resources_->RunReadyTasks();
  ClientToServerMessage message;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(message.register_operation_size(), 0);
}

TEST_F(InvalidationClientImplTest, InvalidationP) {
  /* Test plan: get a client id and session token, and register for an object.
   * Deliver an invalidation for that object.  Check that the listener's
   * invalidate() method gets called with the right invalidation.  Check that
   * the Ticl acks the invalidation, but only after the listener has acked it.
   */
  TestRegistration(true);

  Closure* callback = NULL;
  ObjectId oid1;
  ConvertFromObjectIdProto(object_id1_, &oid1);
  EXPECT_CALL(*listener_, Invalidate(AllOf(
      Property(&Invalidation::version, InvalidationClientImplTest::VERSION),
      Property(&Invalidation::object_id, ObjectIdEq(oid1))), _))
      .WillOnce(SaveArg<1>(&callback));

  // Deliver an invalidation for an object.
  ServerToClientMessage message;
  InvalidationP* invalidation = message.add_invalidation();
  invalidation->mutable_object_id()->CopyFrom(object_id1_);
  invalidation->set_version(InvalidationClientImplTest::VERSION);
  message.set_session_token(session_token_);
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();

  // Check that the Ticl isn't acking the invalidation yet, since we haven't
  // called the callback.
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  ClientToServerMessage client_message;
  client_message.ParseFromString(serialized);
  ASSERT_EQ(client_message.acked_invalidation_size(), 0);
  outbound_message_ready_ = false;

  // Now run the callback, and check that the Ticl does ack the invalidation.
  callback->Run();
  delete callback;
  resources_->ModifyTime(fine_throttle_interval_);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_TRUE(outbound_message_ready_);
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  client_message.ParseFromString(serialized);
  ASSERT_EQ(client_message.acked_invalidation_size(), 1);
  ASSERT_EQ(InvalidationClientImplTest::VERSION,
            client_message.acked_invalidation(0).version());
  ASSERT_TRUE(
      ObjectIdPsEqual(object_id1_,

                     client_message.acked_invalidation(0).object_id()));
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
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_INVALIDATE_SESSION);
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

TEST_F(InvalidationClientImplTest, GarbageCollection) {
  /* Test plan: get a client id and session, and perform some registrations.
   * Send the Ticl a message indicating it has been garbage-collected.  Check
   * that the Ticl requests a new client id.  Respond with one, along with a
   * session.  Check that it repeats the register operations, and that it sends
   * an invalidateAll once the registrations have completed.
   */
  TestRegistration(true);
  TestInvalidateAndReassignClientId();
}

TEST_F(InvalidationClientImplTest, LoseSessionThenClientId) {
  /* Test plan: get a client is and session.  Send a message indicating the
   * session is invalid.  When it asks to update the session, send another
   * message indicating the client id is invalid.  Check that it then behaves
   * like a fresh client (makes an assign-client-id request, etc.).
   */
  TestInitialization();

  // Tell the Ticl we don't recognize its session.
  EXPECT_CALL(*listener_, SessionStatusChanged(false));
  ServerToClientMessage message;
  message.Clear();
  message.mutable_status()->set_code(Status_Code_INVALID_SESSION);
  message.set_session_token(session_token_);
  string serialized;
  message.set_client_uniquifier(client_uniquifier_);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_INVALIDATE_SESSION);
  message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();

  // Pull a message from it, and check that it's trying to update its session.
  ClientToServerMessage request;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  request.ParseFromString(serialized);
  ASSERT_TRUE(request.has_action());
  ASSERT_EQ(request.action(), ClientToServerMessage_Action_UPDATE_SESSION);

  TestInvalidateAndReassignClientId();
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
  message.set_client_uniquifier("bogus-client-id");
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_INVALIDATE_CLIENT_ID);
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

TEST_F(InvalidationClientImplTest, InvalidateAll) {
  /* Test plan: initialize the Ticl.  Send it a message with the "invalidate
   * all" object id, and check that the app gets an invalidateAll() call.
   */
  TestInitialization();

  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  message.set_session_token(session_token_);
  InvalidationP* inv = message.add_invalidation();
  inv->mutable_object_id()->set_source(ObjectIdP_Source_INTERNAL);
  inv->mutable_object_id()->mutable_name()->set_string_value("ALL");
  inv->set_version(1);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  message.SerializeToString(&serialized);

  Closure* callback = NULL;
  EXPECT_CALL(*listener_, InvalidateAll(_))
      .WillOnce(SaveArg<0>(&callback));

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  callback->Run();
  delete callback;
}

TEST_F(InvalidationClientImplTest, AcceptsProtocolVersion1) {
  /* Test plan: initialize the Ticl.  Send it a message with the "invalidate
   * all" object id and protocol version 1, and check that the app gets an
   * invalidateAll() call.
   */
  TestInitialization();

  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  message.set_session_token(session_token_);
  message.mutable_protocol_version()->mutable_version()->set_major_version(1);
  message.mutable_protocol_version()->mutable_version()->set_minor_version(0);
  InvalidationP* inv = message.add_invalidation();
  inv->mutable_object_id()->set_source(ObjectIdP_Source_INTERNAL);
  inv->mutable_object_id()->mutable_name()->set_string_value("ALL");
  inv->set_version(1);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  message.SerializeToString(&serialized);

  Closure* callback = NULL;
  EXPECT_CALL(*listener_, InvalidateAll(_))
      .WillOnce(SaveArg<0>(&callback));

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  callback->Run();
  delete callback;
}

TEST_F(InvalidationClientImplTest, RejectsProtocolVersion2) {
  /* Test plan: initialize the Ticl.  Send it a message with the "invalidate
   * all" object id and protocol version 2, and check that the app does not get
   * an invalidateAll() call, which implies that the Ticl ignored the message
   * whose version was too high.
   */
  TestInitialization();

  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  message.set_session_token(session_token_);
  message.mutable_protocol_version()->mutable_version()->set_major_version(2);
  message.mutable_protocol_version()->mutable_version()->set_minor_version(0);
  InvalidationP* inv = message.add_invalidation();
  inv->mutable_object_id()->set_source(ObjectIdP_Source_INTERNAL);
  inv->mutable_object_id()->mutable_name()->set_string_value("ALL");
  inv->set_version(1);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  message.SerializeToString(&serialized);

  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
}

TEST_F(InvalidationClientImplTest, Throttling) {
  /* Test plan: initialize the Ticl.  Send it a message telling it to set its
   * heartbeat and polling intervals to 1 ms.  Make sure its pings to the app
   * don't violate the (default) rate limits.
   */
  TestInitialization();

  ServerToClientMessage message;
  message.mutable_status()->set_code(Status_Code_SUCCESS);
  message.set_session_token(session_token_);
  message.set_next_heartbeat_interval_ms(1);
  message.set_next_poll_interval_ms(1);
  message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  string serialized;
  message.SerializeToString(&serialized);

  ticl_->network_endpoint()->HandleInboundMessage(serialized);

  // Run for five minutes in 10ms increments, counting the number of times the
  // Ticl tells us it has a bundle.
  int ping_count = 0;
  for (int i = 0; i < 30000; ++i) {
    resources_->ModifyTime(TimeDelta::FromMilliseconds(10));
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();
    if (outbound_message_ready_) {
      ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
      outbound_message_ready_ = false;
      ++ping_count;
    }
  }
  ASSERT_GE(ping_count, 28);
  ASSERT_LE(ping_count, 31);
}

TEST_F(InvalidationClientImplTest, Smearing) {
  int n_iterations = 500;
  double smear_factor = 0.2;
  TimeDelta base_delay(TimeDelta::FromSeconds(1));
  int num_not_exactly_equal = 0;
  TimeDelta abs_smear_sum(TimeDelta::FromSeconds(0));
  Random random(0);
  for (int i = 0; i < n_iterations; ++i) {
    TimeDelta delay = InvalidationClientImpl::SmearDelay(
        base_delay, smear_factor, &random);
    LOG(INFO) << "delay = " << delay.ToInternalValue();
    ASSERT_TRUE((delay >= TimeDelta::FromMilliseconds(800)) &&
                (delay <= TimeDelta::FromMilliseconds(1200)));
    num_not_exactly_equal += (delay != base_delay);
    if (delay < base_delay) {
      abs_smear_sum = abs_smear_sum + (base_delay - delay);
    } else {
      abs_smear_sum = abs_smear_sum + (delay - base_delay);
    }
  }

  // Make sure we actually smeared values. This is a conservative check -- we
  // actually expect num_not_exactly_equal == n_iterations.
  ASSERT_GT(num_not_exactly_equal, n_iterations / 2);

  // Another check on smearing -- we'd actually expect / 2, but be conservative.
  ASSERT_TRUE(abs_smear_sum >=
      base_delay * static_cast<int64>(smear_factor * n_iterations / 3));
}

TEST_F(InvalidationClientImplTest, MaxSessionRequests) {
  // Start up the Ticl, connect a network listener, and let it do its
  // initialization.
  ticl_->Start("");
  ticl_->network_endpoint()->RegisterOutboundListener(network_listener_.get());

  resources_->RunReadyTasks();
  resources_->RunListenerTasks();

  string serialized;
  ClientToServerMessage message;
  ClientExternalIdP external_id;
  for (int i = 0; i < SessionManager::getMaxSessionAttemptsForTest(); ++i) {
    // Check that it has a message to send, and pull the message.
    ASSERT_TRUE(outbound_message_ready_);

    outbound_message_ready_ = false;
    ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
    message.ParseFromString(serialized);

    // Check that the message is a proper request for client id assignment.
    CheckAssignClientIdRequest(message, &external_id);

    // Don't respond.
    resources_->ModifyTime(
        // Default registration (session request) timeout plus periodic task
        // interval.
        TimeDelta::FromMinutes(1) + TimeDelta::FromMilliseconds(500));
    resources_->RunReadyTasks();
    resources_->RunListenerTasks();
  }

  // Check that it's given up and isn't trying to send anymore.
  ASSERT_FALSE(outbound_message_ready_);

  // Advance time another hour and check again.
  resources_->ModifyTime(
      SessionManager::getWakeUpAfterGiveUpIntervalForTest() +
      TimeDelta::FromMilliseconds(500));
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();

  // Check that it has a message to send, and pull the message.
  ASSERT_TRUE(outbound_message_ready_);

  outbound_message_ready_ = false;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Check that the message is a proper request for client id assignment.
  CheckAssignClientIdRequest(message, &external_id);
}

TEST_F(InvalidationClientImplTest, Persistence) {
  // Do a fresh startup.
  TestInitialization();

  // Make a local copy of the state it persisted.
  string state = last_persisted_state_;

  // Kill the old resources / Ticl and start a new one.
  resources_.reset(new StrictMock<MockSystemResources>());
  resources_->ModifyTime(TimeDelta::FromSeconds(1000000));
  resources_->StartScheduler();

  ClientConfig ticl_config;
  ticl_config.smear_factor = 0.0;  // Disable smearing for determinism.
  ClientType client_type;
  client_type.set_type(ClientType_Type_CHROME_SYNC);

  string new_state;
  StorageCallback* storage_callback = NULL;
  Closure* callback = NULL;

  // On startup, the Ticl should issue AllRegistrationsLost and
  // SessionStatusChanged(true).  It should also try to write back immediately
  // to claim a new block of sequence numbers.
  EXPECT_CALL(*listener_, AllRegistrationsLost(_))
      .WillOnce(SaveArg<0>(&callback));
  EXPECT_CALL(*listener_, SessionStatusChanged(true));
  EXPECT_CALL(*resources_, WriteState(_, _))
      .WillOnce(DoAll(SaveArg<0>(&new_state),
                      SaveArg<1>(&storage_callback)));

  ticl_.reset(new InvalidationClientImpl(
      resources_.get(), client_type, APP_NAME, CLIENT_INFO, ticl_config,
      listener_.get()));
  ticl_->Start(state);
  ticl_->network_endpoint()->RegisterOutboundListener(network_listener_.get());

  resources_->RunReadyTasks();
  resources_->RunListenerTasks();

  storage_callback->Run(true);
  delete storage_callback;

  callback->Run();
  delete callback;

  TiclState parsed_state;
  TiclState new_parsed_state;
  DeserializeState(state, &parsed_state);
  DeserializeState(new_state, &new_parsed_state);

  ASSERT_EQ(parsed_state.sequence_number_limit() + ticl_config.seqno_block_size,
            new_parsed_state.sequence_number_limit());

  resources_->ModifyTime(TimeDelta::FromSeconds(1));
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_TRUE(outbound_message_ready_);
  outbound_message_ready_ = false;

  string serialized;
  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  ClientToServerMessage message;
  message.ParseFromString(serialized);

  // Ticl should be sending a registration sync request.
  ASSERT_EQ(ClientToServerMessage_MessageType_TYPE_REGISTRATION_SYNC,
            message.message_type());
  ASSERT_FALSE(message.has_action());

  // Request to register on some objects.
  ObjectIdP object_id3;
  object_id3.mutable_name()->set_string_value("timeout-object");
  object_id3.set_source(ObjectIdP_Source_CHROME_SYNC);
  ObjectIdP object_id4;
  object_id4.mutable_name()->set_string_value("spontaneous-reg-object");
  object_id4.set_source(ObjectIdP_Source_CHROME_SYNC);

  ObjectId oid1;
  ObjectId oid2;
  ObjectId oid3;
  ConvertFromObjectIdProto(object_id1_, &oid1);
  ConvertFromObjectIdProto(object_id2_, &oid2);
  ConvertFromObjectIdProto(object_id3, &oid3);

  ticl_->Register(oid1);
  ticl_->Register(oid2);
  ticl_->Register(oid3);

  // Wait for the next periodic check / message rate limit.
  resources_->ModifyTime(TimeDelta::FromSeconds(1));
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_TRUE(outbound_message_ready_);
  outbound_message_ready_ = false;

  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);

  // Ticl should be sending a heartbeat.
  ASSERT_EQ(ClientToServerMessage_MessageType_TYPE_OBJECT_CONTROL,
            message.message_type());
  ASSERT_TRUE(message.has_action());
  ASSERT_EQ(ClientToServerMessage_Action_HEARTBEAT, message.action());

  // Push some registration responses back.
  ServerToClientMessage registration_push_message;
  registration_push_message.set_session_token(session_token_);
  registration_push_message.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  registration_push_message.set_num_total_registrations(2);
  RegistrationUpdateResult* result =
      registration_push_message.add_registration_result();
  result->mutable_operation()->mutable_object_id()->CopyFrom(object_id1_);
  result->mutable_operation()->set_type(RegistrationUpdate_Type_REGISTER);
  result->mutable_operation()->set_sequence_number(1);
  result->mutable_status()->set_code(Status_Code_SUCCESS);
  result = registration_push_message.add_registration_result();
  result->mutable_operation()->mutable_object_id()->CopyFrom(object_id4);
  result->mutable_operation()->set_type(RegistrationUpdate_Type_REGISTER);
  result->mutable_operation()->set_sequence_number(2);
  result->mutable_status()->set_code(Status_Code_SUCCESS);

  ObjectId oid4;
  ConvertFromObjectIdProto(object_id4, &oid4);
  EXPECT_CALL(*listener_,
              RegistrationStateChanged(
                  ObjectIdEq(oid4),
                  RegistrationState_REGISTERED,
                  _));
  registration_push_message.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);

  // Ticl should be in synced state now, so it should send requests to register
  // for the oids for which we didn't push registrations.
  resources_->ModifyTime(TimeDelta::FromSeconds(1));
  resources_->RunReadyTasks();
  resources_->RunListenerTasks();
  ASSERT_TRUE(outbound_message_ready_);

  ticl_->network_endpoint()->TakeOutboundMessage(&serialized);
  message.ParseFromString(serialized);
  ASSERT_EQ(ClientToServerMessage_MessageType_TYPE_OBJECT_CONTROL,
            message.message_type());
  ASSERT_EQ(2, message.register_operation_size());
  const RegistrationUpdate& op1 = message.register_operation(0);
  const RegistrationUpdate& op2 = message.register_operation(1);
  ASSERT_TRUE(ObjectIdPsEqual(op1.object_id(), object_id2_) ||
              ObjectIdPsEqual(op1.object_id(), object_id3));
  ASSERT_TRUE(ObjectIdPsEqual(op2.object_id(), object_id2_) ||
              ObjectIdPsEqual(op2.object_id(), object_id3));
  ASSERT_GT(op1.sequence_number(), ticl_config.seqno_block_size);
  ASSERT_GT(op2.sequence_number(), ticl_config.seqno_block_size);

  // Deliver a failure response for object_id2_ and let object_id3 time out.
  ServerToClientMessage response;
  response.set_session_token(session_token_);
  response.set_message_type(
      ServerToClientMessage_MessageType_TYPE_OBJECT_CONTROL);
  result = response.add_registration_result();
  if (ObjectIdPsEqual(op1.object_id(), object_id2_)) {
    result->mutable_operation()->CopyFrom(op1);
  } else {
    result->mutable_operation()->CopyFrom(op2);
  }
  result->mutable_status()->set_code(Status_Code_PERMANENT_FAILURE);

  // Ticl should inform the listener of the permanent state change for
  // object_id2_.
  response.SerializeToString(&serialized);
  ticl_->network_endpoint()->HandleInboundMessage(serialized);
  resources_->RunReadyTasks();
  ObjectId object_id2;
  ConvertFromObjectIdProto(object_id2_, &object_id2);
  EXPECT_CALL(*listener_,
              RegistrationStateChanged(
                  ObjectIdEq(object_id2),
                  RegistrationState_UNKNOWN,
                  Property(&UnknownHint::is_transient, false)));
  resources_->RunListenerTasks();

  // Ticl should inform the listener of the transient state change for
  // object_id3.
  resources_->ModifyTime(TimeDelta::FromSeconds(80));
  resources_->RunReadyTasks();
  ObjectId tmp_object_id3;
  ConvertFromObjectIdProto(object_id3, &tmp_object_id3);
  EXPECT_CALL(*listener_,
              RegistrationStateChanged(
                  ObjectIdEq(tmp_object_id3),
                  RegistrationState_UNKNOWN,
                  Property(&UnknownHint::is_transient, true)));
  resources_->RunListenerTasks();
}

}  // namespace invalidation
