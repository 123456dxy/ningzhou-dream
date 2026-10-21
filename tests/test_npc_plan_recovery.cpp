#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/save.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

// 用途：自然发牌复现低气血时冻结斩杀前缀；流程：NPC真实计划回归；返回：不改NPC牌堆/资源的独立战斗。
Game mfWoundedPlanEncounter() {
    Game State; State.seed = State.rng = 4;
    State.player.name = "受伤防御回归"; State.player.stage = State.player.highestStage = 3;
    State.player.lifespan = realmOf(3).life; State.player.health = 10;
    State.clock.ageBase = State.player.age; State.player.talent.clear(); State.player.traits.clear();
    State.player.arts.main.clear(); State.player.arts.spells = {"earth", "wood"};
    State.player.arts.knownSpells = State.player.arts.spells; State.player.arts.speed = 200;
    Npc Enemy; Enemy.id = "wounded-plan-npc"; Enemy.name = "玄道完整传承"; Enemy.stage = 3;
    Enemy.sect = "xuandao"; Enemy.dao = "阵"; Enemy.combatProfileId = "xuandao-complete";
    Enemy.lifespan = realmOf(3).life; State.npcs.push_back(Enemy);
    EnemySpec Spec; Spec.id = Enemy.id; Spec.name = Enemy.name; Spec.stage = 3;
    Spec.sect = Enemy.sect; Spec.style = Enemy.dao; Spec.speed = 0;
    battleStart(State, Spec);
    return State;
}

vector<string> mfNaturalDefends(const Battle& State) {
    vector<string> Result;
    for (const auto& Uid : State.hand)
        for (const auto& Card : State.deck)
            if (Uid == Card.uid && Card.spell == "defend") Result.push_back(Uid);
    return Result;
}
} // namespace

void mfRunNpcPlanRecoveryTests() {
    std::printf("\n--- 玩家防住预测斩杀后NPC继续当前手牌 ---\n");
    {
        auto State = mfWoundedPlanEncounter(); auto& Combat = *State.player.arts.battle;
        const auto Opening = battleEnemyPreview(State);
        checkEq(Opening.spells.size(), 1, "开轮低气血下预测木剑即可斩杀，仅显示一招");
        const auto Defends = mfNaturalDefends(Combat);
        check(Defends.size() >= 2, "固定种子真实起手自然含两张普通防御");
        if (Defends.size() < 2) return;
        check(battleTurn(State, Defends[0]).ok && battleTurn(State, Defends[1]).ok,
            "玩家真实出两张防御改变当前承伤条件");
        checkEq(Combat.shield, 64, "筑基两张防御实际获得64护盾");
        const auto Frozen = Combat.enemyCards.planned;
        const auto Before = saveToJson(State);
        const auto Preview = battleEnemyPreview(State);
        check(Preview.spells.size() > Frozen.size(), "预测斩杀被护盾化解后，NPC继续已到手的合法招式");
        check(std::find(Preview.spells.begin(), Preview.spells.end(), "skim") != Preview.spells.end(),
            "未洗牌且前缀已结束时，手中明心实际进入续招预览");
        check(Preview.spells.size() <= static_cast<std::size_t>(mfBattleEnemyActionLimit(State, Combat)),
            "恢复行动不突破筑基完整NPC四次动作上限");
        checkStr(saveToJson(State), Before, "续招预演不修改活动RNG、冻结前缀或牌堆");
        const int Hp = Combat.hp, DrawSize = static_cast<int>(Combat.enemyCards.draw.size());
        check(battleTurn(State, "end").ok, "玩家结束回合真实执行前缀与续招");
        check(battleActive(State), "守势与明心续招的自然夹具仍存活");
        if (!State.player.arts.battle) return;
        const auto& After = *State.player.arts.battle;
        checkEq(Hp - After.hp, Preview.damage, "动态完整意图与真实来伤一致");
        checkEq(After.enemyCards.shuffles, 0, "本例全部抽牌没有跨洗牌");
        checkEq(static_cast<int>(After.enemyCards.draw.size()), DrawSize - 2 - 5,
            "明心在真实动作中抽两张，随后正常开轮抽五张");
        check(std::any_of(After.log.begin(), After.log.end(), [](const string& Line) {
            return Line.find("对方施放明心咒：抽 2 张") != string::npos;
        }), "真实战报记录NPC用明心抽牌");
    }
    {
        auto State = mfWoundedPlanEncounter(); auto& Combat = *State.player.arts.battle;
        const auto Defends = mfNaturalDefends(Combat);
        if (Defends.size() < 2) return;
        battleTurn(State, Defends[0]); battleTurn(State, Defends[1]);
        Combat.enemyCards.mana = 0;
        const auto Before = saveToJson(State); const auto Preview = battleEnemyPreview(State);
        check(Preview.spells.empty(), "无合法费用时不因续招机制死循环或免费出牌");
        checkStr(saveToJson(State), Before, "无合法续招的查询仍保持完整只读");
        check(battleTurn(State, "end").ok, "无合法手牌时敌方正常调息完成回合");
    }
    {
        auto State = mfWoundedPlanEncounter(); auto& Combat = *State.player.arts.battle;
        const auto Defends = mfNaturalDefends(Combat);
        if (Defends.size() < 2) return;
        battleTurn(State, Defends[0]); battleTurn(State, Defends[1]);
        Combat.enemyCards.mana = 12;
        checkEq(battleEnemyPreview(State).spells.size(), mfBattleEnemyActionLimit(State, Combat),
            "资源充足时续招准确止于筑基完整NPC四次上限");
        Combat.enemyCards.planned.clear();
        check(battleEnemyPreview(State).spells.empty(), "没有任何前缀真实施放时不自行创造尾动作");
    }
}
} // namespace nztest
