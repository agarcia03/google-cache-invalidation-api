package com.google.ipc.invalidation.examples.android2;

import com.google.common.base.Preconditions;

import android.accounts.Account;
import android.accounts.AccountManager;
import android.accounts.AccountManagerCallback;
import android.accounts.AccountManagerFuture;
import android.app.IntentService;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

/**
 * Example of an {@link Intent} service that responds to authorization token requests from the
 *  client service.
 *
 */
public final class ExampleService extends IntentService {

  /** The tag used for logging. */
  private static final String TAG = "TEA2:ExampleService";

  /**
   * Action requesting that an authorization token to send a message be provided. This is the action
   * used in the intent to the application.
   */
  private static final String ACTION_REQUEST_AUTH_TOKEN =
      "com.google.ipc.invalidation.AUTH_TOKEN_REQUEST";

  /** Extra in an authorization token request response providing the pending intent. */
  private static final String EXTRA_PENDING_INTENT =
      "com.google.ipc.invalidation.AUTH_TOKEN_PENDING_INTENT";

  /** Extra in the intent from the application that provides the authorization token string. */
  private static final String EXTRA_AUTH_TOKEN = "com.google.ipc.invalidation.AUTH_TOKEN";

  /** Extra in the intent from the application that provides the authorization token type. */
  private static final String EXTRA_AUTH_TOKEN_TYPE = "com.google.ipc.invalidation.AUTH_TOKEN_TYPE";

  /**
   * Extra in an authorization token request message indicating that the token provided as the value
   * was invalid when last used. This may be set on the intent to the application.
   */
  
  private static final String EXTRA_INVALIDATE_AUTH_TOKEN =
      "com.google.ipc.invalidaton.AUTH_TOKEN_INVALIDATE";

  /** The account type value for Google accounts */
  private static final String GOOGLE_ACCOUNT_TYPE = "com.google";

  /**
   * This is the authentication token type that's used for  communication to the server.
   * For real applications, it would generally match the authorization type used by the application.
   */
  private static final String AUTH_TYPE = "android";

  public ExampleService() {
    super("ExampleService");
    setIntentRedelivery(true);
  }

  @Override
  protected void onHandleIntent(Intent intent) {
    if ((null != intent) && ACTION_REQUEST_AUTH_TOKEN.equals(intent.getAction())) {
      handleAuthTokenRequest(intent);
    } else {
      Log.e(TAG, "Unhandled intent: " + intent);
    }
  }

  /** Handles a request for an authorization token to send a message. */
  private void handleAuthTokenRequest(Intent intent) {
    final Context context = getApplicationContext();
    AccountManager accountManager = AccountManager.get(context);
    if (intent.hasExtra(EXTRA_INVALIDATE_AUTH_TOKEN)) {
      // Tell the account manager there is a stale authorization token.
      String authToken = intent.getStringExtra(EXTRA_INVALIDATE_AUTH_TOKEN);
      Log.i(TAG, "Invalidating authorization token " + authToken);
      accountManager.invalidateAuthToken(AUTH_TYPE, authToken);
    }
    Account account = getAccount(accountManager);
    Log.i(TAG, "Retrieving authorization token for account " + account.name);
    final PendingIntent pendingIntent = intent.getParcelableExtra(EXTRA_PENDING_INTENT);
    accountManager.getAuthToken(account, AUTH_TYPE, true, new AccountManagerCallback<Bundle>() {
      @Override
      public void run(AccountManagerFuture<Bundle> future) {
        try {
          Bundle result = future.getResult();
          String authToken = result.getString(AccountManager.KEY_AUTHTOKEN);
          Intent responseIntent = new Intent()
             .putExtra(EXTRA_AUTH_TOKEN, authToken)
             .putExtra(EXTRA_AUTH_TOKEN_TYPE, AUTH_TYPE);
          pendingIntent.send(context, 0, responseIntent);
        } catch (Exception e) {
          Log.e(TAG, "Unable to get authorization token", e);
        }
      }
    }, null);
  }

  /** Returns the first Google account enabled on the device. */
  private static Account getAccount(AccountManager accountManager) {
    Preconditions.checkNotNull(accountManager);
    for (Account acct : accountManager.getAccounts()) {
      if (GOOGLE_ACCOUNT_TYPE.equals(acct.type)) {
        return acct;
      }
    }
    throw new RuntimeException("No google account enabled.");
  }
}
