#include "platform/Console.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>

// True when the stream needs a console: it is absent, or it is a console handle
// while this process is not attached to that console. A pipe or a file means the
// caller redirected the stream, which must stay as it is.
static bool streamNeedsConsole(DWORD which) {
  const HANDLE handle = GetStdHandle(which);
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return true;
  if (GetFileType(handle) != FILE_TYPE_CHAR) return false;
  return GetConsoleWindow() == nullptr;
}

void attachParentConsole() {
  const bool needOut = streamNeedsConsole(STD_OUTPUT_HANDLE);
  const bool needErr = streamNeedsConsole(STD_ERROR_HANDLE);
  if (!needOut && !needErr) return;
  // A launch from Explorer has no parent console, and then there is nothing to
  // print to.
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  FILE *stream = nullptr;
  if (needOut) freopen_s(&stream, "CONOUT$", "w", stdout);
  if (needErr) freopen_s(&stream, "CONOUT$", "w", stderr);
  SetConsoleOutputCP(CP_UTF8);
}
#else
void attachParentConsole() {}
#endif
