#include "core/interface/ascension_api.h"

#include <algorithm>

#include "core/exploration/ascension_tournament.h"

namespace nz::api::view {
json::Value mfAscensionTournamentView(const Game& GameState) {
    const auto& Tournament = GameState.player.AscensionTournament;
    const string EntryError = mfAscensionTournamentEntryError(GameState);
    json::Value Result(json::Object{});
    Result.set("name", "升仙大会");
    Result.set("active", Tournament.Active);
    Result.set("isTournamentYear", mfIsAscensionTournamentYear(GameState));
    Result.set("eligible", !GameState.ended && GameState.player.stage >= 0 &&
        GameState.player.stage < 3 && GameState.player.age < AscensionTournamentMaximumAge);
    Result.set("enabled", EntryError.empty());
    Result.set("reason", EntryError);
    Result.set("attempted", Tournament.AttemptedYear == GameState.year);
    Result.set("completedRounds", Tournament.AttemptedYear == GameState.year ? Tournament.Round : 0);
    Result.set("roundCount", AscensionTournamentRoundCount);
    Result.set("rewardStones", AscensionTournamentRewardStones);
    Result.set("rewardPillId", AscensionTournamentPillId);
    Result.set("rewardPillName", "筑基丹");
    Result.set("rewardPillCount", 1);
    Result.set("recoveryPercent", AscensionTournamentRecoveryPercent);
    Result.set("maximumAgeExclusive", AscensionTournamentMaximumAge);
    const int FirstYear = std::max(0, Tournament.FirstYear);
    const int ElapsedYears = std::max(0, GameState.year - FirstYear);
    const int NextYear = FirstYear +
        (ElapsedYears / AscensionTournamentIntervalYears + 1) * AscensionTournamentIntervalYears;
    Result.set("nextYear", NextYear);
    Result.set("nextAge", GameState.player.age + NextYear - GameState.year);
    Result.set("rules", "炼气且未满40岁，每五年一届，开局当年为首届。四战连续，不可换装或穿插其他行动；"
        "每战胜后进入下一战前恢复20%气血上限，不会回满。落败或认输不致死，每届只可参赛一次。");
    json::Array Rounds;
    const char* OpponentNames[] = {"炼气初期散修", "炼气初期宗门弟子", "炼气中期宗门弟子", "炼气后期宗门弟子"};
    for (int Round = 0; Round < AscensionTournamentRoundCount; ++Round) {
        json::Value Entry(json::Object{});
        Entry.set("round", Round + 1);
        Entry.set("name", OpponentNames[Round]);
        Entry.set("stage", std::max(0, Round - 1));
        Entry.set("complete", Tournament.AttemptedYear == GameState.year && Round < Tournament.Round);
        if (Tournament.Active && Round > 0) {
            const string& SectId = Tournament.OpponentSects[static_cast<size_t>(Round - 1)];
            const auto* Sect = findSect(SectId);
            Entry.set("sect", SectId);
            Entry.set("sectName", Sect ? Sect->name : "");
        }
        Rounds.push_back(std::move(Entry));
    }
    Result.set("rounds", json::Value(std::move(Rounds)));
    return Result;
}
} // namespace nz::api::view
