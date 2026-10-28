#include "services/managers/export/ExportManager.h"

#include "services/Export.h"

namespace {

class ExportManagerImpl : public ExportManager {
 public:
  explicit ExportManagerImpl(Workspace &workspace) : workspace_(workspace) {}

  bool canExport() const override { return ::canExport(workspace_); }

  std::string defaultExportName() const override { return ::defaultExportName(workspace_); }

  void doExport(const std::string &outPath) override { ::doExport(workspace_, outPath); }

 private:
  Workspace &workspace_;
};

}  // namespace

std::unique_ptr<ExportManager> createExportManager(Workspace &workspace) {
  return std::make_unique<ExportManagerImpl>(workspace);
}
