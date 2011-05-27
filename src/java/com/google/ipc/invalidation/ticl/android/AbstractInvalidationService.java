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

import com.google.ipc.invalidation.external.client.InvalidationListener.RegistrationState;
import com.google.ipc.invalidation.external.client.android.service.Event;
import com.google.ipc.invalidation.external.client.android.service.InvalidationService;
import com.google.ipc.invalidation.external.client.android.service.ListenerService;
import com.google.ipc.invalidation.external.client.android.service.Request;
import com.google.ipc.invalidation.external.client.android.service.Request.Action;
import com.google.ipc.invalidation.external.client.android.service.Response;
import com.google.ipc.invalidation.external.client.android.service.ServiceBinder;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ErrorInfo;
import com.google.ipc.invalidation.external.client.types.Invalidation;
import com.google.ipc.invalidation.external.client.types.ObjectId;

import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

/**
 * Abstract base class for implementing the Android invalidation service. The
 * service implements the set of actions defined in {@link Request.Action}. For
 * each supported action, the service will extract the action parameters and
 * invoke an abstract methods that will be implemented by subclasses to provide
 * the action-specific processing.
 * <p>
 * The class also provides {@code sendEvent} methods that can be used to
 * generate events back to the client.
 *
 */
public abstract class AbstractInvalidationService extends Service {

  private static final String TAG = "AbstractInvalidationService";

  /**
   * Simple service stub that delegates back to methods on the service.
   */
  private final InvalidationService.Stub serviceBinder = new InvalidationService.Stub() {

    @Override
    public void handleRequest(Bundle input, Bundle output) {
      AbstractInvalidationService.this.handleRequest(input, output);
    }
  };

  @Override
  public int onStartCommand(Intent intent, int flags, int startId) {
    return START_NOT_STICKY;
  }

  @Override
  public IBinder onBind(Intent intent) {
    return serviceBinder;
  }

  protected void handleRequest(Bundle input, Bundle output) {
    Request request = new Request(input);
    Response.Builder response = Response.newBuilder(request.getAction(), output);
    try {
      String action = request.getAction();
      if (Action.CREATE.equals(action)) {
        create(request, response);
      } else if (Action.RESUME.equals(action)) {
        resume(request, response);
      } else if (Action.START.equals(action)) {
        start(request, response);
      } else if (Action.REGISTER.equals(action)) {
        register(request, response);
      } else if (Action.UNREGISTER.equals(action)) {
        unregister(request, response);
      } else if (Action.ACKNOWLEDGE.equals(action)) {
        acknowledge(request, response);
      } else if (Action.STOP.equals(action)) {
        stop(request, response);
      }
    } catch (Exception e) {
      Log.e(TAG, "Error in " + request.getAction(), e);
      response.setException(e);
    }
  }

  protected abstract void create(Request request, Response.Builder response);

  protected abstract void resume(Request request, Response.Builder response);

  protected abstract void start(Request request, Response.Builder response);

  protected abstract void register(Request request, Response.Builder response);

  protected abstract void unregister(Request request, Response.Builder response);

  protected abstract void acknowledge(Request request, Response.Builder response);

  protected abstract void stop(Request request, Response.Builder response);

  /**
   * Sends an ready event back to the application client.
   *
   * @param clientKey receiving client key
   * @param listenerIntent intent used to bind to the listener service.
   */
  protected void sendReadyAllEvent(
      String clientKey, Intent listenerIntent) {
    Event event = Event
        .newBuilder(Event.Action.READY)
        .setClientKey(clientKey)
        .build();
    sendEvent(listenerIntent, event);
  }

  /**
   * Sends an invalidate event back to the application client.
   *
   * @param clientKey receiving client key
   * @param listenerIntent intent used to bind to the listener service.
   * @param invalidation the invalidation to send
   * @param ackHandle the acknowledgement handle
   */
  protected void sendInvalidateEvent(
      String clientKey, Intent listenerIntent, Invalidation invalidation, AckHandle ackHandle) {
    Event event = Event
        .newBuilder(Event.Action.INVALIDATE)
        .setClientKey(clientKey)
        .setInvalidation(invalidation)
        .setAckHandle(ackHandle)
        .build();
    sendEvent(listenerIntent, event);
  }

  /**
   * Sends an invalidate unknown version event back to the application client.
   *
   * @param clientKey receiving client key
   * @param listenerIntent intent used to bind to the listener service.
   * @param objectId the object being invalidated
   * @param ackHandle the acknowledgement handle
   */
  protected void sendInvalidateUnknownVersionEvent(
      String clientKey, Intent listenerIntent, ObjectId objectId, AckHandle ackHandle) {
    Event event = Event
        .newBuilder(Event.Action.INVALIDATE_UNKNOWN)
        .setClientKey(clientKey)
        .setObjectId(objectId)
        .setAckHandle(ackHandle)
        .build();
    sendEvent(listenerIntent, event);
  }

