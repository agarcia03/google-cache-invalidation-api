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

package com.google.ipc.invalidation.ticl;

import com.google.ipc.invalidation.common.CommonProtoStrings2;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.SystemResources.ComponentStorage;
import com.google.ipc.invalidation.external.client.SystemResources.Scheduler;
import com.google.ipc.invalidation.external.client.types.Callback;
import com.google.ipc.invalidation.external.client.types.SimplePair;
import com.google.ipc.invalidation.external.client.types.Status;
import com.google.ipc.invalidation.util.Bytes;
import com.google.ipc.invalidation.util.TypedUtil;

import java.util.HashMap;
import java.util.Map;

/**
 * Map-based in-memory implementation of {@link ComponentStorage}.
 *
 */
public class MemoryStorageImpl implements ComponentStorage {
  private SystemResources systemResources;
  private Map<Bytes, byte[]> ticlPersistentState = new HashMap<Bytes, byte[]>();

  @Override
  public void writeKey(final byte[] key, final byte[] value, final Callback<Status> callback) {
    // Need to schedule immediately because C++ locks aren't reentrant, and
    // C++ locking code assumes that this call will not return directly.

    // Schedule the write even if the resources are started since the
    // scheduler will prevent it from running in case the resources have been
    // stopped.
    systemResources.getInternalScheduler().schedule(Scheduler.NO_DELAY, new Runnable() {
      @Override
      public void run() {
        ticlPersistentState.put(new Bytes(key), value);
        callback.accept(Status.newInstance(Status.Code.SUCCESS, ""));
      }
    });
  }

  int numKeysForTest() {
    return ticlPersistentState.size();
  }

  @Override
  public void setSystemResources(SystemResources resources) {
    this.systemResources = resources;
  }

  @Override
  public void readKey(final byte[] key, final Callback<SimplePair<Status, byte[]>> done) {
    systemResources.getInternalScheduler().schedule(Scheduler.NO_DELAY, new Runnable() {
      @Override
      public void run() {
        byte[] value = TypedUtil.mapGet(ticlPersistentState, new Bytes(key));
        final SimplePair<Status, byte[]> result;
        if (value != null) {
          result = SimplePair.of(Status.newInstance(Status.Code.SUCCESS, ""), value);
        } else {
          String error = "No value present in map for " +
              CommonProtoStrings2.toLazyCompactString(key);
          result = SimplePair.of(Status.newInstance(Status.Code.PERMANENT_FAILURE, error), null);
        }
        done.accept(result);
      }
    });
  }

  @Override
  public void deleteKey(final byte[] key, final Callback<Boolean> done) {
    systemResources.getInternalScheduler().schedule(Scheduler.NO_DELAY, new Runnable() {
      @Override
      public void run() {
        TypedUtil.remove(ticlPersistentState, new Bytes(key));
        done.accept(true);
      }
    });
  }

  @Override
  public void readAllKeys(final Callback<SimplePair<Status, byte[]>> done) {
    systemResources.getInternalScheduler().schedule(Scheduler.NO_DELAY, new Runnable() {
      @Override
      public void run() {
        Status successStatus = Status.newInstance(Status.Code.SUCCESS, "");
        for (Bytes key : ticlPersistentState.keySet()) {
          done.accept(SimplePair.of(successStatus, key.getByteArray()));
        }
      }
    });
  }

  /** Copies the storage from {@code storage} into this. */
  void copyForTest(MemoryStorageImpl storage) {
    ticlPersistentState.putAll(storage.ticlPersistentState);
  }

  /**
   * Same as write except without any callbacks and is NOT done on the internal thread.
   * Test code should typically call this before starting the client.
   */
  void writeForTest(final byte[] key, final byte[] value) {
    ticlPersistentState.put(new Bytes(key), value);
  }
}
