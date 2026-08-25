// Tests for the licence check. No framework: one binary, one CHECK macro,
// no network and no dependency on the machine's real licence file (every
// test points ENI_LICENSE_DIR at a scratch directory).
//
// Test list (written before the code, kept in sync with the Python twin in
// 8-Control's tests/test_eni_auth.py):
//
// ## Verification
// - [x] a licence signed by our key, for this product, in date, is valid
// - [x] a tampered signature is rejected (a data bit, not the padding bits)
// - [x] a tampered payload keeping the old signature is rejected
// - [x] a licence signed by another key is rejected
// - [x] "alg": "none" is rejected before the signature is even looked at
// - [x] a licence for a sibling product is rejected
// - [x] an expired licence is rejected but still reports its exp
// - [x] garbage, empty strings and wrong segment counts are rejected
// - [x] a payload with no exp is malformed, not "valid forever"
// - [x] an absurd exp (1e400) cannot become a licence that never expires
// - [x] DaysLeft reports the remaining whole days
//
// ## Licence file
// - [x] a missing file reads as missing, not as an error
// - [x] round-trips a licence through save and load
// - [x] a save keeps a sibling product's entry intact
// - [x] a refresh (no new secret) keeps the stored refresh secret
// - [x] a corrupt file reads as missing (fails closed)
// - [x] clearing removes the file, and clearing twice is not an error
//
// ## Protocol (through an injected transport)
// - [x] the device-code request returns the user code and both URLs
// - [x] fetching a licence sends product and device info, returns the token
// - [x] a 403 no_subscription surfaces as that code with a Japanese message
// - [x] a refresh sends the secret and returns the new licence
// - [x] a 401 invalid_secret surfaces as that code
// - [x] a network failure (status 0) surfaces as code "network"
// - [x] a malformed 200 response is an error, not a licence
//
// ## Renewal policy
// - [x] renews inside the 14-day window, leaves it alone outside
// - [x] never renews when there is no expiry
//
// ## JSON
// - [x] parses nested objects, escapes, unicode and negative numbers
// - [x] rejects trailing garbage, unterminated strings, lone surrogates
// - [x] form-encodes the characters Auth0 actually sends us

#include "../eni_auth.h"
#include "../eni_config.h"
#include "../eni_json.h"
#include "../vendor/monocypher/monocypher-ed25519.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#ifdef _WIN32
  #include <direct.h>
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace {

int gChecks = 0;
int gFailures = 0;
const char* gCurrentTest = "";

void Check(bool condition, const char* expr, int line)
{
  gChecks++;
  if (!condition)
  {
    gFailures++;
    printf("  FAIL  %s:%d  %s\n", gCurrentTest, line, expr);
  }
}

#define CHECK(expr) Check((expr), #expr, __LINE__)
#define CHECK_EQ(a, b) Check((a) == (b), #a " == " #b, __LINE__)
#define TEST(name) gCurrentTest = name; printf("  %s\n", name);

int64_t Now()
{
  return int64_t(time(nullptr));
}

// Some other product of the label - whichever this build is not. Used by the
// "a licence for a sibling product is rejected" case, which has to work
// unchanged in every copy of the library.
const char* SiblingProduct()
{
  return strcmp(ENI_PRODUCT, "firstsynth") == 0 ? "suikinkutsu" : "firstsynth";
}

// --- signing helpers (the tests own a throwaway key pair) -----------------

std::string Base64UrlEncode(const uint8_t* data, size_t len)
{
  static const char* kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  for (size_t i = 0; i < len; i += 3)
  {
    const uint32_t a = data[i];
    const uint32_t b = i + 1 < len ? data[i + 1] : 0;
    const uint32_t c = i + 2 < len ? data[i + 2] : 0;
    const uint32_t triple = (a << 16) | (b << 8) | c;
    out += kAlphabet[(triple >> 18) & 0x3F];
    out += kAlphabet[(triple >> 12) & 0x3F];
    if (i + 1 < len) out += kAlphabet[(triple >> 6) & 0x3F];
    if (i + 2 < len) out += kAlphabet[triple & 0x3F];
  }
  return out;
}

std::string Base64UrlEncode(const std::string& s)
{
  return Base64UrlEncode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
}

std::string HexEncode(const uint8_t* data, size_t len)
{
  static const char* kHex = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < len; i++)
  {
    out += kHex[data[i] >> 4];
    out += kHex[data[i] & 0x0F];
  }
  return out;
}

struct KeyPair
{
  uint8_t secret[64];
  uint8_t publicKey[32];
  std::string publicHex;
};

