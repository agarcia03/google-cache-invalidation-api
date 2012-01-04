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

#include "google/cacheinvalidation/gmock.h"
#include "google/cacheinvalidation/googletest.h"
#include "google/cacheinvalidation/v2/string_util.h"
#include "google/cacheinvalidation/v2/types.pb.h"
#include "google/cacheinvalidation/v2/basic-system-resources.h"
#include "google/cacheinvalidation/v2/constants.h"
#include "google/cacheinvalidation/v2/protocol-handler.h"
#include "google/cacheinvalidation/v2/statistics.h"
#include "google/cacheinvalidation/v2/test/deterministic-scheduler.h"
#include "google/cacheinvalidation/v2/test/test-logger.h"
#include "google/cacheinvalidation/v2/throttle.h"
#include "google/cacheinvalidation/v2/ticl-message-validator.h"

namespace invalidation {

using ::ipc::invalidation::ClientType_Type_TEST;
using ::ipc::invalidation::ObjectSource_Type_TEST;
using ::testing::_;
using ::testing::DeleteArg;
using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::EqualsProto;
using ::testing::Eq;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::SaveArg;
using ::testing::SetArgPointee;
using ::testing::StrictMock;
using ::testing::proto::Partially;
using ::testing::proto::WhenDeserialized;

// A mock of the Scheduler interface.
class MockScheduler : public Scheduler {
 public:
  MOCK_METHOD2(Schedule, void(TimeDelta, Closure*));  // NOLINT
  MOCK_CONST_METHOD0(IsRunningOnThread, bool());
  MOCK_CONST_METHOD0(GetCurrentTime, Time());
  MOCK_METHOD1(SetSystemResources, void(SystemResources*));  // NOLINT
};

// A mock of the Network interface.
class MockNetwork : public NetworkChannel {
 public:
  MOCK_METHOD1(SendMessage, void(const string&));  // NOLINT
  MOCK_METHOD1(SetMessageReceiver, void(MessageCallback*));  // NOLINT
  MOCK_METHOD1(AddNetworkStatusReceiver, void(NetworkStatusCallback*));  // NOLINT
  MOCK_METHOD1(SetSystemResources, void(SystemResources*));  // NOLINT
};

// A mock of the Storage interface.
class MockStorage : public Storage {
 public:
  MOCK_METHOD3(WriteKey, void(const string&, const string&, WriteKeyCallback*));  // NOLINT
  MOCK_METHOD2(ReadKey, void(const string&, ReadKeyCallback*));  // NOLINT
  MOCK_METHOD2(DeleteKey, void(const string&, Callback1<bool>*));  // NOLINT
  MOCK_METHOD1(ReadAllKeys, void(Callback1<pair<Status, string> >*));  // NOLINT
  MOCK_METHOD1(SetSystemResources, void(SystemResources*));  // NOLINT
};

// A mock of the ProtocolListener interface.
class MockProtocolListener : public ProtocolListener {
 public:
  MOCK_METHOD1(HandleIncomingHeader, void(const ServerMessageHeader&));  // NOLINT

  MOCK_METHOD2(HandleTokenChanged,
               void(const ServerMessageHeader&, const string&));  // NOLINT

  MOCK_METHOD2(
      HandleInvalidations,
      void(const ServerMessageHeader&, const RepeatedPtrField<InvalidationP>&));  // NOLINT

  MOCK_METHOD2(
      HandleRegistrationStatus,
      void(const ServerMessageHeader&,
           const RepeatedPtrField<RegistrationStatus>&));  // NOLINT

  MOCK_METHOD1(HandleRegistrationSyncRequest, void(const ServerMessageHeader&));  // NOLINT

  MOCK_METHOD2(HandleInfoMessage,
               void(const ServerMessageHeader&,
                    const RepeatedField<InfoRequestMessage_InfoType>&));  // NOLINT

  MOCK_METHOD3(
      HandleErrorMessage,
      void(const ServerMessageHeader&, const ErrorMessage::Code,
           const string&));  // NOLINT

  MOCK_METHOD1(GetRegistrationSummary, void(RegistrationSummary*));  // NOLINT

