#include "platform/Report.h"

#include "platform/Console.h"

#include <cstdio>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void reportFatalError(const std::string &message) {
  const bool haveConsole = attachParentConsole();
  std::fprintf(stderr, "%s\n", message.c_str());
  std::fflush(stderr);
#ifdef _WIN32
  // Without a console, a message box is the only way the user learns why the
  // window never appeared.
  if (!haveConsole) {
    const std::wstring wide(message.begin(), message.end());
    MessageBoxW(nullptr, wide.c_str(), L"OFX Raw Host", MB_OK | MB_ICONERROR);
  }
#endif
}
