#pragma once

#include "app/AppState.h"
#include "ofx/OfxRegistry.h"
#include "persist/daos/SidecarDao.h"
#include "persist/daos/WorkspaceProjectDao.h"
#include "services/managers/document/DocumentManager.h"
#include "services/managers/export/ExportManager.h"
#include "services/managers/session/ColorSpaceManager.h"

#include <memory>

// Prints "selftest FAILED: <msg>" and returns 1, so a case reads
// `return fail("...")`.
int fail(const char *msg);

// Plugins the suites skip: the development bundle is not installed on every
// machine, so its absence is not a failure.
bool selftestExcluded(const PluginEntry &pe);

// Owns DAOs and managers for a test App the way RunApp's Bootstrap does. Keep
// the object alive for as long as the App uses the manager pointers.
struct TestBootstrap {
  std::unique_ptr<SidecarDao> sidecarDao;
  std::unique_ptr<WorkspaceProjectDao> projectDao;
  std::unique_ptr<DocumentManager> documentsOwned;
  std::unique_ptr<ExportManager> exportsOwned;
  std::unique_ptr<ColorSpaceManager> colorSpacesOwned;

  explicit TestBootstrap(App &app) {
    sidecarDao = createSidecarDao();
    projectDao = createWorkspaceProjectDao();
    documentsOwned = createDocumentManager(app, *sidecarDao, *projectDao);
    exportsOwned = createExportManager(app, *sidecarDao);
    colorSpacesOwned = createColorSpaceManager(app);
    app.documents = documentsOwned.get();
    app.exports = exportsOwned.get();
    app.colorSpaces = colorSpacesOwned.get();
  }
};
