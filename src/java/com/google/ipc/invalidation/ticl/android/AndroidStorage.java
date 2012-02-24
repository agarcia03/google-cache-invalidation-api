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

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.SystemResources.Storage;
import com.google.ipc.invalidation.external.client.types.Callback;
import com.google.ipc.invalidation.external.client.types.SimplePair;
import com.google.ipc.invalidation.external.client.types.Status;
import com.google.ipc.invalidation.util.NamedRunnable;
import com.google.protobuf.ByteString;
import com.google.protos.ipc.invalidation.AndroidState.ClientMetadata;
import com.google.protos.ipc.invalidation.AndroidState.ClientProperty;
import com.google.protos.ipc.invalidation.AndroidState.StoredState;
import com.google.protos.ipc.invalidation.ClientProtocol.Version;

import android.accounts.Account;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;

/**
 * Provides the storage and in-memory state model for Android client persistent state.   There is
 * one storage instance for each client instance that is responsible for loading, making state
 * available, and storing the persisted state.
 * <b>
 * The class is thread safe <b>after</b> the {@link #create} or {@link #load} method has been
 * called to populate it with initial state.
 *
 */
public class AndroidStorage implements Storage {

  /*
   * The current storage format is based upon a single file containing protocol buffer data.  Each
   * client instance will have a separate state file with a name based upon a client-key derived
   * convention.   The design could easily be evolved later to leverage a shared SQLite database
   * or other mechanisms without requiring any changes to the public interface.
   */

  /** The logging tag */
  private static final String TAG = "AndroidStorage";

  /** The version value that is stored within written state */
  private static final Version CURRENT_VERSION =
      Version.newBuilder().setMajorVersion(1).setMinorVersion(0).build();

  /** The name of the subdirectory in the application files store where state files are stored */
   static final String STATE_DIRECTORY = "InvalidationClient";

  /** A simple success constant */
  private static final Status SUCCESS = Status.newInstance(Status.Code.SUCCESS, "");

  /**
   * Deletes all persisted client state files stored in the state directory and then
   * the directory itself.
   */
   public static void reset(Context context) {
    File stateDir = context.getDir(STATE_DIRECTORY, Context.MODE_PRIVATE);
    for (File stateFile : stateDir.listFiles()) {
      stateFile.delete();
    }
    stateDir.delete();
  }

  /** The execution context */
   final Context context;

  /** The client key associated with this storage instance */
   final String key;

  /** the client metadata associated with the storage instance (or {@code null} if not loaded */
  private ClientMetadata metadata;

  /** Stores the client properties for a client */
  private final Map<String, byte []> properties = new ConcurrentHashMap<String, byte[]>();

  /** Executor used to schedule background reads and writes on a single shared thread */
  
  final ScheduledExecutorService scheduler = Executors.newSingleThreadScheduledExecutor();

  /**
   * Creates a new storage object for reading or writing state for the providing client key using
   * the provided execution context.
   */
  
  protected AndroidStorage(Context context, String key) {
    Preconditions.checkNotNull(context, "context");
    Preconditions.checkNotNull(key, "key");
    this.key = key;
    this.context = context;
  }

  ClientMetadata getClientMetadata() {
    return metadata;
  }

  @Override
  public void deleteKey(final String key, final Callback<Boolean> done) {
    scheduler.execute(new NamedRunnable("AndroidStorage.deleteKey") {
      @Override
      public void run() {
        properties.remove(key);
        store();
        done.accept(true);
      }
    });
  }

  @Override
  public void readAllKeys(final Callback<SimplePair<Status, String>> keyCallback) {
    scheduler.execute(new NamedRunnable("AndroidStorage.readAllKeys") {
      @Override
      public void run() {
        for (String key : properties.keySet()) {
          keyCallback.accept(SimplePair.of(SUCCESS, key));
        }
      }
    });
  }

  @Override
  public void readKey(final String key, final Callback<SimplePair<Status, byte[]>> done) {
    scheduler.execute(new NamedRunnable("AndroidStorage.readKey") {
      @Override
      public void run() {
          byte [] value = properties.get(key);
          if (value != null) {
            done.accept(SimplePair.of(SUCCESS, value));
          } else {
            Status status =
                Status.newInstance(Status.Code.PERMANENT_FAILURE, "No value in map for " + key);
            done.accept(SimplePair.of(status, (byte []) null));
          }
      }
    });
  }

