#pragma once

#include <string>
#include <vector>

namespace nz {
// Exploration owns the tournament schedule and the frozen three sect opponents.
// A default state is an uninitialized old save. Active implies one live round
// and forbids all other activities; updates run on the owning Game's serial loop.
struct AscensionTournamentState {
    int FirstYear = -1;
    int OfferedYear = -1;
    int AttemptedYear = -1;
    int Round = 0;
    bool Active = false;
    std::vector<std::string> OpponentSects;
};
} // namespace nz