  MOCK_METHOD0(GetClientToken, string());
};

// Tests the basic functionality of the protocol handler.
class ProtocolHandlerTest : public testing::Test {
 public:
  virtual ~ProtocolHandlerTest() {}

  // Performs setup for protocol handler unit tests, e.g. creating resource
  // components and setting up common expectations for certain mock objects.
  virtual void SetUp() {
    // Start time at an arbitrary point, just to make sure we don't depend on it
    // being 0.
    start_time = Time() + TimeDelta::FromDays(5742);

    InitSystemResources();  // Set up system resources
    InitCommonExpectations();  // Set up expectations for common mock operations

    message_callback = NULL;
    validator.reset(new TiclMessageValidator(logger));  // Create msg validator

    // Create the protocol handler object.
    protocol_handler.reset(
        new ProtocolHandler(
            config, resources.get(), &statistics, "unit-test", &listener,
            validator.get()));

    // Start the scheduler and resources.
    internal_scheduler->StartScheduler();
    resources->Start();
  }

  virtual void TearDown() {
    CHECK(message_callback != NULL);
    delete message_callback;
    message_callback = NULL;
  }

  // The maximum amount by which smearing can increase a configuration
  // parameter.
  static const int kMaxSmearMultiplier = 2;

  // When "waiting" at the end of a test to make sure nothing happens, how long
  // to wait.
  static TimeDelta EndOfTestWaitTime() {
    return TimeDelta::FromSeconds(5);
  }

  // Initializes |protocol_version| to the current protocol version.
  static void InitProtocolVersion(ProtocolVersion* protocol_version) {
    Version* version = protocol_version->mutable_version();
    version->set_major_version(Constants::kProtocolMajorVersion);
    version->set_minor_version(Constants::kProtocolMinorVersion);
  }

  // Initializes |client_version| to the current client version.
  static void InitClientVersion(ClientVersion* client_version) {
    Version* version = client_version->mutable_version();
    version->set_major_version(Constants::kClientMajorVersion);
    version->set_minor_version(Constants::kClientMinorVersion);
    client_version->set_platform("unit-test");
    client_version->set_language("C++");
    client_version->set_application_info("unit-test");
  }

  // Initializes |summary| with a fake registration summary.  The validity of
  // the contents of the summary are unimportant to the protocol handler, so
  // it is ok to do this.
  static void InitFakeRegistrationSummary(RegistrationSummary* summary) {
    summary->set_num_registrations(4);
    summary->set_registration_digest("bogus digest");
  }

  // Populates |object_ids| with |count| object ids in the TEST id space, each
  // named oid<n>.
  static void InitTestObjectIds(vector<ObjectIdP>* object_ids, int count) {
    for (int i = 0; i < count; ++i) {
      ObjectIdP object_id;
      object_id.set_source(ObjectSource_Type_TEST);
      object_id.set_name(StringPrintf("oid%d", i));
      object_ids->push_back(object_id);
    }
  }

  // For each object id in |object_ids|, adds an invalidation to |invalidations|
  // for that object at an arbitrary version.
  static void MakeInvalidationsFromObjectIds(
      const vector<ObjectIdP>& object_ids,
      vector<InvalidationP>* invalidations) {
    for (int i = 0; i < object_ids.size(); ++i) {
      InvalidationP invalidation;
      invalidation.mutable_object_id()->CopyFrom(object_ids[i]);
      invalidation.set_is_known_version(true);

      // Pick an arbitrary version number; it shouldn't really matter, but we
      // try not to make them correlated too much with the object name.
      invalidation.set_version(100 + ((i * 19) % 31));
      invalidations->push_back(invalidation);
    }
  }

  // For each object in |object_ids|, makes a SUCCESSful registration status for
  // that object, alternating between REGISTER and UNREGISTER.  The precise
  // contents of these messages are unimportant to the protocol handler; we just
  // need them to pass the message validator.
  static void MakeRegistrationStatusesFromObjectIds(
      const vector<ObjectIdP>& object_ids,
      vector<RegistrationStatus>* registration_statuses) {
    for (int i = 0; i < object_ids.size(); ++i) {
      RegistrationStatus registration_status;
      registration_status.mutable_registration()->mutable_object_id()->CopyFrom(
          object_ids[i]);
      registration_status.mutable_registration()->set_op_type(
          (i % 2 == 0) ? RegistrationP_OpType_REGISTER
              : RegistrationP_OpType_UNREGISTER);
      registration_status.mutable_status()->set_code(StatusP_Code_SUCCESS);
      registration_statuses->push_back(registration_status);
    }
  }

