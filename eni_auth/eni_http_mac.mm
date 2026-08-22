// HTTPS POST over NSURLSession.
//
// Objective-C++ because that is what Apple's supported HTTP API is written
// in; the CFNetwork C API that would have kept this a plain .cpp has been
// deprecated since 10.11. iPlug2's macOS targets already compile .mm, so
// this costs nothing in the plug-in build.
//
// The request is made synchronously (a semaphore around the async call).
// Always run it on a worker thread - never the audio thread, never the
// message thread. The completion handler is delivered on one of the
// session's own background queues, so waiting here cannot deadlock against
// the main queue.

#if defined(__APPLE__)

  #import <Foundation/Foundation.h>

  #include "eni_auth.h"

namespace eni {

HttpResponse PlatformHttpPost(const HttpRequest& req)
{
  HttpResponse out;

  @autoreleasepool
  {
    NSString* urlString = [NSString stringWithUTF8String:req.url.c_str()];
    NSURL* url = urlString ? [NSURL URLWithString:urlString] : nil;
    if (!url) { out.error = "URL を解釈できませんでした"; return out; }

    // Same rule as the Windows path: a bearer token never goes out in the
    // clear, whatever a build constant might say.
    if (![[url scheme] isEqualToString:@"https"])
    {
      out.error = "HTTPS 以外には接続しません";
      return out;
    }

    const NSTimeInterval timeout = req.timeoutSeconds > 0 ? req.timeoutSeconds : 15;

    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:url
                                                          cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                      timeoutInterval:timeout];
    [request setHTTPMethod:@"POST"];
    [request setValue:[NSString stringWithUTF8String:req.contentType.c_str()]
        forHTTPHeaderField:@"Content-Type"];
    [request setValue:@"easy-and-nice-instruments/1.0" forHTTPHeaderField:@"User-Agent"];
    if (!req.authorization.empty())
    {
      NSString* value = [NSString stringWithFormat:@"Bearer %s", req.authorization.c_str()];
      [request setValue:value forHTTPHeaderField:@"Authorization"];
    }
    [request setHTTPBody:[NSData dataWithBytes:req.body.data() length:req.body.size()]];

    __block NSData* responseData = nil;
    __block NSHTTPURLResponse* httpResponse = nil;
    __block NSError* error = nil;

    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    NSURLSessionDataTask* task = [[NSURLSession sharedSession]
      dataTaskWithRequest:request
        completionHandler:^(NSData* data, NSURLResponse* response, NSError* taskError) {
          responseData = [data retain];
          httpResponse = (NSHTTPURLResponse*)[response retain];
          error = [taskError retain];
          dispatch_semaphore_signal(done);
        }];
    [task resume];

    // The timeout above governs the request itself; this wait is a backstop
    // a few seconds longer so the thread cannot be pinned if the callback
    // never arrives at all.
    const dispatch_time_t deadline =
      dispatch_time(DISPATCH_TIME_NOW, (int64_t)((timeout + 5) * NSEC_PER_SEC));
    if (dispatch_semaphore_wait(done, deadline) != 0)
    {
      [task cancel];
      out.error = "サーバーの応答がありませんでした";
      return out;
    }

    if (httpResponse) out.status = (int)[httpResponse statusCode];
    if (responseData && [responseData length] > 0)
      out.body.assign((const char*)[responseData bytes], [responseData length]);

    if (out.status == 0)
      out.error = error ? [[error localizedDescription] UTF8String]
                        : "サーバーに接続できませんでした";

    [responseData release];
    [httpResponse release];
    [error release];
  }

  return out;
}

} // namespace eni

#endif // __APPLE__
