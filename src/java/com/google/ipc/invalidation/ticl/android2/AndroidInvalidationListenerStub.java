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

package com.google.ipc.invalidation.ticl.android2;

import com.google.ipc.invalidation.external.client.InvalidationClient;
import com.google.ipc.invalidation.external.client.InvalidationListener;
import com.google.ipc.invalidation.external.client.InvalidationListener.RegistrationState;
import com.google.ipc.invalidation.external.client.android.service.AndroidLogger;
import com.google.ipc.invalidation.external.client.types.AckHandle;
import com.google.ipc.invalidation.external.client.types.ErrorInfo;
import com.google.ipc.invalidation.ticl.ProtoConverter;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protos.ipc.invalidation.AndroidService.ListenerUpcall;
import com.google.protos.ipc.invalidation.AndroidService.ListenerUpcall.ErrorUpcall;
import com.google.protos.ipc.invalidation.AndroidService.ListenerUpcall.InvalidateUpcall;
import com.google.protos.ipc.invalidation.AndroidService.ListenerUpcall.RegistrationFailureUpcall;
import com.google.protos.ipc.invalidation.AndroidService.ListenerUpcall.RegistrationStatusUpcall;
import com.google.protos.ipc.invalidation.AndroidService.ListenerUpcall.ReissueRegistrationsUpcall;

import android.app.IntentService;
import android.content.Intent;

import java.util.Arrays;


/**
 * Class implementing the {@link InvalidationListener} in the application using the  client.
 * This class is configured with the name of the application class implementing the
 * {@link InvalidationListener} for the application. It receives upcalls from the Ticl as
 * {@link Intent}s and dispatches them against dynamically created instances of the provided
 * class. In this way, it serves as a bridge between the intent protocol and the application.
 */
public class AndroidInvalidationListenerStub extends IntentService {
  /* This class needs to be public so that the Android runtime can start it as a service. */

  /** {@link InvalidationClient} that will be provided to listener upcalls. */
  private InvalidationClient client;

  /** Class against instances of which listener calls will be made. */
  private Class<? extends InvalidationListener> listenerClass;

  private final AndroidLogger logger = AndroidLogger.forPrefix("");

  private final ProtocolValidator validator = new ProtocolValidator(logger);

  public AndroidInvalidationListenerStub() {
    super("AndroidInvalidationListener");
  }

  @SuppressWarnings("unchecked")
  @Override
  public void onCreate() {
    super.onCreate();
    try {
      // Find the listener class that the application wants to use to receive upcalls.
      this.listenerClass = (Class<? extends InvalidationListener>)
          Class.forName(new AndroidTiclManifest(this).getListenerClass());
    } catch (ClassNotFoundException exception) {
      throw new RuntimeException("Invalid listener class", exception);
    }
    // Create a stub back to the Ticl service; we will provide this as the client parameter in
    // listener upcalls.
    this.client = new AndroidInvalidationClientStub(this, logger);
  }

  /**
   * Handles a listener upcall by decoding the protocol buffer in {@code intent} and dispatching
   * to the appropriate method on an instance of {@link #listenerClass}.
   */
  @Override
  protected void onHandleIntent(Intent intent) {
    // TODO: use wakelocks

    // Create an instance of the application listener class to handle the upcall.
    InvalidationListener listener;
    try {
      listener = listenerClass.newInstance();
    } catch (InstantiationException exception) {
      throw new RuntimeException("Could not create listener", exception);
    } catch (IllegalAccessException exception) {
      throw new RuntimeException("Could not create listener", exception);
    }
    // Unmarshall the arguments from the Intent and make the appropriate call on the listener.
    ListenerUpcall upcall = tryParseIntent(intent);
    if (upcall == null) {
      return;
    }

    if (upcall.hasReady()) {
      listener.ready(client);
    } else if (upcall.hasInvalidate()) {
      // Handle all invalidation-related upcalls on a common path, since they require creating
      // an AckHandleP.
      onInvalidateUpcall(upcall, listener);
    } else if (upcall.hasRegistrationStatus()) {
      RegistrationStatusUpcall regStatus = upcall.getRegistrationStatus();
      listener.informRegistrationStatus(client,
          ProtoConverter.convertFromObjectIdProto(regStatus.getObjectId()),
          regStatus.getIsRegistered() ?
              RegistrationState.REGISTERED : RegistrationState.UNREGISTERED);
    } else if (upcall.hasRegistrationFailure()) {
      RegistrationFailureUpcall failure = upcall.getRegistrationFailure();
      listener.informRegistrationFailure(client,
          ProtoConverter.convertFromObjectIdProto(failure.getObjectId()),
          failure.getTransient(),
          failure.getMessage());
    } else if (upcall.hasReissueRegistrations()) {
      ReissueRegistrationsUpcall reissueRegs = upcall.getReissueRegistrations();
      listener.reissueRegistrations(client, reissueRegs.getPrefix().toByteArray(),
          reissueRegs.getLength());
    } else if (upcall.hasError()) {
      ErrorUpcall error = upcall.getError();
      ErrorInfo errorInfo = ErrorInfo.newInstance(error.getErrorCode(), error.getIsTransient(),
          error.getErrorMessage(), null);
      listener.informError(client, errorInfo);
    } else {
      logger.warning("Dropping listener Intent with unknown call: %s", upcall);
    }
  }

  /**
   * Handles an invalidation-related listener {@code upcall} by dispatching to the appropriate
   * method on an instance of {@link #listenerClass}.
   */
  private void onInvalidateUpcall(ListenerUpcall upcall, InvalidationListener listener) {
    InvalidateUpcall invalidate = upcall.getInvalidate();
    AckHandle ackHandle = AckHandle.newInstance(invalidate.getAckHandle().toByteArray());
    if (invalidate.hasInvalidation()) {
      listener.invalidate(client,
          ProtoConverter.convertFromInvalidationProto(invalidate.getInvalidation()),
          ackHandle);
    } else if (invalidate.hasInvalidateAll()) {
      listener.invalidateAll(client, ackHandle);
    } else if (invalidate.hasInvalidateUnknown()) {
      listener.invalidateUnknownVersion(client,
          ProtoConverter.convertFromObjectIdProto(invalidate.getInvalidateUnknown()), ackHandle);
    } else {
      throw new RuntimeException("Invalid invalidate upcall: " + invalidate);
    }
  }

  /**
   * Returns a valid {@link ListenerUpcall} from {@code intent}, or {@code null} if one
   * could not be parsed.
   */
  private ListenerUpcall tryParseIntent(Intent intent) {
    if (intent == null) {
      return null;
    }
    byte[] upcallBytes = intent.getByteArrayExtra(ProtocolIntents.LISTENER_UPCALL_KEY);
    try {
      ListenerUpcall upcall = ListenerUpcall.parseFrom(upcallBytes);
      if (!validator.isListenerUpcallValid(upcall)) {
        logger.warning("Ignoring invalid listener upcall: %s", upcall);
        return null;
      }
      return upcall;
    } catch (InvalidProtocolBufferException exception) {
      logger.severe("Could not parse listener upcall from %s", Arrays.toString(upcallBytes));
      return null;
    }
  }
}
