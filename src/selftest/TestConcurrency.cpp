#include "selftest/TestCases.h"
#include "selftest/TestSupport.h"

#include "app/AppState.h"
#include "services/NodeGraph.h"
#include "services/managers/export/ExportManager.h"
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
  TestBootstrap wired(app);
  app.document.full = src;
  app.document.preview = src;
  app.render.start();
  if (!addNode(app.session(), transformPi)) return fail("concurrency addNode 1");
  for (int pi : usable)
    if (pi != transformPi && !addNode(app.session(), pi)) return fail("concurrency addNode 2");

  // UI-side queryOutputSize runs Transform's getRoD, whose clip-RoD suite reads must
  // not race the worker's dim writes in renderEffect/queryOutputSize.
  for (int k = 0; k < 8; ++k) {
    app.scheduleRender();
    int ow = 0, oh = 0;
    queryOutputSize(plugins()[app.chain.nodes[0].pluginIndex].plugin, app.chain.nodes[0].instance.get(), app.document.preview.w,
                    app.document.preview.h, &ow, &oh);
    if (ow != app.document.preview.w || oh != app.document.preview.h) return fail("concurrent queryOutputSize");
  }
  app.render.waitIdle();

  const fs::path out = fs::temp_directory_path() / "ofxrawhost-selftest-export.png";
  for (int i = 0; i < 200; ++i) {
    app.scheduleRender();
    if (i % 11 == 0) app.scheduleRecolor();
    if (i % 13 == 0) {
      // UI keeps writing the preview destination while renders run (the
      // Plugins panel colorspace combo).
      if (app.gui.previewSpace == ColorSpace::sRGB) app.gui.previewSpace = ColorSpace::LinearRec709;
      else app.gui.previewSpace = ColorSpace::sRGB;
      syncChainColorSpaces(app.session());
    }
    if (i % 17 == 0) app.rebuildPreviewImage();  // preview rewrite must wait out the worker
    if (i == 50) {
      app.exports->doExport(out.string());
      // Mutating the graph right after an export must wait it out, not race it.
      if (app.chain.nodes.size() > 1) destroyNode(app.session(), 0);
      else addNode(app.session(), usable[i % (int)usable.size()]);
    }
    if (i % 7 == 0) {
      app.render.waitIdle();
      if (app.chain.nodes.size() > 1) destroyNode(app.session(), 0);
      else addNode(app.session(), usable[i % (int)usable.size()]);
      if (app.chain.nodes.size() > 1) moveNode(app.session(), 0, (int)app.chain.nodes.size() - 1);
    }
  }
  app.render.waitIdle();  // also waits out the export thread
  app.render.stop();
  app.exporter.wait();
  if (!fs::exists(out) || fs::file_size(out) == 0) return fail("concurrent export output");
  fs::remove(out);
  const fs::path side = exportSidecarPath(out.string());
  if (fs::exists(side)) fs::remove(side);
  clearNodes(app.session());
  printf("ok  concurrency\n");
  return 0;
}

// The render worker owns its stop flag and reports into a sink its owner declares
// first. This checks the two things a structural change can silently lose: that
// stopping is the worker's own business, and that the sink is still usable after
// the thread is gone.
int testWorkerShutdown() {
  StatusSink status;
  RenderWorker worker(status);

  Image preview;
  preview.w = 8;
  preview.h = 6;
  preview.px.assign((size_t)preview.w * preview.h * 4, 0.5f);
  Image full = preview;

  RenderInputs inputs;
  inputs.full = &full;
  inputs.preview = &preview;
  inputs.workingSpace = ColorSpace::LinearRec709;
  inputs.previewSpace = ColorSpace::sRGB;
  inputs.previewMaxEdge = 8;

  // No chain: the request publishes the source preview, no thread needed.
  worker.request(inputs);
  if (worker.displayCopy().px.empty()) return fail("worker shutdown: no source preview published");

  worker.setOutputSpace(ColorSpace::sRGB);
  if (worker.outputSpace() != ColorSpace::sRGB) return fail("worker shutdown: output space not stored");

  worker.start();
  if (worker.stopping()) return fail("worker shutdown: reports stopped before start");
  worker.request(inputs);
  worker.stop();
  if (!worker.stopping()) return fail("worker shutdown: stop did not raise the flag");
  worker.stop();      // idempotent
  worker.waitIdle();  // safe with no thread running

  status.report("after stop");
  if (status.text() != "after stop") return fail("worker shutdown: sink lost the last message");
  printf("ok  worker shutdown\n");
  return 0;
}
