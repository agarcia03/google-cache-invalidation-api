// Copyright 2012 Google Inc.
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


#include <vector>

#include "google/cacheinvalidation/v2/gmock.h"
#include "google/cacheinvalidation/v2/string_util.h"
#include "google/cacheinvalidation/v2/invalidation-listener.h"
#include "google/cacheinvalidation/v2/types.h"
#include "google/cacheinvalidation/v2/types.pb.h"
#include "google/cacheinvalidation/v2/basic-system-resources.h"
#include "google/cacheinvalidation/v2/constants.h"
#include "google/cacheinvalidation/v2/invalidation-client-impl.h"
#include "google/cacheinvalidation/v2/statistics.h"
#include "google/cacheinvalidation/v2/test/deterministic-scheduler.h"
#include "google/cacheinvalidation/v2/test/test-logger.h"
#include "google/cacheinvalidation/v2/test/test-utils.h"
#include "google/cacheinvalidation/v2/throttle.h"
#include "google/cacheinvalidation/v2/ticl-message-validator.h"
#include "testing/base/public/gunit.h"

namespace invalidation {

using ::ipc::invalidation::ClientType_Type_TEST;
using ::ipc::invalidation::ObjectSource_Type_TEST;
using ::ipc::invalidation::StatusP_Code_PERMANENT_FAILURE;
using ::testing::_;
using ::testing::AllOf;
using ::testing::DeleteArg;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::EqualsProto;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::Matcher;
using ::testing::Property;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::proto::WhenDeserializedAs;

// Creates an action InvokeAndDeleteClosure<k> that invokes the kth closure and
// deletes it after the Run method has been called.
ACTION_TEMPLATE(
    InvokeAndDeleteClosure,
    HAS_1_TEMPLATE_PARAMS(int, k),
    AND_0_VALUE_PARAMS()) {
  std::tr1::get<k>(args)->Run();
  delete std::tr1::get<k>(args);
}

// Creates an action SaveArgToVector<k>(vector*) that saves the kth argument in
// |vec|.
ACTION_TEMPLATE(
    SaveArgToVector,
    HAS_1_TEMPLATE_PARAMS(int, k),
    AND_1_VALUE_PARAMS(vec)) {
  vec->push_back(std::tr1::get<k>(args));
}

// Given the ReadCallback of Storage::ReadKey as argument 1, invokes it with a
// permanent failure status code.
ACTION(InvokeReadCallbackFailure) {
  arg1->Run(pair<Status, string>(Status(Status::PERMANENT_FAILURE, ""), ""));
  delete arg1;
}

// Given the WriteCallback of Storage::WriteKey as argument 2, invokes it with
// a success status code.
ACTION(InvokeWriteCallbackSuccess) {
  arg2->Run(Status(Status::SUCCESS, ""));
  delete arg2;
}

// A mock of the InvalidationListener interface.
class MockInvalidationListener : public InvalidationListener {
 public:
  MOCK_METHOD1(Ready, void(InvalidationClient*));  // NOLINT

  MOCK_METHOD3(Invalidate,
      void(InvalidationClient *, const Invalidation&,  // NOLINT
           const AckHandle&));  // NOLINT

  MOCK_METHOD3(InvalidateUnknownVersion,
               void(InvalidationClient *, const ObjectId&,
                    const AckHandle&));  // NOLINT

  MOCK_METHOD2(InvalidateAll,
      void(InvalidationClient *, const AckHandle&));  // NOLINT

  MOCK_METHOD3(InformRegistrationStatus,
      void(InvalidationClient*, const ObjectId&, RegistrationState));  // NOLINT

  MOCK_METHOD4(InformRegistrationFailure,
      void(InvalidationClient*, const ObjectId&, bool, const string&));

  MOCK_METHOD3(ReissueRegistrations,
      void(InvalidationClient*, const string&, int));

  MOCK_METHOD2(InformError,
      void(InvalidationClient*, const ErrorInfo&));
};

// Tests the basic functionality of the invalidation client.
class InvalidationClientImplTest : public UnitTestBase {
 public:
  virtual ~InvalidationClientImplTest() {}

  // Performs setup for protocol handler unit tests, e.g. creating resource
  // components and setting up common expectations for certain mock objects.
  virtual void SetUp() {
    UnitTestBase::SetUp();

    // Clear throttle limits so that it does not interfere with any test.
    config.protocol_handler_config.rate_limits.clear();

    // Set up the listener scheduler to run any runnable that it receives.
    EXPECT_CALL(*listener_scheduler, Schedule(_, _))
        .WillRepeatedly(InvokeAndDeleteClosure<1>());

    // Create the actual client.
    client.reset(new InvalidationClientImpl(
        resources.get(), ClientType_Type_TEST, "clientName", config,
        "InvClientTest", &listener));
  }

