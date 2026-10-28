// Disk access for image and export sidecars. Managers call this instead of the
// ProjectPersist free functions.
#pragma once

#include "persist/ProjectPersist.h"

#include <memory>
#include <string>

enum class ColorSpace;

class SidecarDao {
 public:
  virtual ~SidecarDao() = default;
  virtual bool load(const std::string &imagePath, PersistSidecar &out) = 0;
  virtual bool save(const std::string &imagePath, ColorSpace fileSpace, ColorSpace chainInputSpace,
                    const PersistChain &chain) = 0;
  virtual bool saveExport(const std::string &exportPath, const std::string &sourceImagePath,
                          ColorSpace fileSpace, const PersistChain &chain) = 0;
};

std::unique_ptr<SidecarDao> createSidecarDao();
