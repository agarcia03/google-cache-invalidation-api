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

package com.google.ipc.invalidation.external.android;

import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.createServiceIntent;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putAccount;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putAckToken;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putClientId;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putObjectId;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putSender;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.putSource;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.android.InvalidationTypes.AckToken;
import com.google.ipc.invalidation.external.android.InvalidationTypes.ObjectId;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents.Actions;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents.Extras;

import android.accounts.Account;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;

import javax.annotation.Nullable;

/**
 * Implementation of the {@link InvalidationClient} interface for Android.
 * Instances of the class are obtained using {@link AndroidClientFactory#create}
 * or {@link AndroidClientFactory#resume}.
 * <p>
 * The class provides implementations of the {@link InvalidationClient} methods
 * that delegate to the  invalidation service running on the device using
 * the intents model defined in {@link InvalidationIntents}.
 *
 */
final class AndroidInvalidationClient implements InvalidationClient {

  /**
   * The application context associated with the client.
   */
  public final Context context;

  /**
   * Contains the application identifier associated with this client.
   */
  private final String appId;

  /**
   * The Account associated with this client. May be {@code null} for resumed
   * clients.
   */
  private final Account account;

  /**
   * The {@link InvalidationListener} broadcast receiver that handles events for
   * this client. May be {@code null} for resumed clients.
   */
  private final Class<? extends AndroidInvalidationListener> listenerClass;

  /**
   * Contains {@code false} for newly started clients or {@code true} for
   * resumed ones.
   */
  private final boolean isResumed;

  /**
   * Creates a new invalidation client with the provided application id and
   * account that sends invalidation events to the specified component.
   *
   * @param context the execution context for the client.
   * @param clientId a unique id that identifies the created client within the
   *        scope of the application.   May be {@code null} if there is only a
   *        single invalidation client/listener for the application.
   * @param account the user account associated with the client.
   * @param listenerClass the {@link AndroidInvalidationListener} subclass that
   *        will handle invalidation events.
   */
  AndroidInvalidationClient(Context context, @Nullable String clientId, Account account,
      Class<? extends AndroidInvalidationListener> listenerClass) {
    this.context = context;
    this.appId = computeAppId(context, clientId);
    this.account = account;
    this.listenerClass = listenerClass;
    this.isResumed = false;
  }

  /**
   * Constructs a resumed invalidation client with the provided application id
   * and context.
   *
   * @param context the application context for the client.
   * @param clientId a unique id that identifies the resumed client within the
   *        scope of the application.   May be {@code null} if there is only a
   *        single invalidation client/listener for the application.
   */
  AndroidInvalidationClient(Context context, String clientId) {
    this.appId = computeAppId(context, clientId);
    this.context = context;
    this.account = null;
    this.listenerClass = null;
    this.isResumed = true;
  }

  @Override
  public void start() {
    if (!isResumed) {
      Intent intent = createServiceIntent(Actions.CREATE);
      putClientId(intent, appId);
      putAccount(intent, account);

      // Create an event intent that targets the requested listener and use it
      // to derive and intent sender that can be passed to the service.
      Intent eventIntent = new Intent(context, listenerClass);
      eventIntent.addCategory(InvalidationIntents.EVENT_CATEGORY);
      PendingIntent pendingIntent = PendingIntent.getBroadcast(context, 0, eventIntent, 0);
      putSender(intent, pendingIntent.getIntentSender());
      context.startService(intent);
    }
  }

  /**
   * Returns the application id for this client.
   */
  public String getAppId() {
    return appId;
  }

  /**
   * Sets the authentication token that should be used to register or unregister
   * invalidations for a particular invalidation source.
   *
   * @param source the invalidation source
   * @param authToken the authentication token to use for the provided source
   *        type.
   */
  public void setAuthToken(int source, String authToken) {
    Preconditions.checkNotNull(source);
    Intent intent = createServiceIntent(Actions.SET_AUTH);
    putClientId(intent, appId);
    putSource(intent, source);
    if (authToken != null) {
      intent.putExtra(Extras.AUTH_TOKEN, authToken);
    }
    context.startService(intent);
  }

  /**
   * Registers to receive invalidation notifications for an object.
   *
   * @param objectId object id.
   */
  public void register(ObjectId objectId) {
    Preconditions.checkNotNull(objectId);
    Intent intent = createServiceIntent(Actions.REGISTER);
    putClientId(intent, appId);
    putObjectId(intent, objectId);
    context.startService(intent);
  }

  /**
   * Unregisters to disable receipt of invalidations on an object.
   *
   * @param objectId
   */
  public void unregister(ObjectId objectId) {
    Preconditions.checkNotNull(objectId);
    Intent intent = createServiceIntent(Actions.UNREGISTER);
    putClientId(intent, appId);
    putObjectId(intent, objectId);
    context.startService(intent);
  }

  @Override
  public void acknowledge(AckToken ackToken) {
    Preconditions.checkNotNull(ackToken, "ackToken");
    Intent intent = createServiceIntent(Actions.ACKNOWLEDGE);
    putClientId(intent, appId);
    putAckToken(intent, ackToken);
    context.startService(intent);
  }

  private static final String computeAppId(Context context, String clientId) {
    StringBuilder builder = new StringBuilder(context.getApplicationInfo().packageName);
    if (clientId != null) {
      builder.append("-");
      builder.append(clientId);
    }
    return builder.toString();
  }
}
