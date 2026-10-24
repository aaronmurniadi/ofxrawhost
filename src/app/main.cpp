// Minimal still-image OpenFX host: decode RAW/raster, run one OFX filter, preview, export.

#include "app/RunApp.h"
#include "platform/Console.h"
#include "platform/Report.h"
#ifdef OFX_SELFTEST_IN_APP
#include "selftest/Selftest.h"
#endif

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace {

#if !defined(OFX_SELFTEST_IN_APP)
// The self-test lives in its own binary, which links the libraries instead of
// this one. Forward to it so `OfxRawHost --selftest` keeps working for the
// build script and the release smoke test.
int runSiblingSelfTest(const char *argv0) {
  std::error_code ec;
  const std::filesystem::path self = std::filesystem::absolute(argv0, ec);
  if (ec) {
    reportFatalError("Cannot locate the self-test binary next to OfxRawHost.");
    return 1;
  }
  const std::filesystem::path candidate = self.parent_path() / "OfxRawHostTests";
  if (!std::filesystem::is_regular_file(candidate, ec)) {
    reportFatalError("OfxRawHostTests was not found next to OfxRawHost: " + candidate.string());
    return 1;
  }
  const std::string command = "\"" + candidate.string() + "\"";
  const int rc = std::system(command.c_str());
  if (rc == -1) return 1;
  return rc;
}
#endif

}  // namespace

int main(int argc, char **argv) {
  // The Windows build is a GUI executable, so it borrows the console of the
  // process that started it when one exists.
  attachParentConsole();
  if (argc > 1 && !strcmp(argv[1], "--selftest")) {
#ifdef OFX_SELFTEST_IN_APP
    return runSelfTest();
#else
    return runSiblingSelfTest(argv[0]);
#endif
  }
  std::string path;
  if (argc > 1 && argv[1][0] != '-') path = argv[1];
  try {
    return runApp(path);
  } catch (const std::exception &e) {
    reportFatalError(std::string("OFX Raw Host stopped with an error: ") + e.what());
    return 1;
  }
}
