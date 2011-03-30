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

package com.google.ipc.invalidation.external.android.intents;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.android.InvalidationTypes;
import com.google.ipc.invalidation.external.android.InvalidationTypes.AckToken;
import com.google.ipc.invalidation.external.android.InvalidationTypes.Invalidation;
import com.google.ipc.invalidation.external.android.InvalidationTypes.ObjectId;
import com.google.ipc.invalidation.external.android.InvalidationTypes.RegistrationState;
import com.google.ipc.invalidation.external.android.InvalidationTypes.UnknownHint;

import android.accounts.Account;
import android.content.Intent;
import android.content.IntentSender;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * Defines the set of intents that are sent to and received from the 
 * Invalidation Service along with a set of static utility methods for setting
 * and retrieving extra values stored within the intents.
 *
 */
public class InvalidationIntents {

  /**
   * Base name for invalidation actions and events.
   */
  private static final String BASE = "com.google.ipc.invalidation.";

  /**
   * Defines the set of {@link Intent} action names that can be sent to the
   * invalidation service.
   */
  public static class Actions {

    /**
     * Requests a new invalidation client be started by the invalidation
     * service.   This action is idempotent if there is already an existing
     * client with the same id, account, sender combination.l
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the calling client.</li>
     * <li>{@link Extras#ACCOUNT}: user account that is registered
     * invalidations.</li>
     * <li>{@link Extras#SENDER}: the {@link IntentSender} that can be used to
     * send invalidation event intents back to the client.</li>
     * </ul>
     */
    public static final String CREATE = BASE + "action.CREATE";

    /**
     * Associates an authentication token with a invalidation source type. When
     * any subsequent registration or unregistration of invalidation is done for
     * objects of the associated source type, the authentication token will be
     * sent with the associated requests.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the calling client.</li>
     * <li>{@link Extras#SOURCE}: the invalidation source type.</li>
     * <li>{@link Extras#AUTH_TOKEN}: authentication token to use for the source
     * type. If not set, removes any current token for source type.</li>
     * </ul>
     */
    public static final String SET_AUTH = BASE + "action.SET_AUTH";

    /**
     * Registers to enable receipt of invalidation notifications on an object.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the calling client.</li>
     * <li>{@link Extras#OBJECT_ID}: the registered object id.</li>
     * </ul>.
     */
    public static final String REGISTER = BASE + "action.REGISTER";

    /**
     * Unregisters to disable receipt of of invalidation notifications on an
     * object.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the calling client.</li>
     * <li>{@link Extras#OBJECT_ID}: the unregistered object id.</li>
     * </ul>.
     */
    public static final String UNREGISTER = BASE + "action.UNREGISTER";

    /**
     * Acknowledges an event sent by the invalidation service to confirm that it
     * has been processed.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the calling client.</li>
     * <li>{@link Extras#ACK_TOKEN}: the ack token delivered with the
     * acknowledged event.</li.
     * </ul>.
     */
    public static final String ACKNOWLEDGE = BASE + "action.ACCEPT";
  }

  /**
   * Category that is set on all invalidation actions. This make it easy for
   * invalidation action receivers to register for all actions using a single
   * category intent filter.
   */
  public static final String ACTION_CATEGORY = BASE + "ACTIONS";

  /**
   * Creates a new invalidation action intent to be sent to the invalidation
   * service.
   *
   * @param action the action associated with the event.
   *
   * @see Actions
   */
  public static Intent createServiceIntent(String action) {
    Intent intent = new Intent(action);
    intent.addCategory(ACTION_CATEGORY);
    return intent;
  }

  /**
   * Contains the Action constants for the set of intents that are delivered by
   * the invalidation service to its clients.
   */
  public static class Events {

    /**
     * The INVALIDATE event is delivered when an invalidation notification has
     * been received for an object registered by the client.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the target application.</li>
     * <li>{@link Extras#INVALIDATION}: the object invalidation.</li>
     * <li>{@link Extras#ACK_TOKEN}: ack token for the event.</li>
     * </ul>.
     */
    public static final String INVALIDATE = BASE + "event.INVALIDATE";

    /**
     * The INVALIDATE_ALL event is delivered to indicate that all object
     * registered by the client need to be revalidated.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the target application.</li>
     * <li>{@link Extras#ACK_TOKEN}: ack token for the event.</li>
     * </ul>.
     */
    public static final String INVALIDATE_ALL = BASE + "event.INVALIDATE_ALL";

