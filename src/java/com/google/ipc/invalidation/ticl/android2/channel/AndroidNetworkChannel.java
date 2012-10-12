package com.google.ipc.invalidation.ticl.android2.channel;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.types.Callback;
import com.google.ipc.invalidation.ticl.TestableNetworkChannel;
import com.google.ipc.invalidation.ticl.android2.ProtocolIntents;
import com.google.ipc.invalidation.ticl.android2.ResourcesFactory.AndroidResources;
import com.google.protos.ipc.invalidation.Channel.NetworkEndpointId;

import android.content.Context;
import android.content.Intent;

/**
 * A network channel for Android that receives messages by GCM and that sends messages
 * using HTTP.
 *
 */
public class AndroidNetworkChannel implements TestableNetworkChannel {
  private final Context context;
  private AndroidResources resources;

  public AndroidNetworkChannel(Context context) {
    this.context = Preconditions.checkNotNull(context);
  }

  @Override
  public void sendMessage(byte[] outgoingMessage) {
    Intent intent = ProtocolIntents.newOutboundMessageIntent(outgoingMessage);
    intent.setClassName(context, AndroidMessageSenderService.class.getName());
    context.startService(intent);
  }

  @Override
  public void setMessageReceiver(Callback<byte[]> incomingReceiver) {
    resources.setNetworkMessageReceiver(incomingReceiver);
  }

  @Override
  public void addNetworkStatusReceiver(Callback<Boolean> networkStatusReceiver) {
    resources.setNetworkStatusReceiver(networkStatusReceiver);
  }

  @Override
  public void setSystemResources(SystemResources resources) {
    this.resources = (AndroidResources) Preconditions.checkNotNull(resources);
  }

  @Override
  public NetworkEndpointId getNetworkIdForTest() {
    return AndroidMessageSenderService.getNetworkEndpointId(context, resources.getLogger());
  }
}