  // Starts the Ticl and ensures that the initialize message is sent. In
  // response, gives a tokencontrol message to the protocol handler and makes
  // sure that ready is called. client_messages is the list of messages expected
  // from the client. The 0th message corresponds to the initialization message
  // sent out by the client.
  void StartClient(const vector<string>& client_messages) {
    // Start the client.
    client.get()->Start();

    // Let the message be sent out.
    internal_scheduler->PassTime(
        GetMaxBatchingDelay(config.protocol_handler_config));

    // Check that the message contains an initializeMessage.
    ClientToServerMessage client_message;
    client_message.ParseFromString(client_messages[0]);
    ASSERT_TRUE(client_message.has_initialize_message());
    string nonce = client_message.initialize_message().nonce();

    // Create the token control message and hand it to the protocol handler.
    ServerToClientMessage sc_message;
    InitServerHeader(nonce, sc_message.mutable_header());
    string new_token = "new token";
    sc_message.mutable_token_control_message()->set_new_token(new_token);
    ProcessIncomingMessage(sc_message, MessageHandlingDelay());
  }

  // Sets the expectations so that the Ticl is ready to be started such that
  // |num_outgoing_messages| are expected to be sent by the ticl. These messages
  // will be saved in |outgoing_messages|.
  void SetExpectationsForTiclStart(int num_outgoing_msgs,
                                   vector<string>& outgoing_messages) {
    // Set up expectations for number of messages expected on the network.
    EXPECT_CALL(*network, SendMessage(_))
        .Times(num_outgoing_msgs)
        .WillRepeatedly(SaveArgToVector<0>(&outgoing_messages));

    // Expect the storage to perform a read key that we will fail.
    EXPECT_CALL(*storage, ReadKey(_, _))
        .WillOnce(InvokeReadCallbackFailure());

    // Expect the listener to indicate that it is ready and let it reissue
    // registrations.
    EXPECT_CALL(listener, Ready(Eq(client.get())));
    EXPECT_CALL(listener, ReissueRegistrations(Eq(client.get()), _, _));

    // Expect the storage layer to receive the write of the session token.
    EXPECT_CALL(*storage, WriteKey(_, _, _))
        .WillOnce(InvokeWriteCallbackSuccess());
  }

  //
  // Test state maintained for every test.
  //

  // Configuration for the protocol handler (uses defaults).
  InvalidationClientImpl::Config config;

  // The client being tested. Created fresh for each test function.
  scoped_ptr<InvalidationClientImpl> client;

