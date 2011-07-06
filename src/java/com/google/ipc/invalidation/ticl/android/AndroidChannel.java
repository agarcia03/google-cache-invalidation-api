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

import com.google.api.client.http.ByteArrayContent;
import com.google.api.client.http.GenericUrl;
import com.google.api.client.http.HttpRequest;
import com.google.api.client.http.HttpRequestFactory;
import com.google.api.client.http.HttpRequestInitializer;
import com.google.api.client.http.HttpResponse;
import com.google.api.client.http.HttpResponseException;
import com.google.api.client.http.HttpTransport;
import com.google.common.base.Preconditions;
import com.google.ipc.invalidation.common.CommonProtos2;
import com.google.ipc.invalidation.external.client.SystemResources;
import com.google.ipc.invalidation.external.client.SystemResources.ComponentNetworkChannel;
import com.google.ipc.invalidation.external.client.types.Callback;
import com.google.protos.ipc.invalidation.Channel.NetworkEndpointId;

import android.accounts.AccountManager;
import android.accounts.AccountManagerCallback;
import android.accounts.AccountManagerFuture;
import android.accounts.AuthenticatorException;
import android.accounts.OperationCanceledException;
import android.os.Bundle;
import android.util.Base64;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;


/**
 * Provides a bidirectional channel for Android devices using C2DM (data center to device) and the
 * Android HTTP frontend (device to data center). The android channel computes a network endpoint id
 * based upon the C2DM registration ID for the containing application ID and the client key of the
 * client using the channel. If an attempt is made to send messages on the channel before a C2DM
 * registration ID has been assigned (via {@link #setRegistrationId}, it will temporarily buffer the
 * outbound messages and send them when the registration ID is eventually assigned.
 *
 */
class AndroidChannel implements ComponentNetworkChannel {

  private static final String TAG = "AndroidChannel";

  /** Invalidation client proxy using the channel. */
  private final AndroidClientProxy proxy;

  /** Callback receiver for this channel */
  private Callback<byte[]> callbackReceiver;

  /** Status receiver for this channel */
  private Callback<Boolean> statusReceiver;

  /** Request factory for outbound HTTP calls. */
  private final HttpRequestFactory requestFactory;

  /** System resources for this channel */
  @SuppressWarnings("unused")
  private SystemResources resources;

  /** The registration id associated with the channel */
  private String registrationId;

  /** The authentication token that can be used in channel requests to the server */
  private String authToken;

  // TODO: Add code to track time of last network activity (in either direction)
  // so inactive clients can be detected and periodically flushed from memory.

  /**
   * List that holds outbound messages while waiting for a registration ID.   Allocated on
   * demand since it is only needed when there is no registration id.
   */
  private List<byte[]> pendingMessages = null;

  /**
   * Creates a new AndroidChannel.
   *
   * @param proxy the client proxy associated with the channel
   * @param transport the HTTP transport to use to communicate with the Android invalidation
   *        frontend
   * @param c2dmRegistrationId the c2dm registration ID for the service
   */
  AndroidChannel(AndroidClientProxy proxy, HttpTransport transport, String c2dmRegistrationId) {
    this.proxy = Preconditions.checkNotNull(proxy);

    // Store the current registration ID into the channel instance (may be null)
    registrationId = c2dmRegistrationId;

    // Prefetch the auth sub token.  Since this might require an HTTP round trip, we do this
    // at new client creation time.
    getAuthToken();

    // Create a request factory for the provided transport that will automatically set the
    // authentication token for all requests.
    requestFactory = transport.createRequestFactory(new HttpRequestInitializer() {
      @Override
      public void initialize(HttpRequest request) {
        setGoogleAuthHeader(request, authToken);
      }
    });
  }

  /** Returns the C2DM registration ID associated with the channel */
   String getRegistrationId() {
    return registrationId;
  }

  /** Returns the client proxy that is using the channel */
   AndroidClientProxy getClientProxy() {
    return proxy;
  }

  /**
   * Retrieves the list of pending messages in the channel (or {@code null} if there are none).
   */
   List<byte[]> getPendingMessages() {
    return pendingMessages;
  }

   String getAuthToken() {
    return authToken;
  }

  AccountManagerFuture<Bundle> pendingTokenFuture = null;

