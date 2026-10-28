#include "app/Bootstrap.h"

Bootstrap initBootstrap(Workspace &workspace) {
  Bootstrap boot;
  boot.sidecarDao = createSidecarDao();
  boot.projectDao = createWorkspaceProjectDao();
  boot.documents = createDocumentManager(workspace, *boot.sidecarDao, *boot.projectDao);
  boot.exports = createExportManager(workspace, *boot.sidecarDao);
  boot.colorSpaces = createColorSpaceManager(workspace);
  return boot;
}
