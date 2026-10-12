#pragma once
// Private persistence boundary. Callers outside persistence use core/save.h.
#include "core/model.h"
#include "core/json.h"
#include "core/save.h"

namespace nz::persistence {
using json::Value;

// Model codecs preserve saved keys, wire values and historical defaults.
Value personToJson(const Person& person);
void personFromJson(const Value& value, Person& person);
Value playerToJson(const Player& player);
void playerFromJson(const Value& value, Player& player, uint32_t seed);
Value npcToJson(const Npc& npc);
Npc npcFromJson(const Value& value);
Value artsToJson(const Arts& arts);
void artsFromJson(const Value& value, Arts& arts);
Value battleToJson(const Battle& battle);
void battleFromJson(const Value& value, Battle& battle);
Value dungeonWorldToJson(const DungeonWorld& world);
void dungeonWorldFromJson(const Value& value, DungeonWorld& world);
Value gameToJson(const Game& game);
Game gameFromJson(const Value& root);

// Shape validation runs before codecs; state validation runs after migration.
bool saveInteger(const Value& value, double low, double high);
bool validSaveNumbers(const Value& value, const std::string& field = "");
bool saveShapesValid(const Value& root);
std::string saveStateError(const Game& game);
// Detached loads retain the original validation and skip world initialization.
void migrateLegacySave(const Value& root, Game& game);
void completeLoadedSave(const Value& root, Game& game, bool detached);
} // namespace nz::persistence
