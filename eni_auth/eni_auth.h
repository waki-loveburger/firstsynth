// Subscription licence check for easy and nice instruments.
//
// Product-agnostic: FirstSynth and SuiKinKutsu vendor this same folder and
// differ only in eni_config.h. The Python twin that ships with 8-Control
// (eni_auth.py) implements the identical protocol against the same server,
// so behaviour changes belong in both or neither.
//
// The shape of the system (server-side decisions #1-#6, 2026-08-19):
//
//   first run    Device Flow in the *system* browser -> Auth0 access token
//                -> POST /api/license -> Ed25519-signed licence + a refresh
//                   secret that is never shown to the user
//   every start  read the licence file, check signature + expiry locally.
//                No network at all, so it works offline (on stage, in a
//                venue with no wifi) and costs microseconds.
//   monthly      when the expiry draws near and we happen to be online,
//                POST /api/license/refresh once, on a worker thread.
//                Failure is ignored - the licence stays valid until it
//                expires on its own.
//
// Nothing here ever revokes a licence. A cancelled subscription simply
// stops being renewed and lapses at current_period_end + 7 days. There is
// no "kill" path that a bug could fire at a paying customer.
//
// THREADING: no function in this header may be called from the audio
// thread. Verification is pure computation but still reads a file the
// first time; the network calls block for seconds. Call CheckLicence() at
// instantiation (message thread) and let RefreshInBackground() own its own
// worker thread.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace eni {

// ---------------------------------------------------------------------------
// Licence verification (pure, offline)
// ---------------------------------------------------------------------------

enum class Reason
{
  kValid,
  kMissing,      // no licence file, or no entry for this product
  kMalformed,    // not a JWT, or claims missing/of the wrong type
  kBadHeader,    // alg is not EdDSA - refuse before touching the signature
  kBadSignature, // not signed by our key, or tampered with
  kWrongProduct, // a licence for a sibling product
  kExpired,      // signature is good but the clock has moved past exp
};

struct LicenceCheck
{
  bool valid = false;
  Reason reason = Reason::kMissing;
  int64_t exp = 0; // unix seconds; set whenever the token parsed far enough

  int64_t DaysLeft(int64_t now = 0) const;
};

// Human-readable reason, for logs and the "why can't I play" UI.
const char* ReasonString(Reason);

// Verifies signature, product and expiry. `now` defaults to the wall clock;
// pass it in tests. `pubkeyHex` defaults to the built-in production key and
// exists so the tests can use a throwaway key pair.
LicenceCheck VerifyLicence(const std::string& token,
                           const std::string& product,
                           int64_t now = 0,
                           const std::string& pubkeyHex = std::string());

// ---------------------------------------------------------------------------
// Licence file
// ---------------------------------------------------------------------------
//
// One file for every product of the label, so a user who logs in from
// FirstSynth is already licensed in SuiKinKutsu and 8-Control:
//
//   %APPDATA%\easyandnice\license.json                      (Windows)
//   ~/Library/Application Support/easyandnice/license.json   (macOS)
//   $XDG_CONFIG_HOME/easyandnice/license.json                (Linux)
//
// {
//   "version": 1,
//   "refreshSecret": "enis_...",
//   "licenses": { "firstsynth": { "token": "...", "exp": 1790000000 } }
// }
//
// ENI_LICENSE_DIR overrides the directory (tests, portable installs).

std::string LicenceDir();
std::string LicenceFilePath();

struct LicenceFile
{
  std::string refreshSecret;
  // Token for the product this build was compiled for. Other products'
  // entries are preserved on write but not exposed here.
  std::string token;
  int64_t exp = 0;
  std::string rawJson; // whole file, so a write can keep siblings intact
};

bool LoadLicenceFile(LicenceFile& out);

// Writes through a temporary file and renames, so a crash mid-write cannot
// leave a truncated licence behind (that would force a needless re-login).
bool SaveLicence(const LicenceFile& current,
                 const std::string& token,
                 int64_t exp,
                 const std::string& refreshSecret);

bool ClearLicence(); // logout / support ("sign out and try again")

// ---------------------------------------------------------------------------
// HTTP transport
// ---------------------------------------------------------------------------
//
// Platform-native under the hood: WinHTTP on Windows, CFNetwork on macOS.
// Injectable so the tests exercise the whole protocol with no network and
// so a host application can route through its own stack if it must.

struct HttpRequest
{
  std::string url;
  std::string body;
  std::string contentType;   // application/json or x-www-form-urlencoded
  std::string authorization; // bare token; the "Bearer " prefix is added here
  int timeoutSeconds = 15;
};

struct HttpResponse
{
  int status = 0; // 0 = the request never reached the server
  std::string body;
  std::string error;
};

using HttpTransport = std::function<HttpResponse(const HttpRequest&)>;

void SetHttpTransport(HttpTransport); // pass {} to restore the platform one

// ---------------------------------------------------------------------------
// Device Flow and licence issuing
// ---------------------------------------------------------------------------

struct DeviceCode
{
  std::string deviceCode;
  std::string userCode;               // shown as a fallback: "ABCD-EFGH"
  std::string verificationUri;        // shown with the code above
  std::string verificationUriComplete; // opened in the browser (code baked in)
  int interval = 5;
  int expiresIn = 900;
};

struct Issued
{
  std::string token;
  int64_t exp = 0;
  std::string refreshSecret; // only present on the first issue
};

struct Error
{
  std::string message; // already user-facing Japanese
  std::string code;    // machine-readable: no_subscription, invalid_secret, ...
  int status = 0;
};

bool RequestDeviceCode(DeviceCode& out, Error& err);

// Blocks until the user approves in the browser, the code expires, or
// `cancelled` returns true. Poll interval comes from the server.
bool PollForAccessToken(const DeviceCode& dc,
                        std::string& outAccessToken,
                        Error& err,
                        const std::function<bool()>& cancelled = {});

bool FetchLicence(const std::string& accessToken, Issued& out, Error& err);
bool RefreshLicence(const std::string& refreshSecret, Issued& out, Error& err);

// Opens the system browser. Never a WebView: Google refuses OAuth from
// embedded browsers, and an embedded one cannot reuse the user's session.
bool OpenInBrowser(const std::string& url);

// ---------------------------------------------------------------------------
// What the plug-in actually calls
// ---------------------------------------------------------------------------

// Reads and verifies the stored licence. Offline, no allocation of note,
// safe to call on the message thread at instantiation.
LicenceCheck CheckLicence();

// True when the licence is close enough to expiry to be worth renewing.
bool ShouldRefresh(int64_t exp, int64_t now = 0);

// Fire-and-forget renewal on a detached worker thread: reads the refresh
// secret, calls the server once, writes the file back on success. Silent on
// every failure - being offline is the normal case, not an error.
void RefreshInBackground();

// The whole first-run login, start to finish. Blocks (browser + polling +
// issuing), so run it from a worker thread and report progress through the
// callback, which is invoked with the device code as soon as it is known.
bool RunDeviceFlow(const std::function<void(const DeviceCode&)>& onCode,
                   Error& err,
                   const std::function<bool()>& cancelled = {});

} // namespace eni
