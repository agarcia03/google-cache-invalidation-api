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

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.os.IBinder;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

/**
 * Abstract base class that assists in making connections to a bound service. Subclasses can define
 * a concrete binding to a particular bound service interface by binding to an explicit type on
 * declaration, providing a public constructor, and providing an implementation of the
 * {@link #asInterface} method.
 *
 * @param <BoundService> the bound service interface associated with the binder.
 *
 */
public abstract class ServiceBinder<BoundService> {

  /** Logger */
  private static final Logger logger = AndroidLogger.forTag("InvServiceBinder");

  /** The maximum amount of time to wait (milliseconds) for a successful binding to the service */
  private static final int CONNECTION_TIMEOUT = 60 * 1000;

  /** Intent that can be used to bind to the service */
  private final Intent serviceIntent;

  /** Class that represents the bound service interface */
  private final Class<BoundService> serviceClass;

  /** Name of the component that implements the service interface. */
  private final String componentClassName;

  /** Latch used to wait for connection */
  private CountDownLatch connectLatch;

  /** Used to synchronize */
  private final Object lock = new Object();

  /**
   * Bound service instance held by the binder or {@code null} if not bound. We use an
   * AtomicReference so that we can safely set it in {@code onServiceConnected} without holding
   * {@link #lock}.
   */
  private final AtomicReference<BoundService> serviceInstance = new AtomicReference<BoundService>();

  /** Whether bindService has been called. */
  private boolean hasCalledBind = false;

  /**
   * Service connection implementation that handles connection/disconnection
   * events for the binder.
   */
  private final ServiceConnection serviceConnection = new ServiceConnection() {

    @Override
    public void onServiceConnected(ComponentName serviceName, IBinder binder) {
      logger.fine("onServiceConnected: %s", serviceName);
      Preconditions.checkNotNull(connectLatch, "No connection in progress");
      serviceInstance.set(asInterface(binder));
      connectLatch.countDown();
    }

    @Override
    public void onServiceDisconnected(ComponentName serviceName) {
      // TODO: We don't have a test that would catch serviceInstance = null by
      // mistake.
      logger.fine("onServiceDisconnected: %s", serviceClass);
      serviceInstance.set(null);
    }
  };

  /**
   * Constructs a new ServiceBinder that uses the provided intent to bind to the service of the
   * specific type. Subclasses should expose a public constructor that passes the appropriate intent
   * and type into this constructor.
   *
   * @param serviceIntent intent that can be used to connect to the bound service.
   * @param serviceClass interface exposed by the bound service.
   * @param componentClassName name of component implementing the bound service. If non-null, then
   *        an explicit binding to the named component within the same class is guaranteed.
   */
  protected ServiceBinder(Intent serviceIntent, Class<BoundService> serviceClass,
      String componentClassName) {
    this.serviceIntent = serviceIntent;
    this.serviceClass = serviceClass;
    this.componentClassName = componentClassName;
  }

  /** Returns the intent used to bind to the service */
  public Intent getIntent(Context context) {
    Intent bindIntent;
    if (componentClassName == null) {
      return serviceIntent;
    }
    bindIntent = new Intent(serviceIntent);
    bindIntent.setClassName(context, componentClassName);
    return bindIntent;
  }

  /**
   * Binds to the service associated with the binder within the provided context.
   */
  public BoundService bind(Context context) {
    synchronized (lock) {
      if (!hasCalledBind) {
        connectLatch = new CountDownLatch(1);
        Intent bindIntent = getIntent(context);
        if (!context.bindService(bindIntent, serviceConnection, Context.BIND_AUTO_CREATE)) {
          logger.severe("Unable to bind to service: %s", bindIntent);
          return null;
        }
        try {
          connectLatch.await(CONNECTION_TIMEOUT, TimeUnit.SECONDS);
        } catch (InterruptedException exception) {
          logger.warning("Connection interrupted", exception);
        }
        if (connectLatch.getCount() != 0) {
          logger.severe("Failure waiting for service connection");
        } else {
          logger.fine("Bound %s to %s", serviceClass, serviceInstance.get());
          connectLatch = null;
        }
        hasCalledBind = true;
      }
      return serviceInstance.get();
    }
  }

  /**
   * Unbind to the service associated with the binder within the provided context.
   */
  public void unbind(Context context) {
    synchronized (lock) {
      if (hasCalledBind) {
        logger.fine("Unbinding %s from %s", serviceClass, serviceInstance.get());
        try {
          context.unbindService(serviceConnection);
        } catch (IllegalArgumentException exception) {
          logger.fine("Exception unbinding from %s: %s", serviceClass, exception.getMessage());
        }
        serviceInstance.set(null);
        hasCalledBind = false;
      }
    }
  }

  /**
   * Returns {@code true} if the service binder is currently connected to the
   * bound service.
   */
  public boolean isBound() {
    synchronized (lock) {
      return hasCalledBind;
    }
  }

  @Override
  public String toString() {
    return this.getClass().getSimpleName() + "[" + serviceIntent + "]";
  }

  /** Returns a bound service stub of the expected type. */
  protected abstract BoundService asInterface(IBinder binder);
}