  // Initializes a server header with the given token.
  static void InitServerHeader(const string& token, ServerHeader* header) {
    InitProtocolVersion(header->mutable_protocol_version());
    header->set_client_token(token);

    // Use fake registration summary, since it doesn't matter.
    InitFakeRegistrationSummary(header->mutable_registration_summary());

    // Use arbitrary server time and message id, since they don't matter.
    header->set_server_time_ms(314159265);
    header->set_message_id("message-id-for-test");
  }

 private:
  // Initializes the basic system resources used by the protocol handler, using
  // mocks for various components.
  void InitSystemResources() {
    // Use a deterministic scheduler for the protocol handler's internals, since
    // we want precise control over when batching intervals expire.
    internal_scheduler = new DeterministicScheduler();
    internal_scheduler->SetInitialTime(start_time);

    // Use a strict mock scheduler for the listener, since it shouldn't be used
    // at all by the protocol handler.
    listener_scheduler = new StrictMock<MockScheduler>();

    // Use a mock network to let us trap the protocol handler's message receiver
    // and its attempts to send messages.
    network = new StrictMock<MockNetwork>();
    logger = new TestLogger();

    // Storage shouldn't be used by the protocol handler, so use a strict mock
    // to catch any accidental calls.
    storage = new StrictMock<MockStorage>();

    // The BasicSystemResources will set itself in the components.
    EXPECT_CALL(*listener_scheduler, SetSystemResources(_));
    EXPECT_CALL(*network, SetSystemResources(_));
    EXPECT_CALL(*storage, SetSystemResources(_));

    resources.reset(
        new BasicSystemResources(
            logger, internal_scheduler, listener_scheduler, network, storage,
            "unit-test"));
  }

  void InitCommonExpectations() {
    // When we construct the protocol handler, it will set a message receiver on
    // the network.  Intercept the call and save the callback.
    EXPECT_CALL(*network, SetMessageReceiver(_))
        .WillOnce(SaveArg<0>(&message_callback));

    // It will also add a network status receiver.  The network channel takes
    // ownership.  Invoke it once with |true| just to exercise that code path,
    // then delete it since we won't need it anymore.
    EXPECT_CALL(*network, AddNetworkStatusReceiver(_))
        .WillOnce(DoAll(InvokeArgument<0>(true), DeleteArg<0>()));

    // When the handler asks the listener for the client token, return whatever
    // |token| currently is.
    EXPECT_CALL(listener, GetClientToken())
        .WillRepeatedly(ReturnPointee(&token));

    // If the handler asks the listener for a registration summary, respond by
    // supplying a fake summary.
    InitFakeRegistrationSummary(&summary);
    EXPECT_CALL(listener, GetRegistrationSummary(_))
        .WillRepeatedly(SetArgPointee<0>(summary));
  }

 public:
  // The time at which the test started.  Initialized to an arbitrary value to
  // ensure that we don't depend on it starting at 0.
  Time start_time;

  // Configuration for the protocol handler (uses defaults).
  ProtocolHandler::Config config;

  // The protocol handler being tested.  Created fresh for each test function.
  scoped_ptr<ProtocolHandler> protocol_handler;

  // Components of BasicSystemResources.  It takes ownership of all of these,
  // and its destructor deletes them, so we need to create fresh ones for each
  // test.

  // Use a deterministic scheduler for the protocol handler's internals, since
  // we want precise control over when batching intervals expire.
  DeterministicScheduler* internal_scheduler;

  // Use a strict mock scheduler for the listener, since it shouldn't be used
  // at all by the protocol handler.  This lets us catch any unintended use.
  MockScheduler* listener_scheduler;

  // Use a mock network to let us trap the protocol handler's message receiver
  // and its attempts to send messages.
  MockNetwork* network;

  // A logger.
  Logger* logger;

  // Storage shouldn't be used by the protocol handler, so use a strict mock to
  // catch any accidental calls.
  MockStorage* storage;

