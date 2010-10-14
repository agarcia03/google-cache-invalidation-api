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

// Public interfaces between the invalidation client library (often called the
// "Ticl" throughout the codebase) and its embedding application.

#ifndef GOOGLE_CACHEINVALIDATION_INVALIDATION_CLIENT_H_
#define GOOGLE_CACHEINVALIDATION_INVALIDATION_CLIENT_H_

#include <cstddef>
#include <string>
#include <vector>

#include "google/cacheinvalidation/callback.h"
#include "google/cacheinvalidation/stl-namespace.h"
#include "google/cacheinvalidation/time.h"
#include "google/cacheinvalidation/types.pb.h"

namespace invalidation {

using INVALIDATION_STL_NAMESPACE::string;
using INVALIDATION_STL_NAMESPACE::vector;

class NetworkEndpoint;

typedef INVALIDATION_CALLBACK1_TYPE(NetworkEndpoint* const&) NetworkCallback;
typedef INVALIDATION_CALLBACK1_TYPE(bool) StorageCallback;

// -----------------------------------------------------------------------------
// Interfaces that the application using the invalidation client library needs
// to implement.
// -----------------------------------------------------------------------------

// System resources used by the client library, e.g., logging, mutexes, storage,
// etc.  An object implementing this interface needs two distinct threads so
// that tasks within the client library are scheduled separately from callbacks
// to the application.
class SystemResources {
 public:

  // Specifies the level of a log statement.
  enum LogLevel {
    INFO_LEVEL,
    WARNING_LEVEL,
    SEVERE_LEVEL
  };

  virtual ~SystemResources() {}

  // Returns the current time.
  virtual Time current_time() = 0;

  // Starts the scheduler.  If this has not been called exactly once, then the
  // result of calling ScheduleWithDelay() or ScheduleImmediately() is
  // undefined.
  virtual void StartScheduler() = 0;

  // Stops the scheduler: once called, ScheduleWithDelay() and
  // ScheduleImmediately() are free to delete tasks immediately instead of
  // running them.
  //
  // The implementation may enforce stronger semantics as it deems necessary.
  // For example, to ensure that certain "cleanup" tasks are run, an
  // implementation may guarantee that all tasks previously scheduled with
  // ScheduleImmediately() are run before this method returns.
  //
  // May be called either within a scheduled task or outside.
  virtual void StopScheduler() = 0;

  // NOTE: Any objects referenced by tasks passed to the scheduling functions
  // below must live until the tasks actually run.  This could be done either by
  // having the task itself own the objects, or by scheduling another task that
  // deletes the object(s) after the original task is done.

  // Schedules a closure for execution after the given delay. The resources
  // object takes ownership of the task, which must be a repeatable callback
  // (although it is called at most once).  If StopScheduler() is (or was)
  // called before the delay expires, then the task is deleted without being
  // executed.
  //
  // REQUIRES: StartScheduler() has been called.
  virtual void ScheduleWithDelay(TimeDelta delay, Closure* task) = 0;

  // Schedules a task for immediate execution. The resources object takes
  // ownership of the task, which must be a repeatable callback (although it is
  // called at most once).  Tasks scheduled with this function will be run in
  // the order in which they were scheduled.
  //
  // If this function is called before StopScheduler(), then the task is
  // guaranteed to run.  If StopScheduler() has been called, then the task is
  // deleted immediately without being run.
  //
  // REQUIRES: StartScheduler() has been called.
  virtual void ScheduleImmediately(Closure* task) = 0;

  // Like ScheduleImmediately(), except the task is scheduled to run on the
  // listener's (application's) thread, which must be different from the thread
  // used for the other Schedule* functions.
  //
  // REQUIRES: StartScheduler() has been called.
  virtual void ScheduleOnListenerThread(Closure* task) = 0;

  // Returns whether execution is currently on the client library's internal
  // thread (the one used for ScheduleImmediately and ScheduleWithDelay).
  virtual bool IsRunningOnInternalThread() = 0;

  // Log a statement specified by format and the varargs. <file, line> indicate
  // where this call originated from and level indicates the severity of the log
  // statement.  The format string and optional arguments follow the style of
  // the *printf family of functions.
  virtual void Log(LogLevel level, const char* file, int line,
                   const char* format, ...) = 0;

  // Persists the client library's internal state.  The resources object takes
  // ownership of the callback and should invoke it with 'true' if the write
  // succeeds, or 'false' if it fails.  The state should be read back on the
  // next startup and passed to CreatePersistent.
  virtual void WriteState(const string& state, StorageCallback* callback) = 0;
};

// Parameter delivered to InvalidationListener::RegistrationStateChanged() when
// the status is UNKNOWN.  It conveys additional hints about why the status was
// UNKNOWN, e.g., whether it is worthwhile for the application to retry an
// operation.
class UnknownHint {
 public:
  UnknownHint() {}

