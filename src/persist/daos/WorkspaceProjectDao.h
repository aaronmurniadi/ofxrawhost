// Disk access for the workspace project file and the per-user settings file.
// Managers call this instead of the ProjectPersist free functions.
#pragma once

#include "persist/ProjectPersist.h"

#include <memory>
#include <string>

class WorkspaceProjectDao {
 public:
  virtual ~WorkspaceProjectDao() = default;
  virtual bool loadProject(const std::string &workspaceDir, std::string &activeImageRel) = 0;
  virtual bool saveProject(const std::string &workspaceDir, const std::string &activeImageRel) = 0;
  // Writes the project with activeImageAbs turned into a path relative to the
  // workspace. An empty absolute path clears the active image.
  virtual bool saveActiveImage(const std::string &workspaceDir, const std::string &activeImageAbs) = 0;
  virtual bool loadSettings(PersistGui &gui, PersistAppearance &appearance, PersistUpdates &updates) = 0;
  virtual bool saveSettings(const PersistGui &gui, const PersistAppearance &appearance,
                            const PersistUpdates &updates) = 0;
};

std::unique_ptr<WorkspaceProjectDao> createWorkspaceProjectDao();
