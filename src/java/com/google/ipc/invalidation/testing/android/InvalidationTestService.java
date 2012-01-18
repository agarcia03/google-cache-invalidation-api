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

package com.google.ipc.invalidation.testing.android;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.android.service.Event;
import com.google.ipc.invalidation.external.client.android.service.ListenerBinder;
import com.google.ipc.invalidation.external.client.android.service.ListenerService;
import com.google.ipc.invalidation.external.client.android.service.Message;
import com.google.ipc.invalidation.external.client.android.service.Request;
import com.google.ipc.invalidation.external.client.android.service.Response;
import com.google.ipc.invalidation.external.client.android.service.Response.Builder;
import com.google.ipc.invalidation.ticl.android.AbstractInvalidationService;

import android.accounts.Account;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;

import junit.framework.Assert;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * A stub invalidation service implementation that can be used to test the
 * client library or invalidation applications. The test service will validate
 * all incoming events sent by the client. It also supports the ability to store
 * all incoming action intents and outgoing event intents and make them
 * available for retrieval via the {@link InvalidationTest} interface.
 * <p>
 * The implementation of service intent handling will simply log the invocation
 * and do nothing else.
 *
 */
public class InvalidationTestService extends AbstractInvalidationService {

  private static class ClientState {
    final Account account;
    final String authType;
    final Intent eventIntent;

    private ClientState(Account account, String authType, Intent eventIntent) {
      this.account = account;
      this.authType = authType;
      this.eventIntent = eventIntent;
    }
  }

  /**
   * Intent that can be used to bind to the InvalidationTest service.
   */
  public static final Intent TEST_INTENT = new Intent("com.google.ipc.invalidation.TEST");

  /** Logging tag */
  private static final String TAG = "InvalidationTestService";

  /** Map of currently active clients from key to {@link ClientState} */
  private static Map<String, ClientState> clientMap = new HashMap<String, ClientState>();

  /** {@code true} the test service should capture actions */
  private static boolean captureActions;

  /** The stored actions that are available for retrieval */
  private static List<Bundle> actions = new ArrayList<Bundle>();

  /** {@code true} if the client should capture events */
  private static boolean captureEvents;

  /** The stored events that are available for retrieval */
  private static List<Bundle> events = new ArrayList<Bundle>();

  public InvalidationTestService() {
  }

  /**
   * InvalidationTest stub to handle calls from clients.
   */
  private final InvalidationTest.Stub testBinder = new InvalidationTest.Stub() {

    @Override
    public void setCapture(boolean captureActions, boolean captureEvents) {
      InvalidationTestService.captureActions = captureActions;
      InvalidationTestService.captureEvents = captureEvents;
    }

    @Override
    public Bundle[] getRequests() {
      Log.d(TAG, "Reading actions from " + actions + ":" + actions.size());
      Bundle[] value = new Bundle[actions.size()];
      actions.toArray(value);
      actions.clear();
      return value;
    }

    @Override
    public Bundle[] getEvents() {
      Bundle[] value = new Bundle[events.size()];
      events.toArray(value);
      events.clear();
      return value;
    }

    @Override
    public void sendEvent(Bundle eventBundle) {

      // Retrive info for that target client
      String clientKey = eventBundle.getString(Request.Parameter.CLIENT);
      ClientState state = clientMap.get(clientKey);
      Preconditions.checkNotNull(state);

      // Bind to the listener associated with the client and send the event
      ListenerBinder binder = null;
      InvalidationTestService theService = InvalidationTestService.this;
      try {
        binder = new ListenerBinder(state.eventIntent, InvalidationTestListener.class.getName());
        ListenerService service = binder.bind(theService);
        theService.sendEvent(service, new Event(eventBundle));
      } finally {
        // Ensure that listener binding is released
        if (binder != null) {
          binder.unbind(theService);
        }
      }
    }

    @Override
    public void reset() {
      Log.i(TAG, "Resetting test service");
      captureActions = false;
      captureEvents = false;
      clientMap.clear();
      actions.clear();
      events.clear();
    }
  };

  @Override
  public void onCreate() {
    Log.i(TAG, "onCreate");
    super.onCreate();
  }

  @Override
  public void onDestroy() {
    Log.i(TAG, "onDestroy");
    super.onDestroy();
  }

  @Override
  public void onStart(Intent intent, int startId) {
    Log.i(TAG, "onStart");
    super.onStart(intent, startId);
  }

  @Override
  public IBinder onBind(Intent intent) {
    Log.i(TAG, "onBind");

    // For InvalidationService binding, delegate to the superclass
    if (Request.SERVICE_INTENT.getAction().equals(intent.getAction())) {
      return super.onBind(intent);
    }

    // Otherwise, return the test interface binder
    return testBinder;
  }

  @Override
  public boolean onUnbind(Intent intent) {
    Log.i(TAG, "onUnbind");
    return super.onUnbind(intent);
  }

  @Override
  protected void handleRequest(Bundle input, Bundle output) {
    if (captureActions) {
      actions.add(input);
    }
    super.handleRequest(input, output);
    validateResponse(output);
  }

  @Override
  protected void sendEvent(ListenerService listenerService, Event event) {
    if (captureEvents) {
      events.add(event.getBundle());
    }
    super.sendEvent(listenerService, event);
  }


