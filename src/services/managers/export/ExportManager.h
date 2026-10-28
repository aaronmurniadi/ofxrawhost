// Export use-cases.
#pragma once

#include <memory>
#include <string>

struct ManagerSession;
class SidecarDao;

class ExportManager {
 public:
  virtual ~ExportManager() = default;
  virtual bool canExport() const = 0;
  virtual std::string defaultExportName() const = 0;
  virtual void doExport(const std::string &outPath) = 0;
};

std::unique_ptr<ExportManager> createExportManager(ManagerSession session, SidecarDao &sidecar);
