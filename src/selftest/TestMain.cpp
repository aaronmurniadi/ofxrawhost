// Entry point of the self-test binary.
//
// The self-test links the libraries instead of the shipping executable, so it
// needs no window, no OpenGL context, and no command line of its own. The app
// binary forwards --selftest here for one release, which keeps build.sh and the
// release smoke test working unchanged.

#include "selftest/Selftest.h"

#include "platform/Console.h"
#include "platform/Report.h"

#include <exception>
#include <string>

int main() {
  // The Windows build is a GUI executable, so it borrows the console of the
  // process that started it when one exists.
  attachParentConsole();
  try {
    return runSelfTest();
  } catch (const std::exception &e) {
    reportFatalError(std::string("OFX Raw Host self-test stopped with an error: ") + e.what());
    return 1;
  }
}
