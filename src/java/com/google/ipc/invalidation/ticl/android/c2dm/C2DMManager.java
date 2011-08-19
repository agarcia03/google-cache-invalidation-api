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

package com.google.ipc.invalidation.ticl.android.c2dm;

import com.google.common.base.Preconditions;

import android.app.AlarmManager;
import android.app.IntentService;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.util.Log;

import java.util.List;
import java.util.Set;

/**
 * Class for managing C2DM registration and dispatching of messages to observers.
 *
 * Requires setting the {@link #SENDER_ID_METADATA_FIELD} metadata field with the correct e-mail to
 * be used for the C2DM registration.
 *
 * This is based on the open source chrometophone project.
 */
public class C2DMManager extends IntentService {

  private static final String TAG = "C2DM";

  /**
   * The action of intents sent from the android c2dm framework regarding registration
   */
  
  static final String REGISTRATION_CALLBACK_INTENT = "com.google.android.c2dm.intent.REGISTRATION";

  /**
   * The action of intents sent from the Android C2DM framework when we are supposed to retry
   * registration.
   */
  private static final String C2DM_RETRY = "com.google.android.c2dm.intent.RETRY";

  /**
   * The key in the bundle to use for the sender ID when registering for C2DM.
   *
   * The value of the field itself must be the account that the server-side pushing messages
   * towards the client is using when talking to C2DM.
   */
  private static final String EXTRA_SENDER = "sender";

  /**
   * The key in the bundle to use for boilerplate code identifying the client application towards
   * the Android C2DM framework
   */
  private static final String EXTRA_APPLICATION_PENDING_INTENT = "app";

  /**
   * The action of intents sent to the Android C2DM framework when we want to register
   */
  private static final String REQUEST_UNREGISTRATION_INTENT =
      "com.google.android.c2dm.intent.UNREGISTER";

  /**
   * The action of intents sent to the Android C2DM framework when we want to unregister
   */
  private static final String REQUEST_REGISTRATION_INTENT =
      "com.google.android.c2dm.intent.REGISTER";

  /**
   * The package for the Google Services Framework
   */
  private static final String GSF_PACKAGE = "com.google.android.gsf";

  /**
   * The action of intents sent from the Android C2DM framework when a message is received.
   */
  
  static final String C2DM_INTENT = "com.google.android.c2dm.intent.RECEIVE";

  /**
   * The key in the bundle to use when we want to read the C2DM registration ID after a successful
   * registration
   */
  
  static final String EXTRA_REGISTRATION_ID = "registration_id";

  /**
   * The key in the bundle to use when we want to see if we were unregistered from C2DM
   */
  
  static final String EXTRA_UNREGISTERED = "unregistered";

  /**
   * The key in the bundle to use when we want to see if there was any errors when we tried to
   * register.
   */
  
  static final String EXTRA_ERROR = "error";

  /**
   * The android:name we read from the meta-data for the C2DMManager service in the
   * AndroidManifest.xml file when we want to know which sender id we should use when registering
   * towards C2DM
   */
  
  static final String SENDER_ID_METADATA_FIELD = "sender_id";

  /**
   * When this field is true, this service uses WakeLocks during processing.   Always
   * {@code true} except during tests that disable wake locks to simplify.
   */
  private static boolean useWakeLock = true;

  /**
   * The sender ID we have read from the meta-data in AndroidManifest.xml for this service.
   */
  private String senderId;

  /**
   * Observers to dispatch messages from C2DM to
   */
  private Set<C2DMObserver> observers;

  /**
   * Previously registered observers that has requested to unregister from C2DM.
   *
   */
  private Set<C2DMObserver> unregisteringObservers;

  /**
   * A field used by tests to specify a mock context instead of the real one
   */
  private static Context mockContext;

  /**
   * A field which is set to true whenever a C2DM registration is in progress. It is set to false
   * otherwise.
   */
  private boolean registrationInProcess;

  /**
   * The context read during onCreate() which is used throughout the lifetime of this service.
   */
  private Context context;

  /**
   * A field which is set to true whenever a C2DM unregistration is in progress. It is set to false
   * otherwise.
   */
  private boolean unregistrationInProcess;

  /**
   * A reference to our helper service for handling WakeLocks.
   */
  private WakeLockManager wakeLockManager;

