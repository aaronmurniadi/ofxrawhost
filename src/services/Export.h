// Export service: runs the full-resolution render, the conversion into the
// chosen export space, and the write, on a worker thread.
//
// The interface calls doExport and gets a status string back through the
// workspace status, so this module never sees a widget. Workspace is
// forward-declared on purpose: the orchestration reads several state units, and
// the snapshot the worker takes is the only place that does.
#pragma once

#include <string>

struct Workspace;

bool canExport(const Workspace &app);
// Name the save dialog offers: the source stem with an _export suffix, so the
// default never overwrites the original image.
std::string defaultExportName(const Workspace &app);
// Appends the format extension when outPath has none, then exports.
void doExport(Workspace &app, const std::string &outPath);