    /**
     * The REGISTRATIONS_CHANGED event is delivered when the state of a
     * registration state of an object has been changed in a way that is not
     * expected by the invalidation client.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the target application.</li>
     * <li>{@link Extras#OBJECT_ID}: the invalidated object id.</li>
     * <li>{@link Extras#STATE}: the new registration state.</li>
     * <li>{@link Extras#UNKNOWN_HINT}: an additional hint if the state is
     * {@link RegistrationState#UNKNOWN}.
     * <li>{@link Extras#ACK_TOKEN}: ack token for the event.</li>
     * </ul>.
     */
    public static final String REGISTRATION_CHANGED = BASE + "event.REGISTRATION_CHANGED";

    /**
     * The REGISTRATIONS_REMOVED event is delivered to indicate that all object
     * registrations associated with the client have been removed.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the target application.</li>
     * <li>{@link Extras#ACK_TOKEN}: ack token for the event.</li>
     * </ul>.
     */
    public static final String REGISTRATIONS_REMOVED = BASE + "event.REGISTRATIONS_REMOVED";

    /**
     * The INVALID_AUTH_TOKEN event is delivered to indicate that an
     * authentication token provided by the client is invalid.
     * <p>
     * This intent will contain the following extras:
     * <ul>
     * <li>{@link Extras#CLIENT}: identifies the target application.</li>
     * <li>{@link Extras#SOURCE}: the invalidation source type for the expired
     * token.</li>
     * <li>{@link Extras#ACK_TOKEN}: ack token for the event.</li>
     * </ul>.
     */
    public static final String INVALID_AUTH_TOKEN = BASE + "event.AUTH_TOKEN";

    private Events() {
    } // not instantiable
  }

  /**
   * Category that is set on all invalidation events. This make it easy for
   * invalidation event receivers to register for all events using a single
   * category intent filter.
   */
  public static final String EVENT_CATEGORY = BASE + "EVENTS";

  /**
   * Creates a new invalidation event intent.
   *
   * @param action the action associated with the event.
   *
   * @see Events
   */
  public static Intent createEventIntent(String action) {
    Intent intent = new Intent(action);
    intent.addCategory(EVENT_CATEGORY);
    return intent;
  }

  /**
   * Defines the set of extra values used in invalidation intents.
   */
  public static class Extras {

    /**
     * An {@link Account} extra that identifies the user account associated with
     * the client.
     */
    public static final String ACCOUNT = "account";

    /**
     * A byte array extra that contains the serialized byte representation of an
     * {@link AckToken}.
     */
    public static final String ACK_TOKEN = "ackToken";

    /**
     * A String extra that contains the authentication token for an invalidation
     * source type.
     */
    public static final String AUTH_TOKEN = "authToken";

    /**
     * A String extra that identifies the calling client. The value must be
     * prefixed by the package name of the client's application and may have a
     * suffix that uniquely distinguishes multiple clients within the scope of
     * that application.
     */
    public static final String CLIENT = "appId";

    /**
     * A ParcelableInvalidation extra that contains an invalidation object
     */
    public static final String INVALIDATION = "invalidation";

    /**
     * A {@link ParcelableObjectId} extra that contains an object id
     */
    public static final String OBJECT_ID = "objectId";

    /**
     * An {@link IntentSender} extra that can be used to send invalidation
     * intents back to the client.
     */
    public static final String SENDER = "sender";

    /**
     * An integer extra that contains the value of the invalidation source type.
     */
    public static final String SOURCE = "source";

    /**
     * An integer extra that contains the ordinal value of the
     * {@link RegistrationState} for an object.
     */
    public static final String STATE = "state";

    /**
     * An {@link ParcelableUnknownHint} that contains the unknown hint for a
     * registration change.
     */
    public static final String UNKNOWN_HINT = "unknownHint";
  }

  /**
   * Sets an account into the {@link Extras#ACCOUNT} extra for an intent.
   *
   * @param intent the target intent.
   * @param account to set.
   */
  public static void putAccount(Intent intent, Account account) {
    Preconditions.checkNotNull(intent, "intent");
    Preconditions.checkNotNull(account, "account");
    intent.putExtra(Extras.ACCOUNT, account);
  }

