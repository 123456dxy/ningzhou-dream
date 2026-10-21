#include "core/combat/artifact_status.h"

#include "core/json.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace nz {
namespace {
constexpr std::array<int, 5> CleanseLayerLimits{5, 10, 25, 45, 80};
constexpr std::uint64_t StatusHashOffset = 14695981039346656037ULL;
constexpr std::uint64_t StatusHashPrime = 1099511628211ULL;

double mfDisplayLayers(double Layers) {
    return std::round(std::max(0.0, Layers) * 1000000.0) / 1000000.0;
}

void mfAppendStatusTarget(vector<ArtifactStatusTarget>& Targets, const Gear& Item,
        string Id, string Name, double Layers, string Description) {
    Layers = mfDisplayLayers(Layers);
    if (Layers <= 0) return;
    const double Removed = std::min(Layers, static_cast<double>(mfArtifactStatusRemovalLimit(Item)));
    Targets.push_back({std::move(Id), std::move(Name), "层", std::move(Description),
        Layers, Removed, mfDisplayLayers(Layers - Removed)});
}

string mfLayerText(double Layers) {
    return json::Value(mfDisplayLayers(Layers)).dump();
}
} // namespace

bool mfArtifactSelectsStatus(const Gear& Item) {
    return Item.ForgeVersion == 2 && (Item.EffectId == "cleanse" || Item.EffectId == "dispel");
}

int mfArtifactStatusRemovalLimit(const Gear& Item) {
    if (!mfArtifactSelectsStatus(Item) || Item.grade < 1 || Item.grade > 5) return 0;
    return Item.EffectId == "cleanse" ? CleanseLayerLimits[Item.grade - 1] : Item.grade;
}

vector<ArtifactStatusTarget> mfArtifactStatusTargets(const Battle& State, const Gear& Item) {
    vector<ArtifactStatusTarget> Targets;
    if (mfArtifactStatusRemovalLimit(Item) <= 0) return Targets;
    if (Item.EffectId == "cleanse") {
        if (State.playerPoisonTurns > 0) {
            const bool NativeLayers = State.enemyCards.poison > 0;
            if (NativeLayers || !State.enemyCards.enabled)
                mfAppendStatusTarget(Targets, Item, "poison", "中毒",
                    NativeLayers ? State.enemyCards.poison : State.playerPoison,
                    NativeLayers ? "按实际毒层削除，每跳伤害按剩余层数同比保留。"
                        : "妖兽毒：每跳1点毒伤计1层，不按人物属性换算。");
        }
        if (State.enemyCards.burnTurns > 0)
            mfAppendStatusTarget(Targets, Item, "burn", "灼烧", State.enemyCards.burn,
                "沿用灼烧伤害池：每跳1点火伤计1层。");
        mfAppendStatusTarget(Targets, Item, "lock", "悬灵", State.lock, "剩余1回合悬灵计1层。");
        mfAppendStatusTarget(Targets, Item, "weaken", "扰灵", State.enemyWeaken, "按实际扰灵层数削除。");
    } else {
        mfAppendStatusTarget(Targets, Item, "guard", "守势", State.enemyCards.guard * 100,
            "1个百分点守势计1层，保留小数余量。");
        mfAppendStatusTarget(Targets, Item, "sharpness", "锐势", State.enemyCards.discardPower * 100,
            "1个百分点锐势计1层，保留小数余量。");
    }
    return Targets;
}

string mfArtifactStatusToken(const Battle& State, const Gear& Item) {
    if (!mfArtifactSelectsStatus(Item)) return "";
    const auto Cooldown = State.cooldowns.find("artifact:" + Item.id);
    const string Identity = json::Value(json::Array{"artifact-status-v1", Item.id, Item.EffectId, Item.grade,
        Item.EffectPower, Item.InscriptionId, State.enemy, State.npcId, State.kind, State.turn,
        State.played, State.shuffles, State.hp, State.max, State.mana, State.enemyHp,
        Cooldown == State.cooldowns.end() ? 0 : Cooldown->second,
        State.playerPoison, State.playerPoisonTurns, State.enemyCards.poison,
        State.enemyCards.poisonDamage, State.enemyCards.poisonTurns, State.enemyCards.burn,
        State.enemyCards.burnTurns, State.lock, State.enemyWeaken,
        State.enemyCards.guard, State.enemyCards.discardPower}).dump();
    std::uint64_t Hash = StatusHashOffset;
    for (const unsigned char Byte : Identity) { Hash ^= Byte; Hash *= StatusHashPrime; }
    return std::to_string(Hash);
}

string mfApplyArtifactStatusRemoval(Battle& State, const Gear& Item, const string& TargetId) {
    const auto Targets = mfArtifactStatusTargets(State, Item);
    const auto Found = std::find_if(Targets.begin(), Targets.end(), [&](const auto& Target) {
        return Target.Id == TargetId;
    });
    if (Found == Targets.end()) return "";
    const auto& Target = *Found;
    const int Remaining = static_cast<int>(Target.RemainingLayers);
    if (TargetId == "poison") {
        if (State.enemyCards.poison > 0) {
            const int Original = State.enemyCards.poison;
            const long long Numerator = static_cast<long long>(std::max(0, State.playerPoison)) * Remaining;
            State.playerPoison = static_cast<int>((Numerator + Original - 1) / Original);
            State.enemyCards.poison = Remaining;
        } else State.playerPoison = Remaining;
        if (Remaining == 0) State.playerPoisonTurns = 0;
        State.enemyCards.poisonDamage = State.playerPoison;
        State.enemyCards.poisonTurns = State.playerPoisonTurns;
    } else if (TargetId == "burn") {
        State.enemyCards.burn = Remaining;
        if (Remaining == 0) State.enemyCards.burnTurns = 0;
    } else if (TargetId == "lock") State.lock = Remaining;
    else if (TargetId == "weaken") State.enemyWeaken = Remaining;
    else if (TargetId == "guard") State.enemyCards.guard = std::max(0.0,
        State.enemyCards.guard - Target.RemoveLayers / 100.0);
    else if (TargetId == "sharpness") State.enemyCards.discardPower = std::max(0.0,
        State.enemyCards.discardPower - Target.RemoveLayers / 100.0);
    return "削除「" + Target.Name + "」" + mfLayerText(Target.RemoveLayers) + "层，剩余" +
        mfLayerText(Target.RemainingLayers) + "层。";
}
} // namespace nz
