// Minimal still-image OpenFX host: decode RAW/raster, run one OFX filter, preview, export.

#include "UI.h"
#include "platform/Console.h"
#include "platform/Report.h"
#include "selftest/Selftest.h"

#include <cstring>
#include <exception>
#include <string>

int main(int argc, char **argv) {
  // The Windows build is a GUI executable, so it borrows the console of the
  // process that started it when one exists.
  attachParentConsole();
  if (argc > 1 && !strcmp(argv[1], "--selftest")) return runSelfTest();
  const std::string path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "";
  try {
    return runApp(path);
  } catch (const std::exception &e) {
    reportFatalError(std::string("OFX Raw Host stopped with an error: ") + e.what());
    return 1;
  }
}
