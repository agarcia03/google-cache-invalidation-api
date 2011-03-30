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

import com.google.ipc.invalidation.external.android.InvalidationTypes.AckToken;
import com.google.ipc.invalidation.external.android.InvalidationTypes.ObjectId;

/**
 * Interface for the invalidation client library.
 *
 */
public interface InvalidationClient {

  /**
   * Starts the client. This method must be called before any other method is
   * invoked.
   *
   * REQUIRES: {@link #start} has not already been called.
   */
  public void start();

  /**
   * Sets the authentication token that should be used to register or uregister
   * for invalidations of a source.
   *
   * @param source invalidation source type.
   * @param authToken authentication token.
   */
  public void setAuthToken(int source, String authToken);

  /**
   * Requests that the Ticl register to receive notifications for the object
   * with id {@code oid}.
   * <p>
   * REQUIRES: {@link #start}  has been called.
   */
  void register(ObjectId oid);

  /**
   * Requests that the Ticl unregister for notifications for the object with
   * id {@code oid}.
   *
   * REQUIRES: {@link #start} has been called.
   */
  void unregister(ObjectId oid);

  /**
   * Acknowledges the {@link InvalidationListener} event that was delivered with
   * the provided acknowledgement token. This indicates that the client has
   * accepted responsibility for processing the event and it does not need to be
   * redelivered later.
   */
  void acknowledge(AckToken token);

  // TODO: I really seems like there should be a stop() method here as well,
  // that releases any resources (locally and remotely) associated with the client.
}