KeyPair MakeKeyPair(uint8_t seedByte)
{
  KeyPair kp;
  uint8_t seed[32];
  memset(seed, seedByte, sizeof(seed));
  crypto_ed25519_key_pair(kp.secret, kp.publicKey, seed);
  kp.publicHex = HexEncode(kp.publicKey, 32);
  return kp;
}

std::string MakeToken(const KeyPair& kp,
                      const std::string& product,
                      int64_t exp,
                      const std::string& alg = "EdDSA",
                      bool omitExp = false,
                      const std::string& rawExp = std::string())
{
  const std::string header = "{\"alg\":\"" + alg + "\",\"typ\":\"JWT\"}";
  std::string payload = "{\"sub\":\"auth0|test\",\"product\":\"" + product + "\"";
  if (!rawExp.empty()) payload += ",\"exp\":" + rawExp;
  else if (!omitExp) payload += ",\"exp\":" + std::to_string(exp);
  payload += ",\"iat\":" + std::to_string(exp - 86400) + "}";

  const std::string signedPart = Base64UrlEncode(header) + "." + Base64UrlEncode(payload);
  uint8_t signature[64];
  crypto_ed25519_sign(signature, kp.secret,
                      reinterpret_cast<const uint8_t*>(signedPart.data()), signedPart.size());
  return signedPart + "." + Base64UrlEncode(signature, 64);
}

// --- scratch licence directory -------------------------------------------

std::string gScratchDir;

// Never touch the machine's real licence: every file test runs against a
// throwaway directory pointed at by ENI_LICENSE_DIR.
void UseScratchDir()
{
  if (gScratchDir.empty())
  {
#ifdef _WIN32
    char temp[MAX_PATH] = {0};
    GetTempPathA(MAX_PATH, temp);
    gScratchDir = std::string(temp) + "eni_auth_test";
    _mkdir(gScratchDir.c_str());
#else
    char tmpl[] = "/tmp/eni_auth_test_XXXXXX";
    gScratchDir = mkdtemp(tmpl);
#endif
  }

#ifdef _WIN32
  _putenv_s("ENI_LICENSE_DIR", gScratchDir.c_str());
#else
  setenv("ENI_LICENSE_DIR", gScratchDir.c_str(), 1);
#endif
  remove(eni::LicenceFilePath().c_str());
}

void WriteRawLicenceFile(const std::string& text)
{
  FILE* f = fopen(eni::LicenceFilePath().c_str(), "wb");
  if (!f) return;
  fwrite(text.data(), 1, text.size(), f);
  fclose(f);
}

std::string ReadRawLicenceFile()
{
  FILE* f = fopen(eni::LicenceFilePath().c_str(), "rb");
  if (!f) return std::string();
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  fclose(f);
  return out;
}

// --- fake transport -------------------------------------------------------

struct FakeCall
{
  std::string url;
  std::string body;
  std::string authorization;
  std::string contentType;
};

std::vector<FakeCall> gCalls;

void StubTransport(int status, const std::string& body)
{
  gCalls.clear();
  eni::SetHttpTransport([status, body](const eni::HttpRequest& req) {
    gCalls.push_back({req.url, req.body, req.authorization, req.contentType});
    eni::HttpResponse res;
    res.status = status;
    res.body = body;
    if (status == 0) res.error = "boom";
    return res;
  });
}

// ---------------------------------------------------------------------------

