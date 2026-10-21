// Simulation contract: BattleLab::mfStart supplies real realm-legal initial/advanced
// loadouts and natural seeded decks; battlePreview/battleTurn supply every effect,
// draw, RNG transition and outcome. Replaced: human input, rendering and delays.
// No injected HP, mana, stacks, cards or wins. Optional support fixtures equip a
// same-grade, recipe-produced pearl before a fresh production battleStart.
// Sampling previews add a
// pure recipe-produced artifact only to a copied Player; it is never executed in
// the measured natural battle. Candidate multipliers are arithmetic projections
// of observed state, not candidate-victory simulations. Missing rounds stay null.
#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/combat/artifact_effects.h"
#include "core/combat/battle_lab.h"
#include "core/crafting/forge_rules.h"
#include "core/json.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace nz;
using Json = json::Value;
constexpr const char* BalanceVersion = "status-strike-production-observation-20261018";
constexpr std::array RoundSamples{1, 3, 5};

struct Decision {
    string Uid, SpellId;
    Preview Forecast;
    double Score = 0;
};

bool mfConsumesStatus(const string& SpellId) {
    return SpellId == "poisonBurst" || SpellId == "fireburst" || SpellId == "fireSiphon";
}

double mfScore(const Preview& Forecast, const Battle& State, const EnemyPreview& Intent) {
    if (!Forecast.valid || Forecast.cost > State.mana) return 0;
    if (Forecast.damage > 0 && Forecast.damage >= State.enemyHp) return 1000000;
    double StatusValue = 0;
    for (const auto& Status : Forecast.statuses) {
        if (Status.target != "enemy" || (Status.id != "poison" && Status.id != "burn")) continue;
        const int BeforeTick = Status.id == "poison" ? std::max(0, State.poisonDamage) : State.burn;
        const int BeforeTurns = Status.id == "poison" ? State.poisonTurns : State.burnTurns;
        if (Status.amount > 0) StatusValue += std::max(0, Status.perTurn - BeforeTick) * std::min(3, Status.turns) * .8;
        if (Status.turns > BeforeTurns && BeforeTurns <= 1) StatusValue += BeforeTick * .6;
    }
    const double Basis = std::max(10, State.max / 12);
    const int UncoveredThreat = std::max(0, Intent.damage - State.shield);
    const double ShieldWeight = Intent.damage >= State.hp ? 5 : 1.15;
    const double Value = Forecast.damage + StatusValue + std::max(0, Forecast.heal) * 1.3 +
        std::min(std::max(0, Forecast.shield), UncoveredThreat) * ShieldWeight +
        std::max(0, Forecast.manaGain) * Basis * .8 +
        std::min(3, std::max(0, Forecast.draw)) * (State.mana > Forecast.cost ? Basis * .7 : 0);
    return Value / (1 + Forecast.cost * .35);
}

Decision mfChoose(const Game& State, bool Preserve, Json& Candidates) {
    const auto& Combat = *State.player.arts.battle;
    const auto Intent = battleEnemyPreview(State);
    Decision Best;
    for (const auto& Uid : Combat.hand) {
        const auto Held = std::find_if(Combat.deck.begin(), Combat.deck.end(), [&](const Card& Item) { return Item.uid == Uid; });
        if (Held == Combat.deck.end()) throw std::runtime_error("natural hand UID missing from actual deck");
        const auto* Spell = battleSpell(Held->spell);
        if (!Spell || battleNeedsTarget(*Spell) || (Preserve && mfConsumesStatus(Spell->id)) ||
            battleCooldown(Combat, Spell->id) > 0) continue;
        const auto Forecast = battlePreview(State.player, Combat, Spell->id);
        const double Score = mfScore(Forecast, Combat, Intent);
        Json Candidate(json::Object{});
        Candidate.set("uid", Uid); Candidate.set("spell", Spell->id); Candidate.set("score", Score);
        Candidate.set("cost", Forecast.cost); Candidate.set("rawDamage", Forecast.RawDamage); Candidate.set("healthDamage", Forecast.damage);
        Candidates.push(std::move(Candidate));
        if (Score > Best.Score) Best = {Uid, Spell->id, Forecast, Score};
    }
    return Best;
}