  UnknownHint(bool is_transient, const string& message)
      : is_transient_(is_transient),
        message_(message) {}

  // Indicates whether the problem that led to the unknown state is transient or
  // not.
  bool is_transient() const {
    return is_transient_;
  }

  // A message describing the condition that led to the unknown state.
  string message() const {
    return message_;
  }

 private:
  bool is_transient_;
  string message_;
};

// Possible registration states in which an object may exist.
enum RegistrationState {
  RegistrationState_REGISTERED = 0,
  RegistrationState_UNREGISTERED = 1,
  RegistrationState_UNKNOWN = 2
};

// The object on which invalidations (or lost registrations) are delivered by
// the client library to the application.
class InvalidationListener {
 public:

  virtual ~InvalidationListener() {}

  // Calls made in response to high-level invalidation events. ///////////////

  // Indicates that a cached object has been invalidated.  The invalidation
  // object contains the id of the object that has changed, along with its new
  // version number.
  //
  // The invalidation client library guarantees that, if the listener has
  // registered for an object and that object subsequently changes, this method
  // will be invoked at least once.
  //
  // The application owns the "done" callback, which must be repeatable
  // (although it is called at most once).
  virtual void Invalidate(const Invalidation& invalidation, Closure* done) = 0;

  // Indicates that the application should consider all objects to have
  // changed. This callback is generally made when the NCL has been disconnected
  // from the network for too long a period and has been unable to resynchronize
  // with the update stream, but it may be invoked arbitrarily.
  //
  // The application owns the "done" callback, which must be repeatable
  // (although it is called at most once).
  virtual void InvalidateAll(Closure* done) = 0;

  // Indicates that the registration state of an object has changed. Note that
  // if the new state is UNKNOWN, the unknown_hint provides a hint as to whether
  // or not the UNKNOWN state is transient.  If it is, future calls to
  // (un)register may be able to transition the object out of this state.
  virtual void RegistrationStateChanged(const ObjectId& object_id,
                                        RegistrationState new_state,
                                        const UnknownHint& unknown_hint) = 0;

  // Indicates that the application's registrations have been lost.
  //
  // The application must invoke the provided callback to indicate that it's
  // done processing the lost registrations.  The callback is owned by the
  // listener and must be repeatable (although it is called at most once).
  virtual void AllRegistrationsLost(Closure* done) = 0;

  // Indicates that the invalidation client has either acquired or lost its
  // session, depending on the value of has_session.  This is purely for
  // informational purposes and may safely be ignored.
  virtual void SessionStatusChanged(bool has_session) {}
};

// -----------------------------------------------------------------------------
// Classes implemented by the invalidation client library for use by the
// application
// -----------------------------------------------------------------------------

// A network endpoint abstraction that allows an application to send and receive
// messages needed by the client library. If the application receives a message
// for the Ticl, it calls HandleInboundMessage with the message. When the Ticl
// wants to send a message, it calls the the Closure registered via
// RegisterOutboundListener informing the application that the endpoint has some
// data to send. At that point, the application can call TakeOutboundMessage to
// extract the pending message.
//
// Note: If the application is polling-based, i.e., it does not wish to send
// messages on demand but only when it contacts the server on a regular basis,
// it can simply call TakeOutboundMessage periodically, without ever calling
// RegisterOutboundListener.
class NetworkEndpoint {
 public:
  virtual ~NetworkEndpoint() {}

  // Requires: outbound_message_ready is not null
  //
  // Registers a callback (outbound_message_ready) that is called by the Ticl
  // when a message is ready to be fetched by TakeOutboundMessage. The caller
  // retains ownership of the callback, which must be a repeatable
  // callback.
  //
  // TODO: Figure out what to do when we want to delete
  // outbound_message_ready but there's still tasks scheduled which
  // reference it.
  virtual void RegisterOutboundListener(
      NetworkCallback* outbound_message_ready) = 0;

  // Asks the Ticl to handle the message received from the network by the
  // application.
  virtual void HandleInboundMessage(const string& message) = 0;

  // Requires: message is not null
  //
  // Modifies message to contain the message that needs to be delivered to the
  // server and flushes that message from the endpoint.
  virtual void TakeOutboundMessage(string* message) = 0;

  // Advises the Ticl whether the application believes the network connection to
  // be available.  Failure to advise properly may result in higher latency for
  // invalidations and registration operations.
  virtual void AdviseNetworkStatus(bool online) = 0;
};

// A rate limit of 'count' events over a window of duration 'window_size'.  The
// client configuration contains a collection of rate limits to be enforced on
// the outbound network listener.
struct RateLimit {
  RateLimit(TimeDelta window_size, size_t count)
      : window_size(window_size), count(count) {}

