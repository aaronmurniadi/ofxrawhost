#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void MacPinch_Install(void);
void MacPinch_Shutdown(void);

// Sum of NSEvent.magnification since last call; clears the accumulator.
// Scale factor is (1 + returned value).
float MacPinch_Consume(void);

#ifdef __cplusplus
}
#endif
