#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/sim.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立仅境界变化的隔离玩家；流程：统一伤害基底回归；返回：满健康度、体魄零且无被动的值快照。
Player mfScalingPlayer(int Stage) {
    Player PlayerState;
    PlayerState.name = "伤害尺度测试";
    PlayerState.stage = Stage;
    PlayerState.health = 100;
    PlayerState.body = 0;
    PlayerState.talent.clear();
    PlayerState.traits.clear();
    PlayerState.arts.main.clear();
    PlayerState.arts.manuals.clear();
    PlayerState.arts.aux.clear();
    PlayerState.arts.nodes.clear();
    PlayerState.arts.gear.clear();
    PlayerState.arts.equipped.clear();
    return PlayerState;
}
}

// 用途：锁定0.3战力+18的真实伤害尺度；流程：玩家出牌、持续状态、敌方意图及不变的生存资源。
// 返回：无，按手算例检查真实预览和回合结算；测试只修改隔离游戏副本。
void mfRunBattleScalingTests() {
    std::printf("\n--- 统一战斗伤害尺度回归 ---\n");
    struct Sample { int Stage, Health, Strike, Fire, Burn, Poison, Shield, Mana; };
    for (const auto& Row : {Sample{0,144,11,16,2,2,17,3}, Sample{1,168,12,18,3,3,20,3},
                           Sample{2,204,14,21,3,3,24,3}, Sample{3,266,18,27,4,4,32,4},
                           Sample{6,614,39,58,8,9,74,5}, Sample{9,1694,104,156,22,23,203,6},
                           Sample{12,4430,268,402,56,59,532,7}}) {
        const Player PlayerState = mfScalingPlayer(Row.Stage);
        const Battle Reference = battleReferenceState(PlayerState);
        checkEq(Reference.max, Row.Health, "伤害调整不改变各阶段斗法气血");
        checkEq(Reference.maxMana, Row.Mana, "伤害调整不改变每轮基础灵力");
        checkEq(battlePreview(PlayerState, Reference, "strike").damage, Row.Strike, "普通攻击采用零点三战力加十八的新伤害基底");
        const auto Fire = battlePreview(PlayerState, Reference, "fire");
        checkEq(Fire.damage, Row.Fire, "直接火伤与普通攻击采用同一新基底");
        check(Fire.statuses.size() == 1 && Fire.statuses.front().perTurn == Row.Burn,
              "灼烧持续伤害同源缩放，不遗留旧高境界伤害");
        const auto Poison = battlePreview(PlayerState, Reference, "corrode");
        check(Poison.statuses.size() == 1 && Poison.statuses.front().perTurn == Row.Poison,
              "三层腐毒持续伤害采用同一新基底并合计后取整");
        checkEq(battlePreview(PlayerState, Reference, "defend").shield, Row.Shield,
                "普通防御仍按气血百分之十二计算");
    }
    {
        const Player PlayerState = mfScalingPlayer(12);
        Battle Reference = battleReferenceState(PlayerState);
        Reference.poison = 9;
        Reference.poisonTurns = 3;
        const auto WithPoison = battlePreview(PlayerState, Reference, "poisonBurst");
        Reference.poison = 0;
        Reference.poisonTurns = 0;
        const auto WithoutPoison = battlePreview(PlayerState, Reference, "poisonBurst");
        checkEq(WithPoison.damage - WithoutPoison.damage, 904,
                "化神九层毒爆追加558乘九乘百分之十八取整为904");
        checkEq(battlePreview(PlayerState, Reference, "rain").damage, 837,
                "化神剑雨真实首击为558乘一点五，不额外乘境界");
    }
    {
        Game GameState;
        GameState.seed = 1543;
        GameState.rng = 1543;
        GameState.player = mfScalingPlayer(12);
        EnemySpec Enemy;
        Enemy.name = "同阶伤害尺度妖兽";
        Enemy.kind = "spar";
        Enemy.stage = 12;
        Enemy.beast = true;
        Enemy.style = "体";
        Enemy.speed = 0;
        battleStart(GameState, Enemy);
        check(GameState.player.arts.battle.has_value(), "同阶妖兽可以建立真实隔离战斗");
        if (!GameState.player.arts.battle) return;
        const auto& BattleState = *GameState.player.arts.battle;
        checkEq(BattleState.enemyMax, 6424, "妖兽气血一点四五乘区保持不变");
        check(BattleState.intentRaw >= 446 && BattleState.intentRaw <= 503,
              "化神妖兽普击采用558乘零点八五及原随机浮动区间");
        const int BeforeHealth = BattleState.hp;
        const int PredictedDamage = battleEnemyPreview(GameState).damage;
        const int IntentDamage = BattleState.intentRaw;
        checkEq(PredictedDamage, IntentDamage, "妖兽实战意图预览采用已经冻结的新伤害数值");
        const auto Turn = battleTurn(GameState, "end");
        check(Turn.ok && GameState.player.arts.battle &&
              BeforeHealth - GameState.player.arts.battle->hp == IntentDamage,
              "妖兽真实完整回合伤害与意图预览一致");
    }
}
} // namespace nztest
