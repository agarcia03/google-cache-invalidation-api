package com.google.ipc.invalidation.ticl.android2.channel;

import com.google.android.gcm.GCMRegistrar;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.external.client.android.service.AndroidLogger;
import com.google.ipc.invalidation.external.client.contrib.MultiplexingGcmListener;
import com.google.ipc.invalidation.ticl.android2.AndroidTiclManifest;
import com.google.ipc.invalidation.ticl.android2.ProtocolIntents;
import com.google.ipc.invalidation.ticl.android2.channel.AndroidChannelConstants.C2dmConstants;
import com.google.protobuf.InvalidProtocolBufferException;
import com.google.protos.ipc.invalidation.AndroidChannel.AddressedAndroidMessage;

import android.content.Context;
import android.content.Intent;
import android.util.Base64;


/**
 * Service that receives messages from  using GCM.
 *
 */
public class AndroidMessageReceiverService extends MultiplexingGcmListener.AbstractListener {
  /*
   * This class is public so that it can be instantiated by the Android runtime. All of the
   * {@code onYYY} methods are called holding a wakelock that will be automatically released when
   * they return, since this is a subclass of {@code AbstractListener}.
   */

  /**
   * Receiver for broadcasts by the multiplexed GCM service. It forwards them to
   * AndroidMessageReceiverService.
   */
  public static class Receiver extends MultiplexingGcmListener.AbstractListener.Receiver {
    /* This class is public so that it can be instantiated by the Android runtime. */
    @Override
    protected Class<?> getServiceClass() {
      return AndroidMessageReceiverService.class;
    }
  }

  private final Logger logger = AndroidLogger.forTag("MsgRcvrSvc");

  public AndroidMessageReceiverService() {
    super("AndroidMessageReceiverService");
  }

  @Override
  protected void onMessage(Intent intent) {
    // Forward the message to the Ticl service.
    if (intent.hasExtra(C2dmConstants.CONTENT_PARAM)) {
      String content = intent.getStringExtra(C2dmConstants.CONTENT_PARAM);
      byte[] msgBytes = Base64.decode(content, Base64.URL_SAFE);
      try {
        // Look up the name of the Ticl service class from the manifest.
        String serviceClass = new AndroidTiclManifest(this).getTiclServiceClass();
        AddressedAndroidMessage addrMessage = AddressedAndroidMessage.parseFrom(msgBytes);
        Intent msgIntent =
            ProtocolIntents.InternalDowncalls.newServerMessageIntent(addrMessage.getMessage());
        msgIntent.setClassName(this, serviceClass);
        startService(msgIntent);
      } catch (InvalidProtocolBufferException exception) {
        logger.warning("Failed parsing inbound message: %s", exception);
      }
    } else {
      logger.fine("GCM Intent has no message content: %s", intent);
    }

    // Store the echo token.
    String echoToken = intent.getStringExtra(C2dmConstants.ECHO_PARAM);
    if (echoToken != null) {
      AndroidChannelPreferences.setEchoToken(this, echoToken);
    }
  }


  @Override
  protected void onRegistered(String registrationId) {
    // TODO:
    // registration id and sending them when one becomes available. Alternatively, consider
    // using an alarm to try resending after a short interval, to avoid having to store the
    // message. Regardless, we should see whether this actually happens in practice first.

    // TODO:
    // that  can update its stored network endpoint id for this client.
  }

  @Override
  protected void onUnregistered(String registrationId) {
    // Nothing to do.
  }

  @Override
  protected void onDeletedMessages(int total) {
    // This method must be implemented if we start using non-collapsable messages with GCM. For
    // now, there is nothing to do.
  }

  /**
   * Initializes GCM as a convenience method for tests. In production, applications should handle
   * this.
   */
  public static void initializeGcmForTest(Context context, Logger logger, String senderId) {
    // Initialize GCM.
    GCMRegistrar.checkDevice(context);
    GCMRegistrar.checkManifest(context);
    String regId = GCMRegistrar.getRegistrationId(context);
    if (regId.equals("")) {
      logger.info("Not registered with GCM; registering");
      GCMRegistrar.register(context, senderId);
    } else {
      logger.fine("Already registered with GCM: %s", regId);
    }
  }
}
