#include "core/combat/battle_scaling.h"
#include "core/battle.h"
#include "core/arts.h"
#include "core/cultivate.h"
#include "core/talents.h"
#include "core/foundation/state_limits.h"
#include <algorithm>
#include <cmath>

namespace nz {

int mfHealthMaximum(const Player& PlayerState, double ActorScale) {
    const ArtMods Modifiers = talentMods(PlayerState);
    return mfHealthMaximumFromPower(BATTLE_POWER[clampInt(PlayerState.stage, 0, 12)],
        static_cast<double>(PlayerState.arts.BaseHealthBonus) + PlayerState.arts.FoundationHealthBonus,
        Modifiers.hp, PlayerState.body, ActorScale);
}

int mfHealthMaximumFromPower(int BasePower, double BaseHealthBonus,
                            double HpMultiplier, int Body, double ActorScale) {
    if (!std::isfinite(ActorScale) || ActorScale <= 0) return 0;
    const double Maximum = (HealthFlatBasis + (BasePower + BaseHealthBonus) * HealthPowerMultiplier) * HpMultiplier *
        (1.0 + cultivate::bodyHpBonus(Body)) * ActorScale;
    if (!std::isfinite(Maximum) || Maximum <= 0) return 0;
    return jsRound(std::min(Maximum, static_cast<double>(StateLimits::MaxPersistedInteger)));
}

int mfPlayerHealthMaximum(const Player& PlayerState) { return mfHealthMaximum(PlayerState); }

int mfNpcHealthMaximum(const Npc& PersonState) {
    Player Profile;
    static_cast<Person&>(Profile) = static_cast<const Person&>(PersonState);
    Profile.talent.clear();
    Profile.traits.clear();
    Profile.sect = PersonState.sect;
    Profile.arts = artNpcLoadout(PersonState);
    return mfHealthMaximum(Profile, NpcEncounterHealthMultiplier);
}

int mfHealthPercentAmount(int Maximum, int Percent) {
    if (Maximum <= 0 || Percent <= 0) return 0;
    return std::max(1, jsRound(static_cast<double>(Maximum) * clampInt(Percent, 0, 100) / 100.0));
}

int mfHealthFromPercent(int Percent, int Maximum) {
    return mfHealthPercentAmount(Maximum, Percent);
}

int mfHealthToPercent(int Current, int Maximum) {
    if (Current <= 0 || Maximum <= 0) return 0;
    return clampInt(jsRound(static_cast<double>(std::min(Current, Maximum)) * 100.0 / Maximum), 1, 100);
}

int mfRecoveredHealth(int Current, int Maximum, int Amount) {
    if (Current <= 0 || Maximum <= 0) return 0;
    return static_cast<int>(std::clamp(static_cast<long long>(Current) + std::max(0, Amount),
        0LL, static_cast<long long>(Maximum)));
}

int mfMonthlyHealthRecovery(int Maximum) {
    return Maximum <= 0 ? 0 : std::max(1, jsRound(Maximum * HealthMonthlyRecoverFraction));
}

int mfRestExtraHealthRecovery(int Maximum) {
    return Maximum <= 0 ? 0 : std::max(0,
        jsRound(Maximum * HealthRestRecoverFraction) - mfMonthlyHealthRecovery(Maximum));
}

int mfRecoverPlayerHealth(Game& State, int Amount) {
    if (State.ended || State.player.health <= 0) return 0;
    auto& PlayerState = State.player;
    if (PlayerState.arts.battleActive()) {
        auto& Active = *PlayerState.arts.battle;
        if (Active.shadowPlayer) return 0;
        const int Before = Active.hp;
        Active.hp = mfRecoveredHealth(Active.hp, Active.max, Amount);
        PlayerState.health = std::min(Active.hp, mfPlayerHealthMaximum(PlayerState));
        return Active.hp - Before;
    }
    const int Before = PlayerState.health;
    PlayerState.health = mfRecoveredHealth(Before, mfPlayerHealthMaximum(PlayerState), Amount);
    return PlayerState.health - Before;
}

// 用途：集中应用可维护的两个伤害参数；流程：所有按基础战力增长的伤害；返回：未取整伤害基底。
double mfBattleDamageBasisFromPower(int BasePower) {
    return std::max(0, BasePower) * BattleDamagePowerMultiplier + BattleDamageFlatBasis;
}

// 用途：将合法境界基数转换为统一伤害尺度；流程：玩家/NPC神通与只读预览；返回：纯数值。
double mfBattleDamageBasis(int Stage) {
    return mfBattleDamageBasisFromPower(BATTLE_POWER[clampInt(Stage, 0, 12)]);
}

} // namespace nz
