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
import com.google.ipc.invalidation.external.client.SystemResources.ComponentLogger;
import com.google.ipc.invalidation.external.client.SystemResources.ComponentNetworkChannel;
import com.google.ipc.invalidation.external.client.SystemResources.ComponentScheduler;
import com.google.ipc.invalidation.external.client.SystemResourcesBuilder;
import com.google.ipc.invalidation.ticl.MemoryStorageImpl;
import com.google.ipc.invalidation.util.Formatter;

import android.util.Log;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.logging.Level;


/**
 * SystemResources creator for the Android system service.
 *
 */
public class AndroidResourcesFactory {

  /**
   * Implementation of {@link SystemResources.Logger} for Android.
   */
  private static class AndroidLogger implements ComponentLogger {

    /** Tag used in Android logging calls. */
    private final String logTag;

    /** Creates a logger that prefixes every logging stmt with {@code logPrefix}. */
    private AndroidLogger(String logPrefix) {
      this.logTag = "Invalidation-" + logPrefix;
    }

    @Override
    public boolean isLoggable(Level level) {
      return Log.isLoggable(logTag, javaLevelToAndroidLevel(level));
    }

    @Override
    public void log(Level level, String template, Object... args) {
      Log.println(javaLevelToAndroidLevel(level), logTag, Formatter.format(template, args));
    }

    @Override
    public void severe(String template, Object...args) {
      Log.e(logTag, Formatter.format(template, args));
    }

    @Override
    public void warning(String template, Object...args) {
      Log.w(logTag, Formatter.format(template, args));
    }

    @Override
    public void info(String template, Object...args) {
      Log.i(logTag, Formatter.format(template, args));
    }

    @Override
    public void fine(String template, Object...args) {
      Log.v(logTag, Formatter.format(template, args));
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
        return android.util.Log.VERBOSE;
      } else {
        throw new RuntimeException("Unsupported level: " + level);
      }
    }
  }

  /**
   * Implementation of {@link SystemResources.Scheduler} based on {@code ThreadExecutor}.
   *
   */
  private static class ExecutorBasedScheduler implements ComponentScheduler {

    private SystemResources systemResources;

    /** Scheduler for running and scheduling tasks. */
    private final ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();

    /** Thread that belongs to the scheduler. */
    private Thread thread = null;

    private final String threadName;

    /** Creates a scheduler with thread {@code name} for internal logging, etc. */
    private ExecutorBasedScheduler(final String name) {
      threadName = name;
    }

    @Override
    public long getCurrentTimeMs() {
      return System.currentTimeMillis();
    }

    @Override
    public void schedule(final int delayMs, final Runnable runnable) {
      // For simplicity, schedule first and then check when the event runs later if the resources
      // have been shut down.
      scheduler.schedule(new Runnable() {
        @Override
        public void run() {
          if (thread != Thread.currentThread()) {
            // Either at initialization or if the thread has been killed or restarted by the
            // Executor service.
            thread = Thread.currentThread();
            thread.setName(threadName);
          }

          if (systemResources.isStarted()) {
            runnable.run();
          } else {
            systemResources.getLogger().warning("Not running on internal thread since resources " +
              "not started %s, %s", delayMs, runnable);
          }
        }
      }, delayMs, TimeUnit.MILLISECONDS);
    }

    @Override
    public boolean isRunningOnThread() {
      return (thread == null) || (Thread.currentThread() == thread);
    }

    @Override
    public void setSystemResources(SystemResources resources) {
      this.systemResources = resources;
    }
  }

  //
  // End of nested classes.
  //

  /**
   * Constructs a {@link SystemResourcesBuilder} instance using default scheduling, logging (with a
   * prefix {@code logPrefix} for log messages if the default logger is not overridden), and
   * storage, and using {@code network} to send and receive messages.
   */
  public static SystemResourcesBuilder createResourcesBuilder(String logPrefix,
      ComponentNetworkChannel network) {
    return new SystemResourcesBuilder(new AndroidLogger(logPrefix),
      new ExecutorBasedScheduler("ticl" + logPrefix),
      new ExecutorBasedScheduler("ticl-listener" + logPrefix),
      network, new MemoryStorageImpl());
  }
}
