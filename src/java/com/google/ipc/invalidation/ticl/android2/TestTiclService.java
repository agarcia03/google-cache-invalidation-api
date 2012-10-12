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

import com.google.ipc.invalidation.common.DigestFunction;
import com.google.ipc.invalidation.external.client.InvalidationListener;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.external.client.SystemResources.Scheduler;
import com.google.ipc.invalidation.external.client.SystemResources.Storage;
import com.google.ipc.invalidation.external.client.android.service.AndroidLogger;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ObjectId;
import com.google.ipc.invalidation.testing.android.AndroidConditionWaiter;
import com.google.ipc.invalidation.testing.android.TestListener;
import com.google.ipc.invalidation.ticl.DigestStore;
import com.google.ipc.invalidation.ticl.RecurringTask;
import com.google.ipc.invalidation.ticl.Statistics;
import com.google.ipc.invalidation.ticl.TestableInvalidationClient;
import com.google.ipc.invalidation.ticl.android2.ResourcesFactory.AndroidResources;
import com.google.ipc.invalidation.ticl.android2.TestResourcesFactory.TestAndroidResources;
import com.google.ipc.invalidation.ticl.android2.channel.AndroidChannelConstants;
import com.google.ipc.invalidation.ticl.android2.channel.AndroidChannelConstants.AuthTokenConstants;
import com.google.ipc.invalidation.util.Smearer;
import com.google.protobuf.ByteString;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protos.ipc.invalidation.AndroidService.InternalDowncall;
import com.google.protos.ipc.invalidation.Channel.NetworkEndpointId;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientConfigP;
import com.google.protos.ipc.invalidation.ClientProtocol.ObjectIdP;
import com.google.protos.ipc.invalidation.Types.ClientType;

import android.app.PendingIntent;
import android.app.PendingIntent.CanceledException;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import java.util.Collection;
import java.util.Random;
import java.util.concurrent.atomic.AtomicInteger;


/**
 * Subclass of the {@link TiclService} for use in unit tests of the
 * {@code AndroidInternalScheduler}. It provides an implementation of
 * {@link TestableInvalidationClient} for the Android Ticl. It also supports an additional
 * recurring {@code testTask} that keeps track of how many times it has been executed.
 *
 */
public class TestTiclService extends TiclService {
  /* TestTiclService must be public so that Android can instantiate it as a service. */
  // TODO:

  /**
   * Implementing of the {@link TestableInvalidationClient} interface by Android. It works as
   * follows:
   * <p>
   * 1. For {@code InvalidationClient} methods, it uses an {@link AndroidInvalidationClientStub} to
   *    send intents to the Ticl service. I.e., it acts like an ordinary Android client.
   * <p>
   * 2. For methods in {@code TestableInvalidationClient}, it directly reads and writes the Ticl
   *    state itself. I.e, it restores a Ticl object from disk, calls methods on it, and writes
   *    the Ticl state back out. This avoids having to define an entire Intent-based protocol
   *    to calls these methods, many of which have return values to communicate.
   * <p>
   * Because this class may read/write Ticl state, we need to ensure that the Ticl service is
   * not concurrently acting on the same Ticl. Both this class and the main {@link TestTiclService}
   * synchronize on {@link #lock} to guarantee this. Note that this means that the
   * {@link TestableInvalidationClient} class <b>CANNOT</b> be used safely with an ordinary
   * {@link TiclService}.
   */
  
  public static class TestableClient implements TestableInvalidationClient {
    /** Android system context. */
    private final Context context;

    /** Stub to the Ticl, for {@code InvalidationClient} calls. */
    private final AndroidInvalidationClientStub clientStub;

    public TestableClient(Context context, Logger logger) {
      this.context = context;
      this.clientStub = new AndroidInvalidationClientStub(context, logger);
    }

    @Override
    public void start() {
      // We provide our own implementation of start because the AndroidInvalidationClientStub
      // implementation throws UnsupportedOperationException.
      AndroidTiclManifest manifest = new AndroidTiclManifest(context);
      Intent startIntent = ProtocolIntents.ClientDowncalls.newStartIntent().setClassName(
          context, manifest.getTiclServiceClass());
      context.startService(startIntent);
    }

    // InvalidationClient calls all just delegate to the stub.

    @Override
    public void stop() {
      clientStub.stop();
    }

    @Override
    public void register(ObjectId objectId) {
      clientStub.register(objectId);
    }

    @Override
    public void register(Collection<ObjectId> objectIds) {
      clientStub.register(objectIds);
    }

    @Override
    public void unregister(ObjectId objectId) {
      clientStub.unregister(objectId);
    }

    @Override
    public void unregister(Collection<ObjectId> objectIds) {
      clientStub.unregister(objectIds);
    }

    @Override
    public void acknowledge(AckHandle ackHandle) {
      clientStub.acknowledge(ackHandle);
    }

