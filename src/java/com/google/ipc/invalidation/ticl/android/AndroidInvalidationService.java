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

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.android.AndroidInvalidationClient;
import com.google.ipc.invalidation.external.client.android.service.AndroidClientException;
import com.google.ipc.invalidation.external.client.android.service.InvalidationService;
import com.google.ipc.invalidation.external.client.android.service.Request;
import com.google.ipc.invalidation.external.client.android.service.Response.Builder;
import com.google.ipc.invalidation.external.client.android.service.Response.Status;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ObjectId;
import com.google.ipc.invalidation.ticl.android.c2dm.C2DMessaging;

import android.accounts.Account;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.util.Log;

import java.util.List;

/**
 * The AndroidInvalidationService class provides an Android service implementation that bridges
 * between the {@link InvalidationService} interface and invalidation client service instances
 * executing within the scope of that service. The invalidation service will have an associated
 * {@link AndroidClientManager} that is managing the set of active (in memory) clients associated
 * with the service.   It process requests from invalidation applications (as invocations on
 * the {@link InvalidationService} bound service interface along with C2DM registration and
 * activity (from {@link AndroidC2DMReceiver}.
 *
 */
public class AndroidInvalidationService extends AbstractInvalidationService {

  private static final String TAG = "AndroidInvalidationService";

  /**
   * The name of the {@code meta-data} element on this service that contains the HTTP URL
   * of the channel service.
   */
  public static final String CHANNEL_URL = "channel-url";

   static AndroidClientManager clientManager;

  // The AndroidInvalidationService handles a set of internal intents that are used for
  // communication and coordination between the it and the C2DM handling service.   These
  // are documented here with action and extra names documented with package private
  // visibility since they are not intended for use by external components.

  /**
   * Sent when a new C2DM registration activity occurs for the service. This can occur the first
   * time the service is run or at any subsequent time if the Android C2DM service decides to issue
   * a new C2DM registration ID.
   */
  static final String REGISTRATION_ACTION = "register";

  /**
   * The name of the String extra that contains the registration ID for a register intent.  If this
   * extra is not present, then it indicates that a C2DM notification regarding unregistration has
   * been received (not expected during normal operation conditions).
   */
  static final String REGISTER_ID = "id";

  /**
   * This intent is sent when a C2DM message targeting the service is received.
   */
  static final String MESSAGE_ACTION = "message";

  /**
   * The name of the String extra that contains the client key for the C2DM message.
   */
  static final String MESSAGE_CLIENT_KEY = "clientKey";

  /**
   * The name of the String extra that contains the mail box id for the C2DM message.
   */
  static final String MESSAGE_MAILBOX_ID = "mailBoxId";

  /**
   * The name of the byte array extra that contains the encoded event for the C2DM message.
   */
  static final String MESSAGE_DATA = "data";

  /**
   * This intent is sent when C2DM registration has failed irrevocably.
   */
  static final String ERROR_ACTION = "error";

  /**
   * The name of the String extra that contains the error message describing the registration
   * failure.
   */
  static final String ERROR_MESSAGE = "message";

  /**
   * Creates a new registration intent that notifies the service of a registration ID change
   */
  static Intent createRegistrationIntent(Context context, String registrationId) {
    Intent intent = new Intent(REGISTRATION_ACTION);
    intent.setClass(context, AndroidInvalidationService.class);
    if (registrationId != null) {
      intent.putExtra(AndroidInvalidationService.REGISTER_ID, registrationId);
    }
    return intent;
  }

  /**
   * Creates a new message intent to contains event data to deliver directly to a client.
   */
  static Intent createDataIntent(Context context, String clientKey, byte [] data) {
    Intent intent = new Intent(MESSAGE_ACTION);
    intent.setClass(context, AndroidInvalidationService.class);
    intent.putExtra(MESSAGE_CLIENT_KEY, clientKey);
    intent.putExtra(MESSAGE_DATA, data);
    return intent;
  }

  /**
   * Creates a new message intent that references event data to retrieve from a mailbox.
   */
  static Intent createMailboxIntent(Context context, String clientKey, String mailboxId) {
    Intent intent = new Intent(MESSAGE_ACTION);
    intent.setClass(context, AndroidInvalidationService.class);
    intent.putExtra(MESSAGE_CLIENT_KEY, clientKey);
    intent.putExtra(MESSAGE_MAILBOX_ID, mailboxId);
    return intent;
  }

  /**
   * Creates a new error intent that notifies the service of a registration failure.
   */
  static Intent createErrorIntent(Context context, String errorId) {
    Intent intent = new Intent(ERROR_ACTION);
    intent.setClass(context, AndroidInvalidationService.class);
    intent.putExtra(ERROR_MESSAGE, errorId);
    return intent;
  }

  /**
   * The HTTP URL of the channel service.  This value is retrieved from the {@code channel-url}
   * metadata attribute of the service.
   */
  String channelUrl;

  /**
   * Resets the state of the service to destroy any existing clients
   */
  
  static void reset() {
    if (clientManager != null) {
      clientManager.releaseAll();
    }
  }

