#include "core/UpdateChannel.h"

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
