#pragma once

#include <string>

// Reports a failure that stops the host from starting, where the user will see
// it. The message goes to the console when one is attached, and a Windows GUI
// build shows a message box when there is no console to print to.
void reportFatalError(const std::string &message);
