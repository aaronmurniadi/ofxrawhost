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
  syncOutputTag(app);
  scheduleRender(app);
}