  // System resources (owned by the test).
  scoped_ptr<BasicSystemResources> resources;

  // Statistics object for counting occurrences of different types of events.
  Statistics statistics;

  // A mock protocol listener.  We make this strict in order to have tight
  // control over the interactions between this and the protocol handler.
  // SetUp() installs expectations to allow GetClientToken() and
  // GetRegistrationSummary() to be called any time and to give them
  // reasonable behavior.
  StrictMock<MockProtocolListener> listener;

  // Ticl message validator.  We do not mock this, since the correctness of the
  // protocol handler depends on it.
  scoped_ptr<TiclMessageValidator> validator;

  // Message callback installed by the protocol handler.  Captured by the mock
  // network.
  MessageCallback* message_callback;

  // Fake token and registration summary for the mock listener to return when
  // the protocol handler requests them.
  string token;
  RegistrationSummary summary;
};

// Asks the protocol handler to send an initialize message.  Waits for the
// batching delay to pass.  Checks that appropriate calls are made on the
// listener and that a proper message is sent on the network.
TEST_F(ProtocolHandlerTest, SendInitializeOnly) {
  ApplicationClientIdP app_client_id;
  app_client_id.set_client_name("unit-test-client-id");

  // Client's token is initially empty.  Give it an arbitrary nonce.
  token = "";
  string nonce = "unit-test-nonce";

  // SendInitializeMessage checks that it's running on the work queue thread, so
  // we need to schedule the call.
  internal_scheduler->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          protocol_handler.get(), &ProtocolHandler::SendInitializeMessage,
          ClientType_Type_TEST, app_client_id, nonce, "Startup"));

  ClientToServerMessage expected_message;

  // Build the header.
  ClientHeader* header = expected_message.mutable_header();
  InitProtocolVersion(header->mutable_protocol_version());
  header->mutable_registration_summary()->CopyFrom(summary);
  header->set_max_known_server_time_ms(0);
  header->set_message_id("1");

  // Note: because the batching task is smeared, we don't know what the client's
  // timestamp will be.  We omit it from this proto and do a partial match in
  // the EXPECT_CALL but also save the proto and check later that it doesn't
  // contain anything we don't expect.

  // Create the expected initialize message.
  InitializeMessage* initialize_message =
      expected_message.mutable_initialize_message();
  initialize_message->set_client_type(ClientType_Type_TEST);
  initialize_message->set_nonce(nonce);
  initialize_message->mutable_application_client_id()->CopyFrom(app_client_id);
  initialize_message->set_digest_serialization_type(
      InitializeMessage_DigestSerializationType_BYTE_BASED);

  string actual_serialized;
  EXPECT_CALL(
      *network,
      SendMessage(WhenDeserialized(Partially(EqualsProto(expected_message)))))
      .WillOnce(SaveArg<0>(&actual_serialized));

  // The actual message won't be sent until after the batching delay, which is
  // smeared, so double it to be sure enough time will have passed.
  TimeDelta wait_time = config.batching_delay * kMaxSmearMultiplier;
  internal_scheduler->PassTime(wait_time);

  // By now we expect the message to have been sent, so we'll deserialize it
  // and check that it doesn't have anything we don't expect.
  ClientToServerMessage actual_message;
  actual_message.ParseFromString(actual_serialized);
  ASSERT_FALSE(actual_message.has_info_message());
  ASSERT_FALSE(actual_message.has_invalidation_ack_message());
  ASSERT_FALSE(actual_message.has_registration_message());
  ASSERT_FALSE(actual_message.has_registration_sync_message());
  ASSERT_GE(actual_message.header().client_time_ms(),
            InvalidationClientUtil::GetTimeInMillis(start_time));
  ASSERT_LE(actual_message.header().client_time_ms(),
            InvalidationClientUtil::GetTimeInMillis(start_time + wait_time));
}

// Tests the receipt of a token control message like what we'd expect in
// response to an initialize message.  Check that appropriate calls are made on
// the protocol listener.
TEST_F(ProtocolHandlerTest, ReceiveTokenControlOnly) {
  ServerToClientMessage message;
  ServerHeader* header = message.mutable_header();
  string nonce = "fake nonce";
  InitServerHeader(nonce, header);

  string new_token = "new token";
  message.mutable_token_control_message()->set_new_token(new_token);

  string serialized;
  message.SerializeToString(&serialized);

  ServerMessageHeader expected_header(nonce, header->registration_summary());
  EXPECT_CALL(listener, HandleTokenChanged(Eq(expected_header), Eq(new_token)));

  message_callback->Run(serialized);
  internal_scheduler->PassTime(EndOfTestWaitTime());
}

