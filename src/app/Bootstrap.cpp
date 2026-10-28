#include "app/Bootstrap.h"

Bootstrap initBootstrap(Workspace &workspace) {
  Bootstrap boot;
  ManagerSession session = workspace.session();
  boot.sidecarDao = createSidecarDao();
  boot.projectDao = createWorkspaceProjectDao();
  boot.documents = createDocumentManager(session, *boot.sidecarDao, *boot.projectDao);
  boot.exports = createExportManager(session, *boot.sidecarDao);
  boot.colorSpaces = createColorSpaceManager(session);
  boot.nodeGraph = createNodeGraphManager(session);
  boot.paramEdits = createParamEditManager(session);
  return boot;
}