Gear mfSampleArtifact(int Grade, const string& Build, const string& Effect) {
    crafting::ForgeRequest Request;
    Request.MainMaterialId = "fm-" + std::to_string(Grade) + (Build == "wood-poison" ? "-wood-yang-metal" : "-fire-yang-metal");
    Request.MaterialIds.assign(10, Request.MainMaterialId);
    Request.ShapeId = Effect == "turn_poison" || Effect == "turn_fire" ? "pearl" : "sword";
    Request.EffectId = Effect;
    const auto Recipe = crafting::mfEvaluateForgeRecipe(Request);
    if (!Recipe.Valid) throw std::runtime_error("pure real artifact recipe rejected: " + Recipe.Reason);
    Gear Item = Recipe.Product;
    Item.id = "probe-status-artifact";
    return Item;
}

Json mfState(const Battle& Combat) {
    Json Result(json::Object{});
    Result.set("turn", Combat.turn + 1); Result.set("hp", Combat.hp); Result.set("maxHp", Combat.max);
    Result.set("enemyHp", Combat.enemyHp); Result.set("enemyMaxHp", Combat.enemyMax); Result.set("enemyShield", Combat.enemyShield);
    Result.set("mana", Combat.mana); Result.set("poisonLayers", Combat.poison); Result.set("poisonTurns", Combat.poisonTurns);
    Result.set("poisonTickCache", Combat.poisonDamage); Result.set("burnPool", Combat.burn); Result.set("burnTurns", Combat.burnTurns);
    Result.set("fireGrowthRounds", Combat.FireGrowthRounds);
    return Result;
}

Json mfSnapshot(const Game& State, const string& Build, int Grade, int EndedTurns) {
    const auto& Combat = *State.player.arts.battle;
    Json Result = mfState(Combat);
    Result.set("observed", true); Result.set("endedTurnsBefore", EndedTurns);
    Result.set("phase", "after policy actions, immediately before ending player round");
    const string Effect = Build == "wood-poison" ? "poison_burst" : "fire_burst";
    const auto Item = mfSampleArtifact(Grade, Build, Effect);
    const auto ActualQuote = mfQuoteArtifactStatusStrike(Item, Combat);
    Result.set("productionStatusAmount", ActualQuote.StatusLayers);
    Result.set("productionMultiplier", ActualQuote.DamagePerLayer);
    Result.set("productionStatusRawDamage", ActualQuote.Damage);
    const int DirectArtifact = mfArtifactBaseDamage(Grade);
    Result.set("directArtifactRawDamage", DirectArtifact);
    Json Projections(json::Array{});
    for (const double Multiplier : {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 5.0, 15.0, 45.0}) {
        Json Entry(json::Object{});
        const double RawProjection = ActualQuote.StatusLayers * Multiplier;
        Entry.set("multiplier", Multiplier); Entry.set("rawProjection", RawProjection);
        Entry.set("versusFixedDirect", RawProjection / DirectArtifact);
        Entry.set("versusEnemyMaxHp", RawProjection / std::max(1, Combat.enemyMax));
        Projections.push(std::move(Entry));
    }
    Result.set("candidateArithmeticProjections", std::move(Projections));
    Player PreviewActor = State.player;
    PreviewActor.arts.gear.push_back(Item); PreviewActor.arts.equipped["weapon"] = Item.id;
    const auto ArtifactPreview = battlePreview(PreviewActor, Combat, "artifact:" + Item.id);
    Result.set("productionArtifactHealthDamage", ArtifactPreview.damage);
    int MaximumSpellRaw = 0, MaximumSpellHealth = 0;
    Json Spells(json::Array{});
    auto Known = State.player.arts.spells;
    Known.push_back("strike");
    for (const auto& Id : Known) {
        const auto Forecast = battlePreview(State.player, Combat, Id);
        Json Entry(json::Object{}); Entry.set("id", Id); Entry.set("rawDamage", Forecast.RawDamage); Entry.set("healthDamage", Forecast.damage);
        Entry.set("cost", Forecast.cost); Entry.set("consumesStatus", mfConsumesStatus(Id));
        Spells.push(std::move(Entry));
        if (!mfConsumesStatus(Id)) { MaximumSpellRaw = std::max(MaximumSpellRaw, Forecast.RawDamage); MaximumSpellHealth = std::max(MaximumSpellHealth, Forecast.damage); }
    }
    Result.set("learnedSpellPreviewsNotNecessarilyHeld", std::move(Spells));
    Result.set("nonConsumingSpellMaxRaw", MaximumSpellRaw); Result.set("nonConsumingSpellMaxHealth", MaximumSpellHealth);
    return Result;
}

