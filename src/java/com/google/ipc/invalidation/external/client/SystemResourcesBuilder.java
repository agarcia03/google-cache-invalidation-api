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

package com.google.ipc.invalidation.external.client;

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.external.client.SystemResources.ComponentLogger;
import com.google.ipc.invalidation.external.client.SystemResources.ComponentNetworkChannel;
import com.google.ipc.invalidation.external.client.SystemResources.ComponentScheduler;
import com.google.ipc.invalidation.external.client.SystemResources.ComponentStorage;
import com.google.ipc.invalidation.ticl.BasicSystemResources;

/**
 * A builder to override some or all resource components in {@code SystemResources} . See
 * discussion in {@code ResourceComponent} as well.
 *
 */

  // The resources used for constructing the SystemResources in builder.
public class SystemResourcesBuilder {
  private ComponentScheduler internalScheduler;
  private ComponentScheduler listenerScheduler;
  private ComponentLogger logger;
  private ComponentNetworkChannel network;
  private ComponentStorage storage;

  /** If the build method has been called on this builder. */
  private boolean sealed;

  /** See specs at {@code InvalidationClientFactory.createDefaultResourcesBuilder}. */
  public SystemResourcesBuilder(ComponentLogger logger, ComponentScheduler internalScheduler,
      ComponentScheduler listenerScheduler, ComponentNetworkChannel network,
      ComponentStorage storage) {
    this.logger = logger;
    this.internalScheduler = internalScheduler;
    this.listenerScheduler = listenerScheduler;
    this.network = network;
    this.storage = storage;
  }

  /** Returns a new builder that shares all the resources of {@code builder} but is not sealed. */
  public SystemResourcesBuilder(SystemResourcesBuilder builder) {
    this.logger = builder.logger;
    this.internalScheduler = builder.internalScheduler;
    this.listenerScheduler = builder.listenerScheduler;
    this.network = builder.network;
    this.storage = builder.storage;
    this.sealed = false;
  }

  /** Returns the internal scheduler. */
  public ComponentScheduler getInternalScheduler() {
    return internalScheduler;
  }

  /** Returns the listener scheduler. */
  public ComponentScheduler getListenerScheduler() {
    return listenerScheduler;
  }

  /** Returns the network channel. */
  public ComponentNetworkChannel getNetwork() {
    return network;
  }

  /** Returns the logger. */
  public ComponentLogger getLogger() {
    return logger;
  }

  /** Returns the storage. */
  public ComponentStorage getStorage() {
    return storage;
  }

  /**
   * Sets the scheduler for scheduling internal events to be {@code internalScheduler}.
   * <p>
   * REQUIRES: {@link #build} has not been called.
   */
  public SystemResourcesBuilder  setInternalScheduler(ComponentScheduler internalScheduler) {
    Preconditions.checkState(!sealed, "Builder's build method has already been called");
    this.internalScheduler = internalScheduler;
    return this;
  }

  /**
   * Sets the scheduler for scheduling listener events to be {@code listenerScheduler}.
   * <p>
   * REQUIRES: {@link #build} has not been called.
   */
  public SystemResourcesBuilder setListenerScheduler(ComponentScheduler listenerScheduler) {
    Preconditions.checkState(!sealed, "Builder's build method has already been called");
    this.listenerScheduler = listenerScheduler;
    return this;
  }

  /**
   * Sets the logger to be {@code logger}.
   * <p>
   * REQUIRES: {@link #build} has not been called.
   */
  public SystemResourcesBuilder setLogger(ComponentLogger logger) {
    Preconditions.checkState(!sealed, "Builder's build method has already been called");
    this.logger = logger;
    return this;
  }

  /**
   * Sets the network channel for communicating with the server to be {@code network}.
   * <p>
   * REQUIRES: {@link #build} has not been called.
   */
  public SystemResourcesBuilder setNetwork(ComponentNetworkChannel network) {
    Preconditions.checkState(!sealed, "Builder's build method has already been called");
    this.network = network;
    return this;
  }

  /**
   * Sets the persistence layer to be {@code storage}.
   * <p>
   * REQUIRES: {@link #build} has not been called.
   */
  public SystemResourcesBuilder setStorage(ComponentStorage storage) {
    Preconditions.checkState(!sealed, "Builder's build method has already been called");
    this.storage = storage;
    return this;
  }

  /**
   * Builds the {@code SystemResources} object with the given resource components and returns it.
   * <p>
   * Caller must not call any mutation method (on this SystemResourcesBuilder) after
   * {@code build} has been called (i.e., build and the set* methods)
   */
  public SystemResources build() {
    Preconditions.checkState(!sealed, "Builder's build method has already been called");
    seal();
    return new BasicSystemResources(logger, internalScheduler, listenerScheduler, network, storage);
  }

  /** Seals the builder so that no mutation method can be called on this. */
  protected void seal() {
    Preconditions.checkState(!sealed, "Builder's already sealed");
    sealed = true;
  }
}
