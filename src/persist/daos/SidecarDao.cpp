#include "persist/daos/SidecarDao.h"

namespace {

class SidecarDaoImpl : public SidecarDao {
 public:
  bool load(const std::string &imagePath, PersistSidecar &out) override {
    return loadInputSidecar(imagePath, out);
  }

  bool save(const std::string &imagePath, ColorSpace fileSpace, ColorSpace chainInputSpace,
            const PersistChain &chain) override {
    return saveInputSidecar(imagePath, fileSpace, chainInputSpace, chain);
  }

  bool saveExport(const std::string &exportPath, const std::string &sourceImagePath, ColorSpace fileSpace,
                  const PersistChain &chain) override {
    return saveExportSidecar(exportPath, sourceImagePath, fileSpace, chain);
  }
};

}  // namespace

std::unique_ptr<SidecarDao> createSidecarDao() { return std::make_unique<SidecarDaoImpl>(); }