  /**
   * Returns the {@link Extras#ACCOUNT} extra for the provided intent.
   *
   * @param intent source intent
   * @return account extra value or {@code null} if not set.
   */
  public static Account getAccount(Intent intent) {
    return intent.getParcelableExtra(Extras.ACCOUNT);
  }

  /**
   * Sets an acknowledgement token into the {@link Extras#ACK_TOKEN} extra for
   * an intent.
   *
   * @param intent the target intent.
   * @param ackToken token to set.
   */
  public static void putAckToken(Intent intent, AckToken ackToken) {
    Preconditions.checkNotNull(intent, "intent");
    Preconditions.checkNotNull(ackToken, "ackToken");
    intent.putExtra(Extras.ACK_TOKEN, ackToken.tokenData);
  }

  /**
   * Returns the {@link Extras#ACK_TOKEN} extra for the provided intent.
   *
   * @param intent source intent
   * @return ack token extra value or {@code null} if not set.
   */
  public static AckToken getAckToken(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    byte [] tokenData = intent.getByteArrayExtra(Extras.ACK_TOKEN);
    return (tokenData == null) ? null : InvalidationTypes.newAckToken(tokenData);
  }

  /**
   * Sets an authentication token into the {@link Extras#AUTH_TOKEN} extra for
   * an intent.
   *
   * @param intent the target intent.
   * @param authToken token to set.
   */
  public static void putAuthToken(Intent intent, String authToken) {
    Preconditions.checkNotNull(intent, "intent");
    intent.putExtra(Extras.AUTH_TOKEN, authToken);
  }

  /**
   * Returns the {@link Extras#AUTH_TOKEN} extra for the provided intent.
   *
   * @param intent source intent
   * @return client id extra value or {@code null} if not set.
   */
  public static String getAuthToken(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    return intent.getStringExtra(Extras.AUTH_TOKEN);
  }

  /**
   * Sets a client id into the {@link Extras#CLIENT} extra for an intent.
   *
   * @param intent the target intent.
   * @param clientId the client id to set.
   */
  public static void putClientId(Intent intent, String clientId) {
    Preconditions.checkNotNull(intent, "intent");
    intent.putExtra(Extras.CLIENT, clientId);
  }

  /**
   * Returns the {@link Extras#CLIENT} extra for the provided intent.
   *
   * @param intent source intent
   * @return application id extra value or {@code null} if not set.
   */
  public static String getClientId(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    return intent.getStringExtra(Extras.CLIENT);
  }
  /**
   * Wraps an {link Invalidation}value to enable writing to and reading from a
   * {@link Parcel}.
   */
  public static class ParcelableInvalidation implements Parcelable {
    final Invalidation invalidation;
    final boolean includePayload;

    public static final Parcelable.Creator<ParcelableInvalidation> CREATOR =
        new Parcelable.Creator<ParcelableInvalidation>() {
          public ParcelableInvalidation createFromParcel(Parcel in) {
            return new ParcelableInvalidation(in);
          }

          public ParcelableInvalidation[] newArray(int size) {
            return new ParcelableInvalidation[size];
          }
        };

    /**
     * Creates a new wrapper around the provided invalidation
     */
    @VisibleForTesting
    ParcelableInvalidation(Invalidation invalidation, boolean includePayload) {
      this.invalidation = invalidation;
      this.includePayload = includePayload;
    }

    /**
     * Creates a new invalidation wrapper by reading data from a parcel.
     */
    public ParcelableInvalidation(Parcel in) {
      ParcelableObjectId objectId = in.readParcelable(getClass().getClassLoader());
      long version = in.readLong();
      boolean[] values = in.createBooleanArray();
      byte[] payload = null;
      if (values[0]) { // hasBytes
        payload = in.createByteArray();
      }
      this.invalidation =
          InvalidationTypes.newInvalidation(objectId.objectId, version, payload, null);
      this.includePayload = payload != null;
    }

    @Override
    public int describeContents() {
      return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {

      // Data written to parcel is:
      // 1. object id (as ParcelableObjectId)
      // 2. long version
      // 3. boolean [] { hasPayload }
      // 4. byte array for payload (if hasPayload)
      parcel.writeParcelable(new ParcelableObjectId(invalidation.getObjectId()), 0);
      parcel.writeLong(invalidation.getVersion());
      byte[] payload = invalidation.getPayload();
      if (includePayload && payload != null) {
        parcel.writeBooleanArray(new boolean[] {true});
        parcel.writeByteArray(payload);
      } else {
        parcel.writeBooleanArray(new boolean[] {false});
      }
    }

    @Override
    public boolean equals(Object object) {
      return object instanceof ParcelableInvalidation
          && invalidation.equals(((ParcelableInvalidation) object).invalidation);
    }

    @Override
    public int hashCode() {
      return invalidation.hashCode();
    }
  }