  /**
   * Called from the broadcast receiver and from any observer wanting to register (observers usually
   * go through calling C2DMessaging.register(...). Will process the received intent, call
   * handleMessage(), onRegistered(), etc. in background threads, with a wake lock, while keeping
   * the service alive.
   *
   * @param context application to run service in
   * @param intent the intent received
   */
  static void runIntentInService(Context context, Intent intent) {
    if (useWakeLock) {
      // This is called from C2DMBroadcastReceiver and C2DMessaging, there is no init.
      WakeLockManager.getInstance(context).acquire(C2DMManager.class);
    }
    intent.setClassName(context, C2DMManager.class.getCanonicalName());
    context.startService(intent);
  }

  /**
   * A method to override the context to use in this IntentService. Must be called before
   * onCreate().
   *
   * @param context the context to use in the test
   */
  
  static void setMockContextForTest(Context context) {
    mockContext = context;
  }

  public C2DMManager() {
    super("C2DMManager");
  }

  @Override
  public void onCreate() {
    super.onCreate();
    context = (mockContext == null) ? getApplicationContext() : mockContext;
    wakeLockManager = WakeLockManager.getInstance(context);
    readSenderIdFromMetaData();
    observers = C2DMSettings.getObservers(context);
    unregisteringObservers = C2DMSettings.getUnregisteringObservers(context);
    registrationInProcess = C2DMSettings.isRegistering(context);
    unregistrationInProcess = C2DMSettings.isUnregistering(context);
  }

  @Override
  public final void onHandleIntent(Intent intent) {
    try {
      if (intent.getAction().equals(REGISTRATION_CALLBACK_INTENT)) {
        handleRegistration(intent);
      } else if (intent.getAction().equals(C2DM_INTENT)) {
        onMessage(intent);
      } else if (intent.getAction().equals(C2DM_RETRY)) {
        register();
      } else if (intent.getAction().equals(C2DMessaging.ACTION_REGISTER)) {
        registerObserver(intent);
      } else if (intent.getAction().equals(C2DMessaging.ACTION_UNREGISTER)) {
        unregisterObserver(intent);
      } else {
        Log.w(TAG, "Receieved unknown action:" + intent.getAction());
      }
    } finally {
      if (useWakeLock) {
        // Release the power lock, so device can get back to sleep.
        // The lock is reference counted by default, so multiple
        // messages are ok.
        wakeLockManager.release(C2DMManager.class);
      }
    }
  }

  /**
   * Some tests need to call in to the C2DMManager, and at they might not want to handle the burden
   * of releasing the wakelock. As such, this method is offered to tests so they can disable the
   * handling of wakelocks.
   *
   * Setting this to false will make sure the C2DMManager does not ask for the PowerManager system
   * service, does not acquire the wakelock, and does not release it.
   *
   * @param value true if the wakelock should be used, false otherwise
   */
  
  public static void setUseWakelockForTest(boolean value) {
    useWakeLock = value;
  }

  /**
   * Called when a cloud message has been received.
   *
   * @param intent the received intent
   */
  private void onMessage(Intent intent) {
    for (C2DMObserver observer : observers) {
      if (observer.matches(intent)) {
        Intent outgoingIntent = new Intent(intent);
        outgoingIntent.setAction(C2DMessaging.ACTION_MESSAGE);
        outgoingIntent.setClass(context, observer.getObserverClass());
        deliverObserverIntent(observer, outgoingIntent);
      }
    }
  }

  /**
   * Called on registration error. Override to provide better error messages.
   *
   * This is called in the context of a Service - no dialog or UI.
   *
   * @param errorId the errorId String
   */
  private void onRegistrationError(String errorId) {
    setRegistrationInProcess(false);
    for (C2DMObserver observer : observers) {
      Intent outgoingIntent = new Intent(context, observer.getObserverClass());
      outgoingIntent.setAction(C2DMessaging.ACTION_REGISTRATION_ERROR);
      outgoingIntent.putExtra(C2DMessaging.EXTRA_REGISTRATION_ERROR, errorId);
      deliverObserverIntent(observer, outgoingIntent);
    }
  }

  /**
   * Called when a registration token has been received.
   *
   * @param registrationId the registration ID received from C2DM
   */
  private void onRegistered(String registrationId) {
    setRegistrationInProcess(false);
    C2DMSettings.setC2DMRegistrationId(context, registrationId);
    for (C2DMObserver observer : observers) {
      onRegisteredSingleObserver(registrationId, observer);
    }
  }

