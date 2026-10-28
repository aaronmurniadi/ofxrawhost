#include "persist/daos/WorkspaceProjectDao.h"

namespace {

class WorkspaceProjectDaoImpl : public WorkspaceProjectDao {
 public:
  bool loadProject(const std::string &workspaceDir, std::string &activeImageRel) override {
    return loadWorkspaceProject(workspaceDir, activeImageRel);
  }

  bool saveProject(const std::string &workspaceDir, const std::string &activeImageRel) override {
    return saveWorkspaceProject(workspaceDir, activeImageRel);
  }

  bool saveActiveImage(const std::string &workspaceDir, const std::string &activeImageAbs) override {
    std::string active;
    if (!activeImageAbs.empty()) active = relativeToWorkspace(workspaceDir, activeImageAbs);
    return saveWorkspaceProject(workspaceDir, active);
  }

  bool loadSettings(PersistGui &gui, PersistAppearance &appearance, PersistUpdates &updates) override {
    return loadGlobalSettings(gui, appearance, updates);
  }

  bool saveSettings(const PersistGui &gui, const PersistAppearance &appearance,
                    const PersistUpdates &updates) override {
    return saveGlobalSettings(gui, appearance, updates);
  }
};

}  // namespace

std::unique_ptr<WorkspaceProjectDao> createWorkspaceProjectDao() {
  return std::make_unique<WorkspaceProjectDaoImpl>();
}
