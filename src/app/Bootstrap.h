// Composition root: owns DAOs and managers for the process lifetime.
// RunApp builds one Bootstrap and keeps it alive; panels never construct managers.
#pragma once

#include "services/managers/document/DocumentManager.h"
#include "services/managers/export/ExportManager.h"

#include <memory>

struct Workspace;

struct Bootstrap {
  std::unique_ptr<DocumentManager> documents;
  std::unique_ptr<ExportManager> exports;
};

// Builds managers that temporarily bind to workspace. DAOs and further managers
// arrive in later restructure steps.
Bootstrap initBootstrap(Workspace &workspace);
