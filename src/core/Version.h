// The version this build reports, from the build system.
//
// Pure build metadata: no network, no thread, no UI. The update checker, the
// About window and the settings model all read it from here.

#pragma once

#include <string>

// "0.4.2" for a release build and "0.4.2-dev-<sha>" for a development build.
std::string hostVersion();

// True for a development build, which is what the one-time start-up warning and
// the pre-release channel are for.
bool isDevelopmentBuild();
