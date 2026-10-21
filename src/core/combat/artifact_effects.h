#pragma once

#include "core/model.h"
#include "core/combat/artifact_status.h"

namespace nz {
namespace WaterVitality { struct HitFacts; }
// 战斗模块的只读法器报价；值快照，不持有外部资源。
struct ArtifactActionQuote {
    bool Allowed = false;
    string Error;
    int Cost = 0, HealthCost = 0, MaxTargets = 0, SelectedCount = 0;
    int AvailableCards = 0, DrawCount = 0, NextAvailableTurn = 0;
    vector<ArtifactStatusTarget> StatusTargets;
    string StatusToken;
};

// 状态打击的只读值快照；不持有资源，不修改状态或RNG。
// 灼烧沿用现有每跳伤害池，1点灼烧量为1计算单位；毒使用独立毒层。
struct ArtifactStatusStrikeQuote {
    string StatusId, StatusName;
    int StatusLayers = 0;
    double DamagePerLayer = 0, InscriptionMultiplier = 1;
    int Damage = 0;
};

// 输入为V2、一至五阶、白名单灵纹的poison_burst/fire_burst；毒层/灼烧池按各自合法上限钳位。
// DamagePerLayer为每毒层/每点灼烧量对应气血点数；Damage含灵纹并取整，尚未结算扰灵/目标防护。
// 非对应V2效果或非法品阶返回空StatusId；零状态/持续期结束返回0量0伤，动作错误由mfQuoteArtifactAction给出。
// 无资源所有权、状态/时钟/RNG/IO副作用，供成器说明、真实动作及只读视图共用。
[[nodiscard]] ArtifactStatusStrikeQuote mfQuoteArtifactStatusStrike(const Gear& Item, const Battle& State);
[[nodiscard]] int mfArtifactActionCost(const Player& Actor, const Gear& Item, const Battle& State);
// 用途：查询新版法器品阶的基础直伤点数；Grade 为1..5，非法值返回0，无状态/RNG/IO副作用。
[[nodiscard]] int mfArtifactBaseDamage(int Grade);
[[nodiscard]] string mfArtifactEffectDescription(const Gear& Item);
[[nodiscard]] string mfArtifactPassiveDescription(const Gear& Item);
[[nodiscard]] ArtifactActionQuote mfQuoteArtifactAction(const Player& Actor, const Battle& State,
    const Gear& Item, const vector<string>& Targets = {}, bool RequireTargets = false,
    const string& StatusTarget = "");
[[nodiscard]] ArtifactCombatSnapshot mfArtifactCombatSnapshot(const Player& Actor);
// 普通NPC及留影的在途计划/冷却共用真实装备白名单，纯查询。
[[nodiscard]] bool mfEnemyArtifactAvailable(const Game& GameState, const Battle& State, const string& Id);
void mfApplyArtifactRoundSupply(const Player& Actor, Battle& State);
[[nodiscard]] string mfApplyArtifactAction(const Player& Actor, Battle& State, const Gear& Item,
    const vector<string>& Targets, double TargetReduction = 0, double TargetCounter = 0,
    int* RawDamage = nullptr, const string& StatusTarget = "", WaterVitality::HitFacts* WaterHitOutput = nullptr);
// 共用旧主动弃牌规则，避免法器复制金道的凝盾/锐势公式。
void mfBattleApplyActiveDiscardBenefits(const Player& Actor, Battle& State, int CardCount);
} // namespace nz
