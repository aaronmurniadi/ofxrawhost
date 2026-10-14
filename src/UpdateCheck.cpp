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

namespace {

// A GitHub timestamp as a number that orders two of them, or -1 when the text is
// not in the shape the API writes (YYYY-MM-DDTHH:MM:SSZ). Integer fields in that
// order compare as numbers, so no calendar arithmetic is needed.
long long isoUtcKey(const std::string &text) {
  if (text.size() < 20) return -1;
  long long key = 0;
  for (size_t i = 0; i < 19; ++i) {
    const char c = text[i];
    if (i == 4 || i == 7) {
      if (c != '-') return -1;
      continue;
    }
    if (i == 10) {
      if (c != 'T') return -1;
      continue;
    }
    if (i == 13 || i == 16) {
      if (c != ':') return -1;
      continue;
    }
    if (c < '0' || c > '9') return -1;
    key = key * 10 + (c - '0');
  }
  if (text[19] != 'Z') return -1;
  return key;
}

// One entry of the release list, or a false result when the entry carries no tag.
bool readEntry(const JsonValue &entry, ReleaseEntry &out) {
  if (entry.kind != JsonValue::Kind::Object) return false;
  const JsonValue *tagValue = entry.find("tag_name");
  if (!tagValue || tagValue->kind != JsonValue::Kind::String || tagValue->s.empty()) return false;
  out.tag = tagValue->s;
  out.url = entry.text("html_url", std::string());
  out.createdAt = entry.text("created_at", std::string());
  return true;
}

}  // namespace

bool sameVersionText(const std::string &tag, const std::string &version) {
  size_t start = 0;
  if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) start = 1;
  return tag.compare(start, std::string::npos, version) == 0;
}

bool releaseIsNewer(const ReleaseEntry &newest, const ReleaseEntry &current, const std::string &currentVersion) {
  const int order = compareVersions(newest.tag, currentVersion);
  if (order != 0) return order > 0;
  // Equal numbers are what two development builds of one version look like, and
  // their tags differ, so the time each release was created orders them.
  const long long newestKey = isoUtcKey(newest.createdAt);
  const long long currentKey = isoUtcKey(current.createdAt);
  if (newestKey < 0 || currentKey < 0) return false;
  return newestKey > currentKey;
}

bool parseReleaseResponse(const std::string &json, ReleaseEntry &out) {
  JsonValue root;
  if (!JsonValue::parse(json, root)) return false;
  if (root.kind != JsonValue::Kind::Object) return false;
  return readEntry(root, out);
}

bool parseReleasesResponse(const std::string &json, const std::string &currentVersion, ReleaseEntry &newest,
                           ReleaseEntry &current) {
  JsonValue root;
  if (!JsonValue::parse(json, root)) return false;
  if (root.kind != JsonValue::Kind::Array) return false;

  bool found = false;
  bool bestIsPrerelease = false;
  for (const JsonValue &entry : root.arr) {
    ReleaseEntry listed;
    if (!readEntry(entry, listed)) continue;
    // The running build is looked up in the same answer, so its time and the
    // newest time come from one source.
    if (sameVersionText(listed.tag, currentVersion)) current = listed;
    if (found) {
      const int order = compareVersions(listed.tag, newest.tag);
      // Several development builds share one version number, so the newest of
      // them is the first one in the list; only a higher version, or a
      // pre-release at the same version, replaces the candidate.
      const bool prerelease = entry.boolean("prerelease", false);
      if (order < 0) continue;
      if (order == 0 && !(prerelease && !bestIsPrerelease)) continue;
    }
    newest = listed;
    bestIsPrerelease = entry.boolean("prerelease", false);
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
  // The pre-release channel reads the release list, which holds both kinds. The
  // list also names this build, which is what orders two builds of one version,
  // so it is read deep enough to hold a run of pre-releases of one version.
  if (wantPrereleases) url = base + "/releases?per_page=30";

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
    ReleaseEntry newest;
    ReleaseEntry current;
    bool read = false;
    if (wantPrereleases) read = parseReleasesResponse(answer.body, currentVersion, newest, current);
    else read = parseReleaseResponse(answer.body, newest);
    next.tag = newest.tag;
    next.url = newest.url;
    if (!read) {
      next.state = State::Failed;
      next.failure = "the release list could not be read";
    } else if (releaseIsNewer(newest, current, currentVersion)) {
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