Json mfRun(unsigned Seed, const string& Build, const string& Tier, const string& Opponent, int Stage, bool Preserve,
           bool SupportMatrix = false, bool WithSupport = false) {
    BattleLab::Session Session;
    BattleLab::Selection Selection;
    Selection.BuildId = Build; Selection.LoadoutTier = Tier; Selection.Stage = Stage; Selection.OpponentId = Opponent; Selection.Seed = Seed;
    const auto Started = BattleLab::mfStart(Session, Selection);
    if (!Started.Succeeded) throw std::runtime_error(Started.Error);
    auto& State = Session.CurrentGame();
    const string SupportEffect = WithSupport ? (Build == "wood-poison" ? "turn_poison" : "turn_fire") : "none";
    if (SupportMatrix) {
        const auto OriginalOpening = mfState(*State.player.arts.battle).dump();
        const auto OriginalRng = State.rng;
        const auto OriginalHand = State.player.arts.battle->hand;
        Game Prepared;
        Prepared.seed = Prepared.rng = Seed;
        Prepared.player = State.player;
        Prepared.player.arts.battle.reset();
        Prepared.npcs = State.npcs;
        Prepared.clock.ageBase = Prepared.player.age;
        if (WithSupport) {
            auto Item = mfSampleArtifact(Stage / 3 + 1, Build, SupportEffect);
            Item.id = "probe-round-support";
            Prepared.player.arts.gear.push_back(Item);
            Prepared.player.arts.equipped["accessory"] = Item.id;
        }
        const auto Catalog = BattleLab::mfOpponents();
        const auto Definition = std::find_if(Catalog.begin(), Catalog.end(), [&](const auto& Item) { return Item.Id == Opponent; });
        if (Definition == Catalog.end()) throw std::runtime_error("missing real opponent fixture");
        EnemySpec Enemy;
        Enemy.name = Definition->Name; Enemy.id = "battle-lab-opponent"; Enemy.kind = "spar";
        Enemy.stage = Stage; Enemy.style = Definition->Dao; Enemy.sect = Definition->Sect;
        Enemy.beast = Definition->IsBeast; Enemy.gender = "男";
        battleStart(Prepared, Enemy);
        if (!battleActive(Prepared)) throw std::runtime_error("support fixture failed natural start");
        if (!WithSupport && (mfState(*Prepared.player.arts.battle).dump() != OriginalOpening ||
            Prepared.rng != OriginalRng || Prepared.player.arts.battle->hand != OriginalHand))
            throw std::runtime_error("no-support production restart disagrees with original BattleLab opening");
        if (WithSupport && (Build == "wood-poison" ? Prepared.player.arts.battle->ArtifactState.RoundPoison <= 0 :
            Prepared.player.arts.battle->ArtifactState.RoundBurn <= 0))
            throw std::runtime_error("real support snapshot missing");
        State = std::move(Prepared);
    }
    Json Result(json::Object{}), Trace(json::Array{}), Samples(json::Array{});
    Result.set("balance", BalanceVersion); Result.set("policy", Preserve ? "VisibleStackPreserveV1" : "VisibleCombatSpendV1");
    Result.set("seed", static_cast<double>(Seed)); Result.set("build", Build); Result.set("tier", Tier); Result.set("opponent", Opponent);
    Result.set("stage", Stage); Result.set("grade", Stage / 3 + 1); Result.set("main", State.player.arts.main);
    Result.set("supportArtifact", SupportEffect);
    Json Unavailable(json::Array{}); for (const auto& Text : Session.Unavailable) Unavailable.push(Text);
    Result.set("unavailableComponents", std::move(Unavailable));
    int Actions = 0, EndedTurns = 0, MaxPoison = 0, MaxBurn = 0, DirectRaw = 0, DirectHealth = 0;
    int ObservedDot = 0, UnobservableTerminalDot = 0;
    int FirstPoison72 = 0, FirstPoison90 = 0;
    const int TurnCap = SupportMatrix ? 24 : 12;
    std::array<bool, 3> Sampled{};
    const auto StartedAt = std::chrono::steady_clock::now();
    while (battleActive(State) && Actions < 600 && EndedTurns < TurnCap) {
        const Battle Before = *State.player.arts.battle;
        MaxPoison = std::max(MaxPoison, Before.poison); MaxBurn = std::max(MaxBurn, Before.burn);
        if (!FirstPoison72 && Before.poison >= 72) FirstPoison72 = Before.turn + 1;
        if (!FirstPoison90 && Before.poison >= 90) FirstPoison90 = Before.turn + 1;
        Json Candidates(json::Array{});
        const auto Choice = mfChoose(State, Preserve, Candidates);
        const bool Ends = Choice.Uid.empty();
        if (Ends) for (std::size_t Index = 0; Index < RoundSamples.size(); ++Index) {
            if (EndedTurns + 1 == RoundSamples[Index]) { Samples.push(mfSnapshot(State, Build, Stage / 3 + 1, EndedTurns)); Sampled[Index] = true; }
        }
        Json Entry = mfState(Before);
        Entry.set("action", Ends ? "end" : Choice.Uid); Entry.set("spell", Ends ? "end" : Choice.SpellId);
        Entry.set("rngBefore", static_cast<double>(State.rng)); Entry.set("candidates", std::move(Candidates));
        Entry.set("previewRawDamage", Choice.Forecast.RawDamage); Entry.set("previewHealthDamage", Choice.Forecast.damage);
        const int WinsBefore = State.player.arts.progress.wins;
        const auto Turn = battleTurn(State, Ends ? "end" : Choice.Uid);
        if (!Turn.ok) throw std::runtime_error("illegal policy action seed=" + std::to_string(Seed) + ": " + Turn.error);
        Entry.set("rngAfter", static_cast<double>(State.rng)); Entry.set("activeAfter", battleActive(State));
        if (battleActive(State)) {
            const auto& After = *State.player.arts.battle;
            Entry.set("after", mfState(After));
            if (Ends) {
                std::size_t Overlap = std::min(Before.log.size(), After.log.size());
                while (Overlap > 0 && !std::equal(Before.log.end() - Overlap, Before.log.end(), After.log.begin())) --Overlap;
                int DotThisTurn = 0;
                for (auto Log = After.log.begin() + Overlap; Log != After.log.end(); ++Log) {
                    const string Prefix = "回合结束：持续伤害";
                    if (Log->starts_with(Prefix)) { DotThisTurn = std::stoi(Log->substr(Prefix.size())); break; }
                }
                ObservedDot += DotThisTurn; Entry.set("actualLoggedDot", DotThisTurn);
            } else DirectHealth += std::max(0, Before.enemyHp - After.enemyHp);
        } else {
            Entry.set("terminalWon", State.player.arts.progress.wins > WinsBefore);
            if (Ends && (Before.poisonTurns > 0 || Before.burnTurns > 0)) ++UnobservableTerminalDot;
        }
        if (!Ends) DirectRaw += Choice.Forecast.RawDamage;
        if (Ends) ++EndedTurns;
        Trace.push(std::move(Entry)); ++Actions;
    }
    for (std::size_t Index = 0; Index < RoundSamples.size(); ++Index) if (!Sampled[Index]) {
        Json Missing(json::Object{}); Missing.set("turn", RoundSamples[Index]); Missing.set("observed", false);
        Missing.set("reason", battleActive(State) ? "diagnostic stop before sampling" : "natural battle ended before this round-end sample"); Samples.push(std::move(Missing));
    }
    bool Won = false; for (const auto& Log : State.logs) if (Log.text.find("之战：获胜") != string::npos) Won = true;
    Result.set("outcome", battleActive(State) ? "censored" : Won ? "win" : "loss");
    Result.set("furthestStage", Stage); Result.set("turns", EndedTurns); Result.set("actions", Actions);
    Result.set("failureReason", battleActive(State) ? std::to_string(TurnCap) + "-ended-turn or 600-action diagnostic cap" : "");
    Result.set("peakPoisonLayers", MaxPoison); Result.set("peakBurnPool", MaxBurn);
    Result.set("firstPoison72Turn", FirstPoison72 ? Json(FirstPoison72) : Json());
    Result.set("firstPoison90Turn", FirstPoison90 ? Json(FirstPoison90) : Json());
    Result.set("spellPreviewRawDamageTotal", DirectRaw); Result.set("observedSpellHealthDamageTotalExcludingTerminal", DirectHealth);
    Result.set("actualLoggedDotTotalExcludingTerminal", ObservedDot); Result.set("unobservableTerminalDotCount", UnobservableTerminalDot);
    Result.set("samples", std::move(Samples)); Result.set("trace", std::move(Trace));
    Result.set("elapsedMicroseconds", static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - StartedAt).count()));
    return Result;
}
} // namespace

