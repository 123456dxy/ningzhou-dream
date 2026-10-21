#pragma once
#include "core/model.h"

namespace nz {
inline constexpr int MaxPillCountPerBatch = 1000;
// Progression value snapshot; no external resources. All counts describe one recipe.
struct PillBatchQuote {
    bool Enabled = false;
    string Reason, RecipeId, StopReason, Effect;
    int OwnedCount = 0, MaxUsableCount = 0, RequestedCount = 0;
    double TotalEffectValue = 0;
    bool IsPreparation = false;
    string EffectUnit;
    int AdditionalDaoPoints = 0, PoisonAfter = 0, PoisonDelta = 0, UsedAfter = 0;
    int ToleranceRemainingAfter = -1;
};
PillBatchQuote mfQuotePillBatch(const Game& State, const string& RecipeId, int Count);
string mfUsePillBatch(Game& State, const string& RecipeId, int Count);
}
