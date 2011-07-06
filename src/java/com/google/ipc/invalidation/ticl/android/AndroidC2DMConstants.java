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

/**
 * Defines constants related to Invalidation C2DM messages.
 *
 */
public class AndroidC2DMConstants {

  /** Account name associated with delivered C2DM messages */
  public static final  String SENDER_ID = "ipc.invalidation@gmail.com";

  /** Name of C2DM parameter containing the client key. */
  public static final String CLIENT_KEY_PARAM = "data.tid";

  /** Name of C2DM parameter containing the mailbox id. */
  public static final String MAILBOX_ID_PARAM = "data.mid";

  /** Name of C2DM parameter containing message content. */
  public static final String CONTENT_PARAM = "data.content";
}
