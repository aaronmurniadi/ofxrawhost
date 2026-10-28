// Single home for reading and writing OFX parameter values, and for the
// invariant a completed edit must satisfy: notify the plugin, re-derive the
// chain color spaces, and schedule a re-render. Widgets call these instead of
// repeating the value-lock / notify / sync / schedule sequence.
#pragma once

#include "ofx/OfxTypes.h"

#include <cstddef>
#include <string>

struct ManagerSession;
struct Node;

// Reads. Hold the value lock while copying.
double paramScalar(const Param *p, size_t index);
std::string paramText(const Param *p);

// Writes. Hold the value lock for the assignment.
void setParamScalar(Param *p, size_t index, double value);
void setParamString(Param *p, const std::string &text);
// Restores the cached OFX defaults (numeric or string, per the parameter kind).
void resetParamToDefault(Param *p);

// Restores every value parameter of node to its OFX default, keeps the host
// color-space parameters on the values the host drives, notifies the plugin, and
// schedules a render. Returns the number of parameters it reset.
int resetNodeParamsToDefaults(ManagerSession session, Node &node);

// Applies one completed edit end to end.
void commitParamEdit(ManagerSession session, Node &node, Param *p);
