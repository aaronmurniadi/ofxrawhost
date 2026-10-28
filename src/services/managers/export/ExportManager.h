// Export use-cases.
#pragma once

#include "services/dto/Requests.h"

#include <memory>
#include <string>

struct ManagerSession;
class SidecarDao;

class ExportManager {
 public:
  virtual ~ExportManager() = default;
  virtual bool canExport() const = 0;
  virtual std::string defaultExportName() const = 0;
  // Appends the extension of the current format when the request has none.
  virtual void doExport(const ExportRequest &request) = 0;
};

std::unique_ptr<ExportManager> createExportManager(ManagerSession session, SidecarDao &sidecar);
