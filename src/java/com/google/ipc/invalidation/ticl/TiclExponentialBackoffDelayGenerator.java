package com.google.ipc.invalidation.ticl;

import com.google.ipc.invalidation.util.ExponentialBackoffDelayGenerator;
import com.google.ipc.invalidation.util.Marshallable;
import com.google.protos.ipc.invalidation.Client.ExponentialBackoffState;

import java.util.Random;

/**
 * A subclass of {@link ExponentialBackoffDelayGenerator} that supports (un)marshalling to and from
 * protocol buffers.
 *
 */
public class TiclExponentialBackoffDelayGenerator
    extends ExponentialBackoffDelayGenerator implements Marshallable<ExponentialBackoffState> {

  /**
   * Creates an exponential backoff delay generator. Parameters  are as in
   * {@link ExponentialBackoffDelayGenerator#ExponentialBackoffDelayGenerator(Random, int, int)}.
   */
  public TiclExponentialBackoffDelayGenerator(Random random, int initialMaxDelay,
      int maxExponentialFactor) {
    super(random, initialMaxDelay, maxExponentialFactor);
  }

  /**
   * Restores a generator from {@code marshalledState}. Other parameters are as in
   * {@link ExponentialBackoffDelayGenerator#ExponentialBackoffDelayGenerator(Random, int, int)}.
   *
   * @param marshalledState marshalled state from which to restore.
   */
  public TiclExponentialBackoffDelayGenerator(Random random, int initialMaxDelay,
      int maxExponentialFactor, ExponentialBackoffState marshalledState) {
    super(random, initialMaxDelay, maxExponentialFactor, marshalledState.getCurrentMaxDelay(),
        marshalledState.getInRetryMode());
  }


  @Override
  public ExponentialBackoffState marshal() {
    return ExponentialBackoffState.newBuilder()
      .setCurrentMaxDelay(getCurrentMaxDelay())
      .setInRetryMode(getInRetryMode())
      .build();
  }
}