  @Override
  public void writeKey(final String key, final byte[] value, final Callback<Status> done) {
    scheduler.execute(new NamedRunnable("AndroidStorage.writeKey") {
      @Override
      public void run() {
        properties.put(key, value);
        store();
        done.accept(SUCCESS);
      }
    });
  }

  @Override
  public void setSystemResources(SystemResources resources) {}

  /**
   * Returns the file where client state for this storage instance is stored.
   */
   File getStateFile() {
    File stateDir = context.getDir(STATE_DIRECTORY, Context.MODE_PRIVATE);
    return new File(stateDir, key);
  }

  /**
   * Returns the input stream that can be used to read state from the internal file storage for
   * the application.
   */
  
  protected InputStream getStateInputStream() throws FileNotFoundException {
    return new FileInputStream(getStateFile());
  }

  /**
   * Returns the output stream that can be used to write state to the internal file storage for
   * the application.
   */
  
  protected OutputStream getStateOutputStream() throws FileNotFoundException {
    return new FileOutputStream(getStateFile());
  }

  void create(int clientType, Account account, String authType,
      Intent eventIntent) {
    ComponentName component = eventIntent.getComponent();
    Preconditions.checkNotNull(component, "No component found in event intent");
    metadata = ClientMetadata.newBuilder()
        .setVersion(CURRENT_VERSION)
        .setClientKey(key)
        .setClientType(clientType)
        .setAccountName(account.name)
        .setAccountType(account.type)
        .setAuthType(authType)
        .setListenerPkg(component.getPackageName())
        .setListenerClass(component.getClassName())
        .build();
    store();
  }

  /**
   * Attempts to load any persisted client state for the stored client.
   *
   * @returns {@code true} if loaded successfully, false otherwise.
   */
  boolean load() {
    AndroidClientProxy client;
    InputStream inputStream = null;
    try {
      // Load the state from internal storage and parse it the protocol
      inputStream = getStateInputStream();
      StoredState fullState = StoredState.parseFrom(inputStream);
      metadata = fullState.getMetadata();
      if (!key.equals(metadata.getClientKey())) {
        Log.e(TAG, "Unexpected client key mismatch:" + key + "," + metadata.getClientKey());
        return false;
      }
      Log.d(TAG, "Loaded metadata:" + metadata);

      // Unpack the client properties into a map for easy lookup / iteration / update
      for (ClientProperty clientProperty : fullState.getPropertyList()) {
        Log.d(TAG, "Loaded property: " + clientProperty);
        properties.put(clientProperty.getKey(), clientProperty.getValue().toByteArray());
      }
      Log.i(TAG, "Loaded state for " + key);
      return true;
    } catch (FileNotFoundException e) {
      // No state persisted on disk
      client = null;
    } catch (IOException e) {
      // Log error regarding client state read and return null
      Log.e(TAG, "Error reading client state", e);
      client = null;
    } finally {
      if (inputStream != null) {
        try {
          inputStream.close();
        } catch (IOException e) {
          Log.e(TAG, "Unable to close state file", e);
        }
      }
    }
    return false;
  }

  /**
   * Deletes all state associated with the storage instance.
   */
  void delete() {
    File stateFile = getStateFile();
    if (stateFile.exists()) {
      stateFile.delete();
      Log.i(TAG, "Deleted state for " + key + " from " + stateFile.getName());
    }
  }

  /**
   * Store the current state into the persistent storage.
   */
  private void store() {
    StoredState.Builder stateBuilder =
        StoredState.newBuilder()
            .mergeMetadata(metadata);
    for (Map.Entry<String, byte []> entry : properties.entrySet()) {
      stateBuilder.addProperty(
          ClientProperty.newBuilder()
              .setKey(entry.getKey())
              .setValue(ByteString.copyFrom(entry.getValue()))
              .build());
    }
    StoredState state = stateBuilder.build();
    OutputStream outputStream = null;
    try {
      outputStream = getStateOutputStream();
      state.writeTo(outputStream);
      Log.i(TAG, "State written for " + key);
    } catch (FileNotFoundException e) {
      // This should not happen when opening to create / replace
      Log.e(TAG, "Unable to open state file", e);
    } catch (IOException e) {
      Log.e(TAG, "Error writing state", e);
    } finally {
      if (outputStream != null) {
        try {
          outputStream.close();
        } catch (IOException e) {
          Log.w(TAG, "Unable to close state file", e);
        }
      }
    }
  }
}