    // TestableInvalidationClient methods below this line, all of which are implemented by reading
    // the Ticl from persistent storage, calling a method on it, and writing it back out (if the
    // method was a mutator). Methods throwing RuntimeException are not yet implemented.

    @Override
    public boolean isStartedForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.isStartedForTest();
      }
    }

    @Override
    public void stopResources() {
      testResources.stop();
    }

    @Override
    public long getResourcesTimeMs() {
      return testResources.getInternalScheduler().getCurrentTimeMs();
    }

    @Override
    public Scheduler getInternalSchedulerForTest() {
      throw new RuntimeException();
    }

    @Override
    public Storage getStorage() {
      return testResources.getStorage();
    }

    @Override
    public Statistics getStatisticsForTest() {
      synchronized (lock) {
        // The BaseTiclRegistrarTest needs to call this function on a stopped Ticl, so just fake
        // an empty Statistics in that case.
        AndroidInvalidationClientImpl client = loadTicl();
        if (client != null) {
          return client.getStatisticsForTest();
        } else {
          return new Statistics();
        }
      }
    }

    @Override
    public DigestFunction getDigestFunctionForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.getDigestFunctionForTest();
      }
    }

    @Override
    public RegistrationManagerState getRegistrationManagerStateCopyForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.getRegistrationManagerStateCopyForTest();
      }
    }

    @Override
    public void changeNetworkTimeoutDelayForTest(int delayMs) {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        client.changeNetworkTimeoutDelayForTest(delayMs);
        TiclStateManager.saveTicl(context, testResources.getLogger(), client);
      }
    }

    @Override
    public void changeHeartbeatDelayForTest(int delayMs) {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        client.changeHeartbeatDelayForTest(delayMs);
        TiclStateManager.saveTicl(context, testResources.getLogger(), client);
      }
    }

    @Override
    public void setDigestStoreForTest(DigestStore<ObjectIdP> digestStore) {
      throw new RuntimeException();
    }

    @Override
    public byte[] getApplicationClientIdForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.getApplicationClientIdForTest();
      }
    }

    @Override
    public InvalidationListener getInvalidationListenerForTest() {
      synchronized (lock) {
        return TestListener.getDelegate();
      }
    }

    @Override
    
    public ByteString getClientTokenForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.getClientTokenForTest();
      }
    }

    @Override
    public String getClientTokenKeyForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.getClientTokenKeyForTest();
      }
    }

    @Override
    public long getNextMessageSendTimeMsForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.getNextMessageSendTimeMsForTest();
      }
    }

    @Override
    public ClientConfigP getConfigForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.getConfigForTest();
      }
    }

    @Override
    public NetworkEndpointId getNetworkIdForTest() {
      synchronized (lock) {
        AndroidInvalidationClientImpl client = loadTicl();
        return client.getNetworkIdForTest();
      }
    }

    /** Loads the Ticl from disk. */
    private AndroidInvalidationClientImpl loadTicl() {
      // Reset the scheduler to allow binding the recurring task of the new Ticl object.
      ((AndroidInternalScheduler) testResources.getInternalScheduler()).reset();
      return TiclStateManager.restoreTicl(context, testResources);
    }
  }

  /** Type of auth tokens returned to the application. */
  public static final String AUTH_TOKEN_TYPE = "android";

  /** Name of the recurring task provided by the test service. */
  private static final String TEST_TASK_NAME = "testTask";

  private static final Object lock = new Object();

  /** Number of times the test task has been run. */
  static AtomicInteger numTestTasksRun = new AtomicInteger(0);

  /** Number of messages received from the server. */
  public static AtomicInteger numServerMessagesReceived = new AtomicInteger(0);

  /** Resources to use when creating Ticls. */
  private static TestAndroidResources testResources;

  /** Auth token to return in response to requests from AndroidMessageSenderService. */
  public static String authToken = null;

  /**
   * Sets the resources to be used when creating Ticls. If {@code null}, then resources will be
   * created as they normally would.
   */
  public static void setTestResources(TestAndroidResources resources) {
    synchronized (lock) {
      testResources = resources;
    }
  }

  @Override
  AndroidResources createResources() {
    // This method is only called from TiclService.onHandleIntent, so we will already have the
    // lock, but acquire it by convention.
    synchronized (lock) {
      AndroidResources resourcesToReturn;
      if (testResources != null) {
        // We are using a single resources instance across multiple intents. Reset the scheduler
        // so that we will be able to bind tasks for the newly-created Ticl that will be used
        // to handle the incoming Intent.
        ((AndroidInternalScheduler) testResources.getInternalScheduler()).reset();
        resourcesToReturn = testResources;
      } else {
        resourcesToReturn = super.createResources();
      }
      // Register a test task instance with the resources.
      Smearer smearer = new Smearer(new Random(0), 0);
      RecurringTask task = createTask(resourcesToReturn.getInternalScheduler(), smearer);
      ((AndroidInternalScheduler) resourcesToReturn.getInternalScheduler()).registerTask(
          TEST_TASK_NAME, task.getRunnable());
      return resourcesToReturn;
    }
  }

  @Override
  protected void onHandleIntent(Intent intent) {
    synchronized (lock) {
      Log.i("TestTiclService",  "--------- Start ---------------");
      // The Android Ticl relies on the application using it to provide it with auth tokens for
      // the HTTP POST to  to deliver messages (needed since the HTTP servlet requires a
      // logged-in user). In tests, the service acts as the application and supplies the auth tokens
      // needed to send messages.
      if (AuthTokenConstants.ACTION_REQUEST_AUTH_TOKEN.equals(intent.getAction())) {
        handleAuthTokenRequest(intent);
      } else if (intent.hasExtra(ProtocolIntents.INTERNAL_DOWNCALL_KEY)) {
        handleServerMessage(intent);
      } else {
        // The intent is not recognized by the test service; delegate to the real implementation.
        super.onHandleIntent(intent);
      }

      Log.i("TestTiclService",  "--------- End ----------------");
    }
  }

  /**
   * Handles an inbound message from the data center. Increments a counter of received messages
   * and delegates to the real service to handle the message.
   */
  private void handleServerMessage(Intent intent) {
    try {
      // Increment the counter if this is a server-message downcall (it might also be a network
      // status change).
      InternalDowncall downcall = InternalDowncall.parseFrom(
          intent.getByteArrayExtra(ProtocolIntents.INTERNAL_DOWNCALL_KEY));
      if (downcall.hasServerMessage()) {
        numServerMessagesReceived.incrementAndGet();
      }
    } catch (InvalidProtocolBufferException exception) {
      throw new RuntimeException(exception);
    }
    super.onHandleIntent(intent);  // Delegate.
  }

  /** Handles a AndroidMessageSenderService request for an auth token to send a message. */
  private void handleAuthTokenRequest(Intent intent) {
    PendingIntent pendingIntent =
        intent.getParcelableExtra(AuthTokenConstants.EXTRA_PENDING_INTENT);
    Intent responseIntent = new Intent()
        .putExtra(AndroidChannelConstants.AuthTokenConstants.EXTRA_AUTH_TOKEN, authToken)
        .putExtra(
            AndroidChannelConstants.AuthTokenConstants.EXTRA_AUTH_TOKEN_TYPE, AUTH_TOKEN_TYPE);
    try {
      pendingIntent.send(this, 0, responseIntent);
    } catch (CanceledException exception) {
      throw new RuntimeException("Cannot handle auth token request", exception);
    }
  }

  /** Returns an instance of the test task. */
  static RecurringTask createTask(Scheduler scheduler, Smearer smearer) {
    final int initialDelayMs = 250;
    return new RecurringTask(TEST_TASK_NAME, scheduler, AndroidLogger.forPrefix(TEST_TASK_NAME),
        smearer, null, initialDelayMs, 0) {
      @Override
      public boolean runTask() {
        numTestTasksRun.incrementAndGet();
        return false; // Do not reschedule.
      }
    };
  }

  /** Removes the Ticl state file. Used to ensure each test gets a freshly-created Ticl. */
  public static void scrubTiclState(Context context) {
    synchronized (lock) {
      TiclStateManager.deleteStateFile(context);
    }
  }

  /**
   * Same specs as {@code AndroidClientFactory.createClient}, but allows specifying a specific
   * {@code config}, and the created clients are <b>NOT</b> automatically started.
   */
  public static void createClient(Context context, ClientType.Type clientType, byte[] clientName,
      ClientConfigP config) {
    Intent intent = ProtocolIntents.InternalDowncalls.newCreateClientIntent(
        clientType.getNumber(), clientName, config, true);
    intent.setClassName(context, new AndroidTiclManifest(context).getTiclServiceClass());
    context.startService(intent);
  }

  /** Waits for a client to have been created. */
  static void waitForClientCreation(final Context context) {
    AndroidConditionWaiter.await(new AndroidConditionWaiter.AssertionPredicate() {
      @Override
      public void doAssertions() throws Exception {
        synchronized (lock) {
          assertNotNull(TiclStateManager.readTiclState(context, AndroidLogger.forTag("waitCC")));
        }
      }
    });
  }

  /** Waits for a client to have been stopped. */
  static void waitForClientStopped(final Context context) {
    AndroidConditionWaiter.await(new AndroidConditionWaiter.AssertionPredicate() {
      @Override
      public void doAssertions() throws Exception {
        synchronized (lock) {
          assertFalse(TiclStateManager.doesStateFileExistForTest(context));
        }
      }
    });
  }
}