  TimeDelta window_size;
  size_t count;
};

// Maximum number of registrations per message, by default.
static int kDefaultMaxRegistrationsPerMessage = 5;
// Maximum number of operations per message, by default.
static int kDefaultMaxOpsPerMessage = 10;
// Maximum number of attempts to perform a registration, by default.
static int kDefaultMaxRegistrationAttempts = 3;
// Number of sequence numbers to reserve when writing state, by default.
static int kDefaultSeqnoBlockSize = 1024 * 1024;
// Maximum factor by which to randomly increase or decrease an interval, by
// default.
static double kDefaultSmearFactor = 0.2;

// Configuration parameters for the Ticl.
struct ClientConfig {
  ClientConfig()
      : registration_timeout(TimeDelta::FromMinutes(1)),
        initial_heartbeat_interval(TimeDelta::FromMinutes(20)),
        initial_polling_interval(TimeDelta::FromMinutes(60)),
        max_registrations_per_message(kDefaultMaxRegistrationsPerMessage),
        max_ops_per_message(kDefaultMaxOpsPerMessage),
        max_registration_attempts(kDefaultMaxRegistrationAttempts),
        periodic_task_interval(TimeDelta::FromMilliseconds(500)),
        registration_sync_timeout(TimeDelta::FromSeconds(20)),
        seqno_block_size(kDefaultSeqnoBlockSize),
        smear_factor(kDefaultSmearFactor) {
    AddDefaultRateLimits();
  }

  // Adds default rate limits of one message per second, and six messages per
  // minute, to the config.
  void AddDefaultRateLimits() {
    // One message per second.
    rate_limits.push_back(RateLimit(TimeDelta::FromSeconds(1), 1));
    // Six messages per minute.
    rate_limits.push_back(RateLimit(TimeDelta::FromMinutes(1), 6));
  }

  // Registration timeout.  If the Ticl has not received a reply for a
  // registration in this long, it will resend the message.
  TimeDelta registration_timeout;

  // Interval at which heartbeat messages will be sent to the server, until the
  // server specifies a different interval.
  TimeDelta initial_heartbeat_interval;

  // Interval at which the server will be polled for invalidations, until it
  // specifies a different interval.
  TimeDelta initial_polling_interval;

  // The rate limits for the network manager.
  vector<RateLimit> rate_limits;

  // The maximum number of messages that will be sent in a particular message.
  int max_registrations_per_message;

  // The maximum number of registrations and invalidation acks per message.
  int max_ops_per_message;

  // Maximum number of times to attempt a registration.
  int max_registration_attempts;

  // The interval at which to execute the periodic task.
  TimeDelta periodic_task_interval;

  // Timeout for registration sync operations.
  TimeDelta registration_sync_timeout;

  // Number of sequence numbers to allocate per restart.
  int seqno_block_size;

  // Smearing factor for scheduling. Delays will be smeared by +/- this
  // factor. E.g., if this value is 0.2 and a delay has base value 1, the
  // smeared value will be between 0.8 and 1.2.
  double smear_factor;
};

// Allows an application to register and unregister for invalidations for
// specific objects; reliably delivers invalidations when these objects change.
class InvalidationClient {
 public:

  virtual ~InvalidationClient() {}

  // Constructs an invalidation client:
  //
  // client_type - the type of the application (e.g., CHROME)
  //
  // client_id - a name that the application assigns to identify the client
  //     (e.g, user@gmail.com/some-random-string)
  //
  // resources - the system resources for logging, scheduling, etc.
  //
  // persisted_state - previously persisted state for this client, or an empty
  //     string if the client is starting fresh
  //
  // listener - object on which the invalidations will be delivered
  //
  // Ownership for all parameters remains with the caller. However, the
  // application must ensure that "resources" and "listener" are not deleted
  // until this InvalidationClient has been deleted as well.  The caller owns
  // the returned ticl.
  static InvalidationClient* Create(
      SystemResources* resources, const ClientType& client_type,
      const string& client_id, const string& persisted_state,
      InvalidationListener *listener);

  // TODO(ghc): allow Create() to take configuration parameters.

  // Requests that the InvalidationClient register to receive
  // invalidations for the object with id oid.
  virtual void Register(const ObjectId& oid) = 0;

  // Requests that the InvalidationClient unregister for invalidations
  // for the object with id oid.
  virtual void Unregister(const ObjectId& oid) = 0;

  // Returns the network channel from which the application can get messages to
  // send on its network to the invalidation server and provide messages that
  // have been received from the server. The invalidation client owns the
  // endpoint.
  virtual NetworkEndpoint* network_endpoint() = 0;
};

}  // namespace invalidation

#endif  // GOOGLE_CACHEINVALIDATION_INVALIDATION_CLIENT_H_
