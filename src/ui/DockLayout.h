#pragma once

#include "AppState.h"

namespace DockLayout {

inline constexpr const char *kLeft = "OFX Plugins";
inline constexpr const char *kPreview = "Preview";
inline constexpr const char *kParams = "OFX Parameters";
inline constexpr const char *kFilmstrip = "Filmstrip";

// Host dockspace for the frame. Builds a default layout when no .ini exists
// or when app.layoutApplyPending (legacy panel sizes / first workspace open).
void BeginMainDockSpace(App &app);

}  // namespace DockLayout
