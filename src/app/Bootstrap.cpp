#include "app/Bootstrap.h"

#include "services/managers/document/DocumentManager.h"
#include "services/managers/export/ExportManager.h"

Bootstrap initBootstrap(Workspace &workspace) {
  Bootstrap boot;
  boot.documents = createDocumentManager(workspace);
  boot.exports = createExportManager(workspace);
  return boot;
}
