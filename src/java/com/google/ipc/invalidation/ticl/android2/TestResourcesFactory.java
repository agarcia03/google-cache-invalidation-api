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

package com.google.ipc.invalidation.ticl.android2;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.external.client.SystemResources.NetworkChannel;
import com.google.ipc.invalidation.external.client.SystemResources.Storage;
import com.google.ipc.invalidation.external.client.types.Callback;
import com.google.ipc.invalidation.ticl.TestableNetworkChannel;
import com.google.ipc.invalidation.ticl.android2.ResourcesFactory.AndroidResources;
import com.google.protobuf.ByteString;
import com.google.protos.ipc.invalidation.Channel.NetworkEndpointId;

import android.content.Context;
import android.content.Intent;

/**
 * A factory for creating Android {@link SystemResources} for tests.
 *
 */
public class TestResourcesFactory {
  // TODO:
  /**
   * A {@link NetworkChannel} implementation for testing that allows the Ticl service to work
   * with a non-Android network implementation (delegate). It delegates outbound messages to the
   * delegate network, and it installs a message receiver with the delegate network that
   * forwards incoming messages to the Ticl service using Intents.
   */
  private static class TestNetwork implements TestableNetworkChannel {
    /** Network implementation that we are wrapping for the Ticl service. */
    private final NetworkChannel delegate;

    /** Android system resources. */
    private final Context context;

    /** Class implementing the Ticl service. */
    private String serviceClass;

    /** System resources for the Ticl. */
    private AndroidResources resources;

    private TestNetwork(NetworkChannel delegate, Context context) {
      this.delegate = Preconditions.checkNotNull(delegate);
      this.context = Preconditions.checkNotNull(context);
    }

    @Override
    public void setSystemResources(SystemResources resources) {
      // Save the resources and provide them to the delegate as well.
      this.resources = (AndroidResources) resources;
      delegate.setSystemResources(resources);

      // Look up the name of the Ticl service class from the manifest.
      serviceClass = new AndroidTiclManifest(context).getTiclServiceClass();
    }

    @Override
    public void setMessageReceiver(Callback<byte[]> incomingReceiver) {
      // The Ticl is providing us with a receiver that should be given incoming network messages.
      // Save that receiver in a location where the Ticl service will be able to access it.
      resources.setNetworkMessageReceiver(incomingReceiver);

      // Install a message receiver on the delegate that will forward inbound messages to the
      // Ticl service, where they will actually be delivered to the Ticl. Normally, the Ticl
      // would make this call on the network; we are impersonating a Ticl to the delegate network.
      delegate.setMessageReceiver(new Callback<byte[]>() {
        @Override
        public void accept(byte[] message) {
          Intent intent = ProtocolIntents.InternalDowncalls.newServerMessageIntent(
              ByteString.copyFrom(message));
          intent.setClassName(resources.getContext(), serviceClass);
          resources.getContext().startService(intent);
        }
      });
    }

    @Override
    public void sendMessage(byte[] outgoingMessage) {
      // Give the outgoing message to the network directly.
      delegate.sendMessage(outgoingMessage);
    }

    @Override
    public void addNetworkStatusReceiver(final Callback<Boolean> networkStatusReceiver) {
      // The Ticl is providing us with a receiver that should be given network status change
      // events. Save it instead of passing it to the network. Give the network a receiver that
      // will invoke the Ticl-provided receiver in a safe way.
      resources.setNetworkStatusReceiver(networkStatusReceiver);
      delegate.addNetworkStatusReceiver(new Callback<Boolean>() {
        @Override
        public void accept(Boolean status) {
          if (isBeingCalledFromService()) {
            // The network may provide us a network status when we are constructing the Ticl. If
            // we fire a whole new Intent in order to process it, then we can end up in an infinite
            // loop, where constructing a Ticl to handle an event causes us to fire an event that
            // we will later need to construct a Ticl to handle. So, if we are already handling
            // a Ticl event, just process the call immediately.
            resources.getNetworkStatusReceiver().accept(status);
          } else {
            // We are not constructing the Ticl, so forward the call to the service as an Intent.
            Intent intent = ProtocolIntents.InternalDowncalls.newNetworkStatusIntent(status);
            intent.setClassName(resources.getContext(), serviceClass);
            resources.getContext().startService(intent);
          }
        }
      });
    }

    /** Returns whether {@link TestTiclService#onHandleIntent} appears in the caller's stack. */
    private static boolean isBeingCalledFromService() {
      for (StackTraceElement element : Thread.currentThread().getStackTrace()) {
        if (!element.getClassName().equals(TestTiclService.class.getName())) {
          continue;
        }
        if (element.getMethodName().equals("onHandleIntent")) {
          return true;
        }
      }
      return false;
    }

    @Override
    public NetworkEndpointId getNetworkIdForTest() {
      return ((TestableNetworkChannel) delegate).getNetworkIdForTest();
    }
  }

  /** {@link AndroidResources} subclass for testing. */
  public static class TestAndroidResources extends AndroidResources {
    /** Whether the resources have been started. */
    private boolean started = false;

    /**
     * Creates an instance for tests.
     *
     * @param context Android system context
     * @param clock source of time for the internal scheduler
     * @param network object implementing the network
     * @param storage oject implementing storage
     * @param logger logger
     */
    TestAndroidResources(Context context, AndroidClock clock, NetworkChannel network,
        Storage storage, Logger logger) {
      super(logger, new AndroidInternalScheduler(context, clock), network, storage, context);
    }

    /**
     * Starts the resources if they have not yet been started. Tests will cause the Ticl
     * service to call start multiple times; we want to accept only the first call.
     */
    @Override public void start() {
      if (!started) {
        started = true;
        super.start();
      }
    }

    /**
     * The Ticl service will stop the resources after each call during the test; we need to
     * avoid that having any effect. The test will shut down the actual resources that the test
     * resources are wrapping at the end of the test, anyway.
     */
    @Override
    public void stop() {
    }
  }

  /**
   * Creates an instance for unit tests. These are tests such as the
   * {@code InvalidationClientImplTest} subclasses that use non-Intent-aware network channels,
   * so their network channels need to be adapted to work with Android using a {@code TestNetwork}.
   *
   * @param context Android system context
   * @param clock source of time for the internal scheduler
   * @param logger logger
   * @param network object implementing the network
   * @param storage object implementing storage
   */
  public static TestAndroidResources createUnitTestResources(Context context, AndroidClock clock,
      NetworkChannel network, Storage storage, Logger logger) {
    return new TestAndroidResources(
        context, clock, new TestNetwork(network, context), storage, logger);
  }

  /**
   * Creates an instance for integration tests. These are tests such as the
   * {@code BaseTiclRegistrarTest} subclasses that use network channels that already are
   * Intent aware.
   *
   * @param context Android system context
   * @param logger logger
   * @param network object implementing the network
   */
  public static TestAndroidResources createIntegrationTestResources(Context context,
      NetworkChannel network, Logger logger) {
    return new TestAndroidResources(
        context, new AndroidClock.SystemClock(), network, new AndroidStorage(context), logger);
  }
}
