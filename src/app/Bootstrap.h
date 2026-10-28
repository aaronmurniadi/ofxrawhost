// Composition root: owns DAOs and managers for the process lifetime.
// RunApp builds one Bootstrap and keeps it alive; panels never construct managers.
#pragma once

#include "persist/daos/SidecarDao.h"
#include "persist/daos/WorkspaceProjectDao.h"
#include "services/managers/document/DocumentManager.h"
#include "services/managers/export/ExportManager.h"
#include "services/managers/session/ColorSpaceManager.h"

#include <memory>

struct Workspace;

struct Bootstrap {
  std::unique_ptr<SidecarDao> sidecarDao;
  std::unique_ptr<WorkspaceProjectDao> projectDao;
  std::unique_ptr<DocumentManager> documents;
  std::unique_ptr<ExportManager> exports;
  std::unique_ptr<ColorSpaceManager> colorSpaces;
};

// Builds DAOs then managers. Managers temporarily bind to workspace until later
// steps narrow those dependencies.
Bootstrap initBootstrap(Workspace &workspace);
