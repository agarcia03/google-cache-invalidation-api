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

#include "google/cacheinvalidation/registration-update-manager.h"

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/invalidation-client.h"
#include "google/cacheinvalidation/invalidation-client-impl.h"
#include "google/cacheinvalidation/logging.h"
#include "google/cacheinvalidation/log-macro.h"

namespace invalidation {

namespace {

// Helper function to run and delete a RegistrationCallback (meant to
// be passed to new callbacks).
void RunAndDeleteRegistrationCallback(
    RegistrationCallback* registration_callback,
    const RegistrationUpdateResult& result) {
  CHECK(registration_callback != NULL);
  registration_callback->Run(result);
  delete registration_callback;
}

}  // namespace

RegistrationUpdateManager::RegistrationUpdateManager(SystemResources* resources,
                                                     const ClientConfig& config)
    : resources_(resources), config_(config), current_op_seq_num_(0) {}

RegistrationUpdateManager::~RegistrationUpdateManager() {
  // Delete all the callbacks that we haven't responded to.
  for (map<string, PendingOperationInfo>::iterator iter = pending_ops_.begin();
       iter != pending_ops_.end(); ++iter) {
    CHECK(iter->second.callback != NULL);
    delete iter->second.callback;
  }
}

RegistrationState RegistrationUpdateManager::GetObjectState(
    const ObjectId& object_id) {
  RegistrationState state = RegistrationState_NO_INFO;
  const char* description = "NO_INFO";

  string serialized;
  object_id.SerializeToString(&serialized);
  map<string, PendingOperationInfo>::iterator pending_iter =
      pending_ops_.find(serialized);
  map<string, RegistrationUpdate>::iterator confirmed_iter =
      confirmed_ops_.find(serialized);

  if (pending_iter != pending_ops_.end()) {
    // A pending record implies that we must be in a *_PENDING state. Determine
    // whether it's REG_PENDING or UNREG_PENDING.
    switch (pending_iter->second.operation.type()) {
      case RegistrationUpdate_Type_REGISTER:
        state = RegistrationState_REG_PENDING;
        description = "REG_PENDING";
        break;
      case RegistrationUpdate_Type_UNREGISTER:
        state = RegistrationState_UNREG_PENDING;
        description = "UNREG_PENDING";
        break;
      default:
        // Can't happen.
        description = "[pending with unknown op type: should never happen!]";
        break;
    }
  } else if (confirmed_iter != confirmed_ops_.end()) {
    // A confirmed record implies that we must be in a *_CONFIRMED state.
    // Determine whether it's REG_CONFIRMED or UNREG_CONFIRMED.
    switch (confirmed_iter->second.type()) {
      case RegistrationUpdate_Type_REGISTER:
        state = RegistrationState_REG_CONFIRMED;
        description = "REG_CONFIRMED";
        break;
      case RegistrationUpdate_Type_UNREGISTER:
        state = RegistrationState_UNREG_CONFIRMED;
        description = "UNREG_CONFIRMED";
        break;
      default:
        // Can't happen.
        description = "[confirmed with unknown op type: should never happen!]";
        break;
    }
  }
  TLOG(INFO_LEVEL, "Determined object %s/%d to be in state %s (%d)",
       object_id.name().string_value().c_str(), object_id.source(), description,
       state);
  return state;
}

int RegistrationUpdateManager::AddOutboundRegistrationUpdates(
    ClientToServerMessage* message) {
  // Loop over the table of pending registrations and add to the message all
  // those that haven't been sent within the registration timeout.
  int sent_count = 0;
  Time now = resources_->current_time();
  TLOG(INFO_LEVEL, "Will send up to %d registrations.",
       config_.max_registrations_per_message);
  for (map<string, PendingOperationInfo>::iterator iter = pending_ops_.begin();
       (iter != pending_ops_.end()) &&
           (sent_count < config_.max_registrations_per_message); ++iter) {
    PendingOperationInfo& op_info = iter->second;
    TLOG(INFO_LEVEL, "Found pending record for %s/%d, is_sent = %d",
         op_info.operation.object_id().name().string_value().c_str(),
         op_info.operation.object_id().source(), op_info.is_sent);
    if (!op_info.is_sent) {
      op_info.sent_time = now;
      op_info.is_sent = true;
      ++sent_count;
      RegistrationUpdate* op = message->add_register_operation();
      op->CopyFrom(op_info.operation);
    }
  }
  return sent_count;
}

void RegistrationUpdateManager::ProcessRegistrationUpdateResult(
    const RegistrationUpdateResult& result) {
  if (!result.has_status()) {
    // Ignore the message if it didn't contain a valid status.
    TLOG(WARNING_LEVEL, "received a message without a status");
    return;
  }
  const Status& status = result.status();
  if (!status.has_code()) {
    // Ignore the message if the status doesn't have a code.
    TLOG(WARNING_LEVEL, "received a message whose status had no code");
    return;
  }
  const RegistrationUpdate& op = result.operation();
  const ObjectId& object_id = op.object_id();
  string serialized_oid;
  object_id.SerializeToString(&serialized_oid);

  map<string, PendingOperationInfo>::iterator iter =
      pending_ops_.find(serialized_oid);
  if (iter != pending_ops_.end()) {
    // If the result corresponds to a pending operation (i.e., we were expecting
    // a result), then handle it.
    const PendingOperationInfo& pending_operation = iter->second;

    // First, check that the sequence number and operation type all match what's
    // in the pending operation. (We know the object id matches because we
    // looked the record up by object id.) If not, ignore this message.
    if (op.sequence_number() != pending_operation.operation.sequence_number()) {
      TLOG(INFO_LEVEL, "Seqno mismatch for oid %s/%d (%lld vs. %lld); ignoring",
           object_id.name().string_value().c_str(), object_id.source(),
           op.sequence_number(), pending_operation.operation.sequence_number());
      return;
    }
    if (op.type() != pending_operation.operation.type()) {
      TLOG(INFO_LEVEL, "Op type mismatch for oid %s/%d (%d vs. %d); ignoring",
           object_id.name().string_value().c_str(), object_id.source(),
           op.type(), pending_operation.operation.type());
      return;
    }

    // Checks succeeded; process the result.
    if (status.code() == Status_Code_SUCCESS) {
      // If the operation was successful, add it to the map of confirmed
      // operations.
      confirmed_ops_[serialized_oid] = op;
    }

    // In any case, remove the object from the map of pending operations, and
    // call the callback.
    resources_->ScheduleOnListenerThread(
        NewPermanentCallback(&RunAndDeleteRegistrationCallback,
                             pending_operation.callback,
                             result));
    pending_ops_.erase(iter);
  } else {
    // We've received a response for an operation that's not pending.  Don't
    // bother computing a result code or informing the application.  Log the
    // spurious response.
    TLOG(INFO_LEVEL, "Spurious response (seqno = %d)",
         result.operation().sequence_number());
  }
}

void RegistrationUpdateManager::UpdateRegistration(
    const ObjectId& object_id, RegistrationUpdate_Type op_type,
    RegistrationCallback* callback) {
  CHECK(IsCallbackRepeatable(callback));

  // This method is organized in two parts: a switch statement that does
  // specific processing based on the state of the registration, and common code
  // following it that sends a request to the server. In two cases, the switch
  // statement executes an early return, and no message is sent to the
  // server. Given an object "x", these cases are:
  //
  // 1) A register or unregister was already pending for x, and the application
  //    requested that an operation of the same type be performed. In this case,
  //    we have already sent the message that we would send for the requested
  //    operation, so there's no point in sending another. Additionally, if we
  //    did send a new message with a new sequence number, we'd just give the
  //    server more work to do, which can cause a downward spiral under load.
  //
  // 2) A register or unregister was already confirmed for x, and the
  //    application requested an operation of the same type. In this case, we
  //    can simply invoke the callback: there is no work for the server to do on
  //    our behalf for this operation.

  // We first find the registration state of the object, which we need for the
  // switch statement.

  RegistrationState reg_state = GetObjectState(object_id);
  TLOG(INFO_LEVEL, "Starting register for object %s/%d: current state is %d",
       object_id.name().string_value().c_str(), object_id.source(), reg_state);
  string serialized_oid;
  object_id.SerializeToString(&serialized_oid);
  RegistrationUpdateResult result;

  switch (reg_state) {
    case RegistrationState_NO_INFO:
      // We don't know anything about this object, so send a request.
      break;

    case RegistrationState_REG_PENDING:
      // The code for REG_PENDING and UNREG_PENDING is identical, so just
      // fall-through.
    case RegistrationState_UNREG_PENDING: {
      // If we have a pending operation of *any* type on an object, and the
      // application requests *any* operation on that object, then we
      // immediately respond to the first callback with STALE_OPERATION.
      PendingOperationInfo& pending_record = pending_ops_[serialized_oid];

      // Invoke the old callback with STALE_OPERATION;
      RegistrationCallback* old_callback = pending_record.callback;
      result.mutable_operation()->CopyFrom(pending_record.operation);
      result.mutable_status()->set_code(Status_Code_STALE_OPERATION);
      resources_->ScheduleOnListenerThread(
          NewPermanentCallback(&RunAndDeleteRegistrationCallback, old_callback,
                               result));

      // If the operation types match, just keep the new callback for future use
      // and return -- we do NOT send a new request to the server. Otherwise,
      // continue after switch statement and issue a new request;
      if (pending_record.operation.type() == op_type) {
        TLOG(INFO_LEVEL, "Updating callbacks: %llx to %llx",
             pending_record.callback, callback);
        pending_record.callback = callback;
        return;
      }
      break;
    }

    case RegistrationState_UNREG_CONFIRMED:
      // The code for REG_CONFIRMED and UNREG_CONFIRMED is identical, so just
      // fall-through.
    case RegistrationState_REG_CONFIRMED: {
      RegistrationUpdate confirmed_record = confirmed_ops_[serialized_oid];
      if (op_type == confirmed_record.type()) {
        // If we have a confirmed result for the same object and operation type,
        // then we can immediately respond with SUCCESS to the callback.
        //
        // NOTE: in this case, we do NOT want to send a request to the server,
        // so we return and do not execute the NO_INFO common code after the
        // switch statement.
        result.mutable_operation()->CopyFrom(confirmed_record);
        result.mutable_status()->set_code(Status_Code_SUCCESS);
        resources_->ScheduleOnListenerThread(
            NewPermanentCallback(&RunAndDeleteRegistrationCallback, callback,
                                 result));
        return;
      } else {
        // We have a confirmed registration but are requesting an
        // unregistration. Remove the object from confirmed map, then
        // proceed as in NO_INFO.
        confirmed_ops_.erase(serialized_oid);
      }
      break;
    }
    default:
      // Can't happen.
      TLOG(ERROR_LEVEL, "Unrecognized registration state: %d", reg_state);
      return;
  }
  // If we made it through the switch statement without executing an early
  // return, then we need to send a message to the server. This code does not
  // depend on the registration state.

  // Construct a registration update message and add an entry in the pending
  // operations map from it to the callback.
  RegistrationUpdate op;
  op.mutable_object_id()->CopyFrom(object_id);
  op.set_type(op_type);
  uint64 seq_num = ++current_op_seq_num_;
  op.set_sequence_number(seq_num);
  // TODO: Perhaps have a set of callback pointers to check
  // uniqueness.
  pending_ops_[serialized_oid] = PendingOperationInfo(op, callback);
}

bool RegistrationUpdateManager::DoPeriodicRegistrationCheck() {
  Time now = resources_->current_time();

  // Whether we have registrations to send to the server. This is true if we
  // have unsent registrations or timed-out registrations to resend.
  bool data_to_send = false;

  // Visit each pending registration. If it hasn't been sent, then tell the Ticl
  // we have data to send. If it's been sent and has timed out, then handle it
  // by either retrying it or returning STALE_OPERATION to the app, depending on
  // whether retries remain.
  for (map<string, PendingOperationInfo>::iterator iter = pending_ops_.begin();
       iter != pending_ops_.end();) {
    map<string, PendingOperationInfo>::iterator next_iter = iter;
    ++next_iter;
    PendingOperationInfo& op_info = iter->second;
    const ObjectId& object_id = op_info.operation.object_id();
    if (!op_info.is_sent) {
      // If is_sent is false, we have not sent this operation for this attempt;
      // is_sent will only be reset to false if the code that handles a
      // timed-out registration determined that the operation had retry attempts
      // remaining.
      data_to_send = true;
      TLOG(INFO_LEVEL, "Detected unsent (un)registration for: %s/%d",
           object_id.name().string_value().c_str(), object_id.source());
      iter = next_iter;
      continue;
    }

    Time deadline = op_info.sent_time + config_.registration_timeout;
    if (deadline > now) {
      TLOG(INFO_LEVEL, "Not timing out (un)registration for %s/%d since "
           "now = %lld and timeout at %lld",
           object_id.name().string_value().c_str(), object_id.source(),
           now.ToInternalValue(), deadline.ToInternalValue());
    } else {
      TLOG(INFO_LEVEL, "Detected timed-out (un)registration for %s/%d "
           "sent at %lld, now = %lld",
           object_id.name().string_value().c_str(), object_id.source(),
           op_info.sent_time.ToInternalValue(), now.ToInternalValue());
      bool timedout_has_data = HandleTimedOutRegistration(&op_info);
      data_to_send |= timedout_has_data;
    }
    iter = next_iter;
  }
  return data_to_send;
}

bool RegistrationUpdateManager::HandleTimedOutRegistration(
    PendingOperationInfo* op_info) {
  // If we have tried too many times, then issue transient-failure to the app.
  const ObjectId& object_id = op_info->operation.object_id();
  if (op_info->attempt_count == config_.max_registration_attempts) {
    TLOG(INFO_LEVEL, "Maxed-out attempt count for (un)registration on %s/%d "
         "(limit was %d)", object_id.name().string_value().c_str(),
         object_id.source(), config_.max_registration_attempts);
    AbortPending(*op_info);
    return false;  // No data to be sent.
  } else {
    // Otherwise, we still have retries remaining, so increment the attempt
    // count and set sentTime to null. Setting sentTime to null will cause
    // addOutboundRegistrations to include this record when next invoked.
    TLOG(INFO_LEVEL, "Attempt %d of operation on %s/%d timed out; retrying "
         "(limit is %d)", op_info->attempt_count,
         object_id.name().string_value().c_str(), object_id.source());
    ++op_info->attempt_count;
    op_info->is_sent = false;
    return true;  // Data to be sent.
  }
}

void RegistrationUpdateManager::RemoveAllOperations() {
  // Abort all pending operations.
  for (map<string, PendingOperationInfo>::iterator iter = pending_ops_.begin();
       iter != pending_ops_.end();) {
    map<string, PendingOperationInfo>::iterator next_iter = iter;
    ++next_iter;
    AbortPending(iter->second);
    iter = next_iter;
  }

  // Clear the confirmed map.
  confirmed_ops_.clear();
}

void RegistrationUpdateManager::AbortPending(
    const PendingOperationInfo& op_info) {
  // Construct a result for the operation with TRANSIENT_FAILURE status.
  RegistrationUpdateResult result;
  string serialized_oid;
  op_info.operation.object_id().SerializeToString(&serialized_oid);
  result.mutable_operation()->CopyFrom(op_info.operation);
  result.mutable_status()->set_code(Status_Code_TRANSIENT_FAILURE);
  // Invoke the callback with the failure result.
  resources_->ScheduleOnListenerThread(
      NewPermanentCallback(&RunAndDeleteRegistrationCallback, op_info.callback,
                           result));
  // Remove the operation from the map.
  pending_ops_.erase(serialized_oid);
}

}  // namespace invalidation
