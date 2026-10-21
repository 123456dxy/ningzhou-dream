#pragma once

#include "core/model.h"

namespace nz {
inline constexpr int AscensionTournamentRoundCount = 4;
inline constexpr int AscensionTournamentIntervalYears = 5;
inline constexpr int AscensionTournamentMaximumAge = 40;
inline constexpr int AscensionTournamentRewardStones = 500;
inline constexpr int AscensionTournamentRecoveryPercent = 20;
inline constexpr const char* AscensionTournamentPillId = "5205";
inline constexpr const char* AscensionTournamentBattleKind = "ascensionTournament";

// Pure schedule/entry queries; they never generate opponents or mutate the clock.
bool mfIsAscensionTournamentYear(const Game& GameState);
string mfAscensionTournamentEntryError(const Game& GameState);
string mfAscensionTournamentStateError(const Game& GameState);

// Invoke only at real action boundaries, never when assembling a read-only view.
void mfInitializeAscensionTournament(Game& GameState);
bool mfOfferAscensionTournament(Game& GameState);
bool mfChooseAscensionTournament(Game& GameState, const string& OptionId);
bool mfEnterAscensionTournament(Game& GameState, string* Error = nullptr);

// Called after battleFinish has persisted actual HP and reset arts.battle.
// A win starts the next round immediately; the fourth win pays exactly once.
void mfCompleteAscensionTournamentBattle(Game& GameState, const Battle& BattleState,
    bool Win, bool Escape);
} // namespace nz
