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

package com.google.ipc.invalidation.ticl.android;

import com.google.ipc.invalidation.ticl.android.c2dm.C2DMBaseReceiver;

import android.content.Context;
import android.content.Intent;
import android.util.Base64;
import android.util.Log;

import java.io.IOException;

/**
 * Service that handles system C2DM messages (with support from the {@link C2DMBaseReceiver} base
 * class. It receives intents for C2DM registration, errors and message delivery. It does some basic
 * processing and then forwards the messages to the {@link AndroidInvalidationService} for handling.
 *
 */
public class AndroidC2DMReceiver extends C2DMBaseReceiver {

  /** Logging tag */
  private static final String TAG = "AndroidC2DMReceiver";

  public AndroidC2DMReceiver() {
    super(AndroidC2DMConstants.SENDER_ID);
  }

  @Override
  public void onRegistered(Context context, String registrationId) throws IOException {
    super.onRegistered(context, registrationId);

    Log.i(TAG, "Registration received: " + registrationId);

    // Upon receiving a new updated c2dm ID, notify the invalidation service
    Intent serviceIntent =
        AndroidInvalidationService.createRegistrationIntent(context, registrationId);
    context.startService(serviceIntent);
  }

  @Override
  public void onUnregistered(Context context) {
    super.onUnregistered(context);
    Log.w(TAG, "Registraiton revoked");

    // If the c2dm registration ID is revoked, also notify the invalidation service
    Intent serviceIntent = AndroidInvalidationService.createRegistrationIntent(context, null);
    context.startService(serviceIntent);
  }

  @Override
  public void onError(Context context, String errorId) {
    // Send any registration error to the invalidation service
    Intent serviceIntent = AndroidInvalidationService.createErrorIntent(context, errorId);
    context.startService(serviceIntent);
  }

  @Override
  protected void onMessage(Context context, Intent intent) {
    // Extract expected fields and do basic syntactic checks (but no value checking)
    // and forward the result on to the AndroidInvalidationService for processing.
    Intent serviceIntent;
    String clientKey = intent.getStringExtra(AndroidC2DMConstants.CLIENT_KEY_PARAM);
    if (clientKey == null) {
      Log.e(TAG, "Intent does not contain client key value");
      return;
    }
    String encodedData = intent.getStringExtra(AndroidC2DMConstants.CONTENT_PARAM);
    if (encodedData != null) {
      try {
        byte [] rawData = Base64.decode(encodedData, Base64.URL_SAFE);
        serviceIntent = AndroidInvalidationService.createDataIntent(this, clientKey, rawData);
      } catch (IllegalArgumentException iae) {
        Log.e(TAG, "Unable to decode intent data", iae);
        return;
      }
    } else {
      String mailBoxId = intent.getStringExtra(AndroidC2DMConstants.MAILBOX_ID_PARAM);
      if (mailBoxId != null) {
        serviceIntent = AndroidInvalidationService.createMailboxIntent(this, clientKey, mailBoxId);
      } else {
        Log.e(TAG, "Intent did not contain a mailbox id or data");
        return;
      }
    }
    context.startService(serviceIntent);
  }
}