// Test that the protocol handler correctly buffers multiple message types.
// Tell it to send registrations, then unregistrations (with some overlap in the
// sets of objects).  Then send some invalidation acks and finally a
// registration subtree.  Wait for the batching interval to pass, and then check
// that the message sent out contains everything we expect.
TEST_F(ProtocolHandlerTest, SendMultipleMessageTypes) {
  // Concoct some performance counters and config parameters, and ask to send
  // an info message with them.
  vector<pair<string, int> > perf_counters;
  vector<pair<string, int> > config_params;
  perf_counters.push_back(make_pair("x", 3));
  perf_counters.push_back(make_pair("y", 81));
  config_params.push_back(make_pair("z", 2));
  config_params.push_back(make_pair("aa", 55));

  internal_scheduler->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          protocol_handler.get(), &ProtocolHandler::SendInfoMessage,
          perf_counters, config_params, true));

  // Synthesize a few test object ids.
  vector<ObjectIdP> oids;
  InitTestObjectIds(&oids, 3);

  // Register for the first two.
  vector<ObjectIdP> oid_vec;
  oid_vec.push_back(oids[0]);
  oid_vec.push_back(oids[1]);

  internal_scheduler->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          protocol_handler.get(), &ProtocolHandler::SendRegistrations,
          oid_vec, RegistrationP_OpType_REGISTER));

  // Then unregister for the second and third.  This overrides the registration
  // on oids[1].
  oid_vec.clear();
  oid_vec.push_back(oids[1]);
  oid_vec.push_back(oids[2]);
  internal_scheduler->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          protocol_handler.get(), &ProtocolHandler::SendRegistrations,
          oid_vec, RegistrationP_OpType_UNREGISTER));

  // Send a couple of invalidations.
  vector<InvalidationP> invalidations;
  MakeInvalidationsFromObjectIds(oids, &invalidations);
  for (int i = 0; i < 2; ++i) {
    internal_scheduler->Schedule(
        Scheduler::NoDelay(),
        NewPermanentCallback(
            protocol_handler.get(), &ProtocolHandler::SendInvalidationAck,
            invalidations[i]));
  }

  // Send a simple registration subtree.
  RegistrationSubtree subtree;
  subtree.add_registered_object()->CopyFrom(oids[0]);
  internal_scheduler->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          protocol_handler.get(), &ProtocolHandler::SendRegistrationSyncSubtree,
          subtree));

  token = "test token";

  // The message it sends should contain all of the expected information:
  ClientToServerMessage expected_message;

  // Header.
  ClientHeader* header = expected_message.mutable_header();
  InitProtocolVersion(header->mutable_protocol_version());
  header->mutable_registration_summary()->CopyFrom(summary);
  header->set_client_token(token);
  header->set_max_known_server_time_ms(0);
  header->set_message_id("1");

  // Note: because the batching task is smeared, we don't know what the client's
  // timestamp will be.  We omit it from this proto and do a partial match in
  // the EXPECT_CALL but also save the proto and check later that it doesn't
  // contain anything we don't expect.

  // Registrations.
  RegistrationMessage* reg_message =
      expected_message.mutable_registration_message();
  RegistrationP* registration;
  registration = reg_message->add_registration();
  registration->mutable_object_id()->CopyFrom(oids[0]);
  registration->set_op_type(RegistrationP_OpType_REGISTER);

  registration = reg_message->add_registration();
  registration->mutable_object_id()->CopyFrom(oids[1]);
  registration->set_op_type(RegistrationP_OpType_UNREGISTER);

  registration = reg_message->add_registration();
  registration->mutable_object_id()->CopyFrom(oids[2]);
  registration->set_op_type(RegistrationP_OpType_UNREGISTER);

  // Registration sync message.
  expected_message.mutable_registration_sync_message()->add_subtree()
      ->CopyFrom(subtree);

  // Invalidation acks.
  InvalidationMessage* invalidation_message =
      expected_message.mutable_invalidation_ack_message();
  for (int i = 0; i < 2; ++i) {
    invalidation_message->add_invalidation()->CopyFrom(invalidations[i]);
  }

  // Info message.
  InfoMessage* info_message = expected_message.mutable_info_message();
  InitClientVersion(info_message->mutable_client_version());
  info_message->set_server_registration_summary_requested(true);
  PropertyRecord* prop_rec;
  for (int i = 0; i < config_params.size(); ++i) {
    prop_rec = info_message->add_config_parameter();
    prop_rec->set_name(config_params[i].first);
    prop_rec->set_value(config_params[i].second);
  }
  for (int i = 0; i < perf_counters.size(); ++i) {
    prop_rec = info_message->add_performance_counter();
    prop_rec->set_name(perf_counters[i].first);
    prop_rec->set_value(perf_counters[i].second);
  }

  string actual_serialized;
  EXPECT_CALL(
      *network,
      SendMessage(WhenDeserialized(Partially(EqualsProto(expected_message)))))
      .WillOnce(SaveArg<0>(&actual_serialized));

  TimeDelta wait_time = config.batching_delay * kMaxSmearMultiplier;
  internal_scheduler->PassTime(wait_time);

  ClientToServerMessage actual_message;
  actual_message.ParseFromString(actual_serialized);

  ASSERT_FALSE(actual_message.has_initialize_message());
  ASSERT_GE(actual_message.header().client_time_ms(),
            InvalidationClientUtil::GetTimeInMillis(start_time));
  ASSERT_LE(actual_message.header().client_time_ms(),
            InvalidationClientUtil::GetTimeInMillis(start_time + wait_time));
}