void TestVerification()
{
  printf("\nVerification\n");
  const KeyPair ours = MakeKeyPair(0x11);
  const KeyPair theirs = MakeKeyPair(0x22);
  const int64_t future = Now() + 30 * 86400;

  TEST("a valid licence for this product passes");
  {
    const std::string token = MakeToken(ours, ENI_PRODUCT, future);
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(check.valid);
    CHECK(check.reason == eni::Reason::kValid);
    CHECK_EQ(check.exp, future);
  }

  TEST("a tampered signature is rejected");
  {
    // Flip a character in the middle of the signature, not the last one:
    // the final base64url character of a 64-byte signature carries four
    // unused padding bits, so changing it can decode to the very same
    // bytes and prove nothing.
    std::string token = MakeToken(ours, ENI_PRODUCT, future);
    const size_t sigStart = token.rfind('.') + 1;
    const size_t victim = sigStart + (token.size() - sigStart) / 2;
    token[victim] = token[victim] == 'A' ? 'B' : 'A';
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(!check.valid);
    CHECK(check.reason == eni::Reason::kBadSignature);
  }

  TEST("a tampered payload is rejected");
  {
    // The attack that matters: keep the signature, extend the expiry.
    const std::string token = MakeToken(ours, ENI_PRODUCT, Now() - 60);
    const size_t dot1 = token.find('.');
    const size_t dot2 = token.find('.', dot1 + 1);
    const std::string forgedPayload =
      Base64UrlEncode(std::string("{\"sub\":\"auth0|test\",\"product\":\"" ENI_PRODUCT
                                  "\",\"exp\":9999999999}"));
    const std::string forged =
      token.substr(0, dot1 + 1) + forgedPayload + token.substr(dot2);
    const eni::LicenceCheck check = eni::VerifyLicence(forged, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(!check.valid);
    CHECK(check.reason == eni::Reason::kBadSignature);
  }

  TEST("a licence signed by another key is rejected");
  {
    const std::string token = MakeToken(theirs, ENI_PRODUCT, future);
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(check.reason == eni::Reason::kBadSignature);
  }

  TEST("alg=none is rejected at the header");
  {
    const std::string token = MakeToken(ours, ENI_PRODUCT, future, "none");
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(check.reason == eni::Reason::kBadHeader);
  }

  TEST("a licence for a sibling product is rejected");
  {
    // A real sibling slug, but never this build's own - the same test file
    // ships in every product's copy of the library, so naming one product
    // outright would make the test pass here and fail there.
    const std::string token = MakeToken(ours, SiblingProduct(), future);
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(check.reason == eni::Reason::kWrongProduct);
    CHECK_EQ(check.exp, future); // still known: the signature was good
  }

  TEST("an expired licence is rejected but reports its exp");
  {
    const int64_t past = Now() - 60;
    const std::string token = MakeToken(ours, ENI_PRODUCT, past);
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(!check.valid);
    CHECK(check.reason == eni::Reason::kExpired);
    CHECK_EQ(check.exp, past);
    CHECK_EQ(check.DaysLeft(), 0);
  }

  TEST("malformed input is rejected");
  {
    CHECK(eni::VerifyLicence("", ENI_PRODUCT, 0, ours.publicHex).reason == eni::Reason::kMissing);
    CHECK(eni::VerifyLicence("garbage", ENI_PRODUCT, 0, ours.publicHex).reason == eni::Reason::kMalformed);
    CHECK(eni::VerifyLicence("a.b", ENI_PRODUCT, 0, ours.publicHex).reason == eni::Reason::kMalformed);
    CHECK(eni::VerifyLicence("a.b.c.d", ENI_PRODUCT, 0, ours.publicHex).reason == eni::Reason::kMalformed);
    CHECK(eni::VerifyLicence("!!.!!.!!", ENI_PRODUCT, 0, ours.publicHex).reason == eni::Reason::kMalformed);
  }

  TEST("a payload with no exp is malformed, not valid");
  {
    const std::string token = MakeToken(ours, ENI_PRODUCT, future, "EdDSA", true);
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(!check.valid);
    CHECK(check.reason == eni::Reason::kMalformed);
  }

  TEST("an absurd exp cannot become an eternal licence");
  {
    const std::string token = MakeToken(ours, ENI_PRODUCT, 0, "EdDSA", false, "1e400");
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, 0, ours.publicHex);
    CHECK(!check.valid);
    CHECK(check.reason == eni::Reason::kMalformed);
  }

  TEST("DaysLeft counts whole days");
  {
    const int64_t now = Now();
    const std::string token = MakeToken(ours, ENI_PRODUCT, now + 3 * 86400 + 3600);
    const eni::LicenceCheck check = eni::VerifyLicence(token, ENI_PRODUCT, now, ours.publicHex);
    CHECK(check.valid);
    CHECK_EQ(check.DaysLeft(now), 3);
  }
}

