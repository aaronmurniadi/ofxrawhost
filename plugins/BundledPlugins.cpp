#include "BundledPlugins.h"

namespace OFX {
namespace Plugin {

void getPluginIDs(OFX::PluginFactoryArray &ids) {
  registerTransformPlugin(ids);
  registerAutoExposurePlugin(ids);
}

}  // namespace Plugin
}  // namespace OFX
