#pragma once
#include "core/sim.h"

namespace nz {
struct PlayerPortraitDef {
    const char* id;
    const char* name;
    const char* gender;
    const char* src;
};
// Server-owned IDs map only to bundled human portraits; URLs are never accepted.
const vector<PlayerPortraitDef>& playerPortraits();
const PlayerPortraitDef* findPlayerPortrait(const string& id);
string defaultPlayerPortraitId(const string& gender);
string playerPortrait(const Player& player);
// Normalize defaults once for both preview and creation. Invalid choices throw.
CreateOptions resolveCreateOptions(const CreateOptions& options);
} // namespace nz
