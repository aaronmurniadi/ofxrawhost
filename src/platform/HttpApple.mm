// Apple body of platform/Http.h: Foundation's NSURLSession, which is part of the
// system, so the build needs no HTTP library. Objective-C++ because the header
// interface is a C++ struct.

#include "platform/Http.h"

#import <Foundation/Foundation.h>

#include <memory>
#include <mutex>
#include <string>

namespace {

// The completion handler runs on the session's queue and can land after this
// thread gives up waiting, so the result lives on the heap behind a mutex
// instead of in a local the handler would outlive.
struct SharedResult {
  std::mutex mutex;
  HttpGetResult result;
};

}  // namespace

HttpGetResult httpGet(const std::string &url, const std::string &userAgent, int timeoutSeconds) {
  HttpGetResult out;

  @autoreleasepool {
    NSURL *requestUrl = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (!requestUrl) {
      out.error = "the URL could not be parsed";
      return out;
    }

    NSMutableURLRequest *request = [NSMutableURLRequest requestWithURL:requestUrl];
    request.HTTPMethod = @"GET";
    request.timeoutInterval = (NSTimeInterval)timeoutSeconds;
    [request setValue:[NSString stringWithUTF8String:userAgent.c_str()] forHTTPHeaderField:@"User-Agent"];
    [request setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];

    // An ephemeral session keeps cookies and caches out of the user's profile.
    NSURLSessionConfiguration *config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
    config.timeoutIntervalForRequest = (NSTimeInterval)timeoutSeconds;
    config.timeoutIntervalForResource = (NSTimeInterval)timeoutSeconds;
    NSURLSession *session = [NSURLSession sessionWithConfiguration:config];

    // The completion handler runs on the session's own queue, so this thread
    // only waits on the semaphore and needs no run loop.
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    std::shared_ptr<SharedResult> shared = std::make_shared<SharedResult>();
    NSURLSessionDataTask *task = [session
        dataTaskWithRequest:request
          completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
            {
              std::lock_guard<std::mutex> lock(shared->mutex);
              if (error) {
                shared->result.ok = false;
                shared->result.error = "no answer arrived";
              } else {
                shared->result.ok = true;
                if ([response isKindOfClass:[NSHTTPURLResponse class]])
                  shared->result.status = (int)[(NSHTTPURLResponse *)response statusCode];
                if (data.length > 0) shared->result.body.assign((const char *)data.bytes, (size_t)data.length);
              }
            }
            dispatch_semaphore_signal(done);
          }];
    [task resume];

    // Wait a little past the request timeout, so a session that hangs gives up
    // here rather than leaving the update check waiting on this thread.
    const int64_t waitNs = (int64_t)(timeoutSeconds + 2) * (int64_t)NSEC_PER_SEC;
    const bool answered = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, waitNs)) == 0;
    {
      std::lock_guard<std::mutex> lock(shared->mutex);
      out = shared->result;
    }
    if (!answered && !out.ok) out.error = "the request timed out";
    [session finishTasksAndInvalidate];
  }

  return out;
}
