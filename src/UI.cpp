#include "UI.h"

#include "Actions.h"
#include "AppState.h"
#include "persist/DocumentActions.h"
#include "Filmstrip.h"
#include "NodeGraph.h"
#include "ofx/OfxHost.h"
#include "platform/Report.h"
#include "RenderScheduler.h"
#include "UpdateCheck.h"
#include "ui/ImGuiBackend.h"
#include "ui/UiFrame.h"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#include "ui/MacPinch.h"
#endif

// ImGui OpenGL3 backend loads GL symbols; do not include gl.h/gl3.h here.

// A virtual machine, a remote desktop session, and a machine with only the
// generic display driver can lack an OpenGL 3.3 driver. The interface then draws
// through the fixed-function pipeline of the OpenGL2 backend, and this variable
// with a value of 1 forces that path so a test can cover it.
static bool legacyGlRequested() {
  const char *env = std::getenv("OFX_HOST_LEGACY_GL");
  return env && env[0] == '1';
}

static void requestCoreContext() {
#if defined(__APPLE__)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
#endif
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
}

static std::string glfwErrorText() {
  const char *desc = nullptr;
  glfwGetError(&desc);
  if (desc && desc[0]) return desc;
  return "the window system reported no detail";
}

int runApp(const std::string &optionalPath) {
  if (!glfwInit()) {
    reportFatalError("OFX Raw Host could not start the window system: " + glfwErrorText());
    return 1;
  }

  bool legacyGl = legacyGlRequested();
  if (!legacyGl) requestCoreContext();
  GLFWwindow *window = glfwCreateWindow(1400, 900, "OFX Raw Host", nullptr, nullptr);
  if (!window && !legacyGl) {
    // No 3.3 driver here. A legacy context still draws the interface, so retry
    // with the default hints before giving up.
    glfwDefaultWindowHints();
    window = glfwCreateWindow(1400, 900, "OFX Raw Host", nullptr, nullptr);
    legacyGl = true;
  }
  if (!window) {
    reportFatalError(
        "OFX Raw Host could not create its window: " + glfwErrorText() +
        "\n\nThe host needs a graphics driver with OpenGL 3.3, or a legacy OpenGL 1.x "
        "driver. Update the graphics driver of this machine, or run the host outside a "
        "remote desktop session.");
    glfwTerminate();
    return 1;
  }

  App app;
  app.window = window;
  glfwMakeContextCurrent(app.window);
  glfwSwapInterval(1);
#if defined(__APPLE__)
  MacPinch_Install();
#endif

  // Restore the per-user preferences before the ImGui backend applies the theme
  // and loads the font, so the saved theme, font, and view show on the first
  // frame. A file or a directory opens later and overrides the view and the
  // export settings, but never the theme or the font.
  {
    PersistGui saved;
    PersistAppearance savedAppearance;
    PersistUpdates savedUpdates;
    // Without a settings file the channel follows the kind of build this is.
    savedUpdates.channel = static_cast<int>(defaultUpdateChannel());
    if (loadGlobalSettings(saved, savedAppearance, savedUpdates)) {
      applyGui(app, saved);
      applyAppearance(app, savedAppearance);
      applyUpdates(app, savedUpdates);
    }
  }

  ImGuiBackend_Init(app.window, legacyGl, app.gui.themeIndex, app.gui.uiFontPt);

  // A development build warns once, before the check it is about to start.
  if (isDevelopmentBuild() && !app.gui.devBuildWarningShown) app.gui.showDevBuildWarning = true;
  // The check runs on its own thread, so the first frame does not wait for the
  // network, and its answer appears in Settings, Updates.
  if (app.gui.checkUpdatesAtStartup) app.updates.startCheck(hostVersion(), app.gui.updateChannel);

  gOnMessage = [&app](const std::string &msg) { app.setStatus(msg); };
  loadPlugins();
  if (gPlugins.empty())
    app.setStatus("No OFX filter plugins found in the default OFX path or OFX_PLUGIN_PATH");
  else
    app.setStatus("Add plugins with + to build a processing chain.");
  if (!optionalPath.empty()) openPath(app, optionalPath);

  app.render.thread = std::thread(renderWorker, &app);
  app.filmstrip.thread = startFilmstripThumbThread(&app.filmstrip, &app.quit);

  glfwSetDropCallback(app.window, [](GLFWwindow *w, int count, const char **paths) {
    auto *app = static_cast<App *>(glfwGetWindowUserPointer(w));
    if (app && count > 0) openPath(*app, paths[0]);
  });
  glfwSetWindowUserPointer(app.window, &app);

  while (!glfwWindowShouldClose(app.window)) {
    glfwPollEvents();
    pumpDisplayUpload(app);
    pumpFilmstripThumbs(app.filmstrip);
    app.updates.pump();

    // Rebuild the font atlas between frames. The atlas owns the font objects the
    // previous frame referenced, so a rebuild during a frame leaves that frame
    // with a dangling font.
    if (app.gui.fontApplyPending) {
      ImGuiBackend_SetUIFont(app.window, app.gui.uiFontPt);
      app.gui.fontApplyPending = false;
    }

    ImGuiBackend_NewFrame();
    drawUiFrame(app);

    int dw, dh;
    glfwGetFramebufferSize(app.window, &dw, &dh);
    glViewport(0, 0, dw, dh);
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGuiBackend_Render();
    glfwSwapBuffers(app.window);
  }

  app.quit = true;
  app.render.schedule.cv.notify_one();
  app.filmstrip.cv.notify_one();
  if (app.render.thread.joinable()) app.render.thread.join();
  if (app.filmstrip.thread.joinable()) app.filmstrip.thread.join();
  if (app.render.exportThread.joinable()) app.render.exportThread.join();
  saveCurrentInputSidecar(app);
  persistWorkspace(app);
  clearNodes(app);

  freeFilmstripTextures(app.filmstrip);
  app.tex.destroy();
  ImGuiBackend_Shutdown();
#if defined(__APPLE__)
  MacPinch_Shutdown();
#endif
  glfwDestroyWindow(app.window);
  glfwTerminate();
  return 0;
}
