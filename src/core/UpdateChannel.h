// The update channel and its labels.
//
// The channel is a persisted setting (GuiState::updateChannel, PersistUpdates),
// so it lives here rather than in UpdateCheck.h: the model can describe the
// setting without pulling in the thread-owning update checker.

#pragma once

#include "core/Version.h"

// What a check looks for. A stable release build follows stable releases only;
// a development build may follow pre-releases as well, which is what its
// published builds are.
enum class UpdateChannel { Stable = 0, Prereleases };
inline constexpr int kUpdateChannelCount = 2;
inline constexpr const char *kUpdateChannelLabels[kUpdateChannelCount] = {"Stable Release", "Pre-releases"};

// Whether this build may follow the given channel. A release build checks
// releases only, so its channel is always UpdateChannel::Stable.
bool channelAllowed(UpdateChannel channel);

// The channel to check with when nothing is stored yet: a development build
// starts on pre-releases, a release build on stable releases.
UpdateChannel defaultUpdateChannel();
