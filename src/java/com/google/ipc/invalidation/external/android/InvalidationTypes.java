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

import com.google.common.base.Preconditions;

import java.util.Arrays;

import javax.annotation.Nullable;

/**
 * Various types used by the applications using the invalidation client library.
 *
 */
public class InvalidationTypes {

  /**
   * A class for an application id that a client application can use for
   * distinguishing various instantiations of that app (on the same or different
   * machines). This is not interpreted by the invalidation system in any way
   * (and there is no uniqueness requirement on this object.)
   */
  public static class ClientExternalId {

    /** The type of the client. */
    private final int clientType;

    /** The opaque id of the client application. */
    private final byte [] appClientId;

    /**
     * Creates an external id for the given {@code clientType} and {@code
     * appClientId}.
     */
    private ClientExternalId(int clientType, byte [] appClientId) {
      this.clientType = clientType;
      this.appClientId = Preconditions.checkNotNull(appClientId, "appClientId");
    }

    public int getClientType() {
      return clientType;
    }

    public byte [] getAppClientId() {
      return appClientId;
    }

    @Override
    public boolean equals(Object object) {
      if (object == this) {
        return true;
      }

      if (!(object instanceof ClientExternalId)) {
        return false;
      }

      final ClientExternalId other = (ClientExternalId) object;
      return clientType == other.clientType && Arrays.equals(appClientId, other.appClientId);
    }

    @Override
    public int hashCode() {
      return clientType ^ Arrays.hashCode(appClientId);
    }

    @Override
    public String toString() {
      return String.format("<%s, %s>", clientType, appClientId);
    }
  }

  /**
   * A class to represent a unique object id that an application can register or
   * unregister for.
   */
  public static class ObjectId {

    /** The invalidation source type. */
    private final int source;

    /** The name/unique id for the object. */
    private final byte [] name;

    /** Creates an object id for the given {@code source} and id {@code name}. */
    private ObjectId(int source, byte [] name) {
      Preconditions.checkState(source >= 0, "source");
      this.source = source;
      this.name = Preconditions.checkNotNull(name, "name");
    }

    public int getSource() {
      return source;
    }

    public byte [] getName() {
      return name;
    }

    @Override
    public boolean equals(Object object) {
      if (object == this) {
        return true;
      }

      if (!(object instanceof ObjectId)) {
        return false;
      }

      final ObjectId other = (ObjectId) object;
      if (source != other.source || !Arrays.equals(name, other.name)) {
        return false;
      }
      return true;
    }

    @Override
    public int hashCode() {
      return source ^ Arrays.hashCode(name);
    }

    @Override
    public String toString() {
      return String.format("<%s, %s>", source, name);
    }
  }

  /**
   * A class to represent an invalidation for a given object/version and an
   * optional payload.
   */
  public static class Invalidation {

    /** The object being invalidated/updated. */
    private final ObjectId objectId;

    /** The new version of the object. */
    private final long version;

    /** Optional payload for the object. */
    private final byte [] payload;

    /** Optional stamp log. */
    private final Object componentStampLog;

    /**
     * Creates an invalidation for the given {@code object}, {@code version} and
     * optional {@code payload} and optional {@code componentStampLog}.
     */
    private Invalidation(ObjectId objectId, long version, @Nullable byte [] payload,
        @Nullable Object componentStampLog) {
      this.objectId = Preconditions.checkNotNull(objectId, "objectId");
      this.version = version;
      this.payload = payload;
      this.componentStampLog = componentStampLog;
    }

    public ObjectId getObjectId() {
      return objectId;
    }

    public long getVersion() {
      return version;
    }

    /**
     * Returns the optional payload for the object - if none exists, returns
     * {@code null}.
     */
    public byte [] getPayload() {
      return payload;
    }

    /**
     * Returns the optional component stamp log for the object - if none exists,
     * returns {@code null}.
     * <p>
     * For  INTERNAL use only.
     */
    public Object getComponentStampLog() {
      return componentStampLog;
    }

    @Override
    public boolean equals(Object object) {
      if (object == this) {
        return true;
      }

      if (!(object instanceof Invalidation)) {
        return false;
      }

      final Invalidation other = (Invalidation) object;
      if ((payload != null) != (other.payload != null)) {
        // One of the objects has a payload and the other one does not.
        return false;
      }
      // Both have a payload or not.
      return objectId.equals(other.objectId) && (version == other.version) &&
          ((payload == null) || Arrays.equals(payload, other.payload));
    }

