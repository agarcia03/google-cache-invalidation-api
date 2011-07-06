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

package com.google.ipc.invalidation.ticl.android.c2dm;

import android.content.Context;

/**
 * Provides utilities for testing C2DM registration state, primarily access to C2DMMessaging
 * APIs that are not public.   This class should never be included in actual applications.
 *
 */
public class C2DMTest {

  public static void clearRegistrationId(Context context) {
    C2DMessaging.clearRegistrationId(context);
  }

  public static void setRegistrationId(Context context, String registrationId) {
    C2DMessaging.setRegistrationId(context, registrationId);
  }
}
