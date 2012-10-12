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
