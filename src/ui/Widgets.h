#pragma once

#include <string>

bool iconBtn(const char *id, const char *icon);

// Dropdown of fixed choices that moves the highlighted row with the up and down
// arrow keys and applies the highlighted choice at once, instead of waiting for
// Enter or a click. The keys step over a choice that `enabled` marks false, and
// a null list means every choice can be picked. The popup scrolls so that the
// highlighted row stays in view in a long list, and the mouse wheel still moves
// the list on its own. `preview` is the text the closed dropdown shows, or null
// to show the highlighted label. `cur` holds the chosen index on entry and on
// return. Returns true when the value changed.
bool uiChoiceCombo(const char *label, const char *preview, const char *const *labels, int count, int *cur,
                   const bool *enabled = nullptr);

bool icontains(const std::string &hay, const std::string &needle);
