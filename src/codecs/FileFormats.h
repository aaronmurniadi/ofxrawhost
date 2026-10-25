// The supported file extensions and how they classify.
//
// Split out of ImageIO.h so that code which only needs to ask "can this path be
// opened?" (the project writer, the filmstrip, the file dialogs) does not pull in
// the decoder interface.

#pragma once

#include <string>
#include <vector>

// Lowercase file extension with the leading dot (for example ".cr2"). Empty when there is none.
std::string lowerFileExtension(const std::string &path);
// One table in ImageLoad.cpp lists the supported extensions. extLower includes the
// leading dot and is lowercase.
bool isRawImageExtension(const std::string &extLower);
bool isSupportedImageExtension(const std::string &extLower);
// Every supported extension, lowercase, with the leading dot, in table order.
std::vector<std::string> supportedImageExtensions();
