#include "platform/SystemFonts.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Directories the operating system keeps its fonts in. A directory that does
// not exist, or that the process cannot read, is skipped.
std::vector<fs::path> fontDirectories() {
  std::vector<fs::path> dirs;
#if defined(_WIN32)
  if (const char *windir = std::getenv("WINDIR")) dirs.emplace_back(fs::path(windir) / "Fonts");
  else dirs.emplace_back("C:\\Windows\\Fonts");
  // Per-user fonts installed without administrator rights.
  if (const char *local = std::getenv("LOCALAPPDATA"))
    dirs.emplace_back(fs::path(local) / "Microsoft" / "Windows" / "Fonts");
#elif defined(__APPLE__)
  dirs.emplace_back("/System/Library/Fonts");
  dirs.emplace_back("/System/Library/Fonts/Supplemental");
  dirs.emplace_back("/Library/Fonts");
  if (const char *home = std::getenv("HOME")) dirs.emplace_back(fs::path(home) / "Library" / "Fonts");
#else
  dirs.emplace_back("/usr/share/fonts");
  dirs.emplace_back("/usr/local/share/fonts");
  if (const char *home = std::getenv("HOME")) {
    dirs.emplace_back(fs::path(home) / ".local" / "share" / "fonts");
    dirs.emplace_back(fs::path(home) / ".fonts");
  }
#endif
  return dirs;
}

bool hasFontExtension(const fs::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext == ".ttf" || ext == ".otf" || ext == ".ttc" || ext == ".otc";
}

// The file name without its extension reads as a family name, which is what a
// user looks for in the list.
std::string displayName(const fs::path &path) {
  std::string name = path.stem().string();
  std::replace(name.begin(), name.end(), '_', ' ');
  return name;
}

}  // namespace

std::vector<SystemFont> listSystemFonts() {
  std::vector<SystemFont> fonts;
  std::set<std::string> seenNames;
  const auto options = fs::directory_options::skip_permission_denied;
  for (const fs::path &dir : fontDirectories()) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) continue;
    fs::recursive_directory_iterator it(dir, options, ec);
    fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
      const fs::directory_entry &entry = *it;
      std::error_code fileEc;
      if (!entry.is_regular_file(fileEc) || fileEc) continue;
      if (!hasFontExtension(entry.path())) continue;
      SystemFont font;
      font.name = displayName(entry.path());
      if (font.name.empty() || !seenNames.insert(font.name).second) continue;
      font.path = entry.path().string();
      fonts.push_back(std::move(font));
    }
  }
  std::sort(fonts.begin(), fonts.end(),
            [](const SystemFont &a, const SystemFont &b) { return a.name < b.name; });
  return fonts;
}
