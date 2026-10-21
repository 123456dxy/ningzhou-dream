// Isolated deterministic experiment: real createGame, legal artInstant setup,
// mfEnterAscensionTournament, battlePreview and battleTurn. No saves or services.
// Question: can a budget-10 first-year heirloom build clear the real four fights,
// while an unprepared ordinary starter does not clear automatically?
// Policies use only public hand/effect/enemy-intent previews, never future draws.
#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/exploration/ascension_tournament.h"
#include "core/json.h"
#include "core/sim.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace nz;
using Json = json::Value;
constexpr const char* BalanceVersion = "ascension-v1";
constexpr const char* PolicyVersion = "VisibleArtifactGuardV1";

struct Decision {
    string Action;
    string SpellId;
    Preview Forecast;
    double Score = 0;
};

Json mfStrings(const vector<string>& Values) {
    json::Array Result;
    for (const auto& Value : Values) Result.emplace_back(Value);
    return Json(std::move(Result));
}

double mfScore(const Preview& Forecast, const Battle& State, const EnemyPreview& Intent) {
    if (!Forecast.valid || Forecast.cost > State.mana) return 0;
    if (Forecast.damage >= State.enemyHp && Forecast.damage > 0) return 1000000;
    const double Threat = std::max(0, Intent.damage);
    const double ShieldWeight = Threat >= State.hp ? 8.0 : State.hp < State.max * .4 ? 1.8 : 1.1;
    double StatusValue = 0;
    for (const auto& Status : Forecast.statuses)
        if (Status.target == "enemy" && (Status.id == "burn" || Status.id == "poison") && Status.amount > 0)
            StatusValue += std::max(0, Status.perTurn) * std::min(3, Status.turns) * .7;
    const double Value = Forecast.RawDamage * 1.2 + StatusValue +
        std::max(0, Forecast.heal) * 1.2 + std::min<double>(std::max(0, Forecast.shield), Threat) * ShieldWeight +
        Forecast.manaGain * 14.0 + (State.mana > Forecast.cost ? Forecast.draw * 8.0 : 0.0);
    return Value / std::max(1, Forecast.cost);
}

Decision mfChoose(const Game& State, Json& Candidates) {
    const auto& BattleState = *State.player.arts.battle;
    const auto Intent = battleEnemyPreview(State);
    Decision Best;
    const auto Consider = [&](const string& Action, const string& SpellId, const Preview& Forecast) {
        const auto Cooldown = BattleState.cooldowns.find(SpellId);
        if (Cooldown != BattleState.cooldowns.end() && Cooldown->second > 0) return;
        const double Score = mfScore(Forecast, BattleState, Intent);
        Json Entry(json::Object{});
        Entry.set("action", Action); Entry.set("spell", SpellId); Entry.set("score", Score);
        Entry.set("cost", Forecast.cost); Entry.set("damage", Forecast.RawDamage);
        Entry.set("shield", Forecast.shield); Entry.set("heal", Forecast.heal);
        Candidates.push(std::move(Entry));
        if (Score > Best.Score) Best = {Action, SpellId, Forecast, Score};
    };
    for (const auto& Uid : BattleState.hand) {
        const auto Card = std::find_if(BattleState.deck.begin(), BattleState.deck.end(),
            [&](const auto& Item) { return Item.uid == Uid; });
        if (Card == BattleState.deck.end()) throw std::runtime_error("hand UID missing from actual deck");
        const auto* Spell = battleSpell(Card->spell);
        if (!Spell || battleNeedsTarget(*Spell)) continue;
        Consider(Uid, Card->spell, battlePreview(State.player, BattleState, Card->spell));
    }
    for (const auto& Ability : equippedArtifactAbilities(State.player))
        Consider(Ability.spell.id, Ability.spell.id, battlePreview(State.player, BattleState, Ability.spell.id));
    return Best;
}

Game mfStarter(unsigned Seed, const string& BuildId) {
    CreateOptions Options;
    Options.name = "大会首年平衡预演";
    Options.seed = Seed;
    Options.traitsExplicit = Options.allocationExplicit = true;
    if (BuildId == "unprepared") Options.allocation["apt"] = 10;
    else if (BuildId == "relic_guard") {
        Options.traits = {"artifactHeir", "family", "body"};
        Options.allocation["body"] = 1;
    } else if (BuildId == "relic_dragon") Options.traits = {"artifactHeir", "dragonBlood", "family", "poor"};
    else throw std::runtime_error("unknown build");
    Game State = createGame(Options);
    if (State.pending && State.pending->type == AscensionTournamentBattleKind)
        mfChooseAscensionTournament(State, "decline");
    if (BuildId != "unprepared") {
        const auto Main = artInstant(State, "main", "bull");
        const auto Auxiliary = artInstant(State, "aux", "tuna");
        if (!Main.ok || !Auxiliary.ok) throw std::runtime_error("legal starting manual setup failed");
    }
    // No forced talent rewards, stat patches, learned spells, or full heals.
    return State;
}

