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

import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.common.DigestFunction;
import com.google.ipc.invalidation.external.client.SystemResources.Logger;
import com.google.ipc.invalidation.util.Bytes;
import com.google.ipc.invalidation.util.InternalBase;
import com.google.ipc.invalidation.util.TextBuilder;
import com.google.ipc.invalidation.util.TypedUtil;
import com.google.protobuf.Message;
import com.google.protobuf.TextFormat;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.List;
import java.util.Map;
import java.util.SortedMap;
import java.util.TreeMap;

/**
 * Merkle-trie-based implementation of {@link DigestStore}. The trie is indexed on 0 and 1
 * only and it is binary tree as well. Hence, we use trie and tree interchangeably.
 *
 * @param <LeafType> type of data in Merkle trie leaves
 */
class MerkleTrie<LeafType> extends InternalBase implements DigestStore<LeafType> {

  /**
   * An abstraction that given a leaf object in a Merkle trie returns the corresponding digest in
   * bytes.
   *
   * @param <LeafType>
   */
  interface LeafDigester<LeafType> {
    /** Returns the digest corresponding to {@code leaf}. */
    byte[] getDigest(DigestFunction digestFunction, LeafType leaf);
  }

  /**
   * An abstraction to hold all the leaf objects at the bottom of a Merke trie node.
   * That is, for every leaf node in the Merkle trie, there is on bucket with the
   * actual leaf nodes.
   */
  class Bucket extends InternalBase {

    /**
     * Leaves in the bucket indexed on the leaf digest so that computation of the
     * complete digest does not require recomputing the digests for each leaf again.
     */
    private final SortedMap<Bytes, LeafType> leafDigestMap = new TreeMap<Bytes, LeafType>();

    /** Returns the digest for all the leaves in this bucket. */
    public byte[] getDigest() {
      digestFunction.reset();
      for (Bytes leafDigest : leafDigestMap.keySet()) {
        digestFunction.update(leafDigest.getByteArray());
      }
      return digestFunction.getDigest();
    }

    /** Returns the digests for all leaves in this bucket. */
    public List<Bytes> getLeafDigestsForTest() {
      return new ArrayList<Bytes>(leafDigestMap.keySet());
    }

    /** Returns all leaves in this bucket. */
    public List<LeafType> getLeafValues() {
      return new ArrayList<LeafType>(leafDigestMap.values());
    }

    /** Returns the number of leaves in this bucket. */
    public int numLeaves() {
      return leafDigestMap.size();
    }

    /**
     * Adds the {@code leaf} and its corresponding {@code digest} in this bucket and returns true
     * iff the element did not exist.
     */
    public boolean add(Bytes digest, LeafType leaf) {
      return leafDigestMap.put(digest, leaf) == null;
    }

    /**
     * Removes the {@code leaf} and its corresponding digest in this bucket and returns true iff the
     * element did exist.
     */
    public boolean remove(Bytes digest) {
      return TypedUtil.remove(leafDigestMap, digest) != null;
    }

    /** Returns true iff the bucket contains {@code digest}. */
    public boolean contains(Bytes digest) {
      return TypedUtil.containsKey(leafDigestMap, digest);
    }

    @Override
    public void toCompactString(TextBuilder builder) {
      builder.appendFormat("(%d): ", leafDigestMap.size());
      for (Map.Entry<Bytes, LeafType> entry : leafDigestMap.entrySet()) {
        builder.appendFormat("[%s, %s], ", entry.getKey(), leafToString(entry.getValue()));
      }
    }
  }

  /*
   * Implementation notes: The Merkle trie that we have built is a binary complete tree, i.e., all
   * nodes in the binary tree exist at every level including the leaves. Thus, if there are 2 levels
   * in the tree, the tree contains 7 nodes (1, 2, 4 at each level. We store this tree in an
   * array such that the ith node's children are at 2i+1 and 2i+2.
   */

  /** The number of levels in this tree. A tree with one node only has levels == 0. */
  private final int levels;

