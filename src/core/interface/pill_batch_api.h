#pragma once
#include "core/model.h"
#include "core/json.h"

namespace nz::api {
std::string mfReadPillBatchRequest(const json::Value& Input, std::string& RecipeId, int& Count);
json::Value mfPillBatchQuoteView(const Game& State, const std::string& RecipeId, int Count);
std::string mfExecutePillBatch(Game& State, const json::Value& Input);
}