void TestLicenceFile()
{
  printf("\nLicence file\n");

  TEST("a missing file reads as missing");
  {
    UseScratchDir();
    eni::LicenceFile file;
    CHECK(!eni::LoadLicenceFile(file));
    CHECK(eni::CheckLicence().reason == eni::Reason::kMissing);
  }

  TEST("a licence round-trips through save and load");
  {
    UseScratchDir();
    eni::LicenceFile empty;
    CHECK(eni::SaveLicence(empty, "token-value", 1790000000, "enis_secret"));

    eni::LicenceFile loaded;
    CHECK(eni::LoadLicenceFile(loaded));
    CHECK_EQ(loaded.token, std::string("token-value"));
    CHECK_EQ(loaded.exp, int64_t(1790000000));
    CHECK_EQ(loaded.refreshSecret, std::string("enis_secret"));
  }

  TEST("a save keeps a sibling product's entry");
  {
    UseScratchDir();
    WriteRawLicenceFile(
      "{\"version\":1,\"refreshSecret\":\"enis_old\","
      "\"licenses\":{\"8-control\":{\"token\":\"other\",\"exp\":1780000000}}}");

    eni::LicenceFile file;
    CHECK(eni::LoadLicenceFile(file));
    CHECK(file.token.empty()); // nothing for us yet
    CHECK(eni::SaveLicence(file, "mine", 1790000000, ""));

    const std::string raw = ReadRawLicenceFile();
    CHECK(raw.find("8-control") != std::string::npos);
    CHECK(raw.find("other") != std::string::npos);
    CHECK(raw.find(ENI_PRODUCT) != std::string::npos);
  }

  TEST("a refresh without a new secret keeps the stored one");
  {
    UseScratchDir();
    eni::LicenceFile empty;
    CHECK(eni::SaveLicence(empty, "first", 1780000000, "enis_keepme"));

    eni::LicenceFile stored;
    CHECK(eni::LoadLicenceFile(stored));
    CHECK(eni::SaveLicence(stored, "second", 1790000000, "")); // refresh response

    eni::LicenceFile after;
    CHECK(eni::LoadLicenceFile(after));
    CHECK_EQ(after.refreshSecret, std::string("enis_keepme"));
    CHECK_EQ(after.token, std::string("second"));
  }

  TEST("a corrupt file reads as missing");
  {
    UseScratchDir();
    WriteRawLicenceFile("{ this is not json");
    eni::LicenceFile file;
    CHECK(!eni::LoadLicenceFile(file));
    CHECK(eni::CheckLicence().reason == eni::Reason::kMissing);
  }

  TEST("clearing removes the file and is idempotent");
  {
    UseScratchDir();
    eni::LicenceFile empty;
    CHECK(eni::SaveLicence(empty, "token", 1790000000, "enis_x"));
    CHECK(eni::ClearLicence());
    CHECK(eni::ClearLicence()); // already gone is not a failure
    eni::LicenceFile file;
    CHECK(!eni::LoadLicenceFile(file));
  }
}

void TestProtocol()
{
  printf("\nProtocol\n");

  TEST("the device-code request returns the code and URLs");
  {
    StubTransport(200,
      "{\"device_code\":\"dc-123\",\"user_code\":\"ABCD-EFGH\","
      "\"verification_uri\":\"https://auth.easyandnicewaki.com/activate\","
      "\"verification_uri_complete\":\"https://auth.easyandnicewaki.com/activate?user_code=ABCD-EFGH\","
      "\"interval\":5,\"expires_in\":900}");

    eni::DeviceCode dc;
    eni::Error err;
    CHECK(eni::RequestDeviceCode(dc, err));
    CHECK_EQ(dc.userCode, std::string("ABCD-EFGH"));
    CHECK_EQ(dc.deviceCode, std::string("dc-123"));
    CHECK(dc.verificationUriComplete.find("ABCD-EFGH") != std::string::npos);
    CHECK_EQ(dc.interval, 5);
    CHECK_EQ(gCalls.size(), size_t(1));
    CHECK(gCalls[0].url.find("/oauth/device/code") != std::string::npos);
    CHECK(gCalls[0].body.find(ENI_AUTH0_CLIENT_ID) != std::string::npos);
  }

  TEST("fetching a licence sends product and device info");
  {
    StubTransport(200, "{\"token\":\"jwt-here\",\"exp\":1790000000,\"refreshSecret\":\"enis_new\"}");

    eni::Issued issued;
    eni::Error err;
    CHECK(eni::FetchLicence("access-token", issued, err));
    CHECK_EQ(issued.token, std::string("jwt-here"));
    CHECK_EQ(issued.exp, int64_t(1790000000));
    CHECK_EQ(issued.refreshSecret, std::string("enis_new"));
    CHECK_EQ(gCalls[0].authorization, std::string("access-token"));
    CHECK(gCalls[0].url.find("/api/license") != std::string::npos);
    CHECK(gCalls[0].body.find(ENI_PRODUCT) != std::string::npos);
    CHECK(gCalls[0].body.find("\"device\"") != std::string::npos);
    CHECK(gCalls[0].body.find(ENI_APP_VERSION) != std::string::npos);
  }

  TEST("403 no_subscription surfaces as that code");
  {
    StubTransport(403, "{\"error\":\"有効なサブスクリプションがありません\",\"code\":\"no_subscription\"}");
    eni::Issued issued;
    eni::Error err;
    CHECK(!eni::FetchLicence("access-token", issued, err));
    CHECK_EQ(err.code, std::string("no_subscription"));
    CHECK_EQ(err.status, 403);
    CHECK(!err.message.empty());
  }

  TEST("a refresh sends the secret and returns the new licence");
  {
    StubTransport(200, "{\"token\":\"jwt-2\",\"exp\":1795000000}");
    eni::Issued issued;
    eni::Error err;
    CHECK(eni::RefreshLicence("enis_secret", issued, err));
    CHECK_EQ(issued.token, std::string("jwt-2"));
    CHECK(issued.refreshSecret.empty()); // refresh does not re-issue one
    CHECK(gCalls[0].url.find("/api/license/refresh") != std::string::npos);
    CHECK(gCalls[0].body.find("enis_secret") != std::string::npos);
    CHECK(gCalls[0].authorization.empty()); // Auth0 is not involved
  }

  TEST("401 invalid_secret surfaces as that code");
  {
    StubTransport(401, "{\"error\":\"更新用シークレットが無効です\",\"code\":\"invalid_secret\"}");
    eni::Issued issued;
    eni::Error err;
    CHECK(!eni::RefreshLicence("enis_bogus", issued, err));
    CHECK_EQ(err.code, std::string("invalid_secret"));
  }

  TEST("a network failure surfaces as code network");
  {
    StubTransport(0, "");
    eni::Issued issued;
    eni::Error err;
    CHECK(!eni::RefreshLicence("enis_secret", issued, err));
    CHECK_EQ(err.code, std::string("network"));
  }

  TEST("a malformed 200 is an error, not a licence");
  {
    StubTransport(200, "{\"unexpected\":true}");
    eni::Issued issued;
    eni::Error err;
    CHECK(!eni::FetchLicence("access-token", issued, err));
    CHECK_EQ(err.code, std::string("malformed_response"));
  }

  eni::SetHttpTransport({});
}