  /**
   * An array that stores the complete tree. Children of i at 2i+1 and 2i+2. Size of this
   * array is always 2^(levels+1) - 1.
   */
  private final byte[][] digests;

  /** The number of buckets at the bottom of the tree. The size is always 2^levels. */
  private final List<Bucket> buckets;

  /** The digest function used to compute the registration digests. */
  private final DigestFunction digestFunction;

  /** The digester that can compute the digests for each leaf. */
  private final LeafDigester<LeafType> leafDigester;

  private final Logger logger;

  /**
   * Constructs a MerkleTrie that contains {@code levels} number of levels where a tree with one
   * node has {@code levels} == 0. The trie is populated with an initial set of {@code leaves} with
   * {@code digestFunction} used for computing digests and {@code leafDigester} being the function
   * for computing digests from a given leaf type.
   */
  public MerkleTrie(Logger logger, int levels, Collection<LeafType> leaves,
        DigestFunction digestFunction, LeafDigester<LeafType> leafDigester) {
    Preconditions.checkState(levels >= 0);
    this.levels = levels;
    this.logger = logger;
    this.digestFunction = digestFunction;
    this.leafDigester = leafDigester;

    // Create the buckets. If levels == 0, i.e., only the root is created and no bits  are used.
    // In that case, digests = 2^(0+1) - 1 = 1. buckets = 2^0 = 1
    // If levels = 1, digests = 3; buckets = 2.
    int numBuckets = 1 << levels;
    buckets = new ArrayList<Bucket>(numBuckets);
    digests = new byte[2 * numBuckets - 1][];
    initializeLeavesAndBuckets(leaves);
  }

  /**
   * Given an empty {@code this.leaves} and {@code this.buckets}, initializes them using the given
   * set of {@code leaves}.
   */
  private void initializeLeavesAndBuckets(Collection<LeafType> leaves) {
    int numBuckets = 1 << levels;
    for (int i = 0; i < numBuckets; i++) {
      buckets.add(new Bucket());
    }

    // For each leaf, add to appropriate bucket.
    for (LeafType leaf : leaves) {
      // Consider the first "levels" bits and interpret it as a number.
      // if levels = 0, it is the degenerate case with only one bucket 0.
      byte[] leafDigest = getDigest(leaf);
      int bucketNumber = getBucketNumber(leafDigest, levels);
      Bucket bucket = buckets.get(bucketNumber);
      bucket.add(new Bytes(leafDigest), leaf);
    }

    // First compute the bucket digests and then proceed up.
    // Levels = 3.
    //              0
    //       1             2
    //    3     4      5       6
    //  7  8   9 10  11 12   13 14   --> Level 3 is the bucket level. There are 8 buckets
    // So at level-1 (i.e., 2), we just run through the buckets and pick two at a time.
    // In this case, digests' size is 15. So we start at index 7.

    for (int i = 0; i < numBuckets; i++) {
      digests[digestIndexForBucket(i)] = buckets.get(i).getDigest();
    }

    // Now compute the digest tree going level by level up.
    // In the above case : 15/2 - 1 = 6
    for (int i = (digests.length / 2) - 1; i >= 0; i--) {
      digests[i] = getDigest(digests[leftChild(i)], digests[rightChild(i)]);
    }
  }

  @Override
  public int size() {
    int result = 0;
    for (Bucket bucket : buckets) {
      result += bucket.numLeaves();
    }
    return result;
  }

  @Override
  public boolean contains(LeafType element) {
    byte[] elementDigest = getDigest(element);
    int bucketNumber = getBucketNumber(elementDigest, levels);
    Bucket bucket = buckets.get(bucketNumber);
    return bucket.contains(new Bytes(elementDigest));
  }

  @Override
  public byte[] getDigest() {
    return digests[0];
  }

  @Override
  public Collection<LeafType> getElements(byte[] digestPrefix, int prefixLen) {
    List<LeafType> result = new ArrayList<LeafType>(size());
    for (Bucket bucket : buckets) {
      result.addAll(bucket.getLeafValues());
    }
    return result;
  }

