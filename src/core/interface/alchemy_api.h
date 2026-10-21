#pragma once
#include "core/model.h"
#include "core/json.h"
#include "core/crafting/alchemy_rules.h"

namespace nz::api {
json::Value mfAlchemyQuoteView(const crafting::AlchemyQuote& Quote);
std::string mfReadAlchemyQuote(const Game& State, const json::Value& Input, json::Value& Output);
}
