// Workspace open use-case: make a directory the workspace, then reopen the image
// the project remembers.
#pragma once

#include <memory>
#include <string>

struct ManagerSession;
class OpenImageManager;
class WorkspaceProjectDao;

class OpenWorkspaceManager {
 public:
  virtual ~OpenWorkspaceManager() = default;
  virtual bool openWorkspace(const std::string &dir) = 0;
};

std::unique_ptr<OpenWorkspaceManager> createOpenWorkspaceManager(ManagerSession session,
                                                                 OpenImageManager &images,
                                                                 WorkspaceProjectDao &project);
