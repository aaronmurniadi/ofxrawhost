#include "platform/Paths.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>

namespace fs = std::filesystem;

fs::path exeDir() {
#ifdef _WIN32
  wchar_t buf[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) return {};
  return fs::path(buf).parent_path();
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t sz = sizeof(buf);
  if (_NSGetExecutablePath(buf, &sz) != 0) return {};
  std::error_code ec;
  const fs::path resolved = fs::weakly_canonical(fs::path(buf), ec);
  if (ec) return fs::path(buf).parent_path();
  return resolved.parent_path();
#else
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n <= 0) return {};
  buf[n] = '\0';
  return fs::path(buf).parent_path();
#endif
}

fs::path userConfigDir() {
  fs::path dir;
#if defined(_WIN32)
  const char *appData = std::getenv("APPDATA");
  if (appData && appData[0]) dir = fs::path(appData) / "OfxRawHost";
#elif defined(__APPLE__)
  const char *home = std::getenv("HOME");
  if (home && home[0]) dir = fs::path(home) / "Library" / "Application Support" / "OfxRawHost";
#else
  const char *xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0]) {
    dir = fs::path(xdg) / "ofxrawhost";
  } else {
    const char *home = std::getenv("HOME");
    if (home && home[0]) dir = fs::path(home) / ".config" / "ofxrawhost";
  }
#endif
  if (dir.empty()) return dir;
  // ImGui writes the layout .ini here, so the directory has to exist first.
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}
