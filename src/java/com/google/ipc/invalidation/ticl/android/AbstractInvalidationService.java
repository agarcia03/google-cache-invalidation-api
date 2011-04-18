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

package com.google.ipc.invalidation.ticl.android;

import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.createEventIntent;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getAccount;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getAckToken;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getAuthToken;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getClientKey;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getObjectId;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getSender;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getSource;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putAckToken;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putClientKey;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putInvalidation;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putObjectId;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putRegistrationState;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putSource;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putUnknownHint;

import com.google.ipc.invalidation.external.android.InvalidationTypes.AckToken;
import com.google.ipc.invalidation.external.android.InvalidationTypes.Invalidation;
import com.google.ipc.invalidation.external.android.InvalidationTypes.ObjectId;
import com.google.ipc.invalidation.external.android.InvalidationTypes.RegistrationState;
import com.google.ipc.invalidation.external.android.InvalidationTypes.UnknownHint;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents.Actions;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents.Events;

import android.accounts.Account;
import android.app.Service;
import android.content.Intent;
import android.content.IntentSender;
import android.content.IntentSender.SendIntentException;
import android.os.IBinder;
import android.util.Log;

import javax.annotation.Nullable;

/**
 * Abstract base class for implementing the  invalidation service. The
 * service implements the set of actions defined in
 * {@link InvalidationIntents.Actions}. For each supported action, the service
 * will extract the action parameters and invoke an abstract {@code do} methods
 * that will be implemented by subclasses to provide the action-specific
 * processing model.
 * <p>
 * The class also provides {@code sendEvent} methods that can be used to
 * generate events back to the client.
 *
 */
public abstract class AbstractInvalidationService extends Service {

