#import <AppKit/AppKit.h>

#include "MacPinch.h"

static float g_mag = 0.0f;
static id g_monitor = nil;

void MacPinch_Install(void) {
  if (g_monitor) return;
  g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMagnify
                                                    handler:^NSEvent *(NSEvent *event) {
    g_mag += (float)[event magnification];
    return nil;  // swallow; GLFW has no magnify handler
  }];
}

void MacPinch_Shutdown(void) {
  if (g_monitor) {
    [NSEvent removeMonitor:g_monitor];
    g_monitor = nil;
  }
  g_mag = 0.0f;
}

float MacPinch_Consume(void) {
  const float v = g_mag;
  g_mag = 0.0f;
  return v;
}
