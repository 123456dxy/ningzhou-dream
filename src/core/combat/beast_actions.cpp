#include "core/combat/beast_actions.h"
#include "core/combat/battle_scaling.h"
#include "core/battle.h"
#include "core/arts.h"
#include "core/talents.h"
#include "core/model.h"
#include "core/content/game_ids.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace nz::BeastCombat {
namespace {
struct ActionDefinition { ActionKind Kind; std::string_view Id, Name; int Cost; };
constexpr std::array<ActionDefinition, 9> Definitions{{
    ActionDefinition{ActionKind::Pounce,"pounce","扑击",1}, {ActionKind::Maul,"maul","重爪",2},
    {ActionKind::FlameBreath,"flame-breath","烈焰吐息",2}, {ActionKind::VenomBreath,"venom-breath","毒息",2},
    {ActionKind::Carapace,"carapace","凝甲",1}, {ActionKind::Roar,"roar","摄魂咆哮",1},
    {ActionKind::Charge,"charge","蓄势",1}, {ActionKind::Rend,"rend","穿甲撕咬",2},
    {ActionKind::Rejuvenate,"rejuvenate","回元噬击",2}
}};
constexpr std::array ActionLimits{1,2,3,3,4};
constexpr std::array EnergyBudgets{2,3,4,5,6};
constexpr std::array DamageScaleCaps{2.10,2.10,2.90,3.00,3.10};
constexpr std::array RealmPressureScales{1.50,1.00,1.00,1.20,.95};
constexpr int MaximumCharge = 2;
constexpr double ChargeBonusPerLayer = .15;
constexpr double BreathTickCoefficient = .10;
constexpr double LegacyBreathTickCoefficient = .04; // 已冻结的旧计划仍保留每跳值，仅新开轮使用新系数。
// 用途：读取静态行动定义；流程：全模块；返回：借用指针，非法枚举为空。
const ActionDefinition* mfDefinition(ActionKind Kind) {
    for (const auto& Definition : Definitions) if (Definition.Kind == Kind) return &Definition;
    return nullptr;
}
// 用途：统一五个大境界下标；流程：预算/校验；返回：0..4。
int mfMajorIndex(int Stage) { return clampInt(Stage / 3, 0, 4); }
// 用途：选择妖兽本命特殊动作；流程：真实计划；返回：妖兽动作，不读取人类神通表。
ActionKind mfStyleAction(const Battle& State) {
    const auto Dao = content::parseDaoId(State.style);
    if (Dao == content::DaoId::Fire) return ActionKind::FlameBreath;
    if (Dao == content::DaoId::Wood) return ActionKind::VenomBreath;
    if (Dao == content::DaoId::Mind) return ActionKind::Roar;
    if (Dao == content::DaoId::Metal || Dao == content::DaoId::Sword) return ActionKind::Rend;
    if (Dao == content::DaoId::Water || Dao == content::DaoId::Earth) return ActionKind::Carapace;
    return ActionKind::Maul;
}
// 用途：读取行动的直伤分配权重；流程：冻结总预算；返回：同境动作压力，不额外复制战力公式，合计仍受整轮上限约束。
double mfDamageWeight(ActionKind Kind, bool FirstAttack) {
    if (Kind == ActionKind::Pounce) return FirstAttack ? 1.30 : .80;
    if (Kind == ActionKind::Maul) return 1.55;
    if (Kind == ActionKind::FlameBreath) return .85;
    if (Kind == ActionKind::VenomBreath) return .75;
    if (Kind == ActionKind::Rend) return 1.10;
    if (Kind == ActionKind::Rejuvenate) return 1.00;
    if (Kind == ActionKind::Roar) return .60;
    return 0;
}
// 用途：拼接一段已冻结效果说明；流程：预告/战报；返回：中文数值说明，不试算随机。
std::string mfActionDescription(const PlannedAction& Action) {
    std::string Text(mfActionName(Action.Kind));
    if (Action.RawDamage) Text += " 打出" + std::to_string(Action.RawDamage);
    if (Action.ShieldGain) Text += " 护盾+" + std::to_string(Action.ShieldGain);
    if (Action.HealGain) Text += " 回复" + std::to_string(Action.HealGain);
    if (Action.PoisonDamage) Text += " 毒息" + std::to_string(Action.PoisonDamage) + "点/轮×3";
    if (Action.BurnDamage) Text += " 灼烧+" + std::to_string(Action.BurnDamage) + "点/轮×3";
    if (Action.WeakenLayers) Text += " 扰灵+1";
    if (Action.ChargeGain) Text += " 蓄势+1（下次攻击+15%，最多2层，仍受整轮预算限制）";
    if (Action.PiercePercent) Text += " 穿盾" + std::to_string(Action.PiercePercent) + "%";
    return Text;
}
}
// 用途：返回稳定协议ID；流程：codec/视图；返回：非法枚举为空。
std::string_view mfActionId(ActionKind Kind) { const auto* Definition = mfDefinition(Kind); return Definition ? Definition->Id : ""; }
// 用途：返回妖兽专属名称；流程：预告/日志；返回：静态名称。
std::string_view mfActionName(ActionKind Kind) { const auto* Definition = mfDefinition(Kind); return Definition ? Definition->Name : "未知兽行"; }
// 用途：返回妖力费用；流程：计划/校验；返回：1或2，未知为0。
int mfActionCost(ActionKind Kind) { const auto* Definition = mfDefinition(Kind); return Definition ? Definition->Cost : 0; }
// 用途：按白名单解析；流程：持久化；返回：未知为空。
std::optional<ActionKind> mfParseActionKind(std::string_view Id) {
    for (const auto& Definition : Definitions) if (Definition.Id == Id) return Definition.Kind;
    return {};
}
// 用途：提供行动上限；流程：计划/展示；返回：五档固定值。
int mfActionLimit(int Stage) { return ActionLimits[mfMajorIndex(Stage)]; }
// 用途：提供妖力预算；流程：计划/展示；返回：五档固定值。
int mfEnergyBudget(int Stage) { return EnergyBudgets[mfMajorIndex(Stage)]; }
// 用途：在动作边界建立本轮专属行动；流程：开战/回合；返回：无，伤害先合总预算再分配，避免按段数倍增旧重击。
void mfPlanActions(Game& State) {
    auto& BattleState = *State.player.arts.battle;
    if (!BattleState.enemyBeast || BattleState.enemyCards.enabled || BattleState.shadowPlayer) return;
    const int Major = mfMajorIndex(BattleState.enemyStage);
    const int Phase = (BattleState.turn + 1) % 4;
    std::vector<PlannedAction> Actions;
    int UsedEnergy = 0;
    const auto Add = [&](ActionKind Kind) {
        if (Actions.size() >= static_cast<std::size_t>(mfActionLimit(BattleState.enemyStage)) ||
            UsedEnergy + mfActionCost(Kind) > mfEnergyBudget(BattleState.enemyStage)) return;
        Actions.push_back({Kind}); UsedEnergy += mfActionCost(Kind);
    };
    double LowRealmWeight = 0;
    if (Major == 0) {
        const auto OldIntent = battleIntent(BattleState);
        const auto Kind = OldIntent.kind == "guard" ? ActionKind::Carapace : OldIntent.kind == "heavy" ? ActionKind::Maul :
            OldIntent.kind == "pierce" ? ActionKind::Rend : OldIntent.kind == "heal" ? ActionKind::Rejuvenate : ActionKind::Pounce;
        Add(Kind); LowRealmWeight = OldIntent.scale * RealmPressureScales[Major];
    } else if (Phase == 0) {
        // 经营轮仍有一次低压攻击；高境界多余预算用于特性，避免四分之一回合白送。
        if (Major >= 2) Add(ActionKind::Carapace);
        if (Major == 1) Add(ActionKind::Charge);
        Add(ActionKind::Pounce);
        if (Major >= 4) Add(ActionKind::Roar);
        if (Major >= 2) Add(ActionKind::Charge);
    } else {
        Add(Phase == 3 ? ActionKind::Maul : ActionKind::Pounce);
        if (Phase == 2 || Phase == 3) Add(mfStyleAction(BattleState));
        Add(ActionKind::Carapace);
        while (Actions.size() < static_cast<std::size_t>(mfActionLimit(BattleState.enemyStage)) &&
               UsedEnergy < mfEnergyBudget(BattleState.enemyStage)) Add(ActionKind::Pounce);
    }
    const double Basis = mfBattleDamageBasisFromPower(BattleState.enemyPower);
    std::vector<double> Weights(Actions.size(), 0);
    double WeightSum = 0;
    int Charge = clampInt(BattleState.enemyCards.charge, 0, MaximumCharge);
    bool FirstAttack = true;
    for (std::size_t Index = 0; Index < Actions.size(); ++Index) {
        auto& Action = Actions[Index];
        double Weight = Major == 0 ? LowRealmWeight : mfDamageWeight(Action.Kind, FirstAttack) * RealmPressureScales[Major];
        if (Weight > 0) { Weight *= 1 + Charge * ChargeBonusPerLayer; Charge = 0; FirstAttack = false; }
        Weights[Index] = Weight; WeightSum += Weight;
        if (Action.Kind == ActionKind::Carapace) Action.ShieldGain = jsRound(BattleState.enemyMax * (Major == 0 ? .10 : .08));
        if (Action.Kind == ActionKind::Rejuvenate) Action.HealGain = jsRound(BattleState.enemyMax * .035);
        if (Action.Kind == ActionKind::FlameBreath) Action.BurnDamage = std::max(1, jsRound(Basis * BreathTickCoefficient));
        if (Action.Kind == ActionKind::VenomBreath) Action.PoisonDamage = std::max(1, jsRound(Basis * BreathTickCoefficient));
        if (Action.Kind == ActionKind::Roar) Action.WeakenLayers = 1;
        if (Action.Kind == ActionKind::Charge) { Action.ChargeGain = 1; Charge = std::min(MaximumCharge, Charge + 1); }
        if (Action.Kind == ActionKind::Rend) Action.PiercePercent = Major == 0 ? 40 : 25;
    }
    const double Roll = .94 + randomOf(State) * .12;
    const int DamageBudget = jsRound(Basis * std::min(WeightSum, DamageScaleCaps[Major]) * Roll);
    int Remaining = DamageBudget;
    std::size_t LastAttack = 0;
    for (std::size_t Index = 0; Index < Weights.size(); ++Index) if (Weights[Index] > 0) LastAttack = Index;
    for (std::size_t Index = 0; Index < Actions.size(); ++Index) if (Weights[Index] > 0) {
        Actions[Index].RawDamage = Index == LastAttack ? Remaining : std::min(Remaining, jsRound(DamageBudget * Weights[Index] / WeightSum));
        Remaining -= Actions[Index].RawDamage;
    }
    std::string Name;
    for (const auto& Action : Actions) { if (!Name.empty()) Name += " → "; Name += mfActionName(Action.Kind); }
    BattleState.BeastActions = std::move(Actions);
    BattleState.intentKind = "beast-actions"; BattleState.intentName = std::move(Name);
    BattleState.intentRaw = DamageBudget; BattleState.intentScale = std::min(WeightSum, DamageScaleCaps[Major]);
    BattleState.intentSpell.clear();
}
// 用途：真实与副本逐段执行同一计划；流程：先手/回合/意图预览；返回：每段事实，不推进RNG或世界时间。
PhaseResult mfExecuteActions(const Player& Defender, Battle& State, bool WriteLog) {
    PhaseResult Result;
    for (const auto& Action : State.BeastActions) Result.Actions.push_back({Action});
    if (State.hp <= 0 || State.enemyHp <= 0) return Result;
    State.enemyShield = 0;
    const auto Modifiers = talentMods(Defender);
    for (auto& Outcome : Result.Actions) {
        if (State.hp <= 0 || State.enemyHp <= 0) break;
        const auto& Action = Outcome.Action; Outcome.Executed = true;
        if (Action.RawDamage > 0) {
            const int Damage = std::max(0, jsRound(Action.RawDamage * (1 - battleReduction(Defender, State))));
            const int Piercing = jsRound(Damage * Action.PiercePercent / 100.0);
            Outcome.Blocked = std::min(std::max(0, State.shield), Damage - Piercing);
            State.shield -= Outcome.Blocked;
            Outcome.Damage = std::min(State.hp, Damage - Outcome.Blocked);
            State.hp -= Outcome.Damage; Result.Damage += Outcome.Damage;
            Outcome.CounterDamage = std::min(State.enemyHp, jsRound(Outcome.Blocked * Modifiers.bodyCounter));
            State.enemyHp -= Outcome.CounterDamage;
            State.enemyCards.charge = 0;
        }
        if (Action.ShieldGain) State.enemyShield = std::min(jsRound(State.enemyMax * .30), State.enemyShield + Action.ShieldGain);
        if (Action.HealGain && State.enemyHp > 0) {
            const int Gain = std::min(State.enemyMax - State.enemyHp, Action.HealGain);
            State.enemyHp += Gain; Result.Heal += Gain;
        }
        if (State.hp > 0) {
            if (Action.PoisonDamage) { State.playerPoison = std::max(State.playerPoison, Action.PoisonDamage); State.playerPoisonTurns = 1; }
            if (Action.BurnDamage) { State.enemyCards.burn = std::min(BATTLE_BURN_CAP, State.enemyCards.burn + Action.BurnDamage); State.enemyCards.burnTurns = 3; }
            if (Action.WeakenLayers) State.enemyWeaken = std::min(2, State.enemyWeaken + Action.WeakenLayers);
            if (Action.ChargeGain) State.enemyCards.charge = std::min(MaximumCharge, State.enemyCards.charge + Action.ChargeGain);
        }
        std::string Text = mfActionDescription(Action);
        if (Action.RawDamage) Text += "：实际损血" + std::to_string(Outcome.Damage) + "，盾挡" + std::to_string(Outcome.Blocked);
        if (Outcome.CounterDamage) Text += "，反震" + std::to_string(Outcome.CounterDamage);
        if (!Result.Description.empty()) Result.Description += " → "; Result.Description += Text;
        if (WriteLog) State.log.push_back("对方" + Text + "。");
    }
    Result.Shield = State.enemyShield;
    return Result;
}
// 用途：校验冻结计划的身份、容量、费用与数字载荷；流程：读档/预览；返回：错误或空，空计划沿用旧intent不重掷。
std::string mfPlanStateError(const Battle& State) {
    if (State.BeastActions.empty()) return State.intentKind == "beast-actions" ? "妖兽行动意图缺少冻结计划。" : "";
    if (!State.enemyBeast || State.enemyCards.enabled || State.shadowPlayer || State.intentKind != "beast-actions") return "妖兽行动与对手身份不符。";
    if (State.BeastActions.size() > static_cast<std::size_t>(mfActionLimit(State.enemyStage))) return "妖兽行动超过境界上限。";
    if (State.enemyCards.charge < 0 || State.enemyCards.charge > MaximumCharge) return "妖兽蓄势层数无效。";
    const int Major = mfMajorIndex(State.enemyStage);
    const double Basis = mfBattleDamageBasisFromPower(State.enemyPower);
    int Energy = 0, TotalDamage = 0;
    for (const auto& Action : State.BeastActions) {
        if (!mfDefinition(Action.Kind)) return "未知妖兽行动。";
        Energy += mfActionCost(Action.Kind);
        for (const int Amount : {Action.RawDamage,Action.ShieldGain,Action.HealGain,Action.PoisonDamage,Action.BurnDamage,Action.WeakenLayers,Action.ChargeGain,Action.PiercePercent})
            if (Amount < 0 || Amount > 1000000) return "妖兽行动数值无效。";
        if (Action.RawDamage > 0 && mfDamageWeight(Action.Kind, true) <= 0) return "妖兽非攻击行动不能带直伤。";
        const bool BurnPayloadValid = Action.Kind == ActionKind::FlameBreath
            ? Action.BurnDamage == std::max(1, jsRound(Basis * BreathTickCoefficient)) ||
              Action.BurnDamage == std::max(1, jsRound(Basis * LegacyBreathTickCoefficient))
            : Action.BurnDamage == 0;
        const bool PoisonPayloadValid = Action.Kind == ActionKind::VenomBreath
            ? Action.PoisonDamage == std::max(1, jsRound(Basis * BreathTickCoefficient)) ||
              Action.PoisonDamage == std::max(1, jsRound(Basis * LegacyBreathTickCoefficient))
            : Action.PoisonDamage == 0;
        if (Action.ShieldGain != (Action.Kind == ActionKind::Carapace ? jsRound(State.enemyMax * (Major == 0 ? .10 : .08)) : 0) ||
            Action.HealGain != (Action.Kind == ActionKind::Rejuvenate ? jsRound(State.enemyMax * .035) : 0) ||
            !BurnPayloadValid || !PoisonPayloadValid ||
            Action.WeakenLayers != (Action.Kind == ActionKind::Roar ? 1 : 0) || Action.ChargeGain != (Action.Kind == ActionKind::Charge ? 1 : 0) ||
            Action.PiercePercent != (Action.Kind == ActionKind::Rend ? (Major == 0 ? 40 : 25) : 0)) return "妖兽行动附加效果与白名单不符。";
        TotalDamage += Action.RawDamage;
    }
    if (Energy > mfEnergyBudget(State.enemyStage) || TotalDamage > jsRound(Basis * DamageScaleCaps[Major] * 1.06) + mfActionLimit(State.enemyStage) ||
        State.intentRaw != TotalDamage) return "妖兽行动超出本轮妖力或直伤预算。";
    return {};
}
} // namespace nz::BeastCombat
