// Composition root: owns DAOs and managers for the process lifetime.
// RunApp builds one Bootstrap and keeps it alive; panels never construct managers.
#pragma once

#include "persist/daos/SidecarDao.h"
#include "persist/daos/WorkspaceProjectDao.h"
#include "services/Workspace.h"
#include "services/managers/chain/NodeGraphManager.h"
#include "services/managers/chain/ParamEditManager.h"
#include "services/managers/document/DocumentManager.h"
#include "services/managers/export/ExportManager.h"
#include "services/managers/session/ColorSpaceManager.h"

#include <memory>

struct Bootstrap {
  std::unique_ptr<SidecarDao> sidecarDao;
  std::unique_ptr<WorkspaceProjectDao> projectDao;
  std::unique_ptr<DocumentManager> documents;
  std::unique_ptr<ExportManager> exports;
  std::unique_ptr<ColorSpaceManager> colorSpaces;
  std::unique_ptr<NodeGraphManager> nodeGraph;
  std::unique_ptr<ParamEditManager> paramEdits;
};

// Builds DAOs then managers. Managers take a ManagerSession bundle, never the
// window or the preview texture.
Bootstrap initBootstrap(Workspace &workspace);
