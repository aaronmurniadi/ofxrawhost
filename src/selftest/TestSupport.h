#pragma once

#include "ofx/OfxRegistry.h"

// Prints "selftest FAILED: <msg>" and returns 1, so a case reads
// `return fail("...")`.
int fail(const char *msg);

// Plugins the suites skip: the development bundle is not installed on every
// machine, so its absence is not a failure.
bool selftestExcluded(const PluginEntry &pe);
