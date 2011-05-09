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

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;

import android.accounts.Account;
import android.content.Context;

import java.lang.ref.WeakReference;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Factory for obtaining an {@link InvalidationClient} for the Android platform.
 * The {@link #create} method will create a invalidation client associated with a
 * particular application and user account.
 * <p>
 * Applications should persist the unique client key for the new client so
 * invalidation activity can restart later if the application is removed from
 * memory. An application can obtain an invalidation client instance to resume
 * activity by calling the {@link #resume} method with the same application id
 * that was originally passed to {@link #create}.
 *
 */
public class AndroidClientFactory {

  /**
   * A mapping of application id to invalidation client instances that can be
   * used to resume/reassociate an existing invalidation client.   Client
   * instances are not guaranteed (nor required) to be reused.
   */
  private static Map<String, WeakReference<AndroidInvalidationClient>> clientMap =
      new ConcurrentHashMap<String, WeakReference<AndroidInvalidationClient>>();

  /**
   * Starts a new invalidation client for the provided application and account
   * token that will deliver invalidation events to an instance of the provided
   * listener component.
   * <p>
   * The implementation of this method is idempotent. If you call
   * {@link #create} more than once with the same application id, account, and
   * listenerName values, all calls after the first one are equivalent to just
   * calling {@link #resume} with the same application id.
   *
   * @param context the context for the client.
   * @param clientKey a unique id that identifies the created client within the
   *        scope of the application.   May be {@code null} if there is only a
   *        single invalidation client/listener for the application.
   * @param account user account that is registering the invalidations.
   * @param listenerClass the {@link AndroidInvalidationListener} subclass that
   *        is registered to receive the broadcast intents for invalidation
   *        events.
   */
  public static InvalidationClient create(
      Context context, String clientKey, Account account,
      Class<? extends AndroidInvalidationListener> listenerClass) {
    Preconditions.checkNotNull(context, "context");
    Preconditions.checkNotNull(account, "account");
    Preconditions.checkNotNull(listenerClass, "listenerClass");

    AndroidInvalidationClient client = null;
    if (clientMap.containsKey(clientKey)) {
      client = (AndroidInvalidationClient) resume(context, clientKey);
    }
    if (client == null) {
      client = new AndroidInvalidationClientImpl(context, clientKey, account, listenerClass);
      clientMap.put(clientKey, new WeakReference<AndroidInvalidationClient>(client));
    }
    return client;
  }

  /**
   * Creates a new AndroidInvalidationClient instance that is resuming
   * processing for an existing application id.
   *
   * @param context the context for the client.
   * @param clientKey a unique key that identifies the created client within the
   *        scope of the device.   May be {@code null} if there is only a
   *        single invalidation client/listener for the application.
   */
  public static InvalidationClient resume(Context context, String clientKey) {
    Preconditions.checkNotNull(context, "context");

    // See if a cached entry is available with a matching application id
    WeakReference<AndroidInvalidationClient> cachedClientReference = clientMap.get(clientKey);
    if (cachedClientReference != null) {
      InvalidationClient client = cachedClientReference.get();
      if (client != null) {
        return client;
      }
    }

    // Create and return a new instance to represent the resumed client
    return new AndroidInvalidationClientImpl(context, clientKey);
  }

  /**
   * Resets the state of the factory by dropping all cached client references.
   */
  @VisibleForTesting
  static void reset() {
    clientMap.clear();
  }
}
