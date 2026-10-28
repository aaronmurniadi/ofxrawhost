// Export use-cases. UI calls this instead of Export.h free functions once call
// sites move over.
#pragma once

#include <memory>
#include <string>

struct Workspace;
class SidecarDao;

class ExportManager {
 public:
  virtual ~ExportManager() = default;
  virtual bool canExport() const = 0;
  virtual std::string defaultExportName() const = 0;
  virtual void doExport(const std::string &outPath) = 0;
};

std::unique_ptr<ExportManager> createExportManager(Workspace &workspace, SidecarDao &sidecar);
