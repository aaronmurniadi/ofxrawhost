#include "services/managers/document/DocumentManager.h"

#include "persist/daos/SidecarDao.h"
#include "persist/daos/WorkspaceProjectDao.h"
#include "services/Actions.h"
#include "services/ChainIO.h"
#include "services/StateMapping.h"

namespace {

class DocumentManagerImpl : public DocumentManager {
 public:
  DocumentManagerImpl(Workspace &workspace, SidecarDao &sidecar, WorkspaceProjectDao &project)
      : workspace_(workspace), sidecar_(sidecar), project_(project) {}

  void openPath(const std::string &path) override { ::openPath(workspace_, path); }

  bool openWorkspace(const std::string &dir) override { return ::openWorkspace(workspace_, dir); }

  void resolvePendingOpen(bool useSavedChain) override { ::resolvePendingOpen(workspace_, useSavedChain); }

  bool pumpOpen() override { return ::pumpOpen(workspace_); }

  void saveCurrentInputSidecar() override {
    if (workspace_.document.path.empty()) return;
    sidecar_.save(workspace_.document.path, workspace_.document.fileSpace, workspace_.document.inputSpace,
                  captureChain(workspace_.chain));
  }

  void persistWorkspace() override {
    project_.saveSettings(captureGui(workspace_.gui), captureAppearance(workspace_.gui),
                          captureUpdates(workspace_.gui));
    if (workspace_.document.workspaceDir.empty()) return;
    project_.saveActiveImage(workspace_.document.workspaceDir, workspace_.document.path);
  }

 private:
  Workspace &workspace_;
  SidecarDao &sidecar_;
  WorkspaceProjectDao &project_;
};

}  // namespace

std::unique_ptr<DocumentManager> createDocumentManager(Workspace &workspace, SidecarDao &sidecar,
                                                       WorkspaceProjectDao &project) {
  return std::make_unique<DocumentManagerImpl>(workspace, sidecar, project);
}