  /**
   * Sends an invalidation all event back to the application client.
   *
   * @param clientKey receiving client key
   * @param listenerIntent intent used to bind to the listener service.
   * @param ackHandle the acknowledgement handle
   */
  protected void sendInvalidateAllEvent(
      String clientKey, Intent listenerIntent, AckHandle ackHandle) {
    Event event = Event
        .newBuilder(Event.Action.INVALIDATE_ALL)
        .setClientKey(clientKey)
        .setAckHandle(ackHandle)
        .build();
    sendEvent(listenerIntent, event);
  }

  /**
   * Sends an inform registration status event back to the application client.
   *
   * @param clientKey receiving client key
   * @param listenerIntent intent used to bind to the listener service.
   * @param objectId the object with a changed registration status
   * @param regState the new registration state
   */
  protected void sendInformRegistrationStatusEvent(
      String clientKey, Intent listenerIntent, ObjectId objectId,
      RegistrationState regState) {
    Event event = Event
        .newBuilder(Event.Action.INFORM_REGISTRATION_STATUS)
        .setClientKey(clientKey)
        .setObjectId(objectId)
        .setRegistrationState(regState)
        .build();
    sendEvent(listenerIntent, event);
  }

  /**
   * Sends an inform registration failure event back to the application client.
   *
   * @param clientKey receiving client key
   * @param listenerIntent intent used to bind to the listener service.
   * @param objectId the object with a changed registration status
   * @param isTransient indicate the failure is transient if {@code true}
   * @param errorMessage error message
   */
  protected void sendInformRegistrationFailureEvent(
      String clientKey, Intent listenerIntent, ObjectId objectId,
      boolean isTransient, String errorMessage) {
    Event event = Event
        .newBuilder(Event.Action.INFORM_REGISTRATION_FAILURE)
        .setClientKey(clientKey)
        .setObjectId(objectId)
        .setIsTransient(isTransient)
        .setError(errorMessage)
        .build();
    sendEvent(listenerIntent, event);
  }

  /**
   * Sends a reissue registrations event back to the application client.
   *
   * @param clientKey receiving client key
   * @param listenerIntent intent used to bind to the listener service.
   * @param prefix prefix of registrations requestioned
   * @param prefixLength length of prefix in bits
   */
  protected void sendReissueRegistrationsEvent(
      String clientKey, Intent listenerIntent, byte[] prefix, int prefixLength) {
    Event event = Event
        .newBuilder(Event.Action.REISSUE_REGISTRATIONS)
        .setClientKey(clientKey)
        .setPrefix(prefix, prefixLength)
        .build();
    sendEvent(listenerIntent, event);
  }

  /**
   * Sends an inform error event back to the application client.
   *
   * @param clientKey receiving client key
   * @param listenerIntent intent used to bind to the listener service.
   * @param errorInfo error information
   */
  protected void sendInformErrorEvent(
      String clientKey, Intent listenerIntent, ErrorInfo errorInfo) {
    Event event = Event
        .newBuilder(Event.Action.INFORM_ERROR)
        .setClientKey(clientKey)
        .setErrorInfo(errorInfo)
        .build();
    sendEvent(listenerIntent, event);
  }

  /**
   * Send event messages to application clients and provides common processing
   * of the response.
   */
  protected void sendEvent(Intent eventIntent, Event event) {
    ServiceBinder<ListenerService> binder = null;
    ListenerService listenerService = null;
    try {
      Log.i(TAG, "Sending " + event.getAction() + " event");

      // Create a service binder to bind to the client listener service
      // TODO: Implement binding caching so we'll hold the
      // listener binding across multiple event calls, potentially
      // unbinding after some timeout period.
      ServiceBinder<ListenerService> serviceBinder = binder = ServiceBinder.of(
          eventIntent, ListenerService.class, new ServiceBinder.BindHelper<ListenerService>() {
            @Override
            public ListenerService asInterface(IBinder binder) {
              return ListenerService.Stub.asInterface(binder);
            }
          });

      // Bind and send the event to the listener service.
      listenerService = serviceBinder.bind(this);
      Bundle responseBundle = new Bundle();
      listenerService.handleEvent(event.getBundle(), responseBundle);

      // Wrap the response bundle and throw on any failure from the client
      Response response = new Response(responseBundle);
      response.throwOnFailure();
    } catch (RemoteException re) {
      Log.e(TAG, "Unable to send event", re);
      throw new RuntimeException("Unable to send event", re);
    } finally {
      // Ensure that the binding to the listener service is released.
      if (binder != null) {
        binder.unbind(this);
      }
    }
  }
}
