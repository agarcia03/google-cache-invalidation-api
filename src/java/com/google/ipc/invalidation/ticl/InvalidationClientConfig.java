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

import com.google.ipc.invalidation.external.client.types.SimplePair;
import com.google.ipc.invalidation.util.InternalBase;

import java.util.List;

/**
 * Configuration parameters for the  client library.
 *
 */
public class InvalidationClientConfig extends InternalBase {
  /**
   * Initial delay for a heartbeat after restarting from persistent state. We use this so that
   * the application has a chance to respond to the reissueRegistrations call.
   */
  static final int INITIAL_PERSISTENT_HEARTBEAT_DELAY_MS = 2000;

  /** The delay after which a network message sent to the server is considered timed out. */
  public int networkTimeoutDelayMs = 60 * 1000;

  /** Retry delay for a persistent write if it fails. */
  public int writeRetryDelayMs = 10 * 1000;

  /** Delay for sending heartbeats to the server. */
  public int heartbeatIntervalMs = 20 * 60 * 1000;

  /** Delay after which performance counters are sent to the server. */
  public int perfCounterDelayMs = 6 * 60 * 60 * 1000;  // 6 hours.

  /** The maximum exponential backoff factor used for network and persistence timeouts. */
  public int maxExponentialBackoffFactor = 500;

  /** Smearing percent for randomizing delays. */
  public int smearPercent = 20;

  /**
   * Whether the client is transient, that is, does not write its session token to durable
   * storage.
   * TODO: need to expose to the clients.
   * For android the default is false. But for mtrx the default is true.
   */
  public boolean isTransient = false;

  /** Configuration for the protocol client to control batching etc. */
  public ProtocolHandler.Config protocolHandlerConfig = new ProtocolHandler.Config();

  /**
   * Modifies {@code configParams} to contain the list of configuration parameter names and their
   * values.
   */
  public void getConfigParams(List<SimplePair<String, Integer>> configParams) {
    configParams.add(SimplePair.of("networkTimeoutDelayMs", networkTimeoutDelayMs));
    configParams.add(SimplePair.of("writeRetryDelayMs", writeRetryDelayMs));
    configParams.add(SimplePair.of("heartbeatIntervalMs", heartbeatIntervalMs));
    configParams.add(SimplePair.of("perfCounterDelayMs", perfCounterDelayMs));
    configParams.add(SimplePair.of("maxExponentialBackoffFactor", maxExponentialBackoffFactor));
    configParams.add(SimplePair.of("smearPercent", smearPercent));

    // Translate boolean into interger for type compatibility in {@code configParams}.
    configParams.add(SimplePair.of("isTransient", isTransient ? 1 : 0));
    protocolHandlerConfig.getConfigParams(configParams);
  }

  /** Returns a configuration object with parameters set for unit tests. */
  public static InvalidationClientConfig createConfigForTest() {
    InvalidationClientConfig config = new InvalidationClientConfig();
    config.networkTimeoutDelayMs = 2 * 1000;
    config.protocolHandlerConfig.batchingDelayMs = 200;
    config.heartbeatIntervalMs = 5 * 1000;
    config.writeRetryDelayMs = 500;
    config.protocolHandlerConfig = ProtocolHandler.Config.createConfigForTest();
    return config;
  }
}
