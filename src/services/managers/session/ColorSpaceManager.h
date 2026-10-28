// Color space and preview-resolution commands.
#pragma once

#include <memory>

struct ManagerSession;

class ColorSpaceManager {
 public:
  virtual ~ColorSpaceManager() = default;
  virtual void setInputSpace(int index) = 0;
  virtual void setPreviewSpace(int index) = 0;
  virtual void setExportSpace(int index) = 0;
  virtual void setExportIntent(int intentIndex) = 0;
  virtual void setPreviewRes(int index) = 0;
};

std::unique_ptr<ColorSpaceManager> createColorSpaceManager(ManagerSession session);
