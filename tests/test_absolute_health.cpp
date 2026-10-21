#include "harness.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/combat/battle_scaling.h"
#include "core/interface/view_policy.h"
#include "core/progression/zhuji_rules.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
using namespace nz;

// 用途：锁定玩家绝对气血的真实创建、入场与回写；流程：独立内存角色，无文件或网络。
void mfRunAbsoluteHealthTests() {
    CreateOptions Options;
    Options.seed = 19017;
    Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.checkpoint.enabled = false;
    State.checkpoint.snapshot.clear();
    State.pending.reset();
    State.job.reset();
    checkEq(State.player.health, battleReferenceState(State.player).max,
        "新角色在全部构筑初始化后拥有满额绝对气血");
    State.player.arts.CombatPillDoseCount = 3;
    State.player.arts.combatPrep["shield"] = 0.2;
    battleReferenceState(State.player);
    checkEq(State.player.arts.CombatPillDoseCount, 3, "只读开局参考不消费真实战斗丹枚数");
    State.player.health = 60;
    EnemySpec Enemy;
    Enemy.name = "绝对气血回归妖兽";
    Enemy.kind = "spar";
    Enemy.stage = 0;
    Enemy.beast = true;
    Enemy.speed = 0;
    battleStart(State, Enemy);
    checkEq(State.player.arts.CombatPillDoseCount, 0, "真实开战消费准备后清空战斗丹枚数");
    check(State.player.arts.battle.has_value(), "绝对气血回归建立真实斗法");
    if (!State.player.arts.battle) return;
    checkEq(State.player.arts.battle->hp, 60, "入场保留当前气血点数而不再次乘百分比");
    State.player.arts.battle->hp = 47;
    battleFinish(State, false, true);
    checkEq(State.player.health, 47, "撤离直接写回真实气血点数");

    Player Plain;
    Plain.name = "绝对气血规则夹具";
    Plain.talent.clear(); Plain.traits.clear(); Plain.body = 0;
    Plain.arts.main.clear(); Plain.arts.manuals.clear();
    checkNear(calendarDetoxPerYear(Plain), 5, "普通角色每年自然消散5点丹毒");
    Plain.arts.nodes = {"10-1", "10-2"};
    checkNear(calendarDetoxPerYear(Plain), 5, "丹道不额外改变统一的自然消散量");
    Plain.arts.nodes.clear();
    checkEq(mfPlayerHealthMaximum(Plain), 148, "炼气初期曲线A基础上限为148点");
    Plain.arts.BaseHealthBonus = 7;
    Plain.arts.FoundationHealthBonus = 20;
    checkEq(mfPlayerHealthMaximum(Plain), 213, "锻体与根基基础奖励在2.4乘区之前共同结算");
    checkEq(mfHealthMaximum(Plain, 1.45), 309, "遭遇倍率在最终取整前作用于完整基础上限");
    Plain.arts.hpBonus = 100;
    checkEq(mfPlayerHealthMaximum(Plain), 426, "旧hpBonus继续作为倍率，不能被重解释为新基础值");
    Plain.arts.hpBonus = 0;
    checkEq(mfHealthFromPercent(12, 148), 18, "旧12%转换成18点气血");
    checkEq(mfHealthFromPercent(100, 148), 148, "旧100%转换为完整气血上限");
    checkEq(mfHealthFromPercent(0, 148), 0, "百分比迁移不复活死亡角色");
    checkEq(mfHealthToPercent(18, 148), 12, "副本兼容出口按实际点数换百分比");
    checkEq(mfRecoveredHealth(140, 148, 40), 148, "恢复不能超过气血上限");
    checkEq(mfRecoveredHealth(0, 148, 40), 0, "恢复不能复活零气血角色");
    checkEq(mfMonthlyHealthRecovery(148), 4, "月恢复按148上限的3%取整为4点");
    checkEq(mfRestExtraHealthRecovery(148), 11, "疗养额外11点与月4点合计15点即10%取整");

    State.player = Plain;
    State.player.arts.BaseHealthBonus = 0;
    State.player.arts.FoundationHealthBonus = 0;
    State.player.health = 50;
    State.clock.days = 29;
    State.year = 0;
    State.clock.ageBase = State.player.age;
    State.npcs.clear();
    calendarBattleDay(State);
    checkEq(State.player.health, 54, "真实月界恢复4点绝对气血");
    Job Rest;
    Rest.actionType = "plan"; Rest.plan = "rest"; Rest.total = 30;
    monthComplete(State, Rest);
    checkEq(State.player.health, 65, "真实疗养额外11点与月恢复合计15点");
    State.player.health = 50;
    applyDamage(State, 8);
    checkEq(State.player.health, 38, "原8%场外代价在148上限扣除12点");
    State.player.health = 30;
    const int UnwoundedPower = power(State.player);
    Person PercentProfile = State.player;
    PercentProfile.health = mfHealthToPercent(30, 148);
    checkNear(UnwoundedPower, power(PercentProfile), "玩家场外战力与相同受伤比例NPC口径一致", 1.1);

    State.player.health = 100;
    battleStart(State, Enemy);
    if (State.player.arts.battle) {
        State.player.arts.battle->hp = 40;
        State.clock.days = 59;
        calendarBattleDay(State);
        checkEq(State.player.arts.battle->hp, 44, "战中跨月直接恢复实时斗法气血");
        battleFinish(State, false, true);
        checkEq(State.player.health, 44, "战中月恢复不会被战后回写抹去");
    }
    State.player.health = 147;
    State.clock.days = 89;
    calendarBattleDay(State);
    checkEq(State.player.health, 148, "真实月恢复到上限即停止");
    State.player.xp = realmOf(State.player.stage).need;
    for (const int Health : {29, 30, 31}) {
        State.player.health = Health;
        const auto Reason = api::view::breakthroughReason(State, breakthroughInfo(State));
        check(Reason.empty() == (Health >= 30), "真实突破门槛在29/30/31点按固定30分界");
    }

    Game Preparation;
    Preparation.player = Plain;
    Preparation.player.arts.BaseHealthBonus = 0;
    Preparation.player.arts.FoundationHealthBonus = 0;
    Preparation.player.stage = Preparation.player.highestStage = 2;
    Preparation.player.xp = realmOf(2).need;
    Preparation.player.health = 175;
    Preparation.player.stones = 10000;
    Preparation.player.age = 19; Preparation.player.lifespan = 200;
    Preparation.year = 3; Preparation.clock.days = 1080; Preparation.clock.ageBase = 16;
    check(mfStartZhuji(Preparation, ZhujiRoute::Elements, 0, false).empty(), "真实日历回归先提交九十天筑基准备");
    for (int Month = 0; Month < 3 && Preparation.job; ++Month)
        check(calendarRun(Preparation, 90).ok, "九十天准备经真实日历按月推进");
    checkEq(Preparation.clock.days, 1170, "筑基准备真实消耗九十天");
    check(Preparation.player.Zhuji && Preparation.player.Zhuji->Status == ZhujiStatus::Active &&
        !Preparation.job && !Preparation.pending,
        "准备激活小游戏时不再插入第三年山门来信或其它随机抉择");
}
} // namespace nztest
