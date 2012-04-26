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

package com.google.ipc.invalidation.external.client.android;

import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.external.client.android.service.AndroidLogger;
import com.google.ipc.invalidation.external.client.android.service.Event;
import com.google.ipc.invalidation.external.client.android.service.InvalidationBinder;
import com.google.ipc.invalidation.external.client.android.service.InvalidationService;
import com.google.ipc.invalidation.external.client.android.service.Request;
import com.google.ipc.invalidation.external.client.android.service.Request.Action;
import com.google.ipc.invalidation.external.client.android.service.Response;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ObjectId;

import android.accounts.Account;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.os.RemoteException;

import java.util.Collection;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Implementation of the {@code InvalidationClient} interface for Android. Instances of the class
 * are obtained using {@link AndroidClientFactory#create} or {@link AndroidClientFactory#resume}.
 * <p>
 * The class provides implementations of the {@code InvalidationClient} methods that delegate to the
 *  invalidation service running on the device using the bound service model defined in
 * {@link InvalidationService}.
 *
 */
final class AndroidInvalidationClientImpl implements AndroidInvalidationClient {
  /**
   * Work queue for all accesses to all instances of this class. Used to ensure that we never
   * attempt to bind a service in a blocking way on the main thread, which can cause deadlock.
   */
  // TODO: consider a periodic watchdog event to ensure we have never deadlocked
  // on this thread (for testing only).
  private static final ExecutorService STUB_EXECUTOR = Executors.newSingleThreadExecutor();

  /** Logger */
  private static final Logger logger = AndroidLogger.forTag("InvClient");

  /**
   * The application context associated with the client.
   */
  public final Context context;

  /**
   * Contains the device-unique client key associated with this client.
   */
  private final String clientKey;

  /**
   * Contains the client type for this client.
   */
  private final int clientType;

  /**
   * The Account associated with this client. May be {@code null} for resumed clients.
   */
  private Account account;

  /**
   * The authentication type that is used to authenticate the client.
   */
  private String authType;

  /**
   * A service binder used to bind to the invalidation service.
   */
  private final InvalidationBinder serviceBinder = new InvalidationBinder();

  /**
   * The {@code InvalidationListener} service class that handles events for this client. May be
   * {@code null} for resumed clients.
   */
  private final Class<? extends AndroidInvalidationListener> listenerClass;

  /**
   * The number of callers that are sharing a reference to this client instance. Used to decide when
   * the service binding can be safely released.
   */
  private AtomicInteger refcnt = new AtomicInteger(0);

  /** Whether {@link #release} was ever called with a non-positive {@code refcnt}. */
  
  AtomicBoolean wasOverReleasedForTest = new AtomicBoolean(false);

  /**
   * Creates a new invalidation client with the provided client key and account that sends
   * invalidation events to the specified component.
   *
   * @param context the execution context for the client.
   * @param clientKey a unique id that identifies the created client within the scope of the
   *        application.
   * @param account the user account associated with the client.
   * @param listenerClass the {@link AndroidInvalidationListener} subclass that will handle
   *        invalidation events.
   */
  AndroidInvalidationClientImpl(Context context,
      String clientKey,
      int clientType,
      Account account,
      String authType,
      Class<? extends AndroidInvalidationListener> listenerClass) {
    this.context = context;
    this.clientKey = clientKey;
    this.clientType = clientType;
    this.account = account;
    this.authType = authType;
    this.listenerClass = listenerClass;
  }

  /**
   * Constructs a resumed invalidation client with the provided client key and context.
   *
   * @param context the application context for the client.
   * @param clientKey a unique id that identifies the resumed client within the scope of the device.
   */
  AndroidInvalidationClientImpl(Context context, String clientKey) {
    this.clientKey = clientKey;
    this.context = context;
    this.account = null;
    this.authType = null;
    this.listenerClass = null;
    this.clientType = -1;
  }

  /**
   * Returns the {@link Context} within which the client was created or resumed.
   */
  Context getContext() {
    return context;
  }

  @Override
  public String getClientKey() {
    return clientKey;
  }

  /**
   * Returns the event listener class associated with the client or {@code null} if unknown (when
   * resumed).
   */
  Class<? extends AndroidInvalidationListener> getListenerClass() {
    return listenerClass;
  }

  @Override
  public void start() {
    Request request = Request.newBuilder(Action.START).setClientKey(clientKey).build();
    executeServiceRequest(request);
  }

  @Override
  public void stop() {
    Request request = Request.newBuilder(Action.STOP).setClientKey(clientKey).build();
    executeServiceRequest(request);
  }

  /**
   * Registers to receive invalidation notifications for an object.
   *
   * @param objectId object id.
   */
  @Override
  public void register(ObjectId objectId) {
    Request request =
        Request.newBuilder(Action.REGISTER).setClientKey(clientKey).setObjectId(objectId).build();
    executeServiceRequest(request);
  }

  /**
   * Registers to receive invalidation notifications for a collection of objects.
   *
   * @param objectIds object id collection.
   */
  @Override
  public void register(Collection<ObjectId> objectIds) {
    Request request =
        Request.newBuilder(Action.REGISTER).setClientKey(clientKey).setObjectIds(objectIds).build();
    executeServiceRequest(request);
  }

  /**
   * Unregisters to disable receipt of invalidations on an object.
   *
   * @param objectId object id.
   */
  @Override
  public void unregister(ObjectId objectId) {
    Request request =
        Request.newBuilder(Action.UNREGISTER).setClientKey(clientKey).setObjectId(objectId).build();
    executeServiceRequest(request);
  }

  /**
   * Unregisters to disable receipt of invalidations for a collection of objects.
   *
   * @param objectIds object id collection.
   */
  @Override
  public void unregister(Collection<ObjectId> objectIds) {
    Request request = Request
        .newBuilder(Action.UNREGISTER)
        .setClientKey(clientKey)
        .setObjectIds(objectIds)
        .build();
    executeServiceRequest(request);
  }

  @Override
  public void acknowledge(AckHandle ackHandle) {
    Request request = Request
        .newBuilder(Action.ACKNOWLEDGE)
        .setClientKey(clientKey)
        .setAckHandle(ackHandle)
        .build();
    executeServiceRequest(request);
  }

  @Override
  public void release() {
    // It is important to call release on the executor; otherwise, we could release the service
    // binder before pending work in the executor can run.
    STUB_EXECUTOR.execute(new Runnable() {
      @Override
      public void run() {
        // Release the binding and remove from the client factory when the last reference is
        // released.
        final int refsRemaining = refcnt.decrementAndGet();
        if (refsRemaining < 0) {
          wasOverReleasedForTest.set(true);
          logger.warning("Over-release of client %s", clientKey);
        } else if (refsRemaining == 0) {
          AndroidClientFactory.release(clientKey);
          serviceBinder.unbind(context);
        }
      }
    });
  }

  @Override
  public void destroy() {
    Request request = Request
        .newBuilder(Action.DESTROY)
        .setClientKey(clientKey)
        .build();
    executeServiceRequest(request);
  }

  /**
   * Called to initialize a newly created client instance with the invalidation service.
   */
  void initialize() {
    // Create an intent that can be used to fire listener events back to the
    // provided listener service. Use setComponent and not setPackage/setClass so the
    // intent is guaranteed to be valid even if the service is not in the same application
    Intent eventIntent = new Intent(Event.LISTENER_INTENT);
    ComponentName component = new ComponentName(context.getPackageName(), listenerClass.getName());
    eventIntent.setComponent(component);

    Request request = Request
        .newBuilder(Action.CREATE)
        .setClientKey(clientKey)
        .setClientType(clientType)
        .setAccount(account)
        .setAuthType(authType)
        .setIntent(eventIntent)
        .build();
    executeServiceRequest(request);
    addReference();
  }

  /**
   * Called to resume an existing client instance with the invalidation service. Iff
   * {@code sendTiclResumeRequest}, a request is sent to the invalidatation service to ensure
   * that the Ticl is loaded.
   */
  void initResumed(boolean sendTiclResumeRequest) {
    if (sendTiclResumeRequest) {
      Request request = Request.newBuilder(Action.RESUME).setClientKey(clientKey).build();
      executeServiceRequest(request);
    }
    addReference();
  }

  /**
   * Called to indicate that a client instance is being returned as a reference.
   */
  void addReference() {
    refcnt.incrementAndGet();
  }

  /**
   * Returns the number of references to this client instance.
   */
  
  int getReferenceCountForTest() {
    return refcnt.get();
  }

  /**
   * Returns {@code true} if the client has a binding to the invalidation service.
   */
  
  boolean hasServiceBindingForTest() {
    return serviceBinder.isBound();
  }

  /**
   * Executes a request against the invalidation service and does common error processing against
   * the resulting response. If unable to connect to the service or an error status is received from
   * it, a warning will be logged and the request will be dropped.
   *
   * @param request the request to execute.
   */
  private void executeServiceRequest(final Request request) {
    STUB_EXECUTOR.execute(new Runnable() {
      @Override
      public void run() {
        try {
          InvalidationService service = ensureService();
          Bundle outBundle = new Bundle();
          service.handleRequest(request.getBundle(), outBundle);
          Response response = new Response(outBundle);
          response.warnOnFailure();
        } catch (RemoteException exception) {
          // Ok to throw the exeption because the ExecutorService will create a new thread.
          logger.warning("Remote exeption executing request %s: %s", request,
              exception.getMessage());
          throw new RuntimeException("Unable to contact invalidation service", exception);
        }
      }
    });
  }

  /**
   * Ensures that the invalidation service has been started and that the client has a bound service
   * connection to it.
   */
  private InvalidationService ensureService() {
    if (!serviceBinder.isBound()) {
      // Start the service if not currently bound. The invalidation service
      // is responsible for stopping itself when no work remains to be done.
      Intent serviceIntent = serviceBinder.getIntent(context);
      if (context.startService(serviceIntent) == null) {
        logger.severe("Unable to start invalidation service: %s", serviceIntent);
        throw new IllegalStateException("Unable to start invalidation service");
      }
    }
    return serviceBinder.bind(context);
  }
}
