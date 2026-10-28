// Image open use-cases: open a file, answer the saved-chain prompt, apply a
// finished decode, and write the sidecar of the open image.
#pragma once

#include <memory>
#include <string>

struct ManagerSession;
class SidecarDao;
class WorkspaceProjectDao;

class OpenImageManager {
 public:
  virtual ~OpenImageManager() = default;
  virtual void openPath(const std::string &path) = 0;
  virtual void resolvePendingOpen(bool useSavedChain) = 0;
  virtual bool pumpOpen() = 0;
  // Writes the sidecar of the open image through SidecarDao.
  virtual void saveCurrentInputSidecar() = 0;
  // Writes per-user settings and the workspace project through WorkspaceProjectDao.
  virtual void persistWorkspace() = 0;
};

std::unique_ptr<OpenImageManager> createOpenImageManager(ManagerSession session, SidecarDao &sidecar,
                                                         WorkspaceProjectDao &project);
