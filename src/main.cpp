// Minimal still-image OpenFX host: decode RAW/raster, run one OFX filter, preview, export.

#include "UI.h"
#include "platform/Console.h"
#include "selftest/Selftest.h"

#include <cstring>
#include <string>

int main(int argc, char **argv) {
  // The Windows build is a GUI executable, so it borrows the console of the
  // process that started it when one exists.
  attachParentConsole();
  if (argc > 1 && !strcmp(argv[1], "--selftest")) return runSelfTest();
  const std::string path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "";
  return runApp(path);
}
