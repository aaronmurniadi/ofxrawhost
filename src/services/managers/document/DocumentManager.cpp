#include "services/managers/document/DocumentManager.h"

#include "persist/daos/SidecarDao.h"
#include "persist/daos/WorkspaceProjectDao.h"
#include "services/ManagerSession.h"
#include "services/managers/document/OpenImageManager.h"
#include "services/managers/document/OpenWorkspaceManager.h"

namespace {

class DocumentManagerImpl : public DocumentManager {
 public:
  DocumentManagerImpl(ManagerSession session, SidecarDao &sidecar, WorkspaceProjectDao &project)
      : images_(createOpenImageManager(session, sidecar, project)),
        workspaces_(createOpenWorkspaceManager(session, *images_, project)) {}

  void openPath(const std::string &path) override { images_->openPath(path); }

  bool openWorkspace(const std::string &dir) override { return workspaces_->openWorkspace(dir); }

  void resolvePendingOpen(bool useSavedChain) override { images_->resolvePendingOpen(useSavedChain); }

  bool pumpOpen() override { return images_->pumpOpen(); }

  void saveCurrentInputSidecar() override { images_->saveCurrentInputSidecar(); }

  void persistWorkspace() override { images_->persistWorkspace(); }

 private:
  // Declared first: the workspace manager keeps a reference to it.
  std::unique_ptr<OpenImageManager> images_;
  std::unique_ptr<OpenWorkspaceManager> workspaces_;
};

}  // namespace

std::unique_ptr<DocumentManager> createDocumentManager(ManagerSession session, SidecarDao &sidecar,
                                                       WorkspaceProjectDao &project) {
  return std::make_unique<DocumentManagerImpl>(session, sidecar, project);
}
