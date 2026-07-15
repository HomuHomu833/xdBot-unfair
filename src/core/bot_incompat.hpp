#pragma once

namespace bot_incompat {
bool hasIncompatibleMods();

bool enabledIncompatibleGDSettings();

// True when Click Between Steps is active for the current level, either via the
// global game variable or a per-level settings override (mobile).
bool clickBetweenStepsEnabled();

void autoDisableBotSettings();
void restoreAutoDisabledSettings();
} // namespace bot_incompat