  // A mock invalidation listener.
  StrictMock<MockInvalidationListener> listener;
};

// Starts the ticl and checks that appropriate calls are made on the listener
// and that a proper message is sent on the network.
TEST_F(InvalidationClientImplTest, Start) {
  vector<string> outgoing_messages;
  SetExpectationsForTiclStart(1, outgoing_messages);
  StartClient(outgoing_messages);
}

// Starts the Ticl, registers for a few objects, gets success and ensures that
// the right listener methods are invoked.
TEST_F(InvalidationClientImplTest, Register) {
  vector<string> outgoing_messages;
  SetExpectationsForTiclStart(2, outgoing_messages);

  // Set some expectations for registration status messages.
  vector<ObjectId> saved_oids;
  EXPECT_CALL(listener,
              InformRegistrationStatus(Eq(client.get()), _,
                                       InvalidationListener::REGISTERED))
      .Times(3)
      .WillRepeatedly(SaveArgToVector<1>(&saved_oids));

  // Start the Ticl.
  StartClient(outgoing_messages);

  // Synthesize a few test object ids.
  int num_objects = 3;
  vector<ObjectIdP> oid_protos;
  vector<ObjectId> oids;
  InitTestObjectIds(num_objects, &oid_protos);
  ConvertFromObjectIdProtos(oid_protos, &oids);

  // Register
  client.get()->Register(oids);

  // Let the message be sent out.
  internal_scheduler->PassTime(
      GetMaxBatchingDelay(config.protocol_handler_config));

  // Give a registration status message to the protocol handler and wait for
  // the listener calls.
  ServerToClientMessage message;
  InitServerHeader(client.get()->GetClientToken(), message.mutable_header());
  vector<RegistrationStatus> registration_statuses;
  MakeRegistrationStatusesFromObjectIds(oid_protos, true, true,
                                        &registration_statuses);
  for (int i = 0; i < num_objects; ++i) {
    message.mutable_registration_status_message()
        ->add_registration_status()->CopyFrom(registration_statuses[i]);
  }

  // Give this message to the protocol handler.
  ProcessIncomingMessage(message, EndOfTestWaitTime());

  // Check the object ids.
  ASSERT_TRUE(CompareVectorsAsSets(saved_oids, oids));

  // Check the registration message.
  ClientToServerMessage client_msg;
  client_msg.ParseFromString(outgoing_messages[1]);
  ASSERT_TRUE(client_msg.has_registration_message());

  RegistrationMessage expected_msg;
  InitRegistrationMessage(oid_protos, true, &expected_msg);
  const RegistrationMessage& actual_msg = client_msg.registration_message();
  ASSERT_TRUE(CompareMessages(expected_msg, actual_msg));
}

// Tests that given invalidations from the server, the right listener methods
// are invoked. Ack the invalidations and make sure that the ack message is
// sent out.
TEST_F(InvalidationClientImplTest, Invalidations) {
    // Set some expectations for starting the client.
  vector<string> outgoing_messages;
  SetExpectationsForTiclStart(2, outgoing_messages);

  // Synthesize a few test object ids.
  int num_objects = 3;
  vector<ObjectIdP> oid_protos;
  vector<ObjectId> oids;
  InitTestObjectIds(num_objects, &oid_protos);
  ConvertFromObjectIdProtos(oid_protos, &oids);

  // Set up listener invalidation calls.
  vector<InvalidationP> invalidations;
  vector<Invalidation> expected_invs;
  MakeInvalidationsFromObjectIds(oid_protos, &invalidations);
  ConvertFromInvalidationProtos(invalidations, &expected_invs);

  // Set up expectations for the acks.
  vector<Invalidation> saved_invs;
  vector<AckHandle> ack_handles;

  EXPECT_CALL(listener, Invalidate(Eq(client.get()), _, _))
      .Times(3)
      .WillRepeatedly(DoAll(SaveArgToVector<1>(&saved_invs),
                            SaveArgToVector<2>(&ack_handles)));

  // Start the Ticl.
  StartClient(outgoing_messages);

  // Give this message to the protocol handler.
  ServerToClientMessage message;
  InitServerHeader(client.get()->GetClientToken(), message.mutable_header());
  InitInvalidationMessage(invalidations,
      message.mutable_invalidation_message());

  // Process the incoming invalidation message.
  ProcessIncomingMessage(message, MessageHandlingDelay());

  // Check the invalidations.
  ASSERT_TRUE(CompareVectorsAsSets(expected_invs, saved_invs));

  // Ack the invalidations now and wait for them to be sent out.
  for (int i = 0; i < num_objects; i++) {
    client.get()->Acknowledge(ack_handles[i]);
  }
  internal_scheduler->PassTime(
      GetMaxBatchingDelay(config.protocol_handler_config));

  // Check that the ack message is as expected.
  ClientToServerMessage client_msg;
  client_msg.ParseFromString(outgoing_messages[1]);
  ASSERT_TRUE(client_msg.has_invalidation_ack_message());

  InvalidationMessage expected_msg;
  InitInvalidationMessage(invalidations, &expected_msg);
  const InvalidationMessage& actual_msg =
      client_msg.invalidation_ack_message();
  ASSERT_TRUE(CompareMessages(expected_msg, actual_msg));
}

// Give a registration sync request message and an info request message to the
// client and wait for the sync message and the info message to go out.
TEST_F(InvalidationClientImplTest, ServerRequests) {
  // Set some expectations for starting the client.
  vector<string> outgoing_messages;
  SetExpectationsForTiclStart(2, outgoing_messages);

  // Start the ticl.
  StartClient(outgoing_messages);

  // Make the server to client message.
  ServerToClientMessage message;
  InitServerHeader(client.get()->GetClientToken(), message.mutable_header());

  // Add a registration sync request message.
  message.mutable_registration_sync_request_message();

  // Add an info request message.
  message.mutable_info_request_message()->add_info_type(
      InfoRequestMessage_InfoType_GET_PERFORMANCE_COUNTERS);

  // Give it to the prototol handler.
  ProcessIncomingMessage(message, EndOfTestWaitTime());

  // Make sure that the message is as expected.
  ClientToServerMessage client_msg;
  client_msg.ParseFromString(outgoing_messages[1]);
  ASSERT_TRUE(client_msg.has_info_message());
  ASSERT_TRUE(client_msg.has_registration_sync_message());
}

// Tests that an incoming unknown failure message results in the app being
// informed about it.
TEST_F(InvalidationClientImplTest, IncomingErrorMessage) {
  vector<string> outgoing_messages;
  SetExpectationsForTiclStart(1, outgoing_messages);

  // Set up listener expectation for error.
  EXPECT_CALL(listener, InformError(Eq(client.get()), _));

  // Start the ticl.
  StartClient(outgoing_messages);

  // Give the error message to the protocol handler.
  ServerToClientMessage message;
  InitServerHeader(client.get()->GetClientToken(), message.mutable_header());
  InitErrorMessage(ErrorMessage_Code_UNKNOWN_FAILURE, "Some error message",
      message.mutable_error_message());
  ProcessIncomingMessage(message, EndOfTestWaitTime());
}

// Tests that an incoming auth failure message results in the app being informed
// about it and the registrations being removed.
TEST_F(InvalidationClientImplTest, IncomingAuthErrorMessage) {
  vector<string> outgoing_messages;
  SetExpectationsForTiclStart(2, outgoing_messages);

  // One object to register for.
  int num_objects = 1;
  vector<ObjectIdP> oid_protos;
  vector<ObjectId> oids;
  InitTestObjectIds(num_objects, &oid_protos);
  ConvertFromObjectIdProtos(oid_protos, &oids);

  // Expect success for the registration below since the client calls
  // immediately with success.
  EXPECT_CALL(listener, InformRegistrationStatus(Eq(client.get()), Eq(oids[0]),
      InvalidationListener::REGISTERED));

  // Expect error and registration failure from the ticl + a schedule for
  // ticl.stop.
  EXPECT_CALL(listener, InformError(Eq(client.get()), _));
  EXPECT_CALL(listener, InformRegistrationFailure(Eq(client.get()), Eq(oids[0]),
      Eq(false), _));

  // Start the client.
  StartClient(outgoing_messages);

  // Register and let the message be sent out.
  client.get()->Register(oids[0]);
  internal_scheduler->PassTime(
      GetMaxBatchingDelay(config.protocol_handler_config));

  // Give this message to the protocol handler.
  ServerToClientMessage message;
  InitServerHeader(client.get()->GetClientToken(), message.mutable_header());
  InitErrorMessage(ErrorMessage_Code_AUTH_FAILURE, "Auth error message",
      message.mutable_error_message());
  ProcessIncomingMessage(message, EndOfTestWaitTime());
}

// Tests that a registration that times out results in a reg sync message being
// sent out.
TEST_F(InvalidationClientImplTest, NetworkTimeouts) {
  // Set some expectations for starting the client.
  vector<string> outgoing_messages;
  SetExpectationsForTiclStart(3, outgoing_messages);

  // One object to register for.
  int num_objects = 1;
  vector<ObjectIdP> oid_protos;
  vector<ObjectId> oids;
  InitTestObjectIds(num_objects, &oid_protos);
  ConvertFromObjectIdProtos(oid_protos, &oids);

  // Expect success for the registration below since the client calls
  // immediately with success.
  EXPECT_CALL(listener, InformRegistrationStatus(Eq(client.get()), Eq(oids[0]),
      InvalidationListener::REGISTERED));

  // Start the client.
  StartClient(outgoing_messages);

  // Register for an object.
  client.get()->Register(oids[0]);

  // Let the registration message be sent out.
  internal_scheduler->PassTime(
      GetMaxBatchingDelay(config.protocol_handler_config));

  // Now let the network timeout occur and an info message be sent.
  TimeDelta timeout_delay = GetMaxDelay(config.network_timeout_delay);
  internal_scheduler->PassTime(timeout_delay);

  // Check that the message sent out is an info message asking for the server's
  // summary.
  ClientToServerMessage client_msg2;
  client_msg2.ParseFromString(outgoing_messages[2]);
  ASSERT_TRUE(client_msg2.has_info_message());
  ASSERT_TRUE(
      client_msg2.info_message().server_registration_summary_requested());
  internal_scheduler->PassTime(EndOfTestWaitTime());
}

// Tests that heartbeats are sent out as time advances.
TEST_F(InvalidationClientImplTest, Heartbeats) {
  // Set some expectations for starting the client.
  vector<string> outgoing_messages;
  SetExpectationsForTiclStart(2, outgoing_messages);

  // Start the client.
  StartClient(outgoing_messages);

  // Now let the heartbeat occur and an info message be sent.
  TimeDelta heartbeat_delay = GetMaxDelay(config.heartbeat_interval);
  internal_scheduler->PassTime(heartbeat_delay);

  // Check that the heartbeat is sent and it does not ask for the server's
  // summary.
  ClientToServerMessage client_msg1;
  client_msg1.ParseFromString(outgoing_messages[1]);
  ASSERT_TRUE(client_msg1.has_info_message());
  ASSERT_FALSE(
      client_msg1.info_message().server_registration_summary_requested());
  internal_scheduler->PassTime(EndOfTestWaitTime());
}

}  // namespace invalidation