int main(int ArgumentCount, char** Arguments) try {
    if (ArgumentCount < 2 || ArgumentCount > 4) throw std::runtime_error("usage: status-strike-balance-probe <output.jsonl> [seed-count=10] [support]");
    const int SeedCount = ArgumentCount >= 3 ? std::stoi(Arguments[2]) : 10;
    const bool SupportMatrix = ArgumentCount == 4 && string(Arguments[3]) == "support";
    if (ArgumentCount == 4 && !SupportMatrix) throw std::runtime_error("unknown matrix");
    if (SeedCount < 1 || SeedCount > 1000) throw std::runtime_error("seed count out of bounds");
    std::ofstream Output(Arguments[1], std::ios::binary);
    if (!Output) throw std::runtime_error("cannot create explicitly named output");
    int Runs = 0;
    if (SupportMatrix) {
        for (const string Build : {"wood-poison", "fire-stack"}) for (const string Opponent : {"beast-body", "zhushan"})
            for (const int Stage : {0, 3, 6, 9, 12}) for (const bool WithSupport : {false, true})
                for (int SeedIndex = 0; SeedIndex < SeedCount; ++SeedIndex) {
                    Output << mfRun(31u + 997u * SeedIndex, Build, "advanced", Opponent, Stage, true, true, WithSupport).dump() << '\n'; ++Runs;
                }
    } else for (const string Build : {"wood-poison", "fire-stack"}) for (const string Tier : {"initial", "advanced"})
        for (const string Opponent : {"beast-body", "zhushan"}) for (const int Stage : {0, 3, 6, 9, 12}) for (const bool Preserve : {true, false})
            for (int SeedIndex = 0; SeedIndex < SeedCount; ++SeedIndex) {
                Output << mfRun(31u + 997u * SeedIndex, Build, Tier, Opponent, Stage, Preserve).dump() << '\n'; ++Runs;
            }
    std::cout << "completed " << Runs << " real seeded runs\n";
    return 0;
} catch (const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }
