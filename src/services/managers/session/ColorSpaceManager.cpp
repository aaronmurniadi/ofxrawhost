#include "services/managers/session/ColorSpaceManager.h"

#include "core/Image.h"
#include "model/GuiState.h"
#include "services/ManagerSession.h"
#include "services/ParamBridge.h"

#include <algorithm>

namespace {

class ColorSpaceManagerImpl : public ColorSpaceManager {
 public:
  explicit ColorSpaceManagerImpl(ManagerSession session) : session_(session) {}

  void apply(const ColorSpaceChange &change) override {
    switch (change.target) {
      case ColorSpaceChange::Target::Input:
        setInputSpace(change.index);
        return;
      case ColorSpaceChange::Target::Preview:
        setPreviewSpace(change.index);
        return;
      case ColorSpaceChange::Target::Export:
        setExportSpace(change.index);
        return;
      case ColorSpaceChange::Target::ExportIntent:
        setExportIntent(change.index);
        return;
      case ColorSpaceChange::Target::PreviewResolution:
        setPreviewRes(change.index);
        return;
    }
  }

  void setInputSpace(int index) override {
    session_.document.inputSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
    // The chain carries this space into its first plugin, and a later plugin can
    // change what leaves the chain, so the whole walk and a render are needed.
    syncChainColorSpaces(session_);
    session_.scheduleRender();
  }

  void setPreviewSpace(int index) override {
    // The preview is the destination of the last conversion: the chain output is
    // encoded into this space and shown as it is. Only the display conversion
    // changes, so a re-render is not needed.
    session_.gui.previewSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
    session_.scheduleRecolor();
  }

  void setExportSpace(int index) override {
    session_.gui.exportSpace = outputSpace(std::clamp(index, 0, kColorSpaceCount - 1));
  }

  void setExportIntent(int intentIndex) override {
    session_.gui.exportIntent =
        static_cast<RenderIntent>(std::clamp(intentIndex, 0, kRenderIntentCount - 1));
  }

  void setPreviewRes(int index) override {
    session_.gui.previewRes = static_cast<PreviewRes>(std::clamp(index, 0, kPreviewResCount - 1));
    session_.rebuildPreviewImage();
  }

 private:
  ManagerSession session_;
};

}  // namespace

std::unique_ptr<ColorSpaceManager> createColorSpaceManager(ManagerSession session) {
  return std::make_unique<ColorSpaceManagerImpl>(session);
}
