#include "eni_auth.h"

#include "eni_config.h"
#include "eni_json.h"
#include "vendor/monocypher/monocypher-ed25519.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
  #include <windows.h>
  #include <shellapi.h>
  #include <shlobj.h>
#else
  #include <cstdlib>
  #include <pwd.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
#endif

namespace eni {

// Implemented per platform in eni_http_win.cpp / eni_http_mac.mm /
// eni_http_null.cpp. Declared at namespace scope so those translation units
// can define it.
HttpResponse PlatformHttpPost(const HttpRequest&);

namespace {

int64_t NowSeconds()
{
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// --- base64url ------------------------------------------------------------

bool Base64UrlDecode(const std::string& in, std::vector<uint8_t>& out)
{
  auto sextet = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
  };

  out.clear();
  out.reserve(in.size() * 3 / 4 + 3);

  uint32_t bits = 0;
  int have = 0;
  for (const char c : in)
  {
    if (c == '=') break; // tolerated, though JWT parts are unpadded
    const int v = sextet(c);
    if (v < 0) return false;
    bits = (bits << 6) | uint32_t(v);
    have += 6;
    if (have >= 8)
    {
      have -= 8;
      out.push_back(uint8_t((bits >> have) & 0xFF));
    }
  }
  return true;
}

bool HexDecode32(const std::string& hex, uint8_t out[32])
{
  if (hex.size() != 64) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  for (int i = 0; i < 32; i++)
  {
    const int hi = nibble(hex[size_t(i) * 2]);
    const int lo = nibble(hex[size_t(i) * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = uint8_t((hi << 4) | lo);
  }
  return true;
}

// --- filesystem -----------------------------------------------------------

bool ReadWholeFile(const std::string& path, std::string& out)
{
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  std::string data;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
  const bool ok = ferror(f) == 0;
  fclose(f);
  if (!ok) return false;
  out = std::move(data);
  return true;
}

bool MakeDirectories(const std::string& path)
{
  if (path.empty()) return false;
#ifdef _WIN32
  const int rc = SHCreateDirectoryExA(nullptr, path.c_str(), nullptr);
  return rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS;
#else
  std::string partial;
  for (size_t i = 0; i <= path.size(); i++)
  {
    if (i == path.size() || path[i] == '/')
    {
      if (!partial.empty() && mkdir(partial.c_str(), 0700) != 0 && errno != EEXIST)
        return false;
    }
    if (i < path.size()) partial += path[i];
  }
  return true;
#endif
}

std::string EnvOrEmpty(const char* name)
{
#ifdef _WIN32
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || !value) return std::string();
  std::string out(value);
  free(value);
  return out;
#else
  const char* v = getenv(name);
  return v ? std::string(v) : std::string();
#endif
}

std::string HomeDir()
{
#ifdef _WIN32
  const std::string profile = EnvOrEmpty("USERPROFILE");
  if (!profile.empty()) return profile;
  return EnvOrEmpty("HOMEDRIVE") + EnvOrEmpty("HOMEPATH");
#else
  const std::string home = EnvOrEmpty("HOME");
  if (!home.empty()) return home;
  if (const passwd* pw = getpwuid(getuid())) return pw->pw_dir ? pw->pw_dir : "";
  return std::string();
#endif
}

#ifdef _WIN32
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

std::string Join(const std::string& a, const std::string& b)
{
  if (a.empty()) return b;
  if (!a.empty() && (a.back() == '/' || a.back() == '\\')) return a + b;
  return a + kSep + b;
}

std::string HostName()
{
#ifdef _WIN32
  char buf[256] = {0};
  DWORD len = DWORD(sizeof(buf));
  if (GetComputerNameA(buf, &len)) return std::string(buf, len);
  return std::string();
#else
  char buf[256] = {0};
  if (gethostname(buf, sizeof(buf) - 1) == 0) return std::string(buf);
  return std::string();
#endif
}

std::string OsName()
{
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#else
  return "Linux";
#endif
}

// --- transport ------------------------------------------------------------

HttpTransport& Transport()
{
  static HttpTransport t;
  return t;
}

HttpResponse Post(const HttpRequest& req)
{
  if (Transport()) return Transport()(req);
  return PlatformHttpPost(req);
}

// Every server error we surface carries a code the UI can branch on; the
// message is what the user reads, so it is written in Japanese here rather
// than assembled at the call site.
void FillError(Error& err, const HttpResponse& res)
{
  err.status = res.status;
  if (res.status == 0)
  {
    err.code = "network";
    err.message = res.error.empty() ? "サーバーに接続できませんでした" : res.error;
    return;
  }

  json::Value body;
  if (json::Parse(res.body, body))
  {
    err.code = body.GetString("code", body.GetString("error"));
    err.message = body.GetString("error");
  }

  if (err.code == "no_subscription")
    err.message = "有効なサブスクリプションがありません";
  else if (err.code == "invalid_secret")
    err.message = "ライセンスの更新に失敗しました。もう一度ログインしてください";
  else if (err.code == "unknown_product")
    err.message = "製品の指定が誤っています（アプリの不具合です）";
  else if (err.message.empty())
    err.message = "サーバーエラーが発生しました (HTTP " + std::to_string(res.status) + ")";
}

json::Value DeviceInfo()
{
  json::Value device = json::Value::Object();
  device.Set("name", json::Value(HostName()));
  device.Set("os", json::Value(OsName()));
  device.Set("app", json::Value(std::string(ENI_APP_VERSION)));
  return device;
}

bool ReadIssued(const std::string& body, Issued& out, Error& err)
{
  json::Value v;
  if (!json::Parse(body, v) || !v.IsObject())
  {
    err.code = "malformed_response";
    err.message = "サーバーの応答を解釈できませんでした";
    return false;
  }
  out.token = v.GetString("token");
  out.exp = v.GetInt("exp");
  out.refreshSecret = v.GetString("refreshSecret");
  if (out.token.empty() || out.exp == 0)
  {
    err.code = "malformed_response";
    err.message = "サーバーの応答にライセンスが含まれていません";
    return false;
  }
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Verification
// ---------------------------------------------------------------------------

const char* ReasonString(Reason r)
{
  switch (r)
  {
    case Reason::kValid: return "valid";
    case Reason::kMissing: return "missing";
    case Reason::kMalformed: return "malformed";
    case Reason::kBadHeader: return "bad_header";
    case Reason::kBadSignature: return "bad_signature";
    case Reason::kWrongProduct: return "wrong_product";
    case Reason::kExpired: return "expired";
  }
  return "unknown";
}

int64_t LicenceCheck::DaysLeft(int64_t now) const
{
  if (exp == 0) return 0;
  const int64_t remaining = exp - (now ? now : NowSeconds());
  return remaining <= 0 ? 0 : remaining / 86400;
}

LicenceCheck VerifyLicence(const std::string& token,
                           const std::string& product,
                           int64_t now,
                           const std::string& pubkeyHex)
{
  LicenceCheck result;
  if (token.empty()) { result.reason = Reason::kMissing; return result; }

  const size_t dot1 = token.find('.');
  const size_t dot2 = dot1 == std::string::npos ? std::string::npos : token.find('.', dot1 + 1);
  if (dot1 == std::string::npos || dot2 == std::string::npos ||
      token.find('.', dot2 + 1) != std::string::npos)
  {
    result.reason = Reason::kMalformed;
    return result;
  }

  const std::string headerB64 = token.substr(0, dot1);
  const std::string payloadB64 = token.substr(dot1 + 1, dot2 - dot1 - 1);
  const std::string signatureB64 = token.substr(dot2 + 1);

  std::vector<uint8_t> headerRaw, payloadRaw, signature;
  if (!Base64UrlDecode(headerB64, headerRaw) ||
      !Base64UrlDecode(payloadB64, payloadRaw) ||
      !Base64UrlDecode(signatureB64, signature))
  {
    result.reason = Reason::kMalformed;
    return result;
  }

  json::Value header;
  if (!json::Parse(std::string(headerRaw.begin(), headerRaw.end()), header))
  {
    result.reason = Reason::kMalformed;
    return result;
  }

  // Pin the algorithm before looking at the signature. Accepting whatever
  // the token names is how "alg": "none" forgeries get in.
  if (header.GetString("alg") != "EdDSA")
  {
    result.reason = Reason::kBadHeader;
    return result;
  }

  if (signature.size() != 64) { result.reason = Reason::kMalformed; return result; }

  uint8_t publicKey[32];
  const std::string keyHex = pubkeyHex.empty() ? std::string(ENI_LICENSE_PUBKEY_HEX) : pubkeyHex;
  if (!HexDecode32(keyHex, publicKey))
  {
    // A broken build constant, not a broken licence.
    result.reason = Reason::kBadSignature;
    return result;
  }

  const std::string signedPart = headerB64 + "." + payloadB64;
  if (crypto_ed25519_check(signature.data(), publicKey,
                           reinterpret_cast<const uint8_t*>(signedPart.data()),
                           signedPart.size()) != 0)
  {
    result.reason = Reason::kBadSignature;
    return result;
  }

  json::Value payload;
  if (!json::Parse(std::string(payloadRaw.begin(), payloadRaw.end()), payload))
  {
    result.reason = Reason::kMalformed;
    return result;
  }

  const int64_t exp = payload.GetInt("exp");
  if (exp == 0) { result.reason = Reason::kMalformed; return result; }
  result.exp = exp;

  if (payload.GetString("product") != product)
  {
    result.reason = Reason::kWrongProduct;
    return result;
  }

  if (exp <= (now ? now : NowSeconds()))
  {
    result.reason = Reason::kExpired;
    return result;
  }

  result.valid = true;
  result.reason = Reason::kValid;
  return result;
}

// ---------------------------------------------------------------------------
// Licence file
// ---------------------------------------------------------------------------

std::string LicenceDir()
{
  const std::string override_ = EnvOrEmpty("ENI_LICENSE_DIR");
  if (!override_.empty()) return override_;

#if defined(_WIN32)
  std::string base = EnvOrEmpty("APPDATA");
  if (base.empty()) base = Join(HomeDir(), "AppData\\Roaming");
  return Join(base, ENI_LICENSE_DIRNAME);
#elif defined(__APPLE__)
  return Join(Join(HomeDir(), "Library/Application Support"), ENI_LICENSE_DIRNAME);
#else
  std::string base = EnvOrEmpty("XDG_CONFIG_HOME");
  if (base.empty()) base = Join(HomeDir(), ".config");
  return Join(base, ENI_LICENSE_DIRNAME);
#endif
}

std::string LicenceFilePath() { return Join(LicenceDir(), ENI_LICENSE_FILENAME); }

bool LoadLicenceFile(LicenceFile& out)
{
  std::string text;
  if (!ReadWholeFile(LicenceFilePath(), text)) return false;

  json::Value root;
  if (!json::Parse(text, root) || !root.IsObject()) return false;

  out = LicenceFile();
  out.rawJson = text;
  out.refreshSecret = root.GetString("refreshSecret");

  if (const json::Value* licences = root.Find("licenses"))
  {
    if (const json::Value* mine = licences->Find(ENI_PRODUCT))
    {
      out.token = mine->GetString("token");
      out.exp = mine->GetInt("exp");
    }
  }
  return true;
}

bool SaveLicence(const LicenceFile& current,
                 const std::string& token,
                 int64_t exp,
                 const std::string& refreshSecret)
{
  // Start from whatever is on disk so a sibling instrument's licence
  // survives our write. The shared file is the whole point of the design.
  json::Value root;
  if (current.rawJson.empty() || !json::Parse(current.rawJson, root) || !root.IsObject())
    root = json::Value::Object();

  root.Set("version", json::Value(int64_t(1)));

  json::Value licences = json::Value::Object();
  if (const json::Value* existing = root.Find("licenses"))
  {
    if (existing->IsObject())
      for (const auto& kv : existing->Members()) licences.Set(kv.first, kv.second);
  }

  json::Value entry = json::Value::Object();
  entry.Set("token", json::Value(token));
  entry.Set("exp", json::Value(exp));
  licences.Set(ENI_PRODUCT, std::move(entry));
  root.Set("licenses", std::move(licences));

  // The refresh secret is only handed out on the first issue; a refresh
  // response omits it and must not wipe the stored one.
  if (!refreshSecret.empty()) root.Set("refreshSecret", json::Value(refreshSecret));

  const std::string dir = LicenceDir();
  if (!MakeDirectories(dir)) return false;

  const std::string finalPath = LicenceFilePath();
  const std::string tmpPath = finalPath + ".tmp";
  const std::string text = root.Dump(0) + "\n";

  FILE* f = fopen(tmpPath.c_str(), "wb");
  if (!f) return false;
  const bool written = fwrite(text.data(), 1, text.size(), f) == text.size();
  const bool flushed = fflush(f) == 0;
  fclose(f);
  if (!written || !flushed) { remove(tmpPath.c_str()); return false; }

#ifdef _WIN32
  // rename() will not clobber on Windows; MoveFileEx replaces atomically.
  if (!MoveFileExA(tmpPath.c_str(), finalPath.c_str(), MOVEFILE_REPLACE_EXISTING))
  {
    remove(tmpPath.c_str());
    return false;
  }
#else
  if (rename(tmpPath.c_str(), finalPath.c_str()) != 0)
  {
    remove(tmpPath.c_str());
    return false;
  }
#endif
  return true;
}

bool ClearLicence()
{
  const std::string path = LicenceFilePath();
  return remove(path.c_str()) == 0 || errno == ENOENT;
}

// ---------------------------------------------------------------------------
// Transport plumbing
// ---------------------------------------------------------------------------

void SetHttpTransport(HttpTransport t) { Transport() = std::move(t); }

// ---------------------------------------------------------------------------
// Device Flow
// ---------------------------------------------------------------------------

bool RequestDeviceCode(DeviceCode& out, Error& err)
{
  HttpRequest req;
  req.url = "https://" ENI_AUTH0_DOMAIN "/oauth/device/code";
  req.contentType = "application/x-www-form-urlencoded";
  req.body = json::FormEncode({{"client_id", ENI_AUTH0_CLIENT_ID},
                               {"scope", "openid"},
                               {"audience", ENI_AUTH0_AUDIENCE}});

  const HttpResponse res = Post(req);
  if (res.status != 200) { FillError(err, res); return false; }

  json::Value v;
  if (!json::Parse(res.body, v) || !v.IsObject())
  {
    err.code = "malformed_response";
    err.message = "ログインを開始できませんでした";
    return false;
  }

  out.deviceCode = v.GetString("device_code");
  out.userCode = v.GetString("user_code");
  out.verificationUri = v.GetString("verification_uri");
  out.verificationUriComplete = v.GetString("verification_uri_complete");
  out.interval = int(v.GetInt("interval", 5));
  out.expiresIn = int(v.GetInt("expires_in", 900));

  if (out.deviceCode.empty() || out.userCode.empty())
  {
    err.code = "malformed_response";
    err.message = "ログインを開始できませんでした";
    return false;
  }
  if (out.interval < 1) out.interval = 5;
  return true;
}

bool PollForAccessToken(const DeviceCode& dc,
                        std::string& outAccessToken,
                        Error& err,
                        const std::function<bool()>& cancelled)
{
  const int64_t deadline = NowSeconds() + (dc.expiresIn > 0 ? dc.expiresIn : 900);
  int interval = dc.interval;

  for (;;)
  {
    // Sleep in short slices so cancelling the login (window closed, user
    // gave up) is felt immediately instead of after the poll interval.
    for (int slept = 0; slept < interval; slept++)
    {
      if (cancelled && cancelled())
      {
        err.code = "cancelled";
        err.message = "ログインを中止しました";
        return false;
      }
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    if (NowSeconds() > deadline)
    {
      err.code = "expired_token";
      err.message = "ログインの有効期限が切れました。もう一度お試しください";
      return false;
    }

    HttpRequest req;
    req.url = "https://" ENI_AUTH0_DOMAIN "/oauth/token";
    req.contentType = "application/x-www-form-urlencoded";
    req.body = json::FormEncode({{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"},
                                 {"device_code", dc.deviceCode},
                                 {"client_id", ENI_AUTH0_CLIENT_ID}});

    const HttpResponse res = Post(req);

    if (res.status == 200)
    {
      json::Value v;
      if (!json::Parse(res.body, v) || v.GetString("access_token").empty())
      {
        err.code = "malformed_response";
        err.message = "ログインの応答を解釈できませんでした";
        return false;
      }
      outAccessToken = v.GetString("access_token");
      return true;
    }

    json::Value v;
    const std::string error = json::Parse(res.body, v) ? v.GetString("error") : std::string();

    // authorization_pending is the normal state while the user is still in
    // the browser; slow_down asks us to back off. Everything else is fatal.
    if (error == "authorization_pending") continue;
    if (error == "slow_down") { interval += 5; continue; }

    if (error == "access_denied")
    {
      err.code = "access_denied";
      err.message = "ログインが許可されませんでした";
    }
    else if (error == "expired_token")
    {
      err.code = "expired_token";
      err.message = "ログインの有効期限が切れました。もう一度お試しください";
    }
    else
    {
      FillError(err, res);
    }
    return false;
  }
}

bool FetchLicence(const std::string& accessToken, Issued& out, Error& err)
{
  json::Value body = json::Value::Object();
  body.Set("product", json::Value(std::string(ENI_PRODUCT)));
  body.Set("device", DeviceInfo());

  HttpRequest req;
  req.url = ENI_API_BASE "/api/license";
  req.contentType = "application/json";
  req.authorization = accessToken;
  req.body = body.Dump(-1);

  const HttpResponse res = Post(req);
  if (res.status != 200) { FillError(err, res); return false; }
  return ReadIssued(res.body, out, err);
}

bool RefreshLicence(const std::string& refreshSecret, Issued& out, Error& err)
{
  json::Value body = json::Value::Object();
  body.Set("refreshSecret", json::Value(refreshSecret));
  body.Set("product", json::Value(std::string(ENI_PRODUCT)));
  body.Set("device", DeviceInfo());

  HttpRequest req;
  req.url = ENI_API_BASE "/api/license/refresh";
  req.contentType = "application/json";
  req.body = body.Dump(-1);

  const HttpResponse res = Post(req);
  if (res.status != 200) { FillError(err, res); return false; }
  return ReadIssued(res.body, out, err);
}

// ---------------------------------------------------------------------------
// What the plug-in calls
// ---------------------------------------------------------------------------

LicenceCheck CheckLicence()
{
  LicenceFile file;
  if (!LoadLicenceFile(file) || file.token.empty())
  {
    LicenceCheck missing;
    missing.reason = Reason::kMissing;
    return missing;
  }
  return VerifyLicence(file.token, ENI_PRODUCT);
}

bool ShouldRefresh(int64_t exp, int64_t now)
{
  if (exp == 0) return false;
  const int64_t threshold = int64_t(ENI_REFRESH_THRESHOLD_DAYS) * 86400;
  return exp - (now ? now : NowSeconds()) < threshold;
}

void RefreshInBackground()
{
  // One at a time: a host that instantiates eight copies of the plug-in
  // should still make exactly one request.
  static std::atomic<bool> inFlight{false};
  bool expected = false;
  if (!inFlight.compare_exchange_strong(expected, true)) return;

  std::thread([] {
    LicenceFile file;
    if (LoadLicenceFile(file) && !file.refreshSecret.empty())
    {
      Issued issued;
      Error err;
      if (RefreshLicence(file.refreshSecret, issued, err))
        SaveLicence(file, issued.token, issued.exp, issued.refreshSecret);
      // Failure is silent on purpose: offline is the common case, and a
      // cancelled subscription should simply stop being renewed.
    }
    inFlight.store(false);
  }).detach();
}

bool RunDeviceFlow(const std::function<void(const DeviceCode&)>& onCode,
                   Error& err,
                   const std::function<bool()>& cancelled)
{
  DeviceCode dc;
  if (!RequestDeviceCode(dc, err)) return false;

  if (onCode) onCode(dc);

  // Opening the browser can fail on a locked-down machine; the UI still
  // shows the URL and the code, so this is not fatal.
  OpenInBrowser(dc.verificationUriComplete.empty() ? dc.verificationUri
                                                   : dc.verificationUriComplete);

  std::string accessToken;
  if (!PollForAccessToken(dc, accessToken, err, cancelled)) return false;

  Issued issued;
  if (!FetchLicence(accessToken, issued, err)) return false;

  LicenceFile file;
  LoadLicenceFile(file); // may not exist yet; SaveLicence copes either way
  if (!SaveLicence(file, issued.token, issued.exp, issued.refreshSecret))
  {
    err.code = "write_failed";
    err.message = "ライセンスの保存に失敗しました（保存先の権限をご確認ください）";
    return false;
  }
  return true;
}

bool OpenInBrowser(const std::string& url)
{
#if defined(_WIN32)
  const HINSTANCE rc = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(rc) > 32;
#elif defined(__APPLE__)
  // Handed to /usr/bin/open through posix_spawn rather than system(), so a
  // URL can never be interpreted by a shell.
  const std::string quoted = url;
  const char* argv[] = {"/usr/bin/open", quoted.c_str(), nullptr};
  const pid_t pid = fork();
  if (pid == 0)
  {
    execv(argv[0], const_cast<char* const*>(argv));
    _exit(127);
  }
  return pid > 0;
#else
  const char* argv[] = {"/usr/bin/xdg-open", url.c_str(), nullptr};
  const pid_t pid = fork();
  if (pid == 0)
  {
    execv(argv[0], const_cast<char* const*>(argv));
    _exit(127);
  }
  return pid > 0;
#endif
}

} // namespace eni
