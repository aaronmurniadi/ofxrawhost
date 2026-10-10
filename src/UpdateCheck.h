// GitHub release update check. The request needs the network, so it runs on its
// own thread: startCheck() returns at once and the UI reads the answer with
// pump(). A failure never interrupts the user; it is logged and reported in the
// settings section only.
#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Repository the releases are published from. The constants keep start-up free of
// a git call and of any URL from the user.
inline constexpr const char *kUpdatesRepoOwner = "aaronmurniadi";
inline constexpr const char *kUpdatesRepoName = "ofxrawhost";

// Seconds a single check may take before it is abandoned.
inline constexpr int kUpdateCheckTimeoutSeconds = 5;

// What a check looks for. A stable release build follows stable releases only;
// a development build may follow pre-releases as well, which is what its
// published builds are.
enum class UpdateChannel { Stable = 0, Prereleases };
inline constexpr int kUpdateChannelCount = 2;
inline constexpr const char *kUpdateChannelLabels[kUpdateChannelCount] = {"Stable Release", "Pre-releases"};

// The version this build reports, from the build system: "0.4.2" for a release
// build and "0.4.2-dev-<sha>" for a development build.
std::string hostVersion();

// True for a development build, which is what the one-time start-up warning and
// the pre-release channel are for.
bool isDevelopmentBuild();

// Whether this build may follow the given channel. A release build checks
// releases only, so its channel is always UpdateChannel::Stable.
bool channelAllowed(UpdateChannel channel);

// The channel to check with when nothing is stored yet: a development build
// starts on pre-releases, a release build on stable releases.
UpdateChannel defaultUpdateChannel();

// The numeric parts of a version or a release tag, with a leading "v" and a
// trailing "-dev-<sha>" removed, so "v0.4.2-dev-abc1234" reads as 0.4.2. Returns
// false when the text does not start with a number.
bool versionParts(const std::string &text, std::vector<int> &parts);

// Orders two versions by their numbers and returns a negative value, zero, or a
// positive value. A missing part counts as zero (0.4 and 0.4.0 are equal), and
// text with no number sorts before a numbered version.
int compareVersions(const std::string &left, const std::string &right);

// Reads "tag_name" and "html_url" from the JSON body of one GitHub release.
// Returns false when the body is not a release object, which is what a
// rate-limit or an error answer looks like.
bool parseReleaseResponse(const std::string &json, std::string &tag, std::string &url);

// Reads the newest release out of the JSON body of the GitHub release list. The
// list arrives newest first, so a later entry only wins on a higher version; a
// pre-release wins a tie, because the caller asked for pre-releases. Returns
// false when the body holds no release with a tag.
bool parseReleasesResponse(const std::string &json, std::string &tag, std::string &url);

// The state of the update check, its worker thread, and the answer that worker
// left behind. One instance lives in App; the UI thread owns every member except
// for the fields the worker writes, which are behind the mutex.
struct UpdateCheck {
  enum class State { Idle, Checking, UpToDate, Available, Failed };

  State state = State::Idle;
  UpdateChannel checkedChannel = UpdateChannel::Stable;  // what the last check looked for
  std::string latestTag;   // tag of the newest release the API named
  std::string releaseUrl;  // its page, for the "open release page" button
  std::string failure;     // why the last check produced no answer

  UpdateCheck() = default;
  UpdateCheck(const UpdateCheck &) = delete;
  UpdateCheck &operator=(const UpdateCheck &) = delete;
  // Waits out a check that is still in flight, so the worker never touches a
  // destroyed instance. A check waiting on the network delays the exit by at most
  // kUpdateCheckTimeoutSeconds.
  ~UpdateCheck();

  // Asks GitHub for the newest release of this repository. A check that is still
  // running is left to finish, and its answer is published by pump().
  void startCheck(const std::string &currentVersion, UpdateChannel channel);

  // Publishes the answer of a finished check. Call once per frame on the UI
  // thread.
  void pump();

 private:
  void runCheck(const std::string &currentVersion, UpdateChannel channel);

  struct Pending {
    State state = State::Idle;
    UpdateChannel channel = UpdateChannel::Stable;
    std::string tag;
    std::string url;
    std::string failure;
  };

  std::mutex mutex;
  bool running = false;   // the worker is in httpGet or about to publish
  bool finished = false;  // pending holds an answer pump() has not read yet
  Pending pending;

  std::thread worker;  // touched only by the UI thread
};
