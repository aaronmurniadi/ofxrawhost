#include "ui/ParamEdit.h"

#include "AppState.h"
#include "ParamBridge.h"
#include "RenderScheduler.h"

double paramScalar(const Param *p, size_t index) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  return p->v[index];
}

std::string paramText(const Param *p) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  return p->s;
}

void setParamScalar(Param *p, size_t index, double value) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  p->v[index] = value;
}

void setParamString(Param *p, const std::string &text) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  p->s = text;
}

void resetParamToDefault(Param *p) {
  std::lock_guard<std::mutex> lock(gValueMutex);
  if (paramIsString(p->kind)) {
    p->s = p->ui.defaultString;
    return;
  }
  for (size_t i = 0; i < p->v.size() && i < p->ui.defaults.size(); ++i) p->v[i] = p->ui.defaults[i];
}

void commitParamEdit(App &app, Node &node, Param *p) {
  notifyChanged(node, p);
  syncChainColorSpaces(app);
  scheduleRender(app);
}

int resetNodeParamsToDefaults(App &app, Node &node) {
  if (!node.instance) return 0;
  std::vector<Param *> reset;
  for (auto &up : node.instance->params) {
    Param *p = up.get();
    if (p->ui.secret) continue;
    if (p->kind == ParamType::Group || p->kind == ParamType::Page || p->kind == ParamType::PushButton) continue;
    // The host drives the color-space parameters from the document and the
    // chain, so a plugin reset must not move them. syncChainColorSpaces below
    // puts them back on the host values.
    const std::string label = sprop(p->props, kOfxPropLabel);
    if (label == kInputColorSpaceLabel || label == kOutputColorSpaceLabel) continue;
    resetParamToDefault(p);
    reset.push_back(p);
  }
  // Notify after the values change, so the plugin sees the whole new state.
  for (Param *p : reset) notifyChanged(node, p);
  syncChainColorSpaces(app);
  scheduleRender(app);
  return (int)reset.size();
}
