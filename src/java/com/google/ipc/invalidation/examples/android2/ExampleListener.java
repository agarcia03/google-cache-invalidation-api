package com.google.ipc.invalidation.examples.android2;

import com.google.ipc.invalidation.external.client.InvalidationClient;
import com.google.ipc.invalidation.external.client.InvalidationListener;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ErrorInfo;
import com.google.ipc.invalidation.external.client.types.Invalidation;
import com.google.ipc.invalidation.external.client.types.ObjectId;
import com.google.protos.ipc.invalidation.Types.ObjectSource;

import android.util.Log;

import java.util.HashSet;
import java.util.Set;

/**
 * Implements the service that handles  events for this application. The android library
 * encapsulates all the intent handling and just raises events using the general-purpose
 * {@link InvalidationListener} interface. This example listener registers an interest in a small
 * number of objects and calls {@link MainActivity} with any relevant status changes.
 * <p>
 * Because InvalidationListener does not derive from one of the usual proguard protected types,
 * e.g. {@link android.app.IntentService}, you must manually suppress trimming of the class by
 * adding a line
 * <p>
 * <code>-keep public class com.google.ipc.invalidation.examples.android2.ExampleListener</code>
 * <p>
 * to your proguard configuration file.
 *
 */
public final class ExampleListener implements InvalidationListener {

  /** Object source for objects the client is tracking. */
  private static final int DEMO_SOURCE = ObjectSource.Type.DEMO_VALUE;

  /** The tag used for logging in the listener. */
  private static final String TAG = "TEA2:ExampleListener";

  /** Number of objects we're interested in tracking. */
  private static final int NUM_INTERESTING_OBJECTS = 10;

  /** Ids for objects we want to track. */
  private final Set<ObjectId> interestingObjects;

  public ExampleListener() {
    // We're interested in objects with ids Obj0, Obj1, ...
    interestingObjects = new HashSet<ObjectId>();
    for (int i = 1; i <= NUM_INTERESTING_OBJECTS; i++) {
      interestingObjects.add(ObjectId.newInstance(DEMO_SOURCE,
          ("Obj" + Integer.toString(i)).getBytes()));
    }
  }

  @Override
  public void informError(InvalidationClient client, ErrorInfo errorInfo) {
    Log.e(TAG, "informError: " + errorInfo);

    /***********************************************************************************************
     * YOUR CODE HERE
     *
     * Handling of permanent failures is application-specific.
     **********************************************************************************************/
  }

  @Override
  public void informRegistrationFailure(final InvalidationClient client, final ObjectId objectId,
      boolean isTransient, String errorMessage) {
    Log.e(TAG, "informRegistrationFailure: " + objectId + " " + errorMessage);

    /***********************************************************************************************
     * YOUR CODE HERE
     *
     * In case of transient registration failures, retries should use exponential back-off to avoid
     * excessive load. Handling of permanent failures is application-specific.
     **********************************************************************************************/

    MainActivity.State.setRegistrationStatus(objectId, "Error: " + errorMessage);

    if (!isTransient) {
      // There's nothing we can do with a permanent error!
      return;
    }

    // If the error is transient, send another registration or unregistration request depending
    // on whether we're interested in tracking the object or not.
    if (interestingObjects.contains(objectId)) {
      client.register(objectId);
    } else {
      client.unregister(objectId);
    }
  }

  @Override
  public void informRegistrationStatus(InvalidationClient client, ObjectId objectId,
      RegistrationState regState) {
    Log.i(TAG, "informRegistrationStatus: " + objectId + " " + regState);

    /***********************************************************************************************
     * YOUR CODE HERE
     *
     *  will inform client applications of registration status for objects after calls to
     * InvalidationClient.(un)register. It is the responsibility of the client application to verify
     * that the registration status is consistent with its own expectations and to make additional
     * (un)register calls as needed.
     **********************************************************************************************/

    MainActivity.State.setRegistrationStatus(objectId, "Status: " + regState);

    // If the registration status is what we want, ignore. Otherwise, send another registration
    // request.
    if (interestingObjects.contains(objectId)) {
      if (RegistrationState.UNREGISTERED.equals(regState)) {
        //  is informing us that an object we are interested in tracking is unregistered. Send
        // a register request.
        client.register(objectId);
      }
    } else {
      if (RegistrationState.REGISTERED.equals(regState)) {
        //  is informing us that an object we are not interested in tracking is registered.
        // Send an unregister request.
        client.unregister(objectId);
      }
    }
  }

  @Override
  public void invalidate(InvalidationClient client, Invalidation invalidation,
      AckHandle ackHandle) {
    Log.i(TAG, "invalidate: " + invalidation);

    // Do real work here based upon the invalidation
    MainActivity.State.setVersion(invalidation.getObjectId(),
        "Version from invalidate: " + invalidation.getVersion());

    client.acknowledge(ackHandle);
  }

  @Override
  public void invalidateUnknownVersion(InvalidationClient client, ObjectId objectId,
      AckHandle ackHandle) {
    Log.i(TAG, "invalidateUnknownVersion: " + objectId);

    // Do real work here based upon the invalidation.
    MainActivity.State.setVersion(objectId, "Version from backend: " + getBackendVersion(objectId));

    client.acknowledge(ackHandle);
  }

  @Override
  public void invalidateAll(InvalidationClient client, AckHandle ackHandle) {
    Log.i(TAG, "invalidateAll");

    // Do real work here based upon the invalidation.
    for (ObjectId objectId : interestingObjects) {
      MainActivity.State.setVersion(objectId,
          "Version from backend: " + getBackendVersion(objectId));
    }
    client.acknowledge(ackHandle);
  }

  @Override
  public void ready(InvalidationClient client) {
    Log.i(TAG, "ready");
  }

  @Override
  public void reissueRegistrations(InvalidationClient client, byte[] prefix, int prefixLength) {
    // Reissue registrations for all invalidations.   This method will be called for a newly started
    // client.
    Log.i(TAG, "reissueRegistrations: " + prefix + " " + prefixLength);
    for (ObjectId objectId : interestingObjects) {
      client.register(objectId);
    }
  }

  private long getBackendVersion(ObjectId objectId) {
    /***********************************************************************************************
     * YOUR CODE HERE
     *
     *  has no information about the given object. Connect with the application backend to
     * determine its current state. The implementation should be non-blocking.
     **********************************************************************************************/

    // Normally, we would connect to a real application backend. For this example, we return a fixed
    // value.

    return -1;
  }
}
