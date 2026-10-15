#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nz {
struct Game;
struct Player;
struct Battle;
namespace BeastCombat {
enum class ActionKind { Pounce, Maul, FlameBreath, VenomBreath, Carapace, Roar, Charge, Rend, Rejuvenate };
// 妖兽开轮冻结的行动：只持有结果数字，执行时不再掷伤害或借用人类神通。
struct PlannedAction {
    ActionKind Kind = ActionKind::Pounce;
    int RawDamage = 0, ShieldGain = 0, HealGain = 0;
    int PoisonDamage = 0, BurnDamage = 0, WeakenLayers = 0, ChargeGain = 0, PiercePercent = 0;
};
// 一段真实执行或只读预演事实；死亡后的剩余计划保持Executed=false。
struct ActionResult {
    PlannedAction Action;
    bool Executed = false;
    int Damage = 0, Blocked = 0, CounterDamage = 0;
};
struct PhaseResult {
    int Damage = 0, Shield = 0, Heal = 0;
    std::string Description;
    std::vector<ActionResult> Actions;
};
// 用途：映射行动稳定ID/名称/妖力费用；流程：计划/codec/视图；返回：白名单静态值，不读取世界。
std::string_view mfActionId(ActionKind Kind);
std::string_view mfActionName(ActionKind Kind);
int mfActionCost(ActionKind Kind);
// 用途：严格解析妖兽行动ID；流程：加载；返回：未知为空，不转换成人类神通。
std::optional<ActionKind> mfParseActionKind(std::string_view Id);
// 用途：提供境界行动和妖力上限；流程：计划/校验/展示；返回：炼气至化神1/2/3/3/4行动、2/3/4/5/6妖力。
int mfActionLimit(int Stage);
int mfEnergyBudget(int Stage);
// 用途：真实开轮冻结整轮行动；流程：妖兽开局/下一轮；返回：无，只在动作边界消耗一次局部伤害浮动所需的活动RNG。
void mfPlanActions(Game& State);
// 用途：按冻结计划逐段执行；流程：真实先手/回合及预览副本；返回：每段盾耗/损血事实，无RNG，不调用人生结算。
PhaseResult mfExecuteActions(const Player& Defender, Battle& State, bool WriteLog);
// 用途：验证新冻结计划及蓄势；流程：持久化/动作边界；返回：错误或空，空计划保留旧intent。
std::string mfPlanStateError(const Battle& State);
} // namespace BeastCombat
} // namespace nz
