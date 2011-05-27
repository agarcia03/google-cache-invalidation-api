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

package com.google.ipc.invalidation.testing.android;

import com.google.ipc.invalidation.external.client.InvalidationClient;
import com.google.ipc.invalidation.external.client.InvalidationListener;
import com.google.ipc.invalidation.external.client.android.AndroidInvalidationListener;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ErrorInfo;
import com.google.ipc.invalidation.external.client.types.Invalidation;
import com.google.ipc.invalidation.external.client.types.ObjectId;

import android.util.Log;

/**
 * TestListener service that forwards received events to an InvalidationListener
 * instance set on a static field.
 *
 */
public class InvalidationTestListener extends AndroidInvalidationListener {

  /** Logging tag */
  private static final String TAG = "InvalidationTestListener";

  /** Listener delegate to forward events to */
  private static InvalidationListener listener = null;

  /**
   * Sets the invalidation listener delegate to receive events or disables
   * event forwarding if {@code null}.
   */
  public static void setInvalidationListener(InvalidationListener listener) {
    Log.i(TAG, "setListener: " + listener);
    InvalidationTestListener.listener = listener;
  }

  @Override
  public void ready(InvalidationClient client) {
    if (listener != null) {
      listener.ready(client);
    }
  }

  @Override
  public void invalidate(
      InvalidationClient client, Invalidation invalidation, AckHandle ackHandle) {
    if (listener != null) {
      listener.invalidate(client, invalidation, ackHandle);
    }
  }

  @Override
  public void invalidateUnknownVersion(
      InvalidationClient client, ObjectId objectId, AckHandle ackHandle) {
    if (listener != null) {
      listener.invalidateUnknownVersion(client, objectId, ackHandle);
    }
  }

  @Override
  public void invalidateAll(InvalidationClient client, AckHandle ackHandle) {
    if (listener != null) {
      listener.invalidateAll(client, ackHandle);
    }
  }

  @Override
  public void informRegistrationStatus(InvalidationClient client, ObjectId objectId,
      RegistrationState regState) {
    if (listener != null) {
      listener.informRegistrationStatus(client, objectId, regState);
    }
  }

  @Override
  public void informRegistrationFailure(InvalidationClient client, ObjectId objectId,
      boolean isTransient, String errorMessage) {
    if (listener != null) {
      listener.informRegistrationFailure(client, objectId, isTransient, errorMessage);
    }
  }

  @Override
  public void reissueRegistrations(InvalidationClient client, byte[] prefix, int prefixLength) {
    if (listener != null) {
      listener.reissueRegistrations(client, prefix, prefixLength);
    }
  }

  @Override
  public void informError(InvalidationClient client, ErrorInfo errorInfo) {
    if (listener != null) {
      listener.informError(client, errorInfo);
    }
  }
}
