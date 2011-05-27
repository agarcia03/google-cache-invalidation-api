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

package com.google.ipc.invalidation.external.client.android.service;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;
import android.util.Log;

/**
 * Helper class that assists in make connections to a bound service.
 *
 * @param <BoundService> the bound service interface associated with the binder.
 *
 */
public class ServiceBinder<BoundService> {

  /** Logging tag */
  private static final String TAG = "ServiceBinder";

  /** The maximum amount of time to wait (milliseconds) for a successful binding to the service */
  private static final int CONNECTION_TIMEOUT = 60 * 1000;

  /** Intent that can be used to bind to the service */
  private Intent serviceIntent;

  /** Class that represents the bound service interface */
  private Class<BoundService> serviceClass;

  /** BindHelper that aids in casting the service class */
  private BindHelper<BoundService> bindHelper;

  /** Bound service instance held by the binder or {@code null} if not bound */
  BoundService serviceInstance;

  /**
   * Helper class provided at construction time to return an instance of the
   * bound service stub from a binder instance.  This is necessary because
   * Android bound services do not provide any common binding interface on
   * the generated stub classes.
   *
   * @param <T> the bound service interface returned by the helper.
   */
  public interface BindHelper<T> {
    /**
     * Returns the bound service interface for the binder.
     */
    public T asInterface(IBinder binder);
  }

  /**
   * Service connection implementation that handles connection/disconnection
   * events for the binder.
   */
  private final ServiceConnection serviceConnection = new ServiceConnection() {

    @Override
    public void onServiceConnected(ComponentName serviceName, IBinder binder) {
      synchronized (this) {
        serviceInstance = bindHelper.asInterface(binder);
        Log.i(TAG, "onServiceConnected:" + serviceClass);

        // Wake up the thread blocking on the connection
        this.notify();
      }
    }

    @Override
    public void onServiceDisconnected(ComponentName serviceName) {
      Log.i(TAG, "onServiceDisconnected:" + serviceClass);
      serviceInstance = null;
    }
  };

  /**
   * Constructs a new ServiceBinder instance that will use the provided service
   * intent to connect to the bound service that implements the provided service
   * interface.
   *
   * @param <T> the bound service interface associated with the binder.
   * @param serviceIntent the intent used to connect to the bound service.
   * @param serviceClass the bound service interface class
   * @param bindHelper a binding helper provided by the caller.
   */
  public static <T> ServiceBinder<T> of (Intent serviceIntent, Class<T> serviceClass,
      BindHelper<T> bindHelper) {
    return new ServiceBinder<T>(serviceIntent, serviceClass, bindHelper);
  }

  // Private constructor called by of() which provides type safety
  private ServiceBinder(
      Intent serviceIntent, Class<BoundService> serviceClass, BindHelper<BoundService> bindHelper) {
    this.serviceIntent = serviceIntent;
    this.serviceClass = serviceClass;
    this.bindHelper = bindHelper;
  }

  /**
   * Binds to the service associated with the binder within the provided context.
   */
  public BoundService bind(Context context) {
    if (!isBound()) {
      if (!context.bindService(serviceIntent, serviceConnection, Context.BIND_AUTO_CREATE)) {
        Log.e(TAG, "Unable to bind to service:" + serviceClass);
        return null;
      }
      synchronized (serviceConnection) {
        try {
          if (serviceInstance == null) {
            serviceConnection.wait(CONNECTION_TIMEOUT);
          }
        } catch (InterruptedException e) {
          Log.e(TAG, "Failure waiting for service connection", e);
        }
      }
    }
    return serviceInstance;
  }

  /**
   * Unbind to the service associated with the binder within the provided context.
   */
  public void unbind(Context context) {
    context.unbindService(serviceConnection);
    serviceInstance = null;
  }

  /**
   * Returns {@code true} if the service binder is currently connected to the
   * bound service.
   */
  public boolean isBound() {
    return serviceInstance != null;
  }
}
