/*
 * Copyright 2011 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.google.ipc.invalidation.ticl;

import com.google.ipc.invalidation.common.DigestFunction;
import com.google.ipc.invalidation.external.client.InvalidationClient;
import com.google.ipc.invalidation.external.client.InvalidationListener;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.types.SimplePair;
import com.google.protobuf.ByteString;
import com.google.protos.ipc.invalidation.ClientProtocol.ObjectIdP;
import com.google.protos.ipc.invalidation.ClientProtocol.RegistrationSummary;

import java.util.Collection;


/**
 * An interface that exposes some extra methods for testing an invalidation client implementation.
 *
 */
public interface TestableInvalidationClient extends InvalidationClient {

  /** Returns the system resources. */
  SystemResources getResourcesForTest();

  /** Returns the performance counters/statistics . */
  Statistics getStatisticsForTest();

  /** Returns the digest function used for computing digests for object registrations. */
  DigestFunction getDigestFunctionForTest();

  /**
   * Returns a copy of the registration manager's state (the reg summary and all the registered
   * objects).
   * <p>
   * REQUIRES: This method is called on the internal scheduler.
   */
  SimplePair<RegistrationSummary, ? extends Collection<ObjectIdP>>
      getRegistrationManagerStateCopyForTest();

  /**
   * Changes the existing delay for the network timeout delay in the operation scheduler to be
   * {@code delayMs}.
   */
  void changeNetworkTimeoutDelayForTest(int delayMs);

  /**
   * Changes the existing delay for the heartbeat delay in the operation scheduler to be
   * {@code delayMs}.
   */
  void changeHeartbeatDelayForTest(int delayMs);

  /**
   * Sets the digest store to be {@code digestStore} for testing purposes.
   * <p>
   * REQUIRES: This method is called before the Ticl has been started.
   */
  void setDigestStoreForTest(DigestStore<ObjectIdP> digestStore);

  /** Returns the client id that is used for squelching invalidations on the server side. */
  byte[] getApplicationClientIdForTest();

  /** Returns the listener that was registered by the caller. */
  InvalidationListener getInvalidationListenerForTest();

  /** Returns the current client token. */
  ByteString getClientTokenForTest();

  /** Returns the single key used to write all the Ticl state. */
  String getClientTokenKeyForTest();

  /** Returns the next time a message is allowed to be sent to the server (could be in the past). */
  long getNextMessageSendTimeMsForTest();
}
