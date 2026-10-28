// Document open / workspace open use-cases.
#pragma once

#include <memory>
#include <string>

struct ManagerSession;
class SidecarDao;
class WorkspaceProjectDao;

class DocumentManager {
 public:
  virtual ~DocumentManager() = default;
  virtual void openPath(const std::string &path) = 0;
  virtual bool openWorkspace(const std::string &dir) = 0;
  virtual void resolvePendingOpen(bool useSavedChain) = 0;
  virtual bool pumpOpen() = 0;
  // Writes the sidecar of the open image through SidecarDao.
  virtual void saveCurrentInputSidecar() = 0;
  // Writes per-user settings and the workspace project through WorkspaceProjectDao.
  virtual void persistWorkspace() = 0;
};

std::unique_ptr<DocumentManager> createDocumentManager(ManagerSession session, SidecarDao &sidecar,
                                                       WorkspaceProjectDao &project);