  /**
   * Initiates acquisition of an authentication token that can be used with channel HTTP requests.
   * Android token acquisition is asynchronous since it may require HTTP interactions with the
   * ClientLogin servers to obtain the token.
   */
  
  synchronized void requestAuthToken() {
    // If there is currently no token and no pending request, initiate one.
    if ((authToken == null) && (pendingTokenFuture == null)) {

      // Ask the AccountManager for the token, with a pending future to store it on the channel
      // once available.
      final AndroidChannel theChannel = this;
      AccountManager accountManager = AccountManager.get(proxy.service);
      accountManager.getAuthToken(proxy.getAccount(), AndroidHttpConstants.SERVICE, true,
          new AccountManagerCallback<Bundle>() {
            @Override
            public void run(AccountManagerFuture<Bundle> bundle) {
              try {
                Bundle result = pendingTokenFuture.getResult();
                if (result.containsKey(AccountManager.KEY_INTENT)) {
                  // TODO: Handle case where there are no authentication credentials
                  // associated with the client account
                  Log.e(TAG, "Token acquisition requires user login");
                  return;
                }
                setAuthToken(result.getString(AccountManager.KEY_AUTHTOKEN));
              } catch (OperationCanceledException exception) {
                Log.w("Auth cancelled", exception);
                // TODO: Send error to client
              } catch (AuthenticatorException exception) {
                Log.i(TAG, "Auth error acquiring token", exception);
                requestAuthToken();
              } catch (IOException exception) {
                Log.i(TAG, "IO Exception acquiring token", exception);
                requestAuthToken();
              } finally {
                // Reset the pending future to indicate no request is pending
                pendingTokenFuture = null;
              }
            }
      }, null);
    } else {
      Log.d(TAG, "Token request already pending");
    }
  }

  /*
   * Updates the registration ID for this channel, flushing any pending oubound messages that
   * were waiting for an id.
   */
  synchronized void setRegistrationId(String updatedRegistrationId) {
    // Synchronized to avoid concurrent access to pendingMessages
    if (registrationId != updatedRegistrationId) {
      registrationId = updatedRegistrationId;
      if (pendingMessages != null) {
        checkReady();
      } else {
        // TODO: Trigger heartbeat or other action to notify server of new endpoint id
      }
    }
  }

  /**
   * Sets the authentication token to use for HTTP requests to the invalidation frontend and
   * flushes any pending messages (if appropriate).
   *
   * @param authToken the authentication token
   */
  synchronized void setAuthToken(String authToken) {
    this.authToken = authToken;
    checkReady();
  }

  @Override
  public void addNetworkStatusReceiver(Callback<Boolean> statusReceiver) {
    this.statusReceiver = statusReceiver;
  }

  @Override
  public synchronized void sendMessage(byte[] outgoingMessage) {
    // synchronized to avoid concurrent access to pendingMessages

    // If there is no registration id, we cannot compute a network endpoint id. If there is no
    // auth token, then we cannot authenticate the send request.  Defer sending messages until both
    // are received.
    if (registrationId == null || authToken == null) {
      if (pendingMessages == null) {
        pendingMessages = new ArrayList<byte[]>();
      }
      // TODO: Put some limit on maximum amount of requests buffered
      pendingMessages.add(outgoingMessage);
      return;
    }

    NetworkEndpointId networkEndpointId =
        CommonProtos2.newAndroidEndpointId(registrationId, proxy.getClientKey());

    StringBuilder target = new StringBuilder();
    target.append(proxy.service.getChannelUrl());
    target.append(AndroidHttpConstants.REQUEST_URL);
    target.append(Base64.encodeToString(networkEndpointId.toByteArray(),
        Base64.URL_SAFE | Base64.NO_WRAP  | Base64.NO_PADDING));
    GenericUrl url = new GenericUrl(target.toString());

    ByteArrayContent content = createByteArrayContent(outgoingMessage);
    try {
      HttpRequest request = requestFactory.buildPostRequest(url, content);
      request.execute();
    } catch (HttpResponseException exception) {
      // TODO: Distinguish between key HTTP error codes and handle more specifically
      // where appropriate.
      Log.e(TAG, "Error from server on request", exception);
    } catch (IOException exception) {
      Log.e(TAG, "Error writing request", exception);
    }
  }

