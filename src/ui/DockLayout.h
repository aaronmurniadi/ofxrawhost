#pragma once

namespace DockLayout {

inline constexpr const char *kPlugins = "Plugins";
inline constexpr const char *kPreview = "Preview";
inline constexpr const char *kParameters = "Parameters";
inline constexpr const char *kFilmstrip = "Filmstrip";

// Host dockspace for the frame. Builds a default layout when the global layout
// file holds no layout yet, which is the first run.
void BeginMainDockSpace();

}  // namespace DockLayout
