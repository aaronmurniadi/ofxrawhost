#include "core/Version.h"

// The build passes the release version in. Keep a fallback for builds that do
// not, so the version shown and compared is never empty.
#ifndef OFXRAWHOST_VERSION
#define OFXRAWHOST_VERSION "0.0.0"
#endif

std::string hostVersion() { return OFXRAWHOST_VERSION; }

bool isDevelopmentBuild() { return hostVersion().find("-dev") != std::string::npos; }
