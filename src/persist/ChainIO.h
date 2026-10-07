// Adapter between the node chain and the persistence layer. It is the only
// module that knows both, which keeps NodeGraph free of ProjectPersist and
// ProjectPersist free of the OFX instance model.
#pragma once

#include "AppState.h"
#include "persist/ProjectPersist.h"

PersistChain captureChain(const App &app);
void applyChain(App &app, const PersistChain &chain);
