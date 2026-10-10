#pragma once

// Size of the user-interface font that the operating system uses, in points.
//
// The host scales this value for its own "Smaller", "Normal", and "Bigger"
// choices, so the interface starts at the native size on a new install rather
// than at a number the host picked. The caller multiplies the result by the
// monitor scale, so the value is in points at 100% scale.
//
// Falls back to a platform default when the operating system value cannot be
// read, and never returns a value below 1.
float systemUiFontSizePt();