  /**
   * Sets an invalidation into the {@link Extras#INVALIDATION} extra for an
   * intent.
   *
   * @param intent the target intent.
   * @param invalidation the invalidation to set.
   * @param includePayload {@code true} if the invalidation payload should be
   *        sent.
   */
  public static void putInvalidation(
      Intent intent, Invalidation invalidation, boolean includePayload) {
    Preconditions.checkNotNull(intent, "intent");
    Preconditions.checkNotNull(invalidation, "invalidation");
    intent.putExtra(Extras.INVALIDATION, new ParcelableInvalidation(invalidation, includePayload));
  }

  /**
   * Returns the invalidation for the {@link Extras#INVALIDATION} extra.
   *
   * @param intent source intent
   * @return invalidation extra value or {@code null} if not set.
   */
  public static Invalidation getInvalidation(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    ParcelableInvalidation wrapper = intent.getParcelableExtra(Extras.INVALIDATION);
    return (wrapper == null) ? null : wrapper.invalidation;
  }

  /**
   * Wraps an {link ObjectId} value to enable writing to and reading from a
   * {@link Parcel}.
   */
  public static class ParcelableObjectId implements Parcelable {
    final ObjectId objectId;

    public static final Parcelable.Creator<ParcelableObjectId> CREATOR =
        new Parcelable.Creator<ParcelableObjectId>() {
          public ParcelableObjectId createFromParcel(Parcel in) {
            return new ParcelableObjectId(in);
          }

          public ParcelableObjectId[] newArray(int size) {
            return new ParcelableObjectId[size];
          }
        };

    /**
     * Creates a new wrapper around the provided object id.
     */
    @VisibleForTesting
    ParcelableObjectId(ObjectId objectId) {
      this.objectId = objectId;
    }

    /**
     * Creates a new wrapper and object id by reading data from a parcel.
     */
    private ParcelableObjectId(Parcel in) {
      int source = in.readInt();
      byte[] value = in.createByteArray();
      objectId = InvalidationTypes.newObjectId(source, value);
    }

    @Override
    public int describeContents() {
      return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {

      // Data written to parcel is:
      // 1. numeric value of source type
      // 2. byte array for name
      parcel.writeInt(objectId.getSource());
      parcel.writeByteArray(objectId.getName());
    }

    @Override
    public boolean equals(Object object) {
      return object instanceof ParcelableObjectId
          && objectId.equals(((ParcelableObjectId) object).objectId);
    }

    @Override
    public int hashCode() {
      return objectId.hashCode();
    }
  }

  /**
   * Sets an object id into the {@link Extras#OBJECT_ID} extra for an intent.
   *
   * @param intent the target intent.
   * @param objectId the object id to set.
   */
  public static void putObjectId(Intent intent, ObjectId objectId) {
    Preconditions.checkNotNull(intent, "intent");
    Preconditions.checkNotNull(objectId, "objectId");
    intent.putExtra(Extras.OBJECT_ID, new ParcelableObjectId(objectId));
  }

  /**
   * Returns the object id for the {@link Extras#OBJECT_ID} extra.
   *
   * @param intent source intent
   * @return object id extra value or {@code null} if not set.
   */
  public static ObjectId getObjectId(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    ParcelableObjectId wrapper = intent.getParcelableExtra(Extras.OBJECT_ID);
    return (wrapper == null) ? null : wrapper.objectId;
  }

  /**
   * Sets an intent sender into the {@link Extras#SENDER} extra for an intent.
   *
   * @param intent the target intent.
   * @param sender the intent sender for invalidation events.
   */
  public static void putSender(Intent intent, IntentSender sender) {
    Preconditions.checkNotNull(intent, "intent");
    Preconditions.checkNotNull(sender, "sender");
    intent.putExtra(Extras.SENDER, sender);
  }

  /**
   * Returns the {@link Extras#SENDER} extra for the provided intent.
   *
   * @param intent source intent
   * @return sender event intent sender or {@code null} if not set.
   */
  public static IntentSender getSender(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    return intent.getParcelableExtra(Extras.SENDER);
  }

