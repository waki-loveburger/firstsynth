// Fallback transport for platforms with no native client compiled in
// (Linux builds, and the standalone test binary, which injects its own).
//
// It fails rather than falling back to something like a shelled-out curl:
// a licence check that silently changes how it talks to the server is worse
// than one that plainly says it cannot.

#if !defined(_WIN32) && !defined(__APPLE__)

  #include "eni_auth.h"

namespace eni {

HttpResponse PlatformHttpPost(const HttpRequest&)
{
  HttpResponse out;
  out.status = 0;
  out.error = "このプラットフォームでは通信を行えません";
  return out;
}

} // namespace eni

#endif
