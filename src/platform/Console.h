#pragma once

// Connects a Windows GUI build to the console of the process that started it, so
// warnings and self-test results reach the terminal. The Windows release is a GUI
// executable, which has no console of its own, and this keeps the output visible
// when the host is started from a command prompt.
//
// Redirected streams are left untouched, and a launch without a parent console
// stays silent. Does nothing on other platforms.
void attachParentConsole();
