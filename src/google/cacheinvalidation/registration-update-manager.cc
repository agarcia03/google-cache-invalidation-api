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
  registration_callback->Run(result);
  delete registration_callback;
}

}  // namespace

RegistrationUpdateManager::RegistrationUpdateManager(SystemResources* resources,
                                                     const ClientConfig& config)
    : resources_(resources), current_op_seq_num_(0),
      registration_timeout_(config.registration_timeout) {}

RegistrationUpdateManager::~RegistrationUpdateManager() {
  // Delete all the callbacks that we haven't responded to.
  for (map<uint64, PendingOperationInfo>::iterator iter = pending_ops_.begin();
       iter != pending_ops_.end(); ++iter) {
    delete iter->second.callback;
  }
}

void RegistrationUpdateManager::AddOutboundRegistrationUpdates(
    ClientToServerMessage* message) {
  // Loop over the table of pending registrations and add to the message all
  // those that haven't been sent within the registration timeout.
  Time now = resources_->current_time();
  for (map<uint64, PendingOperationInfo>::iterator iter = pending_ops_.begin();
       iter != pending_ops_.end(); ++iter) {
    PendingOperationInfo& op_info = iter->second;
    if (now >= op_info.last_sent + registration_timeout_) {
      op_info.last_sent = now;
      RegistrationUpdate* op = message->add_register_operation();
      op->CopyFrom(op_info.operation);
    }
  }
}

void RegistrationUpdateManager::RepeatLostOperations(
    uint64 last_confirmed_seq_num, RegistrationCallback* callback) {
  CHECK(IsCallbackRepeatable(callback));
  for (map<uint64, RegistrationUpdate>::iterator iter =
           confirmed_ops_by_seq_num_.upper_bound(last_confirmed_seq_num);
       iter != confirmed_ops_by_seq_num_.end();) {
    RegistrationUpdate& op = iter->second;

    // Explicitness hack here to work around broken callback
    // implementations.
    void (RegistrationCallback::*run_function)(
        const RegistrationUpdateResult&) =
        &RegistrationCallback::Run;

    // Make a new callback so that this object owns everything in the map.
    RegistrationCallback* tmp_callback =
        NewPermanentCallback(callback, run_function);

    // Put the operation back into the pending map, so it will be resent the
    // next time the app pulls a message.  Remove it from the map of confirmed
    // ops.
    pending_ops_[iter->first] = PendingOperationInfo(op, tmp_callback);
    map<uint64, RegistrationUpdate>::iterator tmp_iter = iter;
    tmp_iter++;
    confirmed_ops_by_seq_num_.erase(iter);
    iter = tmp_iter;
  }
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

  uint64 seq_num = op.sequence_number();
  map<uint64, PendingOperationInfo>::iterator iter = pending_ops_.find(seq_num);
  if (iter != pending_ops_.end()) {
    // If the result corresponds to a pending operation (i.e., we were expecting
    // a result), then handle it.
    const PendingOperationInfo& pending_operation = iter->second;
    if (status.code() == Status_Code_SUCCESS) {
      // If the operation was successful, add it to the map of confirmed
      // operations.
      confirmed_ops_by_seq_num_[seq_num] = op;
    }

    // In any case, remove the object from the map of pending operations, and
    // call the callback.
    resources_->ScheduleImmediately(
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
  // Construct a registration update message and add an entry in the pending
  // operations map from it to the callback.
  RegistrationUpdate op;
  op.mutable_object_id()->CopyFrom(object_id);
  op.set_type(op_type);
  uint64 seq_num = ++current_op_seq_num_;
  op.set_sequence_number(seq_num);
  // TODO: Perhaps have a set of callback pointers to check
  // uniqueness.
  pending_ops_[seq_num] = PendingOperationInfo(op, callback);
}

}  // namespace invalidation
