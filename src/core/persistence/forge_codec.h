#pragma once
#include "core/model.h"
#include "core/json.h"

namespace nz::persistence {
json::Value mfGearToJson(const Gear& Item);
Gear mfGearFromJson(const json::Value& Input);
bool mfForgeGearShapeValid(const json::Value& Input);
string mfForgeGearStateError(const Gear& Item);
json::Value mfArtifactSnapshotToJson(const ArtifactCombatSnapshot& Snapshot);
ArtifactCombatSnapshot mfArtifactSnapshotFromJson(const json::Value& Input);
bool mfArtifactSnapshotShapeValid(const json::Value& Input);
bool mfArtifactSnapshotStateValid(const ArtifactCombatSnapshot& Snapshot);
}
