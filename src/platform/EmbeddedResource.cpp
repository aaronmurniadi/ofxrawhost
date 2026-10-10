#include "platform/EmbeddedResource.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

std::vector<unsigned char> embeddedResource(int id) {
#ifdef _WIN32
  const HRSRC found = FindResourceA(nullptr, MAKEINTRESOURCEA(id), RT_RCDATA);
  if (!found) return {};
  const DWORD bytes = SizeofResource(nullptr, found);
  const HGLOBAL handle = LoadResource(nullptr, found);
  if (bytes == 0 || !handle) return {};
  const void *data = LockResource(handle);
  if (!data) return {};
  const unsigned char *first = static_cast<const unsigned char *>(data);
  return std::vector<unsigned char>(first, first + bytes);
#else
  (void)id;
  return {};
#endif
}