  private static final String TAG = "AbstractInvalidationService";

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    try {
      String action = intent.getAction();
      String clientKey = getClientKey(intent);

      // Ensure that intent parcelable types can be unmarshalled
      intent.setExtrasClassLoader(InvalidationIntents.class.getClassLoader());
      if (clientKey == null) {
        throw new IllegalArgumentException("Missing client id");
      }
      // Switch by action, extract parameters, and all abstract impl method.
      if (Actions.CREATE.equals(action)) {
        Account account = getAccount(intent);
        IntentSender sender = getSender(intent);
        doCreate(clientKey, account, sender);
      } else if (Actions.SET_AUTH.equals(action)) {
        int source = getSource(intent);
        String authToken = getAuthToken(intent);
        doSetAuth(clientKey, source, authToken);
      } else if (Actions.REGISTER.equals(action)) {
        ObjectId objectId = getObjectId(intent);
        doRegister(clientKey, objectId);
      } else if (Actions.UNREGISTER.equals(action)) {
        ObjectId objectId = getObjectId(intent);
        doUnregister(clientKey, objectId);
      } else if (Actions.ACKNOWLEDGE.equals(action)) {
        AckToken ackToken = getAckToken(intent);
        doAcknowledge(clientKey, ackToken);
      } else {
        throw new IllegalArgumentException("Unexpected action:" + action);
      }
    } catch (IllegalArgumentException iae) {
      Log.e(TAG, "Unable to handle command", iae);
    }
    return START_NOT_STICKY;
  }

  @Override
  public IBinder onBind(Intent intent) {
    // The service does not currently expose a bound service interface.
    return null;
  }

  /**
   * Called when a {@link Actions#CREATE} intent is received by the service.
   *
   * @param clientKey client key
   * @param account user account
   * @param sender intent sender for events
   */
  protected abstract void doCreate(String clientKey, Account account, IntentSender sender);

  /**
   * Called when a {@link Actions#SET_AUTH} intent is received by the service.
   *
   * @param clientKey key of calling client
   * @param source invalidation source type
   * @param authToken authentication token or {@code null}.
   */
  protected abstract void doSetAuth(String clientKey, int source, String authToken);

  /**
   * Called when a {@link Actions#REGISTER} intent is received by the service.
   *
   * @param clientKey key of calling client
   * @param objectId id of the object to register
   */
  protected abstract void doRegister(String clientKey, ObjectId objectId);

  /**
   * Called when a {@link Actions#UNREGISTER} intent is received by the service.
   *
   * @param clientKey key of calling client
   * @param objectId id of the object to unregister
   */
  protected abstract void doUnregister(String clientKey, ObjectId objectId);

  /**
   * Called when a {@link Actions#ACKNOWLEDGE} intent is received by the
   * service.
   *
   * @param clientKey key of the calling client
   * @param ackToken for the event being acknowledged
   */
  protected abstract void doAcknowledge(String clientKey, AckToken ackToken);

  /**
   * Creates a new intent with the specified action that targets a particular
   * client id and receiver combination.
   *
   * @param action intent action
   * @param clientKey target client key
   * @return the created intent
   */
  protected Intent createIntent(String action, String clientKey) {
    Intent intent = createEventIntent(action);
    putClientKey(intent, clientKey);
    return intent;
  }

  /**
   * Sends an {@link Events#INVALIDATE} event to an invalidation service client.
   *
   * @param clientKey receiving client key
   */
  protected void sendInvalidationEvent(
      String clientKey, IntentSender sender, Invalidation invalidation, AckToken ackToken) {
    Intent intent = createIntent(Events.INVALIDATE, clientKey);
    putInvalidation(intent, invalidation, true); // include payload if present
    putAckToken(intent, ackToken);
    sendEvent(sender, intent);
  }

  /**
   * Sends an {@link Events#INVALIDATE_ALL} event to an invalidation service
   * client.
   *
   * @param clientKey receiving client key
   */
  protected void sendInvalidateAllEvent(String clientKey, IntentSender sender, AckToken ackToken) {
    Intent intent = createIntent(Events.INVALIDATE_ALL, clientKey);
    putAckToken(intent, ackToken);
    sendEvent(sender, intent);
  }

  /**
   * Sends an {@link Events#REGISTRATION_CHANGED} event to an invalidation
   * service client.
   *
   * @param clientKey receiving client key
   * @param sender intent sender
   * @param objectId the object being invalidated
   * @param state the new registration state
   * @param hint a hint if the state is unknown or {@code null}
   * @param ackToken the acknowledgement token to use
   */
  protected void sendRegistrationChanged(String clientKey, IntentSender sender, ObjectId objectId,
      RegistrationState state, @Nullable UnknownHint hint, AckToken ackToken) {
    Intent intent = createIntent(Events.REGISTRATION_CHANGED, clientKey);
    putObjectId(intent, objectId);
    putRegistrationState(intent, state);
    if (hint != null) {
      putUnknownHint(intent, hint);
    }
    putAckToken(intent, ackToken);
    sendEvent(sender, intent);
  }

  /**
   * Sends an {@link Events#INVALID_AUTH_TOKEN} event to an invalidation service
   * client.
   *
   * @param clientKey receiving client key
   * @param sender intent sender for events
   * @param source the invalid source type for the invalid token
   * @param ackToken the acknowledgement token to use
   */
  protected void sendInvalidAuthTokenEvent(
      String clientKey, IntentSender sender, int source, AckToken ackToken) {
    Intent intent = createIntent(Events.INVALIDATE_ALL, clientKey);
    putSource(intent, source);
    putAckToken(intent, ackToken);
    sendEvent(sender, intent);
  }

  /**
   * Logs and send event intents back to client applications.
   */
  protected void sendEvent(IntentSender sender, Intent intent) {
      try {
        Log.i(TAG, "Sending " + intent.getAction() + " event");
        sender.sendIntent(this, 0, intent, null, null);
      } catch (SendIntentException se) {
        Log.e(TAG, "Unable to send intent", se);
      }
  }
}
