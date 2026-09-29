#include "UI.h"

#include "Actions.h"
#include "AppState.h"
#include "persist/DocumentActions.h"
#include "Filmstrip.h"
#include "NodeGraph.h"
#include "ofx/OfxHost.h"
#include "RenderPipeline.h"
#include "ui/ImGuiBackend.h"
#include "ui/UiFrame.h"

#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include "ui/MacPinch.h"
#endif

// ImGui OpenGL3 backend loads GL symbols; do not include gl.h/gl3.h here.

int runApp(const std::string &optionalPath) {
  if (!glfwInit()) return 1;
#if defined(__APPLE__)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#endif

  App app;
  app.window = glfwCreateWindow(1400, 900, "OFX Raw Host", nullptr, nullptr);
  if (!app.window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(app.window);
  glfwSwapInterval(1);
#if defined(__APPLE__)
  MacPinch_Install();
#endif

  ImGuiBackend_Init(app.window, app.themeIndex);

  gOnMessage = [&app](const std::string &msg) { app.setStatus(msg); };
  loadPlugins();
  if (gPlugins.empty())
    app.setStatus("No OFX filter plugins found in the default OFX path or OFX_PLUGIN_PATH");
  else
    app.setStatus("Add plugins with + to build a processing chain.");
  if (!optionalPath.empty()) openPath(app, optionalPath);

  app.renderThread = std::thread(renderWorker, &app);
  app.thumbThread = startFilmstripThumbThread(&app);

  glfwSetDropCallback(app.window, [](GLFWwindow *w, int count, const char **paths) {
    auto *app = static_cast<App *>(glfwGetWindowUserPointer(w));
    if (app && count > 0) openPath(*app, paths[0]);
  });
  glfwSetWindowUserPointer(app.window, &app);

  while (!glfwWindowShouldClose(app.window)) {
    glfwPollEvents();
    pumpDisplayUpload(app);

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
  app.renderCv.notify_one();
  app.thumbCv.notify_one();
  if (app.renderThread.joinable()) app.renderThread.join();
  if (app.thumbThread.joinable()) app.thumbThread.join();
  saveCurrentInputSidecar(app);
  persistWorkspace(app);
  clearNodes(app);

  freeFilmstripTextures(app.filmstrip);
  if (app.tex) glDeleteTextures(1, &app.tex);
  ImGuiBackend_Shutdown();
#if defined(__APPLE__)
  MacPinch_Shutdown();
#endif
  glfwDestroyWindow(app.window);
  glfwTerminate();
  return 0;
}
