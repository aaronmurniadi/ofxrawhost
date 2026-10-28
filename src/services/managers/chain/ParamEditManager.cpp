#include "services/managers/chain/ParamEditManager.h"

#include "services/ManagerSession.h"
#include "services/ParamEdit.h"

namespace {

class ParamEditManagerImpl : public ParamEditManager {
 public:
  explicit ParamEditManagerImpl(ManagerSession session) : session_(session) {}

  void commit(Node &node, Param *p) override { commitParamEdit(session_, node, p); }

  int resetToDefaults(Node &node) override { return resetNodeParamsToDefaults(session_, node); }

 private:
  ManagerSession session_;
};

}  // namespace

std::unique_ptr<ParamEditManager> createParamEditManager(ManagerSession session) {
  return std::make_unique<ParamEditManagerImpl>(session);
}
