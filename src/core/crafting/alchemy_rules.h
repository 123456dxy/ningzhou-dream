#pragma once
#include "core/model.h"

namespace nz::crafting {
// Crafting value snapshots, no resources or retained state; re-quote at commit.
struct AlchemyIngredient {
    string Id, Name;
    int Required = 0, Owned = 0, Missing = 0;
};
struct AlchemyQuote {
    bool Valid = false, Enabled = false;
    string Reason, RecipeId, Name;
    int BatchCount = 0, StoneCost = 0, DurationDays = 0, DurabilityCost = 0;
    int OutputPerSuccess = 0;
    double SuccessProbability = 0, ExpectedOutput = 0;
    vector<AlchemyIngredient> Ingredients;
};
AlchemyQuote mfQuoteAlchemy(const Game& State, const string& RecipeId, int BatchCount);
}
