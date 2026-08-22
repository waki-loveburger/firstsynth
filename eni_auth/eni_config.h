// Per-product settings for the licence check.
//
// This is the only file that differs between FirstSynth and SuiKinKutsu:
// copy eni_auth/ across and change ENI_PRODUCT and ENI_APP_VERSION.
// Everything else - endpoints, key, protocol - is shared on purpose, so a
// user logs in once and every instrument of the label unlocks.

#pragma once

// Product slug. Must match the `slug` column of the server's products table
// (see eni-api migrations): pc-guruve / 8-control / firstsynth / suikinkutsu.
// A typo here surfaces as a clean 404 unknown_product rather than as a
// licence that mysteriously never validates.
#define ENI_PRODUCT "firstsynth"

// Reported to the server with each licence issue/refresh and recorded there
// for support ("which build is this machine running?"). Not used in any
// authorisation decision.
#define ENI_APP_VERSION "firstsynth/1.0.0"

// Licence server and Auth0 tenant.
#define ENI_API_BASE "https://api.easyandnicewaki.com"
#define ENI_AUTH0_DOMAIN "easyandnice.jp.auth0.com"
#define ENI_AUTH0_AUDIENCE "https://api.easyandnicewaki.com"

// Auth0 native application "easy and nice instruments (native)", created
// 2026-08-22 with the device-code grant only. A public client: this ID is
// meant to be embedded, and on its own it can do nothing without a human
// approving the request in a browser.
#define ENI_AUTH0_CLIENT_ID "bTqsvSVduO2zK1JT631N44odj2pPrbi1"

// Ed25519 public key (raw 32 bytes, hex) that licences are signed with.
// The private half exists only in 1Password and as a Cloudflare Workers
// secret. Changing this key invalidates every licence in the field, so it
// can only be rotated together with an update of all shipped instruments.
#define ENI_LICENSE_PUBKEY_HEX \
  "81d6eb84b61d5937cbb53f0ffb4438b4e1630e5f6ae64e2ed04598f6bf7a689a"

// Renew once the licence has less than this left. The server issues
// current_period_end + 7 days, so a fortnight is comfortably more than one
// billing cycle's worth of chances to be online, and a user who is offline
// for a fortnight still never sees an interruption.
#define ENI_REFRESH_THRESHOLD_DAYS 14

// Where the shared licence file lives, under the platform's config dir.
#define ENI_LICENSE_DIRNAME "easyandnice"
#define ENI_LICENSE_FILENAME "license.json"

// Shown when there is no active subscription.
#define ENI_SIGNUP_URL "https://easyandnicewaki.com/instruments.html"
