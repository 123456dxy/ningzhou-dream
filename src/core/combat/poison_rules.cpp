#include "core/combat/poison_rules.h"
#include "core/combat/battle_scaling.h"
#include "core/arts.h"
#include <algorithm>
#include <cstdint>

namespace nz::PoisonRules {
int mfPoisonTickFromLayers(double DamageBasis, int PoisonLayerCount) {
    return jsRound(DamageBasis * PoisonTickBasisCoefficient * std::max(0, PoisonLayerCount));
}

int mfAddedPoisonTick(double DamageBasis, int BeforeLayers, int AfterLayers, int ExistingDamage) {
    const int BeforeDamage = mfPoisonTickFromLayers(DamageBasis, BeforeLayers);
    return std::min(BATTLE_BURN_CAP, std::max(0, ExistingDamage >= 0 ? ExistingDamage : BeforeDamage) +
        std::max(0, mfPoisonTickFromLayers(DamageBasis, AfterLayers) - BeforeDamage));
}

int mfLegacyPoisonTickFromLayers(double DamageBasis, int PoisonLayerCount) {
    return jsRound(DamageBasis * LegacyPoisonTickBasisCoefficient * std::max(0, PoisonLayerCount));
}

int mfLegacyPlayerPoisonTickFromLayers(int Stage, int PoisonLayerCount) {
    static constexpr int LegacyPower[13] = {14, 24, 39, 65, 95, 140, 210, 310, 440,
                                           660, 900, 1250, 1800};
    return mfLegacyPoisonTickFromLayers(
        mfBattleDamageBasisFromPower(LegacyPower[clampInt(Stage, 0, 12)]), PoisonLayerCount);
}

BloomQuote mfQuoteBloom(int ActorStage, const Battle& State) {
    BloomQuote Quote;
    Quote.BeforeLayers = clampInt(State.poison, 0, BATTLE_POISON_CAP);
    if (Quote.BeforeLayers <= 0 || State.poisonTurns <= 0) {
        Quote.Error = "对方尚未中毒，无法施放滋毒诀。";
        return Quote;
    }
    if (Quote.BeforeLayers >= BATTLE_POISON_CAP) {
        Quote.Error = "毒已达到99层，滋毒诀无法再增加。";
        return Quote;
    }
    Quote.AddedLayers = std::min(BATTLE_POISON_CAP - Quote.BeforeLayers, (Quote.BeforeLayers + 1) / 2);
    Quote.AfterLayers = Quote.BeforeLayers + Quote.AddedLayers;
    Quote.BeforeDamage = State.poisonDamage >= 0 ? State.poisonDamage :
        mfLegacyPlayerPoisonTickFromLayers(ActorStage, Quote.BeforeLayers);
    const std::int64_t ScaledDamage = static_cast<std::int64_t>(Quote.BeforeDamage) * Quote.AfterLayers;
    Quote.AfterDamage = static_cast<int>(std::min<std::int64_t>(BATTLE_BURN_CAP,
        (ScaledDamage + Quote.BeforeLayers / 2) / Quote.BeforeLayers));
    Quote.Allowed = true;
    return Quote;
}

SiphonQuote mfQuoteSiphon(int ActorStage, const Battle& State) {
    SiphonQuote Quote;
    Quote.PoisonLayers = clampInt(State.poison, 0, BATTLE_POISON_CAP);
    if (Quote.PoisonLayers <= 0 || State.poisonTurns <= 0) {
        Quote.Error = "对方尚未中毒，无法施放借毒回春。";
        return Quote;
    }
    const auto HealthCap = static_cast<std::int64_t>(std::max(0, State.max)) * SiphonMaximumHealthPercent / 100;
    Quote.HealPotential = static_cast<int>(std::min<std::int64_t>(HealthCap,
        jsRound(mfBattleDamageBasis(ActorStage) * SiphonHealPerLayer * Quote.PoisonLayers)));
    if (Quote.HealPotential <= 0) {
        Quote.Error = "本次借毒治疗量为0，无法施放。";
        return Quote;
    }
    if (State.hp >= State.max || State.hp <= 0) {
        Quote.Error = "自身没有可恢复的气血，无法施放借毒回春。";
        return Quote;
    }
    Quote.Healing = std::min(Quote.HealPotential, State.max - State.hp);
    Quote.Allowed = true;
    return Quote;
}
} // namespace nz::PoisonRules