Json mfPlay(unsigned Seed, const string& BuildId) {
    Game State = mfStarter(Seed, BuildId);
    Json Output(json::Object{}), Trace(json::Array{}), Openings(json::Array{}), RoundResults(json::Array{});
    Output.set("balance", BalanceVersion); Output.set("policy", PolicyVersion);
    Output.set("seed", static_cast<double>(Seed)); Output.set("build", BuildId);
    Output.set("traits", mfStrings(State.player.traits)); Output.set("startingBody", State.player.body);
    Output.set("main", State.player.arts.main); Output.set("aux", mfStrings(State.player.arts.aux));
    Output.set("spells", mfStrings(State.player.arts.spells)); Output.set("budgetSpent", State.player.build.spent);
    Output.set("startYear", State.year); Output.set("startDay", State.clock.days);
    string Error;
    if (!mfEnterAscensionTournament(State, &Error)) throw std::runtime_error("entry rejected: " + Error);
    Output.set("sects", mfStrings(State.player.AscensionTournament.OpponentSects));
    const auto Started = std::chrono::steady_clock::now();
    auto RoundStarted = Started;
    int Actions = 0, EndTurns = 0, LastRound = -1;
    int RoundActions = 0, RoundTurns = 0;
    while (battleActive(State) && Actions < 2000 && EndTurns < 100) {
        const auto& BattleState = *State.player.arts.battle;
        const int Round = State.player.AscensionTournament.Round;
        if (LastRound != Round) {
            RoundStarted = std::chrono::steady_clock::now();
            RoundActions = 0;
            RoundTurns = 0;
            Json Opening(json::Object{});
            Opening.set("round", Round + 1); Opening.set("hp", BattleState.hp); Opening.set("hpMax", BattleState.max);
            Opening.set("enemy", BattleState.enemy); Opening.set("enemyHpMax", BattleState.enemyMax);
            Opening.set("enemyStage", BattleState.enemyStage); Opening.set("playerFirst", BattleState.playerFirst);
            Openings.push(std::move(Opening)); LastRound = Round;
        }
        Json Entry(json::Object{}), Candidates(json::Array{});
        const auto Choice = mfChoose(State, Candidates);
        const string Action = Choice.Action.empty() ? "end" : Choice.Action;
        Entry.set("round", Round + 1); Entry.set("turn", BattleState.turn);
        Entry.set("hp", BattleState.hp); Entry.set("shield", BattleState.shield);
        Entry.set("enemyHp", BattleState.enemyHp); Entry.set("mana", BattleState.mana);
        Entry.set("incoming", battleEnemyPreview(State).damage);
        Entry.set("rngBefore", static_cast<double>(State.rng));
        Entry.set("hand", mfStrings(handSpells(BattleState)));
        Entry.set("action", Action); Entry.set("spell", Choice.SpellId); Entry.set("score", Choice.Score);
        Entry.set("candidates", std::move(Candidates));
        const auto Result = battleTurn(State, Action);
        if (!Result.ok) throw std::runtime_error("illegal policy action seed=" + std::to_string(Seed) + ": " + Result.error);
        Entry.set("rngAfter", static_cast<double>(State.rng));
        Entry.set("completedRoundsAfter", State.player.AscensionTournament.Round);
        Entry.set("battleActiveAfter", battleActive(State));
        Trace.push(std::move(Entry)); ++Actions;
        ++RoundActions;
        if (Action == "end") { ++EndTurns; ++RoundTurns; }
        if (State.player.AscensionTournament.Round != Round || !battleActive(State)) {
            Json Finished(json::Object{});
            Finished.set("round", Round + 1);
            Finished.set("outcome", State.player.AscensionTournament.Round > Round ? "win" : "loss");
            Finished.set("actions", RoundActions); Finished.set("turns", RoundTurns);
            Finished.set("elapsedMicroseconds", static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - RoundStarted).count()));
            for (auto Log = State.logs.rbegin(); Log != State.logs.rend(); ++Log) {
                if (Log->text.find("之战：") == string::npos) continue;
                Finished.set("productionOutcome", Log->text);
                break;
            }
            RoundResults.push(std::move(Finished));
        }
    }
    if (battleActive(State)) throw std::runtime_error("diagnostic action cap reached seed=" + std::to_string(Seed));
    Output.set("outcome", State.player.AscensionTournament.Round == 4 ? "win" : "loss");
    Output.set("furthestStage", State.player.AscensionTournament.Round);
    Output.set("finalHealth", State.player.health); Output.set("finalStones", State.player.stones);
    Output.set("endYear", State.year); Output.set("endDay", State.clock.days);
    Output.set("lifeEnded", State.ended); Output.set("actions", Actions); Output.set("turns", EndTurns);
    Output.set("elapsedMilliseconds", static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - Started).count()));
    Output.set("failureReason", State.player.AscensionTournament.Round == 4 ? "" : "actual four-fight battle loss or timeout");
    Output.set("openings", std::move(Openings)); Output.set("roundResults", std::move(RoundResults));
    Output.set("trace", std::move(Trace));
    return Output;
}
} // namespace

int main(int ArgumentCount, char** Arguments) try {
    if (ArgumentCount < 2 || ArgumentCount > 3) {
        std::cerr << "usage: ascension-tournament-probe <output.jsonl> [seed-count=30]\n";
        return 2;
    }
    const int SeedCount = ArgumentCount == 3 ? std::stoi(Arguments[2]) : 30;
    if (SeedCount < 1 || SeedCount > 1000) throw std::runtime_error("seed count out of bounds");
    std::ofstream Output(Arguments[1], std::ios::binary);
    if (!Output) throw std::runtime_error("cannot create explicitly named report");
    for (const string BuildId : {"unprepared", "relic_guard", "relic_dragon"}) {
        int Wins = 0;
        for (int SeedIndex = 0; SeedIndex < SeedCount; ++SeedIndex) {
            const unsigned Seed = 31u + 997u * SeedIndex;
            const Json Run = mfPlay(Seed, BuildId);
            Output << Run.dump() << '\n';
            if (Run["outcome"].asString() == "win") ++Wins;
            std::cout << BuildId << " seed=" << Seed << " outcome=" << Run["outcome"].asString()
                << " rounds=" << Run["furthestStage"].asInt() << " turns=" << Run["turns"].asInt() << '\n';
        }
        std::cout << "SUMMARY " << BuildId << " " << Wins << "/" << SeedCount << '\n';
    }
    return 0;
} catch (const std::exception& Error) {
    std::cerr << Error.what() << '\n';
    return 1;
}
