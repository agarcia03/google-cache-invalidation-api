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

import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.util.Formatter;

import android.util.Log;

import java.util.logging.Level;


/**
 * Provides the implementation of {@link Logger} for Android. The logging tag will be based upon the
 * top-level class name containing the code invoking the logger (the outer class, not an inner or
 * anonymous class name).   For severe and warning level messages, the Android logger will also
 * dump the stack trace of the first argument if it is a throwable.
 */
public class AndroidLogger implements Logger {

  /**
   * The maximum length of an Android logging tag. There's no formal constants but the constraint is
   * mentioned in the Log javadoc
   */
  private static final int MAX_TAG_LENGTH = 23;

  /** Prefix added to Android logging messages */
  private final String logPrefix;

  /** Creates a logger that prefixes every logging stmt with {@code logPrefix}. */
  public AndroidLogger(String logPrefix) {
    this.logPrefix = logPrefix;
  }

  @Override
  public boolean isLoggable(Level level) {
    return Log.isLoggable(getTag(), javaLevelToAndroidLevel(level));
  }

  @Override
  public void log(Level level, String template, Object... args) {
    int androidLevel = javaLevelToAndroidLevel(level);
    String tag = getTag();
    if (Log.isLoggable(tag, androidLevel)) {
      Log.println(androidLevel, tag, format(template, args));
    }
  }

  @Override
  public void severe(String template, Object...args) {
    String tag = getTag();
    if (Log.isLoggable(tag, Log.ERROR)) {
      // If the first argument is an exception, use the form of Log that will dump a stack trace
      if ((args.length > 0) && (args[0] instanceof Throwable)) {
        Log.e(tag, format(template, args), (Throwable) args[0]);
      } else {
        Log.e(tag, format(template, args));
      }
    }
  }

  @Override
  public void warning(String template, Object...args) {
    String tag = getTag();
    if (Log.isLoggable(tag, Log.WARN)){
      // If the first argument is an exception, use the form of Log that will dump a stack trace
      if ((args.length > 0) && (args[0] instanceof Throwable)) {
        Log.w(tag, format(template, args), (Throwable) args[0]);
      } else {
        Log.w(tag, format(template, args));
      }
    }
  }

  @Override
  public void info(String template, Object...args) {
    String tag = getTag();
    if (Log.isLoggable(tag, Log.INFO)) {
      Log.i(tag, format(template, args));
    }
  }

  @Override
  public void fine(String template, Object...args) {
    String tag = getTag();
    if (Log.isLoggable(tag, Log.DEBUG)) {
      Log.v(tag, format(template, args));
    }
  }

  @Override
  public void setSystemResources(SystemResources resources) {
    // No-op.
  }

  /** Given a Java logging level, returns the corresponding Android level. */
  private static int javaLevelToAndroidLevel(Level level) {
    if (level == Level.INFO) {
      return android.util.Log.INFO;
    } else if (level ==  Level.WARNING) {
      return android.util.Log.WARN;
    } else if (level == Level.SEVERE) {
      return android.util.Log.ERROR;
    } else if (level == Level.FINE) {
      return android.util.Log.DEBUG;
    } else {
      throw new RuntimeException("Unsupported level: " + level);
    }
  }

  private String format(String template, Object...args) {
    return "[" + logPrefix + "] " + Formatter.format(template, args);
  }

  private static String getTag() {
    StackTraceElement[] stackTrace = new Throwable().getStackTrace();
    String className = null;
    for (int i = 0; i < stackTrace.length; i++) {
      className = stackTrace[i].getClassName();

      // Skip over this class's methods
      if (!className.equals(AndroidLogger.class.getName())) {
        break;
      }
    }

    // Compute the unqualified class name w/out any inner class, then truncate to the
    // maximum tag length.
    int unqualBegin = className.lastIndexOf('.') + 1;
    if (unqualBegin < 0) { // should never happen, but be safe
      unqualBegin = 0;
    }
    int unqualEnd = className.indexOf('$', unqualBegin);
    if (unqualEnd < 0) {
      unqualEnd = className.length();
    }
    if ((unqualEnd - unqualBegin) > MAX_TAG_LENGTH) {
      unqualEnd = unqualBegin + MAX_TAG_LENGTH;
    }
    return className.substring(unqualBegin, unqualEnd);
  }
}