  public List<Bucket> getBucketsForTest() {
    return buckets;
  }

  
  public byte[][] getDigestListForTest() {
    return digests;
  }

  @Override
  public void add(LeafType element) {
    byte[] elementDigest = getDigest(element);
    int bucketNumber = getBucketNumber(elementDigest, levels);
    if (!buckets.get(bucketNumber).add(new Bytes(elementDigest), element)) {
      logger.info("Leaf already present: %s", leafToString(element));
      return;
    }
    // Need to recompute the path to the top starting at bucketNumber.
    recomputePathFromBucket(bucketNumber);
  }

  @Override
  public void add(Collection<LeafType> elements) {
    // TODO: [perf] With a more complex implementation, we can consider doing one or both of:
    // (a) Identify the leaf nodes that get affected and call recomputePathFromBucket for those
    // buckets once each.
    // (b) If elements is greater than a certain fraction of the whole trie, just recompute the
    // whole trie.
    for (LeafType element : elements) {
      add(element);
    }
  }

  /** Removes {@code element} from the trie (if does not exist, this is a no op). */
  @Override
  public void remove(LeafType element) {
    byte[] elementDigest = getDigest(element);
    int bucketNumber = getBucketNumber(elementDigest, levels);
    if (!buckets.get(bucketNumber).remove(new Bytes(elementDigest))) {
      logger.info("Leaf not present: %s", leafToString(element));
      return;
    }
    // Need to recompute the path to the top.
    recomputePathFromBucket(bucketNumber);
  }

  @Override
  public void remove(Collection<LeafType> elements) {
    // TODO: [perf] see comments in add(Collection) on how this could be made more efficient.
    for (LeafType element : elements) {
      remove(element);
    }
  }

  @Override
  public Collection<LeafType> removeAll() {
    Collection<LeafType> result = getElements(new byte[0], 0);
    // Clear the buckets and re-initialize.
    buckets.clear();
    initializeLeavesAndBuckets(new ArrayList<LeafType>());
    return result;
  }

  /** Returns the digest for the leaf. */
  private byte[] getDigest(LeafType leaf) {
    return leafDigester.getDigest(digestFunction, leaf);
  }

  /** Returns the digest for the {@code element1} and {@code element2} (the order matters). */
  private byte[] getDigest(byte[] element1, byte[] element2) {
    digestFunction.reset();
    digestFunction.update(element1);
    digestFunction.update(element2);
    return digestFunction.getDigest();
  }

  /** Recompute the digests for the path starting from bucket number {@code bucketNumber}. */
  private void recomputePathFromBucket(int bucketNumber) {
    // Update the digest for the bucket in the leaf of the trie.
    int digestIndex = digestIndexForBucket(bucketNumber);
    digests[digestIndex] = buckets.get(bucketNumber).getDigest();

    // Go up the path to the root.
    for (int nodeIndex = parent(digestIndex); nodeIndex >= 0; nodeIndex = parent(nodeIndex)) {
      digests[nodeIndex] = getDigest(digests[leftChild(nodeIndex)],
          digests[rightChild(nodeIndex)]);
    }
  }

  /** Returns the digest index for the given {@code bucketNumber}. */
  private int digestIndexForBucket(int bucketNumber) {
    // For 3 levels, there are 2^4 - 1 digests. So digests.length/2 is 7. For 8 buckets, the numbers
    // go from 7 to 14.
    // For 0 levels, digests.lenght is 2^1 - 1. So digests.length/2 is 0. For 1 bucket, the numbers
    // from 0 to 0.
    return digests.length / 2 + bucketNumber;
  }

  /** Returns the left child of the node at {@code nodeIndex}. */
  private int leftChild(int nodeIndex) {
    int result = 2 * nodeIndex + 1;
    Preconditions.checkState(result < digests.length);
    return result;
  }

  /** Returns the right child of the node at {@code nodeIndex}. */
  private int rightChild(int nodeIndex) {
    int result = 2 * nodeIndex + 2;
    Preconditions.checkState(result < digests.length);
    return result;
  }

