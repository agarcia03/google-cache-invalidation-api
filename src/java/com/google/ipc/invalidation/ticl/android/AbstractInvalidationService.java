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

import com.google.ipc.invalidation.external.client.android.service.Event;
import com.google.ipc.invalidation.external.client.android.service.InvalidationService;
import com.google.ipc.invalidation.external.client.android.service.ListenerService;
import com.google.ipc.invalidation.external.client.android.service.Request;
import com.google.ipc.invalidation.external.client.android.service.Request.Action;
import com.google.ipc.invalidation.external.client.android.service.Response;

import android.app.Service;
import android.content.Intent;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

/**
 * Abstract base class for implementing the Android invalidation service. The service implements the
 * set of actions defined in {@link Action}. For each supported action, the service will extract the
 * action parameters and invoke an abstract methods that will be implemented by subclasses to
 * provide the action-specific processing.
 * <p>
 * The class also provides {@code sendEvent} methods that can be used to generate events back to the
 * client.
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
    Response.Builder response = Response.newBuilder(request.getActionOrdinal(), output);
    Action action = request.getAction();
    Log.d(TAG, "Request: " + action + " from " + request.getClientKey());
    try {
      switch(action) {
        case CREATE:
          create(request, response);
          break;
        case RESUME:
          resume(request, response);
          break;
        case START:
          start(request, response);
          break;
        case STOP:
          stop(request, response);
          break;
        case REGISTER:
          register(request, response);
          break;
        case UNREGISTER:
          unregister(request, response);
          break;
        case ACKNOWLEDGE:
          acknowledge(request, response);
          break;
        case DESTROY:
          destroy(request, response);
          break;
        default:
          throw new IllegalStateException("Unknown action:" + action);
      }
    } catch (Exception e) {
      Log.e(TAG, "Error in " + request.getAction(), e);
      response.setException(e);
    }
  }

  protected abstract void create(Request request, Response.Builder response);

  protected abstract void resume(Request request, Response.Builder response);

  protected abstract void start(Request request, Response.Builder response);

  protected abstract void stop(Request request, Response.Builder response);

  protected abstract void register(Request request, Response.Builder response);

  protected abstract void unregister(Request request, Response.Builder response);

  protected abstract void acknowledge(Request request, Response.Builder response);

  protected abstract void destroy(Request request, Response.Builder response);

  /**
   * Send event messages to application clients and provides common processing
   * of the response.
   */
  protected void sendEvent(ListenerService listenerService, Event event) {
    try {
      Log.i(TAG, "Sending " + event.getAction() + " event");
      Bundle responseBundle = new Bundle();
      listenerService.handleEvent(event.getBundle(), responseBundle);

      // Wrap the response bundle and throw on any failure from the client
      Response response = new Response(responseBundle);
      response.throwOnFailure();
    } catch (RemoteException re) {
      Log.e(TAG, "Unable to send event", re);
      throw new RuntimeException("Unable to send event", re);
    }
  }
}