  /**
   * Called when either the registration or authentication token has been received to check to
   * see if channel is ready for network activity.  If so, the status receiver is notified and
   * any pending messages are flushed.
   */
  private synchronized void checkReady() {
    if (registrationId != null && authToken != null) {

      // Notify the status receiver that we are now network enebled
      statusReceiver.accept(true);

      // Flush any pending messages
      if (pendingMessages != null) {
        for (byte [] message : pendingMessages) {
          sendMessage(message);
        }
        pendingMessages = null;
      }
    }
  }

  void receiveMessage(byte[] inboundMessage) {
    callbackReceiver.accept(inboundMessage);
  }

  void retrieveMailbox(String mailboxId) {

    // It's highly unlikely that we'll start receiving events before we have an auth token, but
    // if that is the case then we cannot retrieve mailbox contents
    // TODO: Should we buffer and flush these like outbound requests?
    if (authToken == null) {
      Log.e(TAG, "Unable to retrieve mailbox.  No auth token");
      return;
    }
    StringBuilder target = new StringBuilder();
    target.append(proxy.service.getChannelUrl());
    target.append(AndroidHttpConstants.MAILBOX_URL);
    target.append(mailboxId);
    GenericUrl url = new GenericUrl(target.toString());
    try {
      HttpRequest request = requestFactory.buildPostRequest(url, null);
      HttpResponse response = request.execute();

      // Retrieve and validate the Content-Length header
      String contentLengthHeader = getContentLength(response);
      int contentLength = 0;
      try {
        contentLength = Integer.parseInt(contentLengthHeader);
      } catch (NumberFormatException exception) {
        Log.e(TAG, "Invalid mailbox Content-Length:" + contentLengthHeader);
        return;
      }
      if (contentLength <= 0) {
        Log.e(TAG, "Invalid mailbox Content-Length value:" + contentLength);
        return;
      }
      byte[] mailboxData = new byte[contentLength];

      // Retrieve the content from the response and forward it to the message receiver
      InputStream contentStream = response.getContent();
      if (contentStream == null) {
        Log.e(TAG, "Missing content for mailbox " + mailboxId);
        return;
      }
      try {
        int bytesRead = 0;
        while (bytesRead < contentLength) {
          int numRead = contentStream.read(mailboxData, bytesRead, contentLength - bytesRead);
          if (numRead < 0) {
            Log.e(TAG, "Premature end of data: read " + bytesRead + ", expected " + contentLength);
            return;
          }
          bytesRead += numRead;
        }
      } catch (IOException exception) {
        Log.e(TAG, "Error reading mailbox data", exception);
        return;
      }

      // Send the mailbox content on to the message receiver
      receiveMessage(mailboxData);

    } catch (HttpResponseException exception) {
      // TODO: Distinguish between key HTTP error codes and handle more specifically
      // where appropriate.
      Log.e(TAG, "Error from server on mailbox retrieval", exception);
    } catch (IOException exception) {
      Log.e(TAG, "Error retrieving mailbox", exception);
    }
  }

  @Override
  public void setMessageReceiver(Callback<byte[]> incomingReceiver) {
    this.callbackReceiver = incomingReceiver;
  }

  @Override
  public void setSystemResources(SystemResources resources) {
    this.resources = resources;
  }

  // It is necessary to use some versions of deprecated Google API Client apis to maintain
  // backwards compatibility with the open source version of the api client.   These are
  // isolated below in private methods to minimize the scope of deprecation suppression
  // TODO: Remove the methods and replace with non-deprecated usage once the open
  // source project is updated to version 1.5.

  @SuppressWarnings("deprecation")
  private String getContentLength(HttpResponse response) {
    return response.headers.contentLength;
  }

  @SuppressWarnings("deprecation")
  private ByteArrayContent createByteArrayContent(byte [] data) {
    ByteArrayContent content = new ByteArrayContent(data);
    content.type = AndroidHttpConstants.PROTO_CONTENT_TYPE;
    return content;
  }

  @SuppressWarnings("deprecation")
  private void setGoogleAuthHeader(HttpRequest request, String token) {
    request.headers.authorization = "GoogleLogin auth=" + token;
  }
}