  @Override
  protected void create(Request request, Builder response) {
    validateRequest(request,
        Request.Action.CREATE,
        Request.Parameter.ACTION,
        Request.Parameter.CLIENT,
        Request.Parameter.CLIENT_TYPE,
        Request.Parameter.ACCOUNT,
        Request.Parameter.AUTH_TYPE,
        Request.Parameter.INTENT);
    Log.i(TAG, "Creating client " + request.getClientKey() + ":" + clientMap.keySet());
    clientMap.put(
        request.getClientKey(), new ClientState(request.getAccount(), request.getAuthType(),
            request.getIntent()));
    response.setStatus(Response.Status.SUCCESS);
  }

  @Override
  protected void resume(Request request, Builder response) {
    validateRequest(
        request, Request.Action.RESUME, Request.Parameter.ACTION, Request.Parameter.CLIENT);
    ClientState state = clientMap.get(request.getClientKey());
    if (state != null) {
      Log.i(TAG, "Resuming client " + request.getClientKey() + ":" + clientMap.keySet());
      response.setStatus(Response.Status.SUCCESS);
      response.setAccount(state.account);
      response.setAuthType(state.authType);
    } else {
      Log.w(TAG, "Cannot resume client " + request.getClientKey() + ":" + clientMap.keySet());
      response.setStatus(Response.Status.INVALID_CLIENT);
    }
  }

  @Override
  protected void register(Request request, Builder response) {
    // Ensure that one (and only one) of the variant object id forms is used
    String objectParam =
      request.getBundle().containsKey(Request.Parameter.OBJECT_ID) ?
          Request.Parameter.OBJECT_ID :
          Request.Parameter.OBJECT_ID_LIST;
    validateRequest(request, Request.Action.REGISTER, Message.Parameter.ACTION,
        Message.Parameter.CLIENT, objectParam);
    if (!validateClient(request)) {
      response.setStatus(Response.Status.INVALID_CLIENT);
      return;
    }
    response.setStatus(Response.Status.SUCCESS);
  }

  @Override
  protected void unregister(Request request, Builder response) {
    // Ensure that one (and only one) of the variant object id forms is used
    String objectParam =
      request.getBundle().containsKey(Request.Parameter.OBJECT_ID) ?
          Request.Parameter.OBJECT_ID :
          Request.Parameter.OBJECT_ID_LIST;
    validateRequest(request, Request.Action.UNREGISTER, Request.Parameter.ACTION,
        Request.Parameter.CLIENT, objectParam);
    if (!validateClient(request)) {
      response.setStatus(Response.Status.INVALID_CLIENT);
      return;
    }
    response.setStatus(Response.Status.SUCCESS);
  }

  @Override
  protected void start(Request request, Builder response) {
    validateRequest(
        request, Request.Action.START, Request.Parameter.ACTION, Request.Parameter.CLIENT);
    if (!validateClient(request)) {
      response.setStatus(Response.Status.INVALID_CLIENT);
      return;
    }
    response.setStatus(Response.Status.SUCCESS);
  }

  @Override
  protected void stop(Request request, Builder response) {
    validateRequest(
        request, Request.Action.STOP, Message.Parameter.ACTION, Message.Parameter.CLIENT);
    if (!validateClient(request)) {
      response.setStatus(Response.Status.INVALID_CLIENT);
      return;
    }
    response.setStatus(Response.Status.SUCCESS);
  }

  @Override
  protected void acknowledge(Request request, Builder response) {
    validateRequest(request, Request.Action.ACKNOWLEDGE, Request.Parameter.ACTION,
        Request.Parameter.CLIENT, Request.Parameter.ACK_TOKEN);
    if (!validateClient(request)) {
      response.setStatus(Response.Status.INVALID_CLIENT);
      return;
    }
    response.setStatus(Response.Status.SUCCESS);
  }

  /**
   * Validates that the client associated with the request is one that has
   * previously been created or resumed on the test service.
   */
  private boolean validateClient(Request request) {
    if (!clientMap.containsKey(request.getClientKey())) {
      Log.w(TAG,
          "Client " + request.getClientKey() + " is not an active client: " + clientMap.keySet());
      return false;
    }
    return true;
  }

  /**
   * Validates that the request contains exactly the set of parameters expected.
   *
   * @param request request to validate
   * @param action expected action
   * @param parameters expected parameters
   */
  private void validateRequest(Request request, String action, String... parameters) {
    Assert.assertEquals(action, request.getAction());
    List<String> expectedParameters = new ArrayList<String>(Arrays.asList(parameters));
    Bundle requestBundle = request.getBundle();
    for (String parameter : requestBundle.keySet()) {
      Assert.assertTrue("Unexpected parameter: " + parameter, expectedParameters.remove(parameter));

      // Validate the value
      Object value = requestBundle.get(parameter);
      Assert.assertNotNull(value);
    }
    Assert.assertTrue("Missing parameter:" + expectedParameters, expectedParameters.isEmpty());
  }

  /**
   * Validates a response bundle being returned to a client contains valid
   * success response.
   */
  protected void validateResponse(Bundle output) {
    int status = output.getInt(Response.Parameter.STATUS, Response.Status.UNKNOWN);
    Assert.assertEquals("Unexpected failure: " + output, Response.Status.SUCCESS, status);
    String error = output.getString(Response.Parameter.ERROR);
    Assert.assertNull(error);
  }
}