// Check that if the protocol handler receives a message with several sub-
// messages set, it makes all the appropriate calls on the listener.
TEST_F(ProtocolHandlerTest, IncomingCompositeMessage) {
  // Build up a message with a number of sub-messages in it:
  ServerToClientMessage message;

  // First the header.
  token = "test token";
  InitServerHeader(token, message.mutable_header());

  // Fabricate a few object ids for use in invalidations and registration
  // statuses.
  vector<ObjectIdP> object_ids;
  InitTestObjectIds(&object_ids, 3);

  // Add invalidations.
  vector<InvalidationP> invalidations;
  MakeInvalidationsFromObjectIds(object_ids, &invalidations);
  for (int i = 0; i < 3; ++i) {
    message.mutable_invalidation_message()->add_invalidation()->CopyFrom(
        invalidations[i]);
  }

  // Add registration statuses.
  vector<RegistrationStatus> registration_statuses;
  MakeRegistrationStatusesFromObjectIds(object_ids, &registration_statuses);
  for (int i = 0; i < 3; ++i) {
    message.mutable_registration_status_message()
        ->add_registration_status()->CopyFrom(registration_statuses[i]);
  }

  // Add a registration sync request message.
  message.mutable_registration_sync_request_message();

  // Add an info request message.
  message.mutable_info_request_message()->add_info_type(
      InfoRequestMessage_InfoType_GET_PERFORMANCE_COUNTERS);

  string serialized;
  message.SerializeToString(&serialized);

  // The header we expect the listener to be called with.
  ServerMessageHeader expected_header(token, summary);

  // Listener should get each of the following calls:

  // Incoming header.
  EXPECT_CALL(listener, HandleIncomingHeader(Eq(expected_header)));

  // Invalidations.
  EXPECT_CALL(
      listener,
      HandleInvalidations(
          Eq(expected_header),
          ElementsAre(EqualsProto(invalidations[0]),
                      EqualsProto(invalidations[1]),
                      EqualsProto(invalidations[2]))));

  // Registration statuses.
  EXPECT_CALL(
      listener,
      HandleRegistrationStatus(
          Eq(expected_header),
          ElementsAre(EqualsProto(registration_statuses[0]),
                      EqualsProto(registration_statuses[1]),
                      EqualsProto(registration_statuses[2]))));

  // Registration sync request.
  EXPECT_CALL(listener, HandleRegistrationSyncRequest(Eq(expected_header)));

  // Info request message.
  EXPECT_CALL(
      listener,
      HandleInfoMessage(
          Eq(expected_header),
          ElementsAre(
              Eq(InfoRequestMessage_InfoType_GET_PERFORMANCE_COUNTERS))));

  message_callback->Run(serialized);
  internal_scheduler->PassTime(EndOfTestWaitTime());
}