void TestRenewalPolicy()
{
  printf("\nRenewal policy\n");
  const int64_t now = Now();

  TEST("renews inside the window, not outside");
  {
    CHECK(eni::ShouldRefresh(now + 3 * 86400, now));
    CHECK(eni::ShouldRefresh(now + 13 * 86400, now));
    CHECK(!eni::ShouldRefresh(now + 20 * 86400, now));
    CHECK(eni::ShouldRefresh(now - 86400, now)); // already lapsed: try anyway
  }

  TEST("never renews without an expiry");
  {
    CHECK(!eni::ShouldRefresh(0, now));
  }
}

void TestJson()
{
  printf("\nJSON\n");

  TEST("parses nested objects, escapes and numbers");
  {
    eni::json::Value v;
    CHECK(eni::json::Parse(
      "{\"a\":{\"b\":[1,-2,3.5,true,null]},\"s\":\"x\\ny\\u3042\",\"n\":1790000000}", v));
    CHECK_EQ(v.GetInt("n"), int64_t(1790000000));
    CHECK_EQ(v.GetString("s"), std::string("x\ny\xE3\x81\x82"));
    const eni::json::Value* a = v.Find("a");
    CHECK(a != nullptr);
    CHECK_EQ(a->Find("b")->Items().size(), size_t(5));
  }

  TEST("rejects malformed input");
  {
    eni::json::Value v;
    CHECK(!eni::json::Parse("{\"a\":1} trailing", v));
    CHECK(!eni::json::Parse("{\"a\":\"unterminated}", v));
    CHECK(!eni::json::Parse("{\"a\":\"\\uD800\"}", v)); // lone surrogate
    CHECK(!eni::json::Parse("{\"a\":01}", v));
    CHECK(!eni::json::Parse("", v));
  }

  TEST("form-encodes reserved characters");
  {
    const std::string encoded = eni::json::FormEncode(
      {{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}, {"x", "a b+c"}});
    CHECK(encoded.find("urn%3Aietf%3Aparams") != std::string::npos);
    CHECK(encoded.find("a%20b%2Bc") != std::string::npos);
  }
}

} // namespace

int main()
{
  printf("eni_auth tests (product=%s)\n", ENI_PRODUCT);

  TestVerification();
  TestLicenceFile();
  TestProtocol();
  TestRenewalPolicy();
  TestJson();

  printf("\n%d checks, %d failures\n", gChecks, gFailures);
  return gFailures == 0 ? 0 : 1;
}
