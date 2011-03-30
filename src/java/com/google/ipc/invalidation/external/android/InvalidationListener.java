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
import com.google.ipc.invalidation.external.android.InvalidationTypes.Invalidation;
import com.google.ipc.invalidation.external.android.InvalidationTypes.ObjectId;
import com.google.ipc.invalidation.external.android.InvalidationTypes.RegistrationState;
import com.google.ipc.invalidation.external.android.InvalidationTypes.UnknownHint;

import javax.annotation.Nullable;

/**
 * Interface through which invalidation-related events are delivered by the
 * library to the application.
 *
 */
public interface InvalidationListener {

  /**
   * Indicates that the object with oid {@code oid} has been updated to version
   * {@code version}.
   *
   * The Ticl guarantees that this callback will be invoked at least once for
   * every invalidation that it guaranteed to deliver. It does not guarantee
   * exactly-once delivery or in-order delivery (with respect to the version
   * number).
   *
   * The application should acknowledge this event by calling
   * {@link InvalidationClient#acknowledge(AckToken)} with the provided {@code
   * ackToken} otherwise the event may be redelivered.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param ackToken event acknowledgement token. If {@code null}, the event
   *        does not require acknowledgement.
   */
  void invalidate(InvalidationClient client, Invalidation invalidation, AckToken ackToken);

  /**
   * Indicates that the application should consider all objects to have changed.
   * This event is generally sent when the client has been disconnected from the
   * network for too long a period and has been unable to resynchronize with the
   * update stream, but it may be invoked arbitrarily (although  tries hard
   * not to invoke it under normal circumstances).
   *
   * The application should acknowledge this event by calling
   * {@link InvalidationClient#acknowledge(AckToken)} with the provided {@code
   * ackToken} otherwise the event may be redelivered.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param ackToken event acknowledgement token. If {@code null}, the event
   *        does not require acknowledgement.
   */
  void invalidateAll(InvalidationClient client, AckToken ackToken);


  /**
   * Indicates that the registration state of an object has changed. Note that
   * if the new state is {@code UNKNOWN}, the {@code unknownHint} provides a
   * hint as to whether or not the {@code UNKNOWN} state is transient. If it is,
   * future calls to {@code (un)register} may be able to transition the object
   * out of this state.
   *
   * The application should acknowledge this event by calling
   * {@link InvalidationClient#acknowledge(AckToken)} with the provided {@code
   * ackToken} otherwise the event may be redelivered.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param objectId the id of the object whose state changed
   * @param newState the new state of the object
   * @param unknownHint if {@code newState == UNKNOWN}, a hint as to the cause
   *        of the unknown state
   * @param ackToken event acknowledgement token. If {@code null}, the event
   *        does not require acknowledgement.
   */
  void registrationStateChanged(InvalidationClient client, ObjectId objectId,
      RegistrationState newState, @Nullable UnknownHint unknownHint, AckToken ackToken);

  /**
   * Indicates that the application's registrations have been removed.
   *
   * The application should acknowledge this event by calling
   * {@link InvalidationClient#acknowledge(AckToken)} with the provided {@code
   * ackToken} otherwise the event may be redelivered.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param ackToken event acknowledgement token. If {@code null}, the event
   *        does not require acknowledgement.
   */
  void registrationsRemoved(InvalidationClient client, AckToken ackToken);

  /**
   * Informs the listener that the provided authentication token is not valid
   * for use with an invalidation source. This can happen because the token is
   * not valid or has expired.
   *
   * The application should acknowledge this event by calling
   * {@link InvalidationClient#acknowledge(AckToken)} with the provided {@code
   * ackToken} otherwise the event may be redelivered.
   *
   * @param client the {@link InvalidationClient} invoking the listener
   * @param source identifies the invalidation source type that was has an
   *        expired token.
   * @param ackToken event acknowledgement token. If {@code null}, the event
   *        does not require acknowledgement.
   */
  void invalidAuthToken(
      InvalidationClient client, int source, AckToken ackToken);
}
