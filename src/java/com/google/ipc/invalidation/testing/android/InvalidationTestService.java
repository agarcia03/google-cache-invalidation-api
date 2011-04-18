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


import com.google.ipc.invalidation.external.android.InvalidationTypes.AckToken;
import com.google.ipc.invalidation.external.android.InvalidationTypes.Invalidation;
import com.google.ipc.invalidation.external.android.InvalidationTypes.ObjectId;
import com.google.ipc.invalidation.external.android.InvalidationTypes.RegistrationState;
import com.google.ipc.invalidation.external.android.InvalidationTypes.UnknownHint;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents.Actions;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents.Extras;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents.ParcelableObjectId;
import com.google.ipc.invalidation.ticl.android.AbstractInvalidationService;

import android.accounts.Account;
import android.content.Intent;
import android.content.IntentSender;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;

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

  /** Logging tag */
  private static final String TAG = "InvalidationTestService";

  /** Map of currently active clients from id to {@link IntentSender} */
  static Map<String, IntentSender> clientMap = new HashMap<String, IntentSender>();

  /** {@code true} the test service should capture actions */
  static boolean captureActions;

  /** The stored actions that are available for retrieval */
  static List<Intent> actions = new ArrayList<Intent>();

  /** {@code true} if the client should capture events */
  static boolean captureEvents;

  /** The stored events that are available for retrieval */
  static List<Intent> events = new ArrayList<Intent>();

  /**
   * Contains a map from {@link Actions} name to the set of {@link Extras} names that are
   * may be set.
   */
  private static final Map<String, List<String>> actionExtrasMap =
      new HashMap<String, List<String>>();

  static {
    actionExtrasMap.put(Actions.ACKNOWLEDGE,
        Arrays.asList(new String [] {Extras.CLIENT, Extras.ACK_TOKEN, Extras.INVALIDATION}));
    actionExtrasMap.put(Actions.REGISTER,
        Arrays.asList(new String [] {Extras.CLIENT, Extras.OBJECT_ID}));
    actionExtrasMap.put(Actions.SET_AUTH,
        Arrays.asList(new String [] {Extras.CLIENT, Extras.SOURCE, Extras.AUTH_TOKEN}));
    actionExtrasMap.put(Actions.CREATE,
        Arrays.asList(new String [] {Extras.CLIENT, Extras.ACCOUNT, Extras.SENDER}));
    actionExtrasMap.put(Actions.UNREGISTER,
        Arrays.asList(new String [] {Extras.CLIENT, Extras.OBJECT_ID}));
  }

  /**
   * Contains a map from {@link Actions} name to the set of {@link Extras} names that are
   * optional.   Any valid extras not found in this list are considered to be required.
   */
  private static final Map<String, List<String>> actionOptionalExtrasMap =
      new HashMap<String, List<String>>();

  static {
    actionOptionalExtrasMap.put(Actions.ACKNOWLEDGE,
        Arrays.asList(new String [] {Extras.INVALIDATION}));
  }

  /**
   * Maps from an {@link Extras} name to the expected type of the extra's value.
   * Used to validate that a passed extra has the appropriate type.
   */
  private static final Map<String, Class<?>> extraTypeMap = new HashMap<String, Class<?>>();

  static {
    extraTypeMap.put(Extras.ACCOUNT, Account.class);
    extraTypeMap.put(Extras.CLIENT, String.class);
    extraTypeMap.put(Extras.AUTH_TOKEN, String.class);
    extraTypeMap.put(Extras.INVALIDATION, Invalidation.class);
    extraTypeMap.put(Extras.OBJECT_ID, ParcelableObjectId.class);
    extraTypeMap.put(Extras.SENDER, IntentSender.class);
    extraTypeMap.put(Extras.SOURCE, Integer.class);
    extraTypeMap.put(Extras.STATE, RegistrationState.class);
    extraTypeMap.put(Extras.UNKNOWN_HINT, UnknownHint.class);
  }


  public InvalidationTestService() {}

  private final InvalidationTest.Stub mBinder = new InvalidationTest.Stub() {

    @Override
    public void setCapture(boolean captureActions, boolean captureEvents) {
      InvalidationTestService.captureActions = captureActions;
      InvalidationTestService.captureEvents = captureEvents;
    }

    @Override
    public Intent [] getActionIntents() {
      Log.d(TAG, "Reading actions from " + actions + ":" + actions.size());
      Intent [] value = new Intent[actions.size()];
      actions.toArray(value);
      actions.clear();
      return value;
    }

    @Override
    public Intent [] getEventIntents() {
      Intent [] value = new Intent[events.size()];
      events.toArray(value);
      events.clear();
      return value;
    }

    @Override
    public void sendEventIntent(String clientKey, Intent event) {
      IntentSender sender = clientMap.get(clientKey);
      if (sender == null) {
        throw new IllegalStateException("Invalid clientId:" + clientKey);
      }
      sendEvent(sender, event);
    }

    @Override
    public void reset() {
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
  public int onStartCommand(Intent intent, int flags, int startId) {
    try {
      /*
       * Log the intent (for test debugging) and do basic validation, then
       * delegate to the base class.
       */
      Log.i(TAG, "onStartCommand:" + intent.getAction());
      validateIntent(intent);
      Log.d(TAG, "Added intent to " + actions + ":" + actions.size());
      int result = super.onStartCommand(intent, flags, startId);
      return result;
    } catch (Exception e) {
      Log.e(TAG, e.getClass().getSimpleName() + " on " + intent.getAction() + ":" + e.getMessage());
      // TODO: Make this available to test clients!
      return START_NOT_STICKY;
    } finally {
      // Add this only after all processing is complete, so clients can poll on
      // the getActionIntents API to wait for completion.
      actions.add(intent);
    }
  }

  @Override
  public IBinder onBind(Intent intent) {
    Log.i(TAG, "onBind");
    return mBinder;
  }

  @Override
  protected void doCreate(String clientKey, Account account, IntentSender sender) {
    Log.i(TAG, "onCreate");
    clientMap.put(clientKey, sender);
  }

  @Override
  protected void doSetAuth(String clientKey, int source, String authToken) {
    Log.i(TAG, "doSetAuth");
  }

  @Override
  protected void doRegister(String clientKey, ObjectId objectId) {
    Log.i(TAG, "doRegister");
  }

  @Override
  protected void doUnregister(String clientKey, ObjectId objectId) {
    Log.i(TAG, "doUnregister");
  }

  @Override
  protected void doAcknowledge(String clientKey, AckToken ackToken) {
    Log.i(TAG, "doAcknowledge");
  }

  /**
   * Validates that an intent contains all of the expected extra values for the
   * given action type and where possible will validate the extra values
   * themselves.
   *
   * @param intent intent to validate
   * @throws Exception if the intent is invalid in any way
   */
  protected void validateIntent(Intent intent) throws Exception {
    // Validate the action and category
    String action = intent.getAction();
    if (action == null) {
      throw new NullPointerException("action");
    }
    List<String> actionExtras = actionExtrasMap.get(action);
    if (actionExtras == null) {
      throw new IllegalStateException("Invalid action:" + action);
    }
    if (!intent.getCategories().contains(InvalidationIntents.ACTION_CATEGORY)){
      throw new IllegalStateException("Missing action category");
    }

    // Validate extras
    Bundle extras = intent.getExtras();
    List<String> expectedExtras = new ArrayList<String>(actionExtras);
    for (String extraName : extras.keySet()) {
      if (!expectedExtras.remove(extraName)) {
        throw new IllegalStateException("Unexpected extra:" + extraName);
      }

      // Validate the value type
      Object value = extras.get(extraName);
      if (value == null) {
        throw new NullPointerException("Null value for " + extraName + " extra");
      }
      Class<?> extraType = extraTypeMap.get(extraName);
      if (!extraType.isAssignableFrom(value.getClass())) {
        throw new IllegalStateException("Illegal type for " + extraName + "extra," +
            "Expected: " + extraType + ", found: " + value.getClass());
      }
    }
    List<String> optionalExtras = actionOptionalExtrasMap.get(action);
    if (optionalExtras != null) {
      expectedExtras.removeAll(optionalExtras);
    }
    if (!expectedExtras.isEmpty()) {
      throw new IllegalStateException("Missing extras:" + expectedExtras.toArray());
    }
  }
}
