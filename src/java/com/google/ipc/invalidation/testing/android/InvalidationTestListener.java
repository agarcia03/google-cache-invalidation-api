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

import com.google.ipc.invalidation.external.android.AndroidInvalidationListener;
import com.google.ipc.invalidation.external.android.InvalidationClient;
import com.google.ipc.invalidation.external.android.InvalidationListener;
import com.google.ipc.invalidation.external.android.InvalidationTypes.AckToken;
import com.google.ipc.invalidation.external.android.InvalidationTypes.Invalidation;
import com.google.ipc.invalidation.external.android.InvalidationTypes.ObjectId;
import com.google.ipc.invalidation.external.android.InvalidationTypes.RegistrationState;
import com.google.ipc.invalidation.external.android.InvalidationTypes.UnknownHint;

import android.util.Log;

/**
 * TestListener that forwards received events to an InvalidationListener
 * instance set on a static field.
 *
 */
public class InvalidationTestListener extends AndroidInvalidationListener {

  /** Loggging tag */
  private static final String TAG = "InvalidationTestListener";

  private static InvalidationListener listener = null;

  public static void setInvalidationListener(InvalidationListener listener) {
    Log.i(TAG, "setListener: " + listener);
    InvalidationTestListener.listener = listener;
  }

  @Override
  public void invalidAuthToken(InvalidationClient client, int source, AckToken ackToken) {
    if (listener != null) {
      listener.invalidAuthToken(client, source, ackToken);
    }
  }

  @Override
  public void invalidate(InvalidationClient client, Invalidation invalidation, AckToken ackToken) {
    if (listener != null) {
      listener.invalidate(client, invalidation, ackToken);
    }
  }

  @Override
  public void invalidateAll(InvalidationClient client, AckToken ackToken) {
    if (listener != null) {
      listener.invalidateAll(client, ackToken);
    }
  }

  @Override
  public void registrationStateChanged(InvalidationClient client, ObjectId objectId,
      RegistrationState newState, UnknownHint unknownHint, AckToken ackToken) {
    if (listener != null) {
      listener.registrationStateChanged(client, objectId, newState, unknownHint, ackToken);
    }

  }

  @Override
  public void registrationsRemoved(InvalidationClient client, AckToken ackToken) {
    if (listener != null) {
      listener.registrationsRemoved(client, ackToken);
    }
  }
}
