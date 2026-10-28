#include "services/managers/document/OpenWorkspaceManager.h"

#include "persist/daos/WorkspaceProjectDao.h"
#include "services/Filmstrip.h"
#include "services/ManagerSession.h"
#include "services/managers/document/OpenImageManager.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace {

class OpenWorkspaceManagerImpl : public OpenWorkspaceManager {
 public:
  OpenWorkspaceManagerImpl(ManagerSession session, OpenImageManager &images, WorkspaceProjectDao &project)
      : session_(session), images_(images), project_(project) {}

  bool openWorkspace(const std::string &dir) override {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
      session_.reportStatus("Not a directory");
      return false;
    }
    images_.saveCurrentInputSidecar();
    images_.persistWorkspace();
    session_.document.workspaceDir = fs::weakly_canonical(fs::path(dir), ec).string();
    refreshFilmstrip(session_.filmstrip, session_.document.workspaceDir, session_.document.path);
    // The project records which image was open. Its chain lives in the sidecar of
    // that image, and the interface state lives in the settings file.
    std::string activeRel;
    project_.loadProject(session_.document.workspaceDir, activeRel);
    std::string toOpen;
    if (!activeRel.empty()) {
      fs::path p = fs::path(session_.document.workspaceDir) / activeRel;
      if (fs::is_regular_file(p, ec)) toOpen = p.string();
    }
    if (toOpen.empty() && !session_.filmstrip.entries.empty()) toOpen = session_.filmstrip.entries[0].path;
    if (!toOpen.empty())
      images_.openPath(toOpen);
    else
      session_.reportStatus("Directory: " + fs::path(session_.document.workspaceDir).filename().string() +
                            " (no images)");
    // An open that waits for the user to answer records the active image itself
    // once it finishes, so a half-finished open leaves the project alone.
    if (session_.gui.pendingOpenPath.empty()) images_.persistWorkspace();
    session_.gui.showFilmstrip = true;
    return true;
  }

 private:
  ManagerSession session_;
  OpenImageManager &images_;
  WorkspaceProjectDao &project_;
};

}  // namespace

std::unique_ptr<OpenWorkspaceManager> createOpenWorkspaceManager(ManagerSession session,
                                                                 OpenImageManager &images,
                                                                 WorkspaceProjectDao &project) {
  return std::make_unique<OpenWorkspaceManagerImpl>(session, images, project);
}