  /**
   * Informs the given observer about the registration ID
   */
  private void onRegisteredSingleObserver(String registrationId, C2DMObserver observer) {
    Intent outgoingIntent = new Intent(context, observer.getObserverClass());
    outgoingIntent.setAction(C2DMessaging.ACTION_REGISTERED);
    outgoingIntent.putExtra(C2DMessaging.EXTRA_REGISTRATION_ID, registrationId);
    deliverObserverIntent(observer, outgoingIntent);
  }

  /**
   * Called when the device has been unregistered.
   */
  private void onUnregistered() {
    setUnregisteringInProcess(false);
    C2DMSettings.clearC2DMRegistrationId(context);
    for (C2DMObserver observer : observers) {
      onUnregisteredSingleObserver(observer);
    }
    for (C2DMObserver observer : unregisteringObservers) {
      onUnregisteredSingleObserver(observer);
    }
    unregisteringObservers.clear();
    C2DMSettings.setUnregisteringObservers(context, unregisteringObservers);
  }

  /**
   * Informs the given observer that the application is no longer registered to C2DM
   */
  private void onUnregisteredSingleObserver(C2DMObserver observer) {
    Intent outgoingIntent = new Intent(context, observer.getObserverClass());
    outgoingIntent.setAction(C2DMessaging.ACTION_UNREGISTERED);
    deliverObserverIntent(observer, outgoingIntent);
  }

  /**
   * Starts the observer service by delivering it the provided intent. If the observer has asked us
   * to get a WakeLock for it, we do that and inform the observer that the WakeLock has been
   * acquired through the flag C2DMessaging.EXTRA_RELEASE_WAKELOCK.
   */
  private void deliverObserverIntent(C2DMObserver observer, Intent intent) {
    if (observer.isHandleWakeLock()) {
      // Set the extra so the observer knows that it needs to release the wake lock
      intent.putExtra(C2DMessaging.EXTRA_RELEASE_WAKELOCK, true);
      wakeLockManager.acquire(observer.getObserverClass());
    }
    context.startService(intent);
  }

  /**
   * Registers an observer.
   *
   *  If this was the first observer we also start registering towards C2DM. If we were already
   * registered, we do a callback to inform about the current C2DM registration ID.
   */
  private void registerObserver(Intent intent) {
    C2DMObserver observer = C2DMObserver.createFromIntent(intent);
    observers.add(observer);
    C2DMSettings.setObservers(context, observers);
    if (C2DMSettings.hasC2DMRegistrationId(context)) {
      onRegisteredSingleObserver(C2DMSettings.getC2DMRegistrationId(context), observer);
    } else {
      if (!isRegistrationInProcess()) {
        register();
      }
    }
  }

  /**
   * Unregisters an observer.
   *
   *  The observer is moved to unregisteringObservers which only gets messages from C2DMManager if
   * we unregister from C2DM completely. If this was the last observer, we also start the process of
   * unregistering from C2DM.
   */
  private void unregisterObserver(Intent intent) {
    C2DMObserver observer = C2DMObserver.createFromIntent(intent);
    if (observers.remove(observer)) {
      C2DMSettings.setObservers(context, observers);
      unregisteringObservers.add(observer);
      C2DMSettings.setUnregisteringObservers(context, unregisteringObservers);
    }
    if (observers.isEmpty()) {
      // No more observers, need to unregister
      if (!isUnregisteringInProcess()) {
        unregister();
      }
    }
  }

  /**
   * Called when the Android C2DM framework sends us a message regarding registration.
   *
   *  This method parses the intent from the Android C2DM framework and calls the appropriate
   * methods for when we are registered, unregistered or if there was an error when trying to
   * register.
   */
  private void handleRegistration(Intent intent) {
    String registrationId = intent.getStringExtra(EXTRA_REGISTRATION_ID);
    String error = intent.getStringExtra(EXTRA_ERROR);
    String removed = intent.getStringExtra(EXTRA_UNREGISTERED);
    if (Log.isLoggable(TAG, Log.DEBUG)) {
      Log.d(TAG,
          "dmControl: registrationId = " + registrationId + ", error = " + error + ", removed = "
              + removed);
    }
    if (removed != null) {
      onUnregistered();
    } else if (error != null) {
      handleRegistrationBackoffOnError(error);
    } else {
      handleRegistration(registrationId);
    }
  }

