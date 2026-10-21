#pragma once

#include "core/exploration/ascension_state.h"
#include "core/json.h"

namespace nz::persistence {
json::Value mfAscensionTournamentToJson(const AscensionTournamentState& State);
bool mfAscensionTournamentFromJson(const json::Value& Input, AscensionTournamentState& Output,
    std::string& Error);
// Missing fields are handled by playerFromJson; explicit null is invalid.
bool mfAscensionTournamentShapeValid(const json::Value& Input);
} // namespace nz::persistence