  /**
   * Sets registration state into the {@link Extras#STATE} extra for an intent.
   *
   * @param intent the target intent.
   * @param state registration state to set.
   */
  public static void putRegistrationState(Intent intent, RegistrationState state) {
    Preconditions.checkNotNull(intent, "intent");
    Preconditions.checkNotNull(state, "state");
    intent.putExtra(Extras.STATE, state.ordinal());
  }

  /**
   * Returns the {@link Extras#STATE} extra for the provided intent.
   *
   * @param intent source intent
   * @return registration state or {@code null} if not set.
   */
  public static RegistrationState getRegistrationState(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    int ordinal = intent.getIntExtra(Extras.STATE, -1);
    if (ordinal == -1) {
      return null;
    }
    return RegistrationState.values()[ordinal];
  }

  /**
   * Sets an invalidation source type into the {@link Extras#SOURCE} extra for
   * an intent.
   *
   * @param intent the target intent.
   * @param source invalidation source type.
   */
  public static void putSource(Intent intent, int source) {
    Preconditions.checkNotNull(intent, "intent");
    Preconditions.checkNotNull(source, "source");
    intent.putExtra(Extras.SOURCE, source);
  }

  /**
   * Returns the {@link Extras#SOURCE} extra for the provided intent.
   *
   * @param intent source intent
   * @return invalidation source extra value or {@code -1} if not set.
   */
  public static int getSource(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    return intent.getIntExtra(Extras.SOURCE, -1);
  }

  /**
   * Wraps an {link RegistrationState} value to enable writing to and reading
   * from a {@link Parcel}.
   */
  public static class ParcelableUnknownHint implements Parcelable {
    final UnknownHint hint;

    public static final Parcelable.Creator<ParcelableUnknownHint> CREATOR =
        new Parcelable.Creator<ParcelableUnknownHint>() {
          public ParcelableUnknownHint createFromParcel(Parcel in) {
            return new ParcelableUnknownHint(in);
          }

          public ParcelableUnknownHint[] newArray(int size) {
            return new ParcelableUnknownHint[size];
          }
        };

    /**
     * Creates a new wrapper around the provided object id.
     */
    @VisibleForTesting
    ParcelableUnknownHint(UnknownHint hint) {
      this.hint = hint;
    }

    /**
     * Creates a new wrapper and object id by reading data from a parcel.
     */
    private ParcelableUnknownHint(Parcel in) {
      boolean[] values = in.createBooleanArray();
      String msg = values[1] ? in.readString() : null;
      this.hint = InvalidationTypes.newUnknownHint(values[0], msg);
    }

    @Override
    public int describeContents() {
      return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {

      // Data written to parcel is:
      // 1. boolean [] [isTransient, hasMessage]
      // 2. message String if hasMessage is true
      String msg = hint.getMessage();
      parcel.writeBooleanArray(new boolean[] {hint.isTransient(), msg != null});
      if (msg != null) {
        parcel.writeString(msg);
      }
    }

    @Override
    public boolean equals(Object object) {
      return object instanceof ParcelableUnknownHint
          && hint.equals(((ParcelableUnknownHint) object).hint);
    }

    @Override
    public int hashCode() {
      return hint.hashCode();
    }
  }

  /**
   * Sets an unknown hint into the {@link Extras#UNKNOWN_HINT} extra for an
   * intent.
   *
   * @param intent the target intent.
   * @param hint the hint to set.
   */
  public static void putUnknownHint(Intent intent, UnknownHint hint) {
    Preconditions.checkNotNull(intent, "intent");
    Preconditions.checkNotNull(hint, "hint");
    intent.putExtra(Extras.UNKNOWN_HINT, new ParcelableUnknownHint(hint));
  }

  /**
   * Returns the unknown hint for the {@link Extras#UNKNOWN_HINT} extra.
   *
   * @param intent source intent
   * @return unknown hint or {@code null} if not set.
   */
  public static UnknownHint getUnknownHint(Intent intent) {
    Preconditions.checkNotNull(intent, "intent");
    ParcelableUnknownHint wrapper = intent.getParcelableExtra(Extras.UNKNOWN_HINT);
    return (wrapper == null) ? null : wrapper.hint;
  }

  private InvalidationIntents() {
  } // not instantiable
}
