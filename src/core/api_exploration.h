#pragma once
#include "core/json.h"
#include "core/model.h"
namespace nz::api {
json::Value dungeonView(const Game& g);
json::Value caveView(const Game& g);
json::Value checkpointView(const Game& g);
}
