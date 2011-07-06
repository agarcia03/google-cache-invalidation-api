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

import com.google.ipc.invalidation.external.client.InvalidationListener;
import com.google.ipc.invalidation.external.client.android.service.Event;
import com.google.ipc.invalidation.external.client.android.service.Event.Action;
import com.google.ipc.invalidation.external.client.android.service.ListenerService;
import com.google.ipc.invalidation.external.client.android.service.Response;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ErrorInfo;
import com.google.ipc.invalidation.external.client.types.Invalidation;
import com.google.ipc.invalidation.external.client.types.ObjectId;

import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.util.Log;

/**
 * An abstract base class for implementing a {@link Service} component
 * that handles events from the invalidation service. This class should be
 * subclassed and concrete implementations of the {@link InvalidationListener}
 * methods added to provide application-specific handling of invalidation
 * events.
 * <p>
 * This implementing subclass should be registered in {@code
 * AndroidManifest.xml} as a service of the invalidation
 * listener binding intent, as in the following sample fragment:
 *
 * <pre>
 * {@code
 * <manifest ...>
 *   <application ...>
 *     ...
 *     service android:name="com.myco.example.AppListenerService" ...>
 *       <intent-filter>
 *         <action android:name="com.google.ipc.invalidation.LISTENER"/>
 *       </intent-filter>
 *     </receiver>
 *     ...
 *   <application>
 *   ...
 * </manifest>
 * }
 * </pre>
 *
 */
public abstract class AndroidInvalidationListener extends Service
    implements InvalidationListener {

  /** Logging tag */
  private static final String TAG = "AndroidInvalidationListener";

  /**
   * Simple service stub that delegates back to methods on the service.
   */
  private final ListenerService.Stub listenerBinder = new ListenerService.Stub() {

    @Override
    public void handleEvent(Bundle input, Bundle output) {
      AndroidInvalidationListener.this.handleEvent(input, output);
    }
  };

  @Override
  public void onCreate() {
    super.onCreate();
    Log.i(TAG, "onCreate:" + this.getClass());
  }

  @Override
  public IBinder onBind(Intent arg0) {
    Log.i(TAG, "Binding: " + arg0);
    return listenerBinder;
  }

  /**
   * Handles a {@link ListenerService#handleEvent} call received by the
   * listener service.
   *
   * @param input bundle containing event parameters.
   * @param output bundled used to return response to the invalidation service.
   */
  protected void handleEvent(Bundle input, Bundle output) {

    Event event = new Event(input);
    Response.Builder response = Response.newBuilder(event.getAction(), output);
    // All events should contain an action and client id
    String action = event.getAction();
    String clientKey = event.getClientKey();
    Log.d(TAG, "Received " + action + " event for " + clientKey);

    try {

      if (clientKey == null) {
        throw new IllegalStateException("Missing client id:" + event);
      }

      // Obtain the client instance for the client receiving the event
      AndroidInvalidationClient client = AndroidClientFactory.resume(this, clientKey);

      // Determine the event type based upon the request action, extract parameters
      // from extras, and invoke the listener event handler method.
      if (Action.READY.equals(action)) {
        Log.i(TAG, "READY event for " + clientKey);
        ready(client);
      } else if (Action.INVALIDATE.equals(action)) {
        Log.i(TAG, "INVALIDATE event for " + clientKey);
        Invalidation invalidation = event.getInvalidation();
        AckHandle ackHandle = event.getAckHandle();
        invalidate(client, invalidation, ackHandle);
      } else if (Action.INVALIDATE_UNKNOWN.equals(action)) {
        Log.i(TAG, "INVALIDATE_UNKNOWN_VERSION event for " + clientKey);
        ObjectId objectId = event.getObjectId();
        AckHandle ackHandle = event.getAckHandle();
        invalidateUnknownVersion(client, objectId, ackHandle);
      } else if (Action.INVALIDATE_ALL.equals(action)) {
        Log.i(TAG, "INVALIDATE_ALL event for " + clientKey);
        AckHandle ackHandle = event.getAckHandle();
        invalidateAll(client, ackHandle);
      } else if (Action.INFORM_REGISTRATION_STATUS.equals(action)) {
        Log.i(TAG, "INFORM_REGISTRATION_STATUS event for " + clientKey);
        ObjectId objectId = event.getObjectId();
        RegistrationState state = event.getRegistrationState();
        informRegistrationStatus(client, objectId, state);
      } else if (Action.INFORM_REGISTRATION_FAILURE.equals(action)) {
        Log.i(TAG, "INFORM_REGISTRATION_FAILURE event for " + clientKey);
        ObjectId objectId = event.getObjectId();
        String errorMsg = event.getError();
        boolean isTransient = event.getIsTransient();
        informRegistrationFailure(client, objectId, isTransient, errorMsg);
      } else if (Action.REISSUE_REGISTRATIONS.equals(action)) {
        Log.i(TAG, "REISSUE_REGISTRATIONS event for " + clientKey);
        byte[] prefix = event.getPrefix();
        int prefixLength = event.getPrefixLength();
        reissueRegistrations(client, prefix, prefixLength);
      } else if (Action.INFORM_ERROR.equals(action)) {
        Log.i(TAG, "INFORM_ERROR event for " + clientKey);
        ErrorInfo errorInfo = event.getErrorInfo();
        informError(client, errorInfo);
      } else {
        Log.w(TAG, "Urecognized event: " + event);
      }
      response.setStatus(Response.Status.SUCCESS);
    } catch (RuntimeException re) {
      // If an exception occurs during processing, log it, store the
      // result in the response sent back to the service, then rethrow.
      Log.e(TAG, "Failure in handleEvent", re);
      response.setException(re);
      throw re;
    }
  }
}
