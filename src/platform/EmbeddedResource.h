#pragma once

#include <vector>

// Identifier of the icon font resource inside the Windows executable. The
// matching definition is IDR_ICON_FONT in packaging/windows/OfxRawHost.rc.in.
inline constexpr int kIconFontResourceId = 101;

// Copy of a resource compiled into the executable image (Windows RCDATA).
// Returns an empty vector on other platforms, and when the resource is absent.
std::vector<unsigned char> embeddedResource(int id);
