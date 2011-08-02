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

import com.google.ipc.invalidation.external.client.InvalidationClient;
import com.google.ipc.invalidation.external.client.InvalidationListener;
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
import android.util.Log;

import java.util.Collection;

/**
 * Implementation of the {@link InvalidationClient} interface for Android.
 * Instances of the class are obtained using {@link AndroidClientFactory#create}
 * or {@link AndroidClientFactory#resume}.
 * <p>
 * The class provides implementations of the {@link InvalidationClient} methods
 * that delegate to the  invalidation service running on the device using
 * the bound service model defined in {@link InvalidationService}.
 *
 */
final class AndroidInvalidationClientImpl implements AndroidInvalidationClient {

  /** Logging tag */
  private static final String TAG = "AndroidInvalidationClient";

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
   * The Account associated with this client. May be {@code null} for resumed
   * clients.
   */
  private Account account;

  /**
   * The authentication type that is used to authenticate the client.
   */
  private String authType;

  /**
   * A service binder used to bind to the invalidation service.
   */
  private static final InvalidationBinder serviceBinder = new InvalidationBinder();

  /**
   * The {@link InvalidationListener} service class that handles events for
   * this client. May be {@code null} for resumed clients.
   */
  private final Class<? extends AndroidInvalidationListener> listenerClass;

  /**
   * Creates a new invalidation client with the provided client key and
   * account that sends invalidation events to the specified component.
   *
   * @param context the execution context for the client.
   * @param clientKey a unique id that identifies the created client within the
   *        scope of the application.
   * @param account the user account associated with the client.
   * @param listenerClass the {@link AndroidInvalidationListener} subclass that
   *        will handle invalidation events.
   */
  AndroidInvalidationClientImpl(Context context, String clientKey, int clientType, Account account,
      String authType, Class<? extends AndroidInvalidationListener> listenerClass) {
    this.context = context;
    this.clientKey = clientKey;
    this.clientType = clientType;
    this.account = account;
    this.authType = authType;
    this.listenerClass = listenerClass;
  }

  /**
   * Constructs a resumed invalidation client with the provided client key
   * and context.
   *
   * @param context the application context for the client.
   * @param clientKey a unique id that identifies the resumed client within the
   *        scope of the device.
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

  @Override
  public Account getAccount() {
    return account;
  }

  @Override
  public String getAuthType() {
    return authType;
  }

  /**
   * Returns the event listener class associated with the client or {@code null}
   * if unknown (when resumed).
   */
  Class<? extends AndroidInvalidationListener> getListenerClass() {
    return listenerClass;
  }

  @Override
  public void start() {
    Request request = Request
        .newBuilder(Action.START)
        .setClientKey(clientKey)
        .build();
    executeServiceRequest(request);
  }

  @Override
  public void stop() {
    Request request = Request
        .newBuilder(Action.STOP)
        .setClientKey(clientKey)
        .build();
    executeServiceRequest(request);
  }

  /**
   * Registers to receive invalidation notifications for an object.
   *
   * @param objectId object id.
   */
  public void register(ObjectId objectId) {
    Request request = Request
        .newBuilder(Action.REGISTER)
        .setClientKey(clientKey)
        .setObjectId(objectId)
        .build();
    executeServiceRequest(request);
  }

  /**
   * Registers to receive invalidation notifications for a collection of objects.
   *
   * @param objectIds object id collection.
   */
  public void register(Collection<ObjectId> objectIds) {
    Request request = Request
        .newBuilder(Action.REGISTER)
        .setClientKey(clientKey)
        .setObjectIds(objectIds)
        .build();
    executeServiceRequest(request);
  }

  /**
   * Unregisters to disable receipt of invalidations on an object.
   *
   * @param objectId object id.
   */
  public void unregister(ObjectId objectId) {
    Request request = Request
        .newBuilder(Action.UNREGISTER)
        .setClientKey(clientKey)
        .setObjectId(objectId)
        .build();
    executeServiceRequest(request);
  }

  /**
   * Unregisters to disable receipt of invalidations for a collection of objects.
   *
   * @param objectIds object id collection.
   */
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

  /**
   * Called to initialize a newly created client instance with the invalidation
   * service.
   */
  void initialize() {
    // Create an intent that can be used to fire listener events back to the
    // provided listener service.   Use setComponent and not setPackage/setClass so the
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
  }

  /**
   * Called to resume an existing client instance with the invalidation service.
   */
  void initResumed() {
    Request request = Request
        .newBuilder(Action.RESUME)
        .setClientKey(clientKey)
        .build();
    Response response = executeServiceRequest(request);

    // Save the account associated with the resumed client
    account = response.getAccount();
    authType = response.getAuthType();
  }

  /**
   * Ensures that the invalidation service has been started and that the
   * client has a bound service connection to it.
   */
  private InvalidationService ensureService() {
    if (!serviceBinder.isBound()) {

      // Start the service if not currently bound.  The invalidation service
      // is responsible for stopping itself when no work remains to be done.
      if (context.startService(Request.SERVICE_INTENT) == null) {
        Log.e(TAG, "Unable to start invalidation service");
        throw new IllegalStateException("Unable to start invalidation service");
      }
    }
    return serviceBinder.bind(context);
  }

  /**
   * Executes a request against the invalidation service and does common error
   * processing against the resulting response.  If unable to connect to the
   * service or an error status is received from it, a runtime exception will
   * be thrown.
   *
   * @param request the request to execute.
   * @return the resulting response if the request was successful.
   */
  private Response executeServiceRequest(Request request) {
    try {
        InvalidationService service = ensureService();
        Bundle outBundle = new Bundle();
        service.handleRequest(request.getBundle(), outBundle);
        Response response = new Response(outBundle);
        response.throwOnFailure();
        return response;
      } catch (RemoteException re) {
        throw new RuntimeException("Unable to contact invalidation service", re);
      }
  }
}
