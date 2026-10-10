#pragma once

#include <string>
#include <vector>

// A font file the operating system makes available to applications.
struct SystemFont {
  std::string name;  // display name, taken from the font file name
  std::string path;  // absolute path to the font file
};

// Lists the font files in the standard system and user font directories, sorted
// by display name with duplicates removed. Returns an empty list when no font
// directory is readable.
std::vector<SystemFont> listSystemFonts();
