package com.google.ipc.invalidation.external.client.android2;

import com.google.ipc.invalidation.ticl.InvalidationClientCore;
import com.google.ipc.invalidation.ticl.android2.AndroidTiclManifest;
import com.google.ipc.invalidation.ticl.android2.ProtocolIntents;
import com.google.protos.ipc.invalidation.ClientProtocol.ClientConfigP;
import com.google.protos.ipc.invalidation.Types.ClientType;

import android.content.Context;
import android.content.Intent;

/**
 * Factory for creating  Android clients.
 *
 */
public final class AndroidClientFactory {
  /**
   * Creates a new client.
   * <p>
   * REQUIRES: no client exist, or a client exists with the same type and name as provided. In
   * the latter case, this call is a no-op.
   *
   * @param context Android system context
   * @param clientType type of the client to create
   * @param clientName name of the client to create
   */
  public static void createClient(Context context, ClientType.Type clientType, byte[] clientName) {
    ClientConfigP config = InvalidationClientCore.createConfig().build();
    Intent intent = ProtocolIntents.InternalDowncalls.newCreateClientIntent(
        clientType.getNumber(), clientName, config, false);
    intent.setClassName(context, new AndroidTiclManifest(context).getTiclServiceClass());
    context.startService(intent);
  }

  private AndroidClientFactory() {
    // Disallow instantiation.
  }
}