// Test that the protocol handler drops an invalid message.
TEST_F(ProtocolHandlerTest, InvalidInboundMessage) {
  // Make an invalid message (omit protocol version from header).
  ServerToClientMessage message;
  string token = "test token";
  ServerHeader* header = message.mutable_header();
  InitServerHeader(token, header);
  header->clear_protocol_version();

  // Add an info request message to check that it doesn't get processed.
  message.mutable_info_request_message()->add_info_type(
      InfoRequestMessage_InfoType_GET_PERFORMANCE_COUNTERS);

  string serialized;
  message.SerializeToString(&serialized);
  message_callback->Run(serialized);
  internal_scheduler->PassTime(EndOfTestWaitTime());

  ASSERT_EQ(1, statistics.GetClientErrorCounterForTest(
      Statistics::ClientErrorType_INCOMING_MESSAGE_FAILURE));
}

// Test that the protocol handler drops a message whose major version doesn't
// match what it understands.
TEST_F(ProtocolHandlerTest, MajorVersionMismatch) {
  // Make a message with a different protocol major version.
  ServerToClientMessage message;
  token = "test token";
  ServerHeader* header = message.mutable_header();
  InitServerHeader(token, header);
  header->mutable_protocol_version()->mutable_version()->set_major_version(1);
  header->mutable_protocol_version()->mutable_version()->set_minor_version(4);

  // Add an info request message to check that it doesn't get processed.
  message.mutable_info_request_message()->add_info_type(
      InfoRequestMessage_InfoType_GET_PERFORMANCE_COUNTERS);

  string serialized;
  message.SerializeToString(&serialized);
  message_callback->Run(serialized);
  internal_scheduler->PassTime(EndOfTestWaitTime());

  ASSERT_EQ(1, statistics.GetClientErrorCounterForTest(
      Statistics::ClientErrorType_PROTOCOL_VERSION_FAILURE));
}

// Test that the protocol handler doesn't drop a message whose minor version
// doesn't match what it understands.
TEST_F(ProtocolHandlerTest, MinorVersionMismatch) {
  // Make a message with a different protocol minor version.
  ServerToClientMessage message;
  token = "test token";
  InitServerHeader(token, message.mutable_header());

  ServerMessageHeader expected_header(token, summary);
  EXPECT_CALL(listener, HandleIncomingHeader(Eq(expected_header)));

  string serialized;
  message.SerializeToString(&serialized);
  message_callback->Run(serialized);
  internal_scheduler->PassTime(EndOfTestWaitTime());

  ASSERT_EQ(0, statistics.GetClientErrorCounterForTest(
      Statistics::ClientErrorType_PROTOCOL_VERSION_FAILURE));
}

// Test that the protocol handler honors a config message (even if the server
// token doesn't match) and does not call any listener methods.
TEST_F(ProtocolHandlerTest, ConfigMessage) {
  // Fabricate a config message.
  ServerToClientMessage message;
  token = "test token";
  InitServerHeader(token, message.mutable_header());
  token = "token-that-should-mismatch";

  int next_message_delay_ms = 2000000;
  message.mutable_config_change_message()->set_next_message_delay_ms(
      next_message_delay_ms);

  string serialized;
  message.SerializeToString(&serialized);
  message_callback->Run(serialized);
  internal_scheduler->PassTime(TimeDelta());

  // Check that the protocol handler recorded receiving the config change
  // message, and that it has updated the next time it will send a message.
  ASSERT_EQ(1, statistics.GetReceivedMessageCounterForTest(
      Statistics::ReceivedMessageType_CONFIG_CHANGE));
  ASSERT_EQ(
      InvalidationClientUtil::GetTimeInMillis(
          start_time + TimeDelta::FromMilliseconds(next_message_delay_ms)),
      protocol_handler->GetNextMessageSendTimeMsForTest());

  // Request to send an info message, and check that it doesn't get sent.
  vector<pair<string, int> > empty_vector;
  internal_scheduler->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          protocol_handler.get(), &ProtocolHandler::SendInfoMessage,
          empty_vector, empty_vector, false));

  // Keep simulating passage of time until just before the quiet period ends.
  // Nothing should be sent.  (The mock network will catch any attempts to send
  // and fail the test.)
  internal_scheduler->PassTime(
      TimeDelta::FromMilliseconds(next_message_delay_ms - 1));
}

