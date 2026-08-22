// HTTPS POST over WinHTTP.
//
// WinHTTP rather than WinINet or libcurl: it ships with Windows, is
// supported in services and background threads, and adds no DLL to the
// installer. The plug-in makes at most a handful of requests a month, so
// there is nothing to gain from a connection-pooling library.

#ifdef _WIN32

  #include "eni_auth.h"

  #include <windows.h>
  #include <winhttp.h>

  #pragma comment(lib, "winhttp.lib")

namespace eni {

namespace {

std::wstring Widen(const std::string& s)
{
  if (s.empty()) return std::wstring();
  const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
  std::wstring out(size_t(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &out[0], len);
  return out;
}

struct UrlParts
{
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
  bool secure = true;
  bool ok = false;
};

UrlParts SplitUrl(const std::string& url)
{
  UrlParts parts;
  const std::wstring wide = Widen(url);

  URL_COMPONENTS uc = {};
  uc.dwStructSize = sizeof(uc);
  uc.dwHostNameLength = DWORD(-1);
  uc.dwUrlPathLength = DWORD(-1);
  uc.dwExtraInfoLength = DWORD(-1);

  if (!WinHttpCrackUrl(wide.c_str(), DWORD(wide.size()), 0, &uc)) return parts;

  parts.host.assign(uc.lpszHostName, uc.dwHostNameLength);
  parts.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
  if (uc.dwExtraInfoLength) parts.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
  if (parts.path.empty()) parts.path = L"/";
  parts.port = uc.nPort;
  parts.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
  parts.ok = true;
  return parts;
}

} // namespace

HttpResponse PlatformHttpPost(const HttpRequest& req)
{
  HttpResponse out;

  const UrlParts url = SplitUrl(req.url);
  if (!url.ok) { out.error = "URL を解釈できませんでした"; return out; }

  // The licence server is TLS-only; refuse to send a bearer token in clear
  // even if a build somehow points at http://.
  if (!url.secure) { out.error = "HTTPS 以外には接続しません"; return out; }

  const HINTERNET session = WinHttpOpen(L"easy-and-nice-instruments/1.0",
                                        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session) { out.error = "ネットワークを初期化できませんでした"; return out; }

  const DWORD timeoutMs = DWORD(req.timeoutSeconds > 0 ? req.timeoutSeconds : 15) * 1000;
  WinHttpSetTimeouts(session, int(timeoutMs), int(timeoutMs), int(timeoutMs), int(timeoutMs));

  const HINTERNET connect = WinHttpConnect(session, url.host.c_str(), url.port, 0);
  if (!connect)
  {
    WinHttpCloseHandle(session);
    out.error = "サーバーに接続できませんでした";
    return out;
  }

  const HINTERNET request = WinHttpOpenRequest(connect, L"POST", url.path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
  if (!request)
  {
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    out.error = "リクエストを作成できませんでした";
    return out;
  }

  std::wstring headers = L"Content-Type: " + Widen(req.contentType) + L"\r\n";
  if (!req.authorization.empty())
    headers += L"Authorization: Bearer " + Widen(req.authorization) + L"\r\n";

  BOOL ok = WinHttpSendRequest(request, headers.c_str(), DWORD(-1),
                               const_cast<char*>(req.body.data()), DWORD(req.body.size()),
                               DWORD(req.body.size()), 0);
  if (ok) ok = WinHttpReceiveResponse(request, nullptr);

  if (ok)
  {
    DWORD status = 0;
    DWORD size = sizeof(status);
    if (WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX))
      out.status = int(status);

    for (;;)
    {
      DWORD available = 0;
      if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
      std::string chunk(size_t(available), '\0');
      DWORD read = 0;
      if (!WinHttpReadData(request, &chunk[0], available, &read)) break;
      out.body.append(chunk.data(), size_t(read));
    }
  }
  else
  {
    out.error = "サーバーに接続できませんでした";
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connect);
  WinHttpCloseHandle(session);
  return out;
}

} // namespace eni

#endif // _WIN32
