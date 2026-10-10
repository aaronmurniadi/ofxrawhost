#pragma once

#include <filesystem>

// Directory that holds the running executable. Packaged builds place the icon
// font and the bundled OFX plugin bundle next to the binary, so the host finds
// them no matter what the current working directory is. Returns an empty path
// when the location cannot be determined.
std::filesystem::path exeDir();
