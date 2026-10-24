#include "selftest/TestSupport.h"

#include <cstdio>
#include <cstring>

int fail(const char *msg) {
  fprintf(stderr, "selftest FAILED: %s\n", msg);
  return 1;
}

bool selftestExcluded(const PluginEntry &pe) {
  return pe.plugin && pe.plugin->pluginIdentifier && !strcmp(pe.plugin->pluginIdentifier, "org.spektrafilm.dev");
}