    @Override
    public int hashCode() {
      int result = 17;
      result = 31 * result + objectId.hashCode();
      result = 31 * result + (int) (version ^ (version >>> 32));
      if (payload != null) {
        result = 31 * result + Arrays.hashCode(payload);
      }
      return result;
    }

    @Override
    public String toString() {
      return String.format("<%s, %d, %s>", objectId, version, payload);
    }
  }

  /**
   * Possible registration states in which an object may exist.
   */
  public enum RegistrationState {
    REGISTERED,
    UNREGISTERED,
    UNKNOWN,
  }

  /**
   * Hint given to invalidation listeners when the registration state is
   * {@link RegistrationState#UNKNOWN}.
   *
   */
  public static class UnknownHint {
    /**
     * Whether the application should attempt to transition out of the unknown
     * state.
     */
    private final boolean isTransient;

    /** A message describing why the state was unknown, for debugging. */
    private final String message;

    private UnknownHint(boolean isTransient, String message) {
      this.isTransient = isTransient;
      this.message = message;
    }

    /**
     * Whether the application can attempt to transition out of the unknown
     * state, e.g., by calling {@code register}.
     */
    public boolean isTransient() {
      return isTransient;
    }

    /** A message describing why the state was unknown, for debugging. */
    public String getMessage() {
      return message;
    }

    @Override
    public String toString() {
      return "isTransient = " + isTransient + "; message = " + message;
    }

    @Override
    public boolean equals(Object object) {
      if (object == this) {
        return true;
      }

      if (!(object instanceof UnknownHint)) {
        return false;
      }

      final UnknownHint other = (UnknownHint) object;
      if (isTransient != other.isTransient) {
        return false;

      }
      if (message == null) {
        return other.message == null;
      }
      return message.equals(other.message);
    }

    @Override
    public int hashCode() {
      if (message == null) {
        return isTransient ? 1 : 0;
      }
      int result = 19;
      result = 31 * result + (isTransient ? 1 : 0);
      result = 31 * result + message.hashCode();
      return result;
    }
  }

  /**
   * Represents an opaque token that can be used to acknowledge an invalidation event by
   * calling {@link InvalidationClient#acknowledge(AckToken)} to indicate that the client has
   * successfully handled the event.
   */
  public static class AckToken {
    /** The serialized representation of the token */
    public final byte [] tokenData;

    protected AckToken(byte [] tokenData) {
      this.tokenData = tokenData;
    }

    @Override
    public boolean equals(Object object) {
      if (object == this) {
        return true;
      }

      if (!(object instanceof AckToken)) {
        return false;
      }

      final AckToken other = (AckToken) object;
      return Arrays.equals(tokenData, other.tokenData);
    }

    @Override
    public int hashCode() {
      return Arrays.hashCode(tokenData);
    }
  }

  // Factory methods to create objects for the above classes.

  /**
   * Creates an external id for the given {@code clientType} and {@code
   * appClientId} (does not make a copy of the byte array).
   */
  public static ClientExternalId newClientExternalId(int clientType,
      byte[] appClientId) {
    return new ClientExternalId(clientType, appClientId);
  }

  /**
   * Creates an object id for the given {@code source} and id {@code name}
   * (does not make a copy of the array).
   */
  public static ObjectId newObjectId(int source, byte[] name) {
    return new ObjectId(source, name);
  }

  /**
   * Creates an invalidation for the given {@code object} and {@code version}.
   */
  public static Invalidation newInvalidation(ObjectId objectId, long version) {
    return new Invalidation(objectId, version, null, null);
  }
  /**
   * Creates an invalidation for the given {@code object}, {@code version} and
   * optional {@code payload}.
   * <p>
   * {@code stampLog} is for  internal use only - please pass null.
   */
  public static Invalidation newInvalidation(ObjectId objectId, long version,
      @Nullable byte [] payload, @Nullable Object stampLog) {
    return new Invalidation(objectId, version, payload, stampLog);
  }

  /**
   * Creates a new unknown hint with the provided {@code isTransient} state and
   * {@code message}.
   */
  public static UnknownHint newUnknownHint(boolean isTransient, String message) {
    return new UnknownHint(isTransient, message);
  }

  /**
   * Creates a new ack token from the serialized {@code tokenData} representation.
   */
  public static AckToken newAckToken(byte [] tokenData) {
    return new AckToken(tokenData);
  }
}
