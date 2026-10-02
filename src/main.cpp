// Minimal still-image OpenFX host: decode RAW/raster, run one OFX filter, preview, export.

#include "UI.h"
#include "selftest/Selftest.h"

#include <cstring>
#include <string>

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "--selftest")) return runSelfTest();
  const std::string path = (argc > 1 && argv[1][0] != '-') ? argv[1] : "";
  return runApp(path);
}