// Test that the protocol handler properly delivers an error message to the
// listener.
TEST_F(ProtocolHandlerTest, ErrorMessage) {
  // Fabricate an error message.
  ServerToClientMessage message;
  token = "test token";
  InitServerHeader(token, message.mutable_header());

  // Add an error message.
  ErrorMessage::Code error_code = ErrorMessage_Code_AUTH_FAILURE;
  string description = "invalid auth token";
  message.mutable_error_message()->set_code(error_code);
  message.mutable_error_message()->set_description(description);

  ServerMessageHeader expected_header(token, summary);

  // The listener should still get a call to handle the incoming header.
  EXPECT_CALL(
      listener,
      HandleIncomingHeader(Eq(expected_header)));

  // It should also get a call to handle an error message.
  EXPECT_CALL(
      listener,
      HandleErrorMessage(Eq(expected_header), Eq(error_code), Eq(description)));

  // Deliver the message.
  string serialized;
  message.SerializeToString(&serialized);
  message_callback->Run(serialized);
  internal_scheduler->PassTime(TimeDelta());
}

// Tests that the protocol handler rejects a message from the server if the
// token doesn't match the client's.
TEST_F(ProtocolHandlerTest, TokenMismatch) {
  // Create the server message with one token.
  token = "test token";
  ServerToClientMessage message;
  InitServerHeader(token, message.mutable_header());

  // Give the client a different token.
  token = "token-that-should-mismatch";

  // Deliver the message.
  string serialized;
  message.SerializeToString(&serialized);
  message_callback->Run(serialized);
  internal_scheduler->PassTime(EndOfTestWaitTime());

  // No listener calls should be made, and the handler should have recorded the
  // token mismatch.
  ASSERT_EQ(1, statistics.GetClientErrorCounterForTest(
      Statistics::ClientErrorType_TOKEN_MISMATCH));
}

// Tests that the protocol handler won't send out a non-initialize message if
// the client has no token.
TEST_F(ProtocolHandlerTest, TokenMissing) {
  token = "";
  vector<pair<string, int> > empty_vector;

  internal_scheduler->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          protocol_handler.get(),
          &ProtocolHandler::SendInfoMessage, empty_vector, empty_vector, true));

  internal_scheduler->PassTime(config.batching_delay * kMaxSmearMultiplier);

  ASSERT_EQ(1, statistics.GetClientErrorCounterForTest(
      Statistics::ClientErrorType_TOKEN_MISSING_FAILURE));
}

// Tests that the protocol handler won't send out a message that fails
// validation (in this case, an invalidation ack with a missing version).
TEST_F(ProtocolHandlerTest, InvalidOutboundMessage) {
  token = "test token";

  vector<ObjectIdP> object_ids;
  InitTestObjectIds(&object_ids, 1);
  vector<InvalidationP> invalidations;
  MakeInvalidationsFromObjectIds(object_ids, &invalidations);
  invalidations[0].clear_version();

  internal_scheduler->Schedule(
      Scheduler::NoDelay(),
      NewPermanentCallback(
          protocol_handler.get(),
          &ProtocolHandler::SendInvalidationAck,
          invalidations[0]));

  internal_scheduler->PassTime(config.batching_delay * kMaxSmearMultiplier);

  ASSERT_EQ(1, statistics.GetClientErrorCounterForTest(
      Statistics::ClientErrorType_OUTGOING_MESSAGE_FAILURE));
}

// Tests that the protocol handler drops an unparseable message.
TEST_F(ProtocolHandlerTest, UnparseableInboundMessage) {
  // Make an unparseable message.
  string serialized = "this can't be a valid protocol buffer!";
  message_callback->Run(serialized);
  internal_scheduler->PassTime(EndOfTestWaitTime());
}

}  // namespace invalidation
