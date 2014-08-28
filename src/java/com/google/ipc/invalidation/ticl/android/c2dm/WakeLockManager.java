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


import android.content.Context;

/**
 * Forwarding placeholder for {@link com.google.ipc.invalidation.ticl.android2.WakeLockManager} in
 * its original location. This class can be removed after all dependencies have been removed.
 */
@Deprecated
public class WakeLockManager {
  private final com.google.ipc.invalidation.ticl.android2.WakeLockManager delegate;

  private WakeLockManager(com.google.ipc.invalidation.ticl.android2.WakeLockManager delegate) {
    this.delegate = delegate;
  }

  /** Returns the wake lock manager. */
  public static WakeLockManager getInstance(Context context) {
    return new WakeLockManager(
        com.google.ipc.invalidation.ticl.android2.WakeLockManager.getInstance(context));
  }

  /**
   * Acquires a wake lock identified by the {@code key} that will be automatically released after at
   * most {@code timeoutMs}.
   */
  public void acquire(Object key, int timeoutMs) {
    delegate.acquire(key, timeoutMs);
  }

  /**
   * Releases the wake lock identified by the {@code key} if it is currently held.
   */
  public void release(Object key) {
    delegate.release(key);
  }

  /**
   * Returns whether there is currently a wake lock held for the provided {@code key}.
   */
  public boolean isHeld(Object key) {
    return delegate.isHeld(key);
  }

  /** Returns whether the manager has any active (held) wake locks. */
  
  public boolean hasWakeLocks() {
    return delegate.hasWakeLocks();
  }

  /** Discards (without releasing) all wake locks. */
  
  public void resetForTest() {
    delegate.resetForTest();
  }
}
