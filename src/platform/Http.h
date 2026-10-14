#pragma once

#include <string>

// One HTTPS GET for the update check. The body is small (a release object), so
// the whole response is read into memory and returned.
//
// The request always validates the server certificate; there is no option to
// turn that off. Callers run this off the UI thread, because it blocks until the
// response arrives or the timeout expires. Every failure is reported in the
// result instead of thrown, so a machine with no network degrades to "no answer"
// rather than an error at start-up.
struct HttpGetResult {
  bool ok = false;    // an HTTP response arrived; read status for what it said
  int status = 0;     // HTTP status code, 0 when no response arrived
  std::string body;   // response body; may hold the API error text on a bad status
  std::string error;  // why no response arrived, for the log
};

// Sends "User-Agent: <userAgent>" and "Accept: application/vnd.github+json",
// follows redirects, and gives up after timeoutSeconds. The GitHub API rejects a
// request with no User-Agent, so the caller must pass one.
HttpGetResult httpGet(const std::string &url, const std::string &userAgent, int timeoutSeconds);
