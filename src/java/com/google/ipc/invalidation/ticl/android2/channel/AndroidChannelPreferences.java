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
package com.google.ipc.invalidation.ticl.android2.channel;

import com.google.ipc.invalidation.ticl.android2.channel.AndroidChannelConstants.C2dmConstants;

import android.content.Context;
import android.content.SharedPreferences;


/** Accessor class for shared preference entries used by the channel. */

public class AndroidChannelPreferences {
  /** Name of the preferences in which channel preferences are stored. */
  private static final String PREFERENCES_NAME = "com.google.ipc.invalidation.gcmchannel";

  /** Sets the token echoed on subsequent HTTP requests. */
  static void setEchoToken(Context context, String token) {
    SharedPreferences.Editor editor = getPreferences(context).edit();

    // This might fail, but at worst it just means we lose an echo token; the channel
    // needs to be able to handle that anyway since it can never assume an echo token
    // makes it to the client (since the channel can drop messages).
    editor.putString(C2dmConstants.ECHO_PARAM, token).commit();
  }

  /** Returns the echo token that should be included on HTTP requests. */
  
  public static String getEchoToken(Context context) {
    return getPreferences(context).getString(C2dmConstants.ECHO_PARAM, null);
  }

  /** Returns a new {@link SharedPreferences} instance to access the channel preferences. */
  private static SharedPreferences getPreferences(Context context) {
    return context.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE);
  }
}