  /** Returns the parent of the node at index {@code nodeIndex}. */
  private int parent(int nodeIndex) {
    Preconditions.checkState(nodeIndex < digests.length);
    if (nodeIndex == 0){
      return -1;
    }
    return (nodeIndex - 1) / 2;
  }

  /**
   * Given a {@code leafDigest} and the number of levels in the trie, returns the bucket that the
   * {@code leafDigest} belongs to.
   */
  
  static int getBucketNumber(byte[] leaf, int levels) {

    // We take the bytes in little endian order. But inside a byte, we start from the
    // LSB end (the code turns out to "natural" with these choices.
    // For example, for 0x55, levels = 5, the bucket number is 0x15, i.e., 5 LSB bits interpreted
    // as an integer.

    Preconditions.checkNotNull(leaf);

    // Number of bytes to consider for the trie.
    int numBytes = levels / 8 + 1;
    ByteBuffer buffer = ByteBuffer.allocate(8).order(ByteOrder.LITTLE_ENDIAN);

    // Number of bits to consider in the last byte.
    byte numBitsLastByte = (byte) (levels % 8);

    // If numBits = 3, then 8 - 1 = 7.
    byte lastBytePattern = (byte) ((1 << numBitsLastByte) - 1);
    int bucketNumber = 0;

    // Now add the bytes in the byte buffer so that they can interpreted as an
    // integer later.
    for (int i = 0; i < Math.min(numBytes, leaf.length); i++) {
      byte actualByte = leaf[i];
      if (i == numBytes - 1) {
        actualByte &= lastBytePattern;
      }
      buffer.put(actualByte);
    }

    // Pad up enough for the zeroth integer else getInt can crash.
    buffer.put((byte) 0);
    buffer.put((byte) 0);
    buffer.put((byte) 0);
    buffer.put((byte) 0);
    bucketNumber = buffer.getInt(0);
    return bucketNumber;
  }

  /**
   * Checks that the internal representation of the trie is consistent. This is an *expensive*
   * method.
   */
  
  public void checkRep() {
    Preconditions.checkState((1 << levels) == buckets.size());
    Preconditions.checkState(digests.length == 2 * buckets.size() - 1);

    // Check for non-null.
    for (int i = 0; i < digests.length; i++) {
      Preconditions.checkNotNull(digests[i]);
    }

    // Check all the digests internally.
    for (int i = 0; i < digests.length / 2 - 1; i++) {
      byte[] nodeDigest = getDigest(digests[leftChild(i)], digests[rightChild(i)]);
      Preconditions.checkState(Arrays.equals(nodeDigest, digests[i]));
    }

    // Now check the buckets.
    for (int i = 0; i < buckets.size(); i++) {
      Preconditions.checkNotNull(buckets.get(i));
      int bucketIndex = digestIndexForBucket(i);
      Preconditions.checkState(Arrays.equals(buckets.get(i).getDigest(), digests[bucketIndex]));
    }
  }

  /** Convests a leaf to a readable string for debugging. */
  private static <LeafType> String leafToString(LeafType leaf) {
    if (leaf instanceof Message) {
      return TextFormat.shortDebugString((Message) leaf);
    } else {
      return leaf.toString();
    }
  }

  @Override
  public void toCompactString(TextBuilder builder) {
    builder.appendFormat("Levels = %d, Tree:\n", levels);
    int nodeNum = 0;
    // Print the tree nodes.
    for (int i = 0; i <= levels; i++) {
      int numNodesAtLevel = 1 << i;
      builder.appendFormat("\nLevel %d: ", i);
      for (int j = 0; j < numNodesAtLevel; j++) {
        builder.appendFormat("%s, ", Bytes.toString(digests[nodeNum++]));
      }
    }
    builder.append('\n');
    // Print the buckets.
    for (int i = 0; i < buckets.size(); i++) {
      builder.appendFormat("B%d: ", i);
      buckets.get(i).toCompactString(builder);
      builder.append('\n');
    }
  }
}
