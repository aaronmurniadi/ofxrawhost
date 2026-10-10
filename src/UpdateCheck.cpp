#include "UpdateCheck.h"

#include "persist/Json.h"
#include "platform/Http.h"

#include <cctype>
#include <cstdio>
#include <string>
#include <utility>

// The build passes the release version in. Keep a fallback for builds that do
// not, so the version shown and compared is never empty.
#ifndef OFXRAWHOST_VERSION
#define OFXRAWHOST_VERSION "0.0.0"
#endif

std::string hostVersion() { return OFXRAWHOST_VERSION; }

bool isDevelopmentBuild() { return hostVersion().find("-dev") != std::string::npos; }

bool channelAllowed(UpdateChannel channel) {
  if (channel == UpdateChannel::Stable) return true;
  // A release build must not be offered a pre-release, which is the only reason
  // its channel is fixed.
  return isDevelopmentBuild();
}

UpdateChannel defaultUpdateChannel() {
  if (isDevelopmentBuild()) return UpdateChannel::Prereleases;
  return UpdateChannel::Stable;
}

bool versionParts(const std::string &text, std::vector<int> &parts) {
  parts.clear();
  size_t pos = 0;
  // A release tag carries a "v" prefix; the build version does not.
  if (pos < text.size() && (text[pos] == 'v' || text[pos] == 'V')) ++pos;
  while (pos < text.size() && std::isdigit((unsigned char)text[pos])) {
    int value = 0;
    while (pos < text.size() && std::isdigit((unsigned char)text[pos])) {
      // A part far larger than any real version saturates instead of wrapping.
      if (value < 1000000) value = value * 10 + (text[pos] - '0');
      ++pos;
    }
    parts.push_back(value);
    if (pos >= text.size() || text[pos] != '.') break;
    ++pos;
  }
  return !parts.empty();
}

int compareVersions(const std::string &left, const std::string &right) {
  std::vector<int> lhs;
  std::vector<int> rhs;
  const bool lhsNumbered = versionParts(left, lhs);
  const bool rhsNumbered = versionParts(right, rhs);
  // Text with no number at all, such as an unexpected tag, sorts first.
  if (!lhsNumbered || !rhsNumbered) {
    if (lhsNumbered == rhsNumbered) return 0;
    if (lhsNumbered) return 1;
    return -1;
  }
  size_t count = rhs.size();
  if (lhs.size() > count) count = lhs.size();
  for (size_t i = 0; i < count; ++i) {
    // 0.4 and 0.4.0 are the same version, so a missing part reads as zero.
    int a = 0;
    if (i < lhs.size()) a = lhs[i];
    int b = 0;
    if (i < rhs.size()) b = rhs[i];
    if (a != b) {
      if (a < b) return -1;
      return 1;
    }
  }
  return 0;
}

bool parseReleaseResponse(const std::string &json, std::string &tag, std::string &url) {
  JsonValue root;
  if (!JsonValue::parse(json, root)) return false;
  if (root.kind != JsonValue::Kind::Object) return false;
  const JsonValue *tagValue = root.find("tag_name");
  if (!tagValue || tagValue->kind != JsonValue::Kind::String || tagValue->s.empty()) return false;
  tag = tagValue->s;
  // The page is only used by a button, so a missing one is not a failure.
  url = root.text("html_url", std::string());
  return true;
}

bool parseReleasesResponse(const std::string &json, std::string &tag, std::string &url) {
  JsonValue root;
  if (!JsonValue::parse(json, root)) return false;
  if (root.kind != JsonValue::Kind::Array) return false;

  bool found = false;
  bool bestIsPrerelease = false;
  for (const JsonValue &entry : root.arr) {
    if (entry.kind != JsonValue::Kind::Object) continue;
    const JsonValue *tagValue = entry.find("tag_name");
    if (!tagValue || tagValue->kind != JsonValue::Kind::String || tagValue->s.empty()) continue;
    const bool prerelease = entry.boolean("prerelease", false);
    if (found) {
      const int order = compareVersions(tagValue->s, tag);
      // Several development builds share one version number, so the newest of
      // them is the first one in the list; only a higher version, or a
      // pre-release at the same version, replaces the candidate.
      if (order < 0) continue;
      if (order == 0 && !(prerelease && !bestIsPrerelease)) continue;
    }
    tag = tagValue->s;
    url = entry.text("html_url", std::string());
    bestIsPrerelease = prerelease;
    found = true;
  }
  return found;
}

UpdateCheck::~UpdateCheck() {
  if (worker.joinable()) worker.join();
}

void UpdateCheck::startCheck(const std::string &currentVersion, UpdateChannel channel) {
  if (!channelAllowed(channel)) channel = UpdateChannel::Stable;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (running) return;
    running = true;
    finished = false;
  }
  // A check that is not running has already published, so this only collects the
  // finished worker thread.
  if (worker.joinable()) worker.join();

  state = State::Checking;
  failure.clear();
  worker = std::thread(&UpdateCheck::runCheck, this, currentVersion, channel);
}

void UpdateCheck::runCheck(const std::string &currentVersion, UpdateChannel channel) {
  // The stable channel reads releases/latest, which skips pre-releases. The
  // pre-release channel reads the release list, which holds both.
  const std::string base = std::string("https://api.github.com/repos/") + kUpdatesRepoOwner + "/" + kUpdatesRepoName;
  const bool wantPrereleases = channel == UpdateChannel::Prereleases;
  std::string url = base + "/releases/latest";
  if (wantPrereleases) url = base + "/releases?per_page=10";

  const std::string agent = std::string("OfxRawHost/") + currentVersion;
  const HttpGetResult answer = httpGet(url, agent, kUpdateCheckTimeoutSeconds);

  Pending next;
  next.channel = channel;
  if (!answer.ok) {
    next.state = State::Failed;
    if (answer.error.empty()) next.failure = "no answer arrived";
    else next.failure = answer.error;
  } else if (answer.status != 200) {
    next.state = State::Failed;
    next.failure = "GitHub answered HTTP " + std::to_string(answer.status);
  } else {
    bool read = false;
    if (wantPrereleases) read = parseReleasesResponse(answer.body, next.tag, next.url);
    else read = parseReleaseResponse(answer.body, next.tag, next.url);
    if (!read) {
      next.state = State::Failed;
      next.failure = "the release list could not be read";
    } else if (compareVersions(next.tag, currentVersion) > 0) {
      next.state = State::Available;
    } else {
      next.state = State::UpToDate;
    }
  }

  if (next.state == State::Failed) {
    // Start-up stays quiet about a failure, so the terminal is the only place it
    // shows up.
    std::fprintf(stderr, "[updates] check failed: %s\n", next.failure.c_str());
  }

  std::lock_guard<std::mutex> lock(mutex);
  pending = std::move(next);
  finished = true;
  running = false;
}

void UpdateCheck::pump() {
  std::lock_guard<std::mutex> lock(mutex);
  if (!finished) return;
  finished = false;
  state = pending.state;
  checkedChannel = pending.channel;
  latestTag = pending.tag;
  releaseUrl = pending.url;
  failure = pending.failure;
}
