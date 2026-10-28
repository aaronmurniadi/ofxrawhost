#include "services/managers/session/ColorSpaceManager.h"

#include "core/Image.h"
#include "model/GuiState.h"
#include "services/ParamBridge.h"
#include "services/Workspace.h"

#include <algorithm>

namespace {

class ColorSpaceManagerImpl : public ColorSpaceManager {
 public:
  explicit ColorSpaceManagerImpl(Workspace &workspace) : workspace_(workspace) {}

  void setInputSpace(int index) override {
    workspace_.document.inputSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
    // The chain carries this space into its first plugin, and a later plugin can
    // change what leaves the chain, so the whole walk and a render are needed.
    syncChainColorSpaces(workspace_);
    workspace_.scheduleRender();
  }

  void setPreviewSpace(int index) override {
    // The preview is the destination of the last conversion: the chain output is
    // encoded into this space and shown as it is. Only the display conversion
    // changes, so a re-render is not needed.
    workspace_.gui.previewSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
    workspace_.scheduleRecolor();
  }

  void setExportSpace(int index) override {
    workspace_.gui.exportSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
  }

  void setExportIntent(int intentIndex) override {
    workspace_.gui.exportIntent =
        static_cast<RenderIntent>(std::clamp(intentIndex, 0, kRenderIntentCount - 1));
  }

  void setPreviewRes(int index) override {
    workspace_.gui.previewRes = static_cast<PreviewRes>(std::clamp(index, 0, kPreviewResCount - 1));
    workspace_.rebuildPreviewImage();
  }

 private:
  Workspace &workspace_;
};

}  // namespace

std::unique_ptr<ColorSpaceManager> createColorSpaceManager(Workspace &workspace) {
  return std::make_unique<ColorSpaceManagerImpl>(workspace);
}
