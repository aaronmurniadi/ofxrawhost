#include "services/managers/document/DocumentManager.h"

#include "services/Actions.h"

namespace {

class DocumentManagerImpl : public DocumentManager {
 public:
  explicit DocumentManagerImpl(Workspace &workspace) : workspace_(workspace) {}

  void openPath(const std::string &path) override { ::openPath(workspace_, path); }

  bool openWorkspace(const std::string &dir) override { return ::openWorkspace(workspace_, dir); }

  void resolvePendingOpen(bool useSavedChain) override { ::resolvePendingOpen(workspace_, useSavedChain); }

  bool pumpOpen() override { return ::pumpOpen(workspace_); }

 private:
  Workspace &workspace_;
};

}  // namespace

std::unique_ptr<DocumentManager> createDocumentManager(Workspace &workspace) {
  return std::make_unique<DocumentManagerImpl>(workspace);
}
