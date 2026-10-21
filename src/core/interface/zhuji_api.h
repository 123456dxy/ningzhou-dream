#pragma once
#include "core/model.h"
#include "core/json.h"

namespace nz::api::view {
json::Value mfZhujiView(const Game& State);
}
namespace nz::api {
// Queries never checkpoint or mutate. Mutations validate revision and idempotency before committing.
std::string mfExecuteZhujiCommand(Game& State, const json::Value& Input, json::Value& Output);
}
