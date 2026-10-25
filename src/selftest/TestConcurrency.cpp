#include "selftest/TestCases.h"
#include "selftest/TestSupport.h"

#include "app/AppState.h"
#include "services/Export.h"
#include "services/NodeGraph.h"
#include "services/ParamBridge.h"
#include "engine/RenderWorker.h"
#include "codecs/ImageIO.h"
#include "ofx/OfxHost.h"
#include "persist/ProjectPersist.h"

#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// Concurrency smoke: render worker vs. graph mutation vs. a full-res export
// on its detached thread. No window/GL: preview display stays in memory.
int testConcurrency(const Image &src) {
  std::vector<int> usable;
  for (int i = 0; i < (int)plugins().size(); ++i)
    if (!selftestExcluded(plugins()[i])) usable.push_back(i);
  if (usable.empty()) return fail("concurrency: no usable plugins");

  int transformPi = -1;
  for (int pi = 0; pi < (int)plugins().size(); ++pi)
    if (plugins()[pi].label == "Transform") transformPi = pi;
  if (transformPi < 0) return fail("concurrency: Transform plugin missing");

  App app;
  app.document.full = src;
  app.document.preview = src;
  app.render.start();
  if (!addNode(app, transformPi)) return fail("concurrency addNode 1");
  for (int pi : usable)
    if (pi != transformPi && !addNode(app, pi)) return fail("concurrency addNode 2");

  // UI-side queryOutputSize runs Transform's getRoD, whose clip-RoD suite reads must
  // not race the worker's dim writes in renderEffect/queryOutputSize.
  for (int k = 0; k < 8; ++k) {
    app.render.request();
    int ow = 0, oh = 0;
    queryOutputSize(plugins()[app.chain.nodes[0].pluginIndex].plugin, app.chain.nodes[0].instance.get(), app.document.preview.w,
                    app.document.preview.h, &ow, &oh);
    if (ow != app.document.preview.w || oh != app.document.preview.h) return fail("concurrent queryOutputSize");
  }
  app.render.waitIdle();

  const fs::path out = fs::temp_directory_path() / "ofxrawhost-selftest-export.png";
  for (int i = 0; i < 200; ++i) {
    app.render.request();
    if (i % 11 == 0) app.render.recolor();
    if (i % 13 == 0) {
      // UI keeps writing the preview destination while renders run (the
      // Plugins panel colorspace combo).
      if (app.gui.previewSpace == ColorSpace::sRGB) app.gui.previewSpace = ColorSpace::LinearRec709;
      else app.gui.previewSpace = ColorSpace::sRGB;
      syncChainColorSpaces(app);
    }
    if (i % 17 == 0) app.render.rebuildPreview();  // preview rewrite must wait out the worker
    if (i == 50) {
      doExport(app, out.string());
      // Mutating the graph right after an export must wait it out, not race it.
      if (app.chain.nodes.size() > 1) destroyNode(app, 0);
      else addNode(app, usable[i % (int)usable.size()]);
    }
    if (i % 7 == 0) {
      app.render.waitIdle();
      if (app.chain.nodes.size() > 1) destroyNode(app, 0);
      else addNode(app, usable[i % (int)usable.size()]);
      if (app.chain.nodes.size() > 1) moveNode(app, 0, (int)app.chain.nodes.size() - 1);
    }
  }
  app.render.waitIdle();  // also waits out the export thread
  app.quit = true;
  app.render.schedule().cv.notify_one();
  app.render.stop();
  app.exporter.wait();
  if (!fs::exists(out) || fs::file_size(out) == 0) return fail("concurrent export output");
  fs::remove(out);
  const fs::path side = exportSidecarPath(out.string());
  if (fs::exists(side)) fs::remove(side);
  clearNodes(app);
  printf("ok  concurrency\n");
  return 0;
}
