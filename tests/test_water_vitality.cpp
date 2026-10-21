#include "harness.h"
#include "core/combat/water_vitality.h"
#include "core/model.h"
#include <limits>
#include <tuple>

namespace nztest {
namespace {
using namespace nz;

// 用途：构造一次主段之后的局部战况；流程：纯回血边界；返回：不建立真实对局或使用RNG的独立值。
Battle mfVitalityAfterHit(int Health = 500, int TargetHealth = 800) {
    Battle State;
    State.hp = Health; State.max = 1000; State.enemyHp = TargetHealth; State.enemyMax = 1000;
    State.enemyShield = 0;
    return State;
}

// 用途：构造主辅资格；流程：满层门槛回归；返回：仅带真实稳定ID的独立功法值。
Arts mfVitalityArts(int Level = WaterVitality::RequiredManualLevel, bool Main = true) {
    Arts Actor;
    Actor.main = Main ? "wave" : "water";
    Actor.manuals["wave"] = Level;
    if (!Main) { Actor.manuals["water"] = 5; Actor.aux = {"wave"}; }
    return Actor;
}

// 用途：比较回春报价；流程：预览、执行及重复查询；返回：失败计入统一测试框架。
void mfCheckVitalityFacts(const WaterVitality::HitFacts& Got, const WaterVitality::HitFacts& Want,
        const string& Context) {
    check(Got.Qualified == Want.Qualified, Context + "：主修资格");
    checkEq(Got.DirectHealthLoss, Want.DirectHealthLoss, Context + "：真实主段损血");
    checkEq(Got.HealingPotential, Want.HealingPotential, Context + "：本次治疗潜力");
    checkEq(Got.Healing, Want.Healing, Context + "：实际回复");
}

// 用途：锁定回春仅写自身HP；流程：查询和执行回归；返回：敌方HP/护盾及全部关联资源的快照。
auto mfVitalityResources(const Battle& State) {
    return std::tuple{State.enemyHp, State.enemyShield, State.shield, State.mana, State.played,
        State.water, State.swordQi, State.soul, State.charge, State.poison, State.poisonTurns,
        State.poisonDamage, State.playerPoison, State.playerPoisonTurns, State.burn, State.burnTurns,
        State.guard, State.buff, State.discardPower, State.enemyWeaken, State.pendingDraw,
        State.WaterFlowDraws, State.comboDraws, State.last, State.cooldowns,
        State.hand, State.draw, State.discard, State.exhausted};
}
}

// 用途：验证主修五层按实际直伤回血且只补缺血；流程：独立/统一harness；返回：测试失败计数。
void mfRunWaterVitalityTests() {
    using namespace nz;
    std::printf("\n--- 浪击诀满层实际直伤回春 ---\n");
    const Arts FullMain = mfVitalityArts();
    check(WaterVitality::mfQualifies(FullMain), "主修浪击诀满五层解锁");
    check(!WaterVitality::mfQualifies(mfVitalityArts(4)), "主修四层不提前解锁");
    check(!WaterVitality::mfQualifies(mfVitalityArts(5, false)), "满五层辅修不解锁");
    check(!WaterVitality::mfQualifies(Arts{}), "未领悟或无主修不解锁");
    check(!WaterVitality::mfQualifies(mfVitalityArts(0)), "零层主修不解锁");
    {
        auto Unrelated = FullMain; Unrelated.main = "ocean"; Unrelated.manuals["ocean"] = 5;
        check(!WaterVitality::mfQualifies(Unrelated), "其他水系主修不混用wave满层资格");
        auto Beyond = FullMain; Beyond.manuals["wave"] = 6;
        check(WaterVitality::mfQualifies(Beyond), "资格明确按至少五层判断");
    }
    const auto Description = WaterVitality::mfRulesDescription();
    check(Description.find("主修") != string::npos && Description.find("5层") != string::npos &&
        Description.find("50%") != string::npos && Description.find("实际") != string::npos,
        "说明使用同一五层及50%常量并明确主修与实际损血");
    check(Description.find("最多回复缺失气血") != string::npos && Description.find("持续伤害") != string::npos &&
        Description.find("反震") != string::npos && Description.find("转伤") == string::npos,
        "说明限定缺血并排除持续伤害与反震，不再包含转伤机制");
    check(WaterVitality::mfHitDescription({}).empty(), "无资格无损血的回春战报为空");
    for (const int Health : {500, 970, 1000}) {
        auto State = mfVitalityAfterHit(Health); State.enemyShield = 70;
        const auto Resources = mfVitalityResources(State);
        const auto Quote = WaterVitality::mfQuote(FullMain, State, 1000);
        checkEq(State.hp, Health, "报价不预先回血");
        check(mfVitalityResources(State) == Resources, "报价不改原命中后的敌方HP、剩余护盾或资源");
        const auto Actual = WaterVitality::mfApply(FullMain, State, 1000);
        mfCheckVitalityFacts(Actual, Quote, "不同缺血下报价与执行一致");
        checkEq(Actual.DirectHealthLoss, 200, "只使用首击实际损失200HP");
        checkEq(Actual.HealingPotential, 100, "实际直伤50%产生100治疗潜力");
        checkEq(Actual.Healing, std::min(100, 1000 - Health), "实际回复不超过本次HP缺口");
        checkEq(State.hp, std::min(1000, Health + 100), "回春不会超出气血上限");
        check(mfVitalityResources(State) == Resources, "半血、轻伤或满血均不追加伤害或消耗目标护盾");
        const auto Text = WaterVitality::mfHitDescription(Actual);
        check(Text.find("实际损血200") != string::npos && Text.find("治疗潜力100") != string::npos &&
            Text.find("回复" + std::to_string(Actual.Healing)) != string::npos && Text.find("转伤") == string::npos,
            "普通神通与V2共用仅包含损血、潜力和回复的战报");
    }
    {
        auto State = mfVitalityAfterHit(1000, 10);
        const auto Actual = WaterVitality::mfApply(FullMain, State, 210);
        checkEq(Actual.HealingPotential, 100, "满血仍保留治疗潜力供预览");
        checkEq(Actual.Healing, 0, "满血实际回复为零");
        checkEq(State.enemyHp, 10, "主段剩余10HP不会被回血机制追加斩杀");
        State.enemyHp -= 8;
        const auto Next = WaterVitality::mfApply(FullMain, State, 10);
        checkEq(Next.DirectHealthLoss, 8, "下一主动攻击只用自身实际直伤计价");
        checkEq(Next.Healing, 0, "连续满血命中不会递归回复");
        checkEq(State.enemyHp, 2, "连续命中只扣原主动攻击伤害");
    }
    {
        auto Blocked = mfVitalityAfterHit(600, 1000); Blocked.enemyShield = 150;
        const auto Actual = WaterVitality::mfApply(FullMain, Blocked, 1000);
        checkEq(Actual.DirectHealthLoss, 0, "首击全被盾挡时实际HP损失为零");
        checkEq(Actual.HealingPotential, 0, "盾挡不虚构回血");
        checkEq(Blocked.enemyShield, 150, "回血机制不消耗剩余护盾");
        checkEq(Blocked.hp, 600, "全盾挡不恢复气血");
        check(WaterVitality::mfHitDescription(Actual).empty(), "全盾挡时不展示回春战报");
    }
    for (const int Health : {0, -1}) {
        auto Dead = mfVitalityAfterHit(Health);
        const auto Actual = WaterVitality::mfApply(FullMain, Dead, 1000);
        checkEq(Actual.HealingPotential, 0, "原命中反震致死后不生成回春潜力");
        checkEq(Dead.hp, Health, "反震致死不会被回春复活");
        checkEq(Dead.enemyHp, 800, "死亡施术者不改变原命中结果");
    }
    {
        auto Overkill = mfVitalityAfterHit(900, -950);
        const auto Actual = WaterVitality::mfApply(FullMain, Overkill, 50);
        checkEq(Actual.DirectHealthLoss, 50, "首击过量伤害1000只计目标原有50HP");
        checkEq(Actual.HealingPotential, 25, "超杀不虚构超过实际存量的治疗");
        checkEq(Overkill.hp, 925, "斩杀仍兑现实际损失的一半");
        checkEq(Overkill.enemyHp, -950, "已死亡目标不重复扣血或改写首击结果");
    }
    for (const int Loss : {1, 3, 99, 101}) {
        auto Odd = mfVitalityAfterHit(500, 1000 - Loss);
        const auto Actual = WaterVitality::mfApply(FullMain, Odd, 1000);
        checkEq(Actual.HealingPotential, Loss / 2, "奇数实际直伤按50%向下取整");
        checkEq(Odd.hp, 500 + Loss / 2, "奇数回血执行与舍入一致");
        checkEq(Odd.enemyHp, 1000 - Loss, "奇数回春不产生额外伤害");
    }
    for (const Arts Actor : {mfVitalityArts(4), mfVitalityArts(5, false), Arts{}}) {
        auto State = mfVitalityAfterHit(1000);
        const auto Actual = WaterVitality::mfApply(Actor, State, 1000);
        check(!Actual.Qualified, "不满足主修满层时无资格");
        checkEq(Actual.Healing, 0, "无资格不回血");
        checkEq(State.enemyHp, 800, "无资格保持原命中结果");
    }
    {
        const int Maximum = std::numeric_limits<int>::max();
        auto Large = mfVitalityAfterHit(); Large.max = Maximum; Large.hp = 1;
        Large.enemyHp = std::numeric_limits<int>::min();
        const auto Quote = WaterVitality::mfApply(FullMain, Large, Maximum);
        checkEq(Quote.DirectHealthLoss, Maximum, "极端HP差使用宽整数并截断初始HP");
        checkEq(Quote.HealingPotential, Maximum / 2, "大值回血无乘法溢出");
        checkEq(Large.hp, 1LL + Maximum / 2, "大值实际回复不越界");
        checkEq(Large.enemyHp, std::numeric_limits<int>::min(), "极端目标值也不因回春再次做伤害减法");
    }
    {
        Game Encounter; Encounter.rng = 20261019; Encounter.player.arts = FullMain;
        Encounter.player.arts.battle = mfVitalityAfterHit(1000);
        auto& State = *Encounter.player.arts.battle;
        State.mana = 6; State.played = 2; State.water = 4; State.swordQi = 3; State.charge = 2;
        State.poison = 30; State.poisonTurns = 1; State.poisonDamage = 123; State.playerPoison = 45; State.playerPoisonTurns = 1;
        State.burn = 67; State.burnTurns = 3; State.pendingDraw = 1; State.WaterFlowDraws = 1; State.comboDraws = 1;
        State.buff = .3; State.discardPower = .2; State.enemyWeaken = 1; State.last = "金";
        State.hand = {"h"}; State.draw = {"d"}; State.discard = {"x"}; State.exhausted = {"e"}; State.cooldowns["sample"] = 3;
        const auto Resources = mfVitalityResources(State); const auto RandomBefore = Encounter.rng;
        const auto First = WaterVitality::mfQuote(Encounter.player.arts, State, 1000);
        const auto Second = WaterVitality::mfQuote(Encounter.player.arts, State, 1000);
        mfCheckVitalityFacts(Second, First, "重复回春报价一致");
        checkEq(State.hp, 1000, "重复报价不写施术者气血");
        check(mfVitalityResources(State) == Resources && Encounter.rng == RandomBefore,
            "报价不动敌方气血/护盾、牌堆、状态或活动RNG");
        const auto Actual = WaterVitality::mfApply(Encounter.player.arts, State, 1000);
        mfCheckVitalityFacts(Actual, First, "真实回复与只读报价一致");
        check(mfVitalityResources(State) == Resources, "回血执行不改敌方或重复触发状态、抽牌、返灵和冷却");
        checkEq(Encounter.rng, RandomBefore, "回血执行不消耗活动RNG");
    }
}
} // namespace nztest

#ifdef NZ_WATER_VITALITY_STANDALONE
int main() {
    nztest::mfRunWaterVitalityTests();
    return nztest::failures() == 0 ? 0 : 1;
}
#endif
