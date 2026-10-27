#include "services/PluginPresets.h"

#include "core/RenderContext.h"
#include "ofx/OfxRegistry.h"
#include "ofx/SpektraPreset.h"
#include "services/ParamBridge.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// Removes the leading and trailing whitespace that a plugin message may carry.
std::string trimSpace(const std::string &text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

// Holds what the plugin reports while its Export LUT handler runs. The plugin
// owns the message text, and the builds differ in case and in what follows the
// path, so the host keys on the marker that all builds share and takes the first
// line after it. A render worker can raise its own message at the same time, so
// the capture sits behind a lock and a shared pointer: a late message keeps the
// state alive instead of reading a destroyed stack frame.
struct LutExportCapture {
  std::mutex lock;
  std::string produced;
  bool reported = false;
};

}  // namespace

bool isHostRunButton(const Node &node, const Param *p, const char *name) {
  if (p->kind != ParamType::PushButton || p->name != name) return false;
  if (node.pluginIndex < 0 || node.pluginIndex >= (int)plugins().size()) return false;
  OfxPlugin *plugin = plugins()[(size_t)node.pluginIndex].plugin;
  if (!plugin) return false;
  return isSpektrafilmPlugin(plugin->pluginIdentifier);
}

PresetImportResult importSpektrafilmPreset(Node &node, const std::string &path) {
  PresetImportResult result;
  std::string error;
  std::vector<Param *> changed = applySpektrafilmPreset(path, node.instance.get(), error);
  if (!error.empty()) {
    result.message = "Preset load failed: " + error;
    return result;
  }
  if (changed.empty()) {
    result.message = "Preset has no parameters this plugin knows";
    return result;
  }
  for (Param *p : changed) notifyChanged(node, p);
  result.changed = (int)changed.size();
  result.message = "Loaded " + std::to_string(result.changed) + " parameters from " +
                   fs::path(path).filename().string();
  return result;
}

LutExportResult runSpektrafilmLutExport(Node &node, Param *button) {
  static const std::string kMarker = "LUT exported: ";
  auto capture = std::make_shared<LutExportCapture>();
  // The sink is installed before notifyChanged and removed after it, so a message
  // the plugin raises during the call reaches both the capture and the host.
  std::function<void(const std::string &)> previous;
  previous = renderContext().swapMessageSink([capture, &previous](const std::string &msg) {
    {
      std::lock_guard<std::mutex> guard(capture->lock);
      capture->reported = true;
      const size_t at = msg.find(kMarker);
      if (at != std::string::npos) {
        // The path sits on the first line, and some builds add a note below it.
        std::string rest = msg.substr(at + kMarker.size());
        const size_t lineEnd = rest.find_first_of("\r\n");
        if (lineEnd != std::string::npos) rest.resize(lineEnd);
        capture->produced = trimSpace(rest);
      }
    }
    if (previous) previous(msg);
  });
  notifyChanged(node, button);
  renderContext().setMessageSink(previous);
  std::lock_guard<std::mutex> guard(capture->lock);
  LutExportResult result;
  result.produced = capture->produced;
  result.reported = capture->reported;
  return result;
}

std::string placeLutExport(const std::string &produced, const std::string &target) {
  if (produced == target) return "Exported " + target;
  std::error_code ec;
  std::filesystem::rename(produced, target, ec);
  if (ec) {
    // A rename fails across volumes, so copy first and delete only on success.
    std::error_code copyEc;
    std::filesystem::copy_file(produced, target, std::filesystem::copy_options::overwrite_existing, copyEc);
    if (copyEc) return "Could not place the LUT at " + target + ": " + copyEc.message();
    std::error_code removeEc;
    std::filesystem::remove(produced, removeEc);
  }
  return "Exported " + target;
}

const char *presetDialogFilter() { return "*.spkpreset"; }

const char *lutDialogFilter() { return "*.cube"; }
