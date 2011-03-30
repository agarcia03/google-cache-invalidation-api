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

import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getAckToken;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getClientId;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getInvalidation;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getObjectId;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getRegistrationState;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getSource;
import static com.google.ipc.invalidation.external.android.intents.InvalidationIntents.getUnknownHint;

import com.google.ipc.invalidation.external.android.InvalidationTypes.AckToken;
import com.google.ipc.invalidation.external.android.InvalidationTypes.Invalidation;
import com.google.ipc.invalidation.external.android.InvalidationTypes.ObjectId;
import com.google.ipc.invalidation.external.android.InvalidationTypes.RegistrationState;
import com.google.ipc.invalidation.external.android.InvalidationTypes.UnknownHint;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents;
import com.google.ipc.invalidation.external.android.intents.InvalidationIntents.Events;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * An abstract base class for implementing a {@link BroadcastReceiver} component
 * that handles events from the  invalidation service. This class should be
 * subclassed and concrete implementations of the {@link InvalidationListener}
 * methods added to provide application-specific handling of invalidation
 * events.
 * <p>
 * This implementating subclass should be registered in {@code
 * AndroidManifest.xml} as a broadcast receiver of the  invalidation
 * service event intents, as in the following sample fragment:
 *
 * <pre>
 * {@code
 * <manifest ...>
 *   <application ...>
 *     ...
 *     receiver android:name="com.myco.example.AppInvalidationReceiver" ...>
 *       <intent-filter>
 *         <category android:name="com.google.ipc.invalidation.EVENTS"/>
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
public abstract class AndroidInvalidationListener extends BroadcastReceiver
    implements InvalidationListener {

  @Override
  public final void onReceive(Context context, Intent intent) {

    // Ensure that it's possible to unmarshall parcelable invalidation types
    intent.setExtrasClassLoader(InvalidationIntents.class.getClassLoader());

    // All events should contain a client id
    String clientId = getClientId(intent);
    if (clientId == null) {
      // TODO: log error
      return;
    }
    String action = intent.getAction();
    InvalidationClient client = AndroidClientFactory.resume(context, clientId);
    AckToken ackToken = getAckToken(intent);
    if (Events.INVALIDATE.equals(action)) {
      Invalidation invalidation = getInvalidation(intent);
      invalidate(client, invalidation, ackToken);
    } else if (Events.INVALIDATE_ALL.equals(action)) {
      invalidateAll(client, ackToken);
    } else if (Events.REGISTRATION_CHANGED.equals(action)) {
      ObjectId objectId = getObjectId(intent);
      RegistrationState state = getRegistrationState(intent);
      UnknownHint hint = getUnknownHint(intent);
      registrationStateChanged(client, objectId, state, hint, ackToken);
    } else if (Events.REGISTRATIONS_REMOVED.equals(action)) {
      registrationsRemoved(client, ackToken);
    } else if (Events.INVALID_AUTH_TOKEN.equals(action)) {
      int source = getSource(intent);
      invalidAuthToken(client, source, ackToken);
    } else {
      // TODO: Log receipt of unrecognized intents
    }
  }
}