  @Override
  public void onCreate() {
    super.onCreate();

    // Retrieve the channel URL from service metadata
    List<ResolveInfo> resolveInfos =
        getPackageManager().queryIntentServices(Request.SERVICE_INTENT,
            PackageManager.GET_META_DATA);
    Preconditions.checkState(!resolveInfos.isEmpty(), "Cannot find service metadata");
    ServiceInfo serviceInfo = resolveInfos.get(0).serviceInfo;
    if (serviceInfo.metaData != null) {
      channelUrl = serviceInfo.metaData.getString(CHANNEL_URL);
      if (channelUrl == null) {
        Log.e(TAG, "No meta-data element with the name " + CHANNEL_URL +
        "found on the service declaration.  An element with this name must have a value that is " +
        "the invalidation channel frontend url");
        stopSelf();
      }
    } else {
      Log.e(TAG, "No meta-data elements found on the service declaration. One with a name of " +
          CHANNEL_URL + "must have a value that is the invalidation channel frontend url.");
      stopSelf();
    }

    // Retrieve the current registration ID and normalize the empty string value (for none)
    // to null
    String registrationId = C2DMessaging.getRegistrationId(this);
    if (registrationId.length() == 0) {
      registrationId = null;
    }

    Log.i(TAG, "Creating:" + registrationId);

    if (clientManager == null) {
      clientManager = new AndroidClientManager(this, registrationId);
    } else {
      clientManager.setRegistrationId(registrationId);
    }

    // If there is no C2DM registration associated with the application than register.
    if (registrationId == null) {
      Log.i(TAG, "Registering for C2DM events for " + AndroidC2DMConstants.SENDER_ID);
      C2DMessaging.register(this, AndroidC2DMConstants.SENDER_ID);
    }
  }

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {

    // Process C2DM related messages from the AndroidC2DMReceiver service
    Log.d(TAG, "Received " + intent.getAction());
    if (MESSAGE_ACTION.equals(intent.getAction())) {
      handleC2dmMessage(intent);
    } else if (REGISTRATION_ACTION.equals(intent.getAction())) {
      handleRegistration(intent);
    } else if (ERROR_ACTION.equals(intent.getAction())) {
      handleError(intent);
    }
    return super.onStartCommand(intent, flags, startId);
  }

  @Override
  public void onDestroy() {
    super.onDestroy();
    reset();
  }

  @Override
  protected void create(Request request, Builder response) {
    String clientKey = request.getClientKey();
    int clientType = request.getClientType();
    Account account = request.getAccount();
    String authType = request.getAuthType();
    Intent eventIntent = request.getIntent();
    clientManager.create(clientKey, clientType, account, authType, eventIntent);
    response.setStatus(Status.SUCCESS);
  }

  @Override
  protected void resume(Request request, Builder response) {
    String clientKey = request.getClientKey();
    AndroidInvalidationClient client = clientManager.get(clientKey);
    response.setStatus(Status.SUCCESS);
    response.setAccount(client.getAccount());
    response.setAuthType(client.getAuthType());
  }

  @Override
  protected void start(Request request, Builder response) {
    String clientKey = request.getClientKey();
    AndroidInvalidationClient client = clientManager.get(clientKey);
    client.start();
    response.setStatus(Status.SUCCESS);
  }

  @Override
  protected void stop(Request request, Builder response) {
    String clientKey = request.getClientKey();
    AndroidInvalidationClient client = clientManager.get(clientKey);
    client.stop();
    response.setStatus(Status.SUCCESS);
  }

  @Override
  protected void register(Request request, Builder response) {
    String clientKey = request.getClientKey();
    AndroidInvalidationClient client = clientManager.get(clientKey);
    ObjectId objectId = request.getObjectId();
    if (objectId != null) {
      client.register(objectId);
    } else {
      client.register(request.getObjectId());
    }
    response.setStatus(Status.SUCCESS);
  }

  @Override
  protected void unregister(Request request, Builder response) {
    String clientKey = request.getClientKey();
    AndroidInvalidationClient client = clientManager.get(clientKey);
    ObjectId objectId = request.getObjectId();
    if (objectId != null) {
      client.unregister(objectId);
    } else {
      client.unregister(request.getObjectId());
    }
    response.setStatus(Status.SUCCESS);
  }

  @Override
  protected void acknowledge(Request request, Builder response) {
    String clientKey = request.getClientKey();
    AckHandle ackHandle = request.getAckHandle();
    AndroidInvalidationClient client = clientManager.get(clientKey);
    client.acknowledge(ackHandle);
    response.setStatus(Status.SUCCESS);
  }

  String getChannelUrl() {
    return channelUrl;
  }

  /** Returns the client manager for this service */
  AndroidClientManager getClientManager() {
    return clientManager;
  }

  private void handleC2dmMessage(Intent intent) {
    String clientKey = intent.getStringExtra(MESSAGE_CLIENT_KEY);
    AndroidClientProxy proxy;
    try {
      proxy = clientManager.get(clientKey);
      if (!proxy.isStarted()) {
        Log.w(TAG, "Dropping C2DM message for unstarted client:" + clientKey);
        return;
      }
    } catch (AndroidClientException e) {
      Log.w(TAG, "Unable to find client: ", e);
      return;
    }
    byte [] message = intent.getByteArrayExtra(MESSAGE_DATA);
    if (message != null) {
      proxy.getChannel().receiveMessage(message);
    } else {
      String mailboxId = intent.getStringExtra(AndroidC2DMConstants.MAILBOX_ID_PARAM);
      if (mailboxId == null) {
        Log.e(TAG, "Missing mailbox ID on message");
        return;
      }
      proxy.getChannel().retrieveMailbox(mailboxId);
    }
  }

  private void handleRegistration(Intent intent) {
    String id = intent.getStringExtra(REGISTER_ID);

    // Notify the client manager of the updated registration ID
    clientManager.setRegistrationId(id);
  }

  private void handleError(Intent intent) {
    Log.e(TAG, "Unable to perform C2DM registration:" + intent.getStringExtra(ERROR_MESSAGE));
  }

  // TODO: Add interval timer to iterate over managed clients, drop the inactive
  // ones from memory, and stop the service if there are no active clients remaining
}
