#include "services/managers/export/ExportManager.h"

#include "persist/daos/SidecarDao.h"
#include "services/Export.h"

namespace {

class ExportManagerImpl : public ExportManager {
 public:
  ExportManagerImpl(Workspace &workspace, SidecarDao &sidecar) : workspace_(workspace), sidecar_(sidecar) {}

  bool canExport() const override { return ::canExport(workspace_); }

  std::string defaultExportName() const override { return ::defaultExportName(workspace_); }

  void doExport(const std::string &outPath) override {
    // The export worker still writes the sidecar through the free function until
    // Step 4 moves that path onto sidecar_. ExportManager holds the DAO so the
    // constructor injection is in place.
    (void)sidecar_;
    ::doExport(workspace_, outPath);
  }

 private:
  Workspace &workspace_;
  SidecarDao &sidecar_;
};

}  // namespace

std::unique_ptr<ExportManager> createExportManager(Workspace &workspace, SidecarDao &sidecar) {
  return std::make_unique<ExportManagerImpl>(workspace, sidecar);
}
