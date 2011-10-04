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

import com.google.ipc.invalidation.common.DigestFunction;
import com.google.ipc.invalidation.common.ObjectIdDigestUtils;
import com.google.ipc.invalidation.util.Bytes;
import com.google.ipc.invalidation.util.InternalBase;
import com.google.protos.ipc.invalidation.ClientProtocol.ObjectIdP;

import java.util.ArrayList;
import java.util.Collection;
import java.util.SortedMap;
import java.util.TreeMap;

/**
 * Simple, map-based implementation of {@link DigestStore}.
 *
 */
class SimpleRegistrationStore extends InternalBase implements DigestStore<ObjectIdP> {

  /** All the registrations in the store mapped from the digest to the Object Id. */
  private final SortedMap<Bytes, ObjectIdP> registrations = new TreeMap<Bytes, ObjectIdP>();

  /** The function used to compute digests of objects. */
  private final DigestFunction digestFunction;

  /** The memoized digest of all objects in registrations. */
  private Bytes digest;

  SimpleRegistrationStore(DigestFunction digestFunction) {
    this.digestFunction = digestFunction;
    recomputeDigest();
  }

  @Override
  public void add(ObjectIdP oid) {
    registrations.put(ObjectIdDigestUtils.getDigest(oid, digestFunction), oid);
    recomputeDigest();
  }

  @Override
  public void add(Collection<ObjectIdP> oids) {
    for (ObjectIdP oid : oids) {
      registrations.put(ObjectIdDigestUtils.getDigest(oid, digestFunction), oid);
    }
    recomputeDigest();
  }

  @Override
  public void remove(ObjectIdP oid) {
    registrations.remove(ObjectIdDigestUtils.getDigest(oid, digestFunction));
    recomputeDigest();
  }

  @Override
  public void remove(Collection<ObjectIdP> oids) {
    for (ObjectIdP oid : oids) {
      registrations.remove(ObjectIdDigestUtils.getDigest(oid, digestFunction));
    }
    recomputeDigest();
  }

  @Override
  public Collection<ObjectIdP> removeAll() {
    Collection<ObjectIdP> result = new ArrayList<ObjectIdP>(registrations.values());
    registrations.clear();
    recomputeDigest();
    return result;
  }

  @Override
  public boolean contains(ObjectIdP oid) {
    return registrations.containsKey(ObjectIdDigestUtils.getDigest(oid, digestFunction));
  }

  @Override
  public int size() {
    return registrations.size();
  }

  @Override
  public byte[] getDigest() {
    return digest.getByteArray();
  }

  @Override
  public Collection<ObjectIdP> getElements(byte[] oidDigestPrefix, int prefixLen) {
    // We always return all the registrations and let the Ticl sort it out.
    return registrations.values();
  }

  /** Recomputes the digests over all objects and sets {@code this.digest}. */
  private void recomputeDigest() {
    this.digest = ObjectIdDigestUtils.getDigest(registrations.keySet(), digestFunction);
  }
}
