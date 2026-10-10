// Windows and Linux body of platform/Http.h. The Apple implementation lives in
// HttpApple.mm, because it uses Foundation and must be compiled as Objective-C++.

#include "platform/Http.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <winhttp.h>

#include <string>
#include <vector>

namespace {

// Closes a WinHTTP handle on every return path.
struct WinHttpHandle {
  HINTERNET handle = nullptr;
  ~WinHttpHandle() {
    if (handle) WinHttpCloseHandle(handle);
  }
};

std::wstring widen(const std::string &text) { return std::wstring(text.begin(), text.end()); }

}  // namespace

HttpGetResult httpGet(const std::string &url, const std::string &userAgent, int timeoutSeconds) {
  HttpGetResult out;

  // Split the URL into host, port, scheme, and path. A URL the API does not
  // produce (no host or no path) is a programming error, not a network failure.
  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof parts;
  wchar_t host[256] = {};
  wchar_t path[2048] = {};
  parts.lpszHostName = host;
  parts.dwHostNameLength = (DWORD)(sizeof host / sizeof host[0]);
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = (DWORD)(sizeof path / sizeof path[0]);
  const std::wstring wideUrl = widen(url);
  if (!WinHttpCrackUrl(wideUrl.c_str(), (DWORD)wideUrl.size(), 0, &parts)) {
    out.error = "the URL could not be parsed";
    return out;
  }

  // The proxy comes from the system settings, so the check works behind a
  // corporate proxy the same way a browser does.
  const std::wstring agent = widen(userAgent);
  WinHttpHandle session{
      WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
  if (!session.handle) {
    out.error = "the network library could not start";
    return out;
  }
  const int timeoutMs = timeoutSeconds * 1000;
  WinHttpSetTimeouts(session.handle, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

  WinHttpHandle connection{WinHttpConnect(session.handle, host, parts.nPort, 0)};
  if (!connection.handle) {
    out.error = "the host could not be reached";
    return out;
  }

  // A https URL gets the secure flag, so a redirect from http to https, or a
  // tampered URL, cannot downgrade the request.
  DWORD flags = 0;
  if (parts.nScheme == INTERNET_SCHEME_HTTPS) flags = WINHTTP_FLAG_SECURE;
  WinHttpHandle request{WinHttpOpenRequest(connection.handle, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
  if (!request.handle) {
    out.error = "the request could not be created";
    return out;
  }
  DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof redirects);

  const std::wstring headers = L"Accept: application/vnd.github+json\r\n";
  if (!WinHttpSendRequest(request.handle, headers.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
    out.error = "the request could not be sent";
    return out;
  }
  if (!WinHttpReceiveResponse(request.handle, nullptr)) {
    out.error = "no answer arrived";
    return out;
  }

  DWORD status = 0;
  DWORD statusSize = sizeof status;
  if (WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX))
    out.status = (int)status;

  // The body arrives in chunks; a release object is a few kilobytes.
  std::vector<char> chunk(4096);
  for (;;) {
    DWORD read = 0;
    if (!WinHttpReadData(request.handle, chunk.data(), (DWORD)chunk.size(), &read)) {
      out.error = "the answer was cut short";
      out.body.clear();
      return out;
    }
    if (read == 0) break;
    out.body.append(chunk.data(), read);
  }
  out.ok = true;
  return out;
}

#elif !defined(__APPLE__)

// Linux and the other Unix platforms read the response with the curl program the
// desktop already ships. That keeps the build free of a new library dependency
// for a feature that runs once per start-up, and a missing curl is reported like
// any other failure, so the check stays silent.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

HttpGetResult httpGet(const std::string &url, const std::string &userAgent, int timeoutSeconds) {
  HttpGetResult out;
  const fs::path bodyPath = fs::temp_directory_path() / ("ofxrawhost-update-" + std::to_string(getpid()) + ".json");

  // --fail hides the body of an error response, so a 4xx status is read from the
  // status code curl prints instead of from the body.
  std::string cmd = "curl --silent --show-error --location --fail --max-time " + std::to_string(timeoutSeconds) +
                    " --user-agent '" + userAgent + "' --header 'Accept: application/vnd.github+json' --output '" +
                    bodyPath.string() + "' --write-out '%{http_code}' '" + url + "' 2>/dev/null";
  FILE *pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    out.error = "curl could not be started";
    return out;
  }
  std::array<char, 32> code{};
  const size_t got = fread(code.data(), 1, code.size() - 1, pipe);
  code[got] = '\0';
  const int exitCode = pclose(pipe);
  const int status = std::atoi(code.data());
  out.status = status;

  std::ifstream body(bodyPath, std::ios::binary);
  if (body) out.body.assign((std::istreambuf_iterator<char>(body)), std::istreambuf_iterator<char>());
  std::error_code ec;
  fs::remove(bodyPath, ec);

  if (exitCode != 0) {
    // curl reports the transport failure and writes no usable body. A response
    // that arrived with an error status still counts as an answer, so the caller
    // can tell a refused request from a missing network.
    out.body.clear();
    if (status == 0) {
      out.error = "no answer arrived";
      return out;
    }
  }
  out.ok = true;
  return out;
}

#endif
