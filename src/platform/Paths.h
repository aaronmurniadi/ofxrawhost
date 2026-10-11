#pragma once

#include <filesystem>

// Directory that holds the running executable. Packaged builds place the icon
// font and the bundled OFX plugin bundle next to the binary, so the host finds
// them no matter what the current working directory is. Returns an empty path
// when the location cannot be determined.
std::filesystem::path exeDir();

// Per-user configuration directory that holds settings.json and the ImGui
// layout .ini: %APPDATA%/OfxRawHost on Windows, ~/Library/Application
// Support/OfxRawHost on macOS, and $XDG_CONFIG_HOME/ofxrawhost (or
// ~/.config/ofxrawhost) elsewhere. Creates the directory so ImGui can write its
// layout file. Returns an empty path when the location cannot be determined.
std::filesystem::path userConfigDir();

// Directory under the configuration directory that holds the state of the files
// the host reads and writes: one entry per image, per export, and per directory.
// Created on demand. Returns an empty path when the location cannot be
// determined, which sends the state back beside the file.
std::filesystem::path userStateDir();