  /**
   * Informs observers about a registration error, and schedules a registration retry if the error
   * was transient.
   */
  private void handleRegistrationBackoffOnError(String error) {
    Log.e(TAG, "Registration error " + error);
    onRegistrationError(error);
    if (C2DMessaging.ERR_SERVICE_NOT_AVAILABLE.equals(error)) {
      long backoffTimeMs = C2DMSettings.getBackoff(context);
      createAlarm(backoffTimeMs);
      increaseBackoff(backoffTimeMs);
    }
  }

  /**
   * When C2DM registration fails, we call this method to schedule a retry in the future.
   */
  private void createAlarm(long backoffTimeMs) {
    Log.d(TAG, "Scheduling registration retry, backoff = " + backoffTimeMs);
    Intent retryIntent = new Intent(C2DM_RETRY);
    PendingIntent retryPIntent = PendingIntent.getBroadcast(context, 0, retryIntent, 0);
    AlarmManager am = (AlarmManager) context.getSystemService(Context.ALARM_SERVICE);
    am.set(AlarmManager.ELAPSED_REALTIME, backoffTimeMs, retryPIntent);
  }

  /**
   * Increases the backoff time for retrying C2DM registration
   */
  private void increaseBackoff(long backoffTimeMs) {
    backoffTimeMs *= 2;
    C2DMSettings.setBackoff(context, backoffTimeMs);
  }

  /**
   * When C2DM registration is complete, this method resets the backoff and makes sure all observers
   * are informed
   */
  private void handleRegistration(String registrationId) {
    C2DMSettings.resetBackoff(context);
    onRegistered(registrationId);
  }

  private void setRegistrationInProcess(boolean registrationInProcess) {
    C2DMSettings.setRegistering(context, registrationInProcess);
    this.registrationInProcess = registrationInProcess;
  }

  private boolean isRegistrationInProcess() {
    return registrationInProcess;
  }

  private void setUnregisteringInProcess(boolean unregisteringInProcess) {
    C2DMSettings.setUnregistering(context, unregisteringInProcess);
    this.unregistrationInProcess = unregisteringInProcess;
  }

  private boolean isUnregisteringInProcess() {
    return unregistrationInProcess;
  }

  /**
   * Initiate c2d messaging registration for the current application
   */
  private void register() {
    Intent registrationIntent = new Intent(REQUEST_REGISTRATION_INTENT);
    registrationIntent.setPackage(GSF_PACKAGE);
    registrationIntent.putExtra(
        EXTRA_APPLICATION_PENDING_INTENT, PendingIntent.getBroadcast(context, 0, new Intent(), 0));
    registrationIntent.putExtra(EXTRA_SENDER, senderId);
    context.startService(registrationIntent);
  }

  /**
   * Unregister the application. New messages will be blocked by server.
   */
  private void unregister() {
    Intent regIntent = new Intent(REQUEST_UNREGISTRATION_INTENT);
    regIntent.setPackage(GSF_PACKAGE);
    regIntent.putExtra(
        EXTRA_APPLICATION_PENDING_INTENT, PendingIntent.getBroadcast(context, 0, new Intent(), 0));
    setUnregisteringInProcess(true);
    context.startService(regIntent);
  }

  /**
   * Reads the meta-data to find the field specified in SENDER_ID_METADATA_FIELD. The value of that
   * field is used when registering towards C2DM.
   */
  private void readSenderIdFromMetaData() {
    List<ResolveInfo> resolveInfos = getPackageManager().queryIntentServices(
        new Intent(context, C2DMManager.class), PackageManager.GET_META_DATA);
    Preconditions.checkState(!resolveInfos.isEmpty(), "Cannot find service metadata");
    ServiceInfo serviceInfo = resolveInfos.get(0).serviceInfo;
    if (serviceInfo.metaData != null) {
      senderId = serviceInfo.metaData.getString(SENDER_ID_METADATA_FIELD);
      if (senderId == null) {
        Log.e(TAG, "No meta-data element with the name " + SENDER_ID_METADATA_FIELD
            + " found on the service declaration.  An element with this name "
            + "must have a value that is the server side account in use for C2DM");
        stopSelf();
      }
    } else {
      Log.e(TAG, "No meta-data elements found on the service declaration. One with a name of "
          + SENDER_ID_METADATA_FIELD
          + " must have a value that is the server side account in use for C2DM");
      stopSelf();
    }
  }
}
