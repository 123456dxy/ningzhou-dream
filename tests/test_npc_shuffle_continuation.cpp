#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/save.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

// 用途：构造普通或Shadow合法牌堆的跨重洗边界；流程：NPC后段少动作回归；返回：仅内存靶场，不用于胜率。
Game mfShuffleEncounter(bool Shadow = false) {
    Game Encounter; Encounter.seed = Encounter.rng = 31;
    auto& Actor = Encounter.player;
    Actor.name = "洗牌续招靶"; Actor.stage = Actor.highestStage = 12;
    Actor.lifespan = realmOf(12).life; Actor.talent.clear(); Actor.traits.clear();
    Actor.arts.main.clear(); Actor.arts.speed = 200; Encounter.clock.ageBase = Actor.age;
    Npc Opponent;
    Opponent.id = "shuffle-opponent"; Opponent.name = "玄道完整传承"; Opponent.stage = 12;
    Opponent.sect = "xuandao"; Opponent.dao = "阵"; Opponent.combatProfileId = "xuandao-complete";
    Opponent.lifespan = realmOf(12).life;
    if (Shadow) {
        Player Foe; Foe.name = Opponent.name; Foe.stage = Foe.highestStage = 12;
        Foe.sect = Opponent.sect; Foe.lifespan = Opponent.lifespan; Foe.talent.clear(); Foe.traits.clear();
        Foe.arts = artNpcLoadout(Opponent);
        const Player Challenger = Actor;
        battleStartShadow(Encounter, Challenger, Foe, 31);
    } else {
        Encounter.npcs.push_back(Opponent);
        EnemySpec Enemy; Enemy.id = Opponent.id; Enemy.name = Opponent.name;
        Enemy.stage = 12; Enemy.sect = Opponent.sect; Enemy.style = Opponent.dao; Enemy.speed = 0;
        battleStart(Encounter, Enemy);
    }
    auto& State = *Encounter.player.arts.battle;
    State.max = State.hp = 100000000; // 仅延长靶生存，保持敌人真实属性/灵力/行动数。
    auto& Enemy = State.enemyCards;
    Enemy.hand.clear(); Enemy.draw.clear(); Enemy.discard.clear(); Enemy.planned.clear();
    int Guards = 0, DrawCards = 0;
    for (const auto& Card : Enemy.deck) {
        if ((Card.spell == "defend" && Guards < 4) || (Card.spell == "skim" && DrawCards < 1)) {
            Enemy.draw.push_back(Card.uid);
            if (Card.spell == "defend") ++Guards; else ++DrawCards;
        } else Enemy.discard.push_back(Card.uid);
    }
    battleTurn(Encounter, "end"); // 下一真实轮抽完五张，draw空，discard仍有19张可抽牌。
    return Encounter;
}

// 用途：指定只含两守势和一抽牌的已有前缀；流程：Shadow兼容或定向边界；返回：无，不改牌堆或给额外资源。
void mfSetPrefix(Battle& State, bool DrawOnly) {
    auto& Enemy = State.enemyCards; Enemy.planned.clear();
    int Guards = 0;
    for (const auto& Uid : Enemy.hand) {
        const auto Card = std::find_if(Enemy.deck.begin(), Enemy.deck.end(), [&](const auto& Entry) { return Entry.uid == Uid; });
        if (Card != Enemy.deck.end() && Card->spell == "defend" && !DrawOnly && Guards++ < 2) Enemy.planned.push_back(Uid);
    }
    for (const auto& Uid : Enemy.hand) {
        const auto Card = std::find_if(Enemy.deck.begin(), Enemy.deck.end(), [&](const auto& Entry) { return Entry.uid == Uid; });
        if (Card != Enemy.deck.end() && Card->spell == "skim") { Enemy.planned.push_back(Uid); break; }
    }
}
}

// 用途：验证普通修士在合法抽牌跨重洗后续招，且预览/RNG/保存/旧前缀/Shadow边界一致。
// 流程：NPC规划与真实回合定向回归；返回：无，失败计入harness；不访问服务和玩家档。
void mfRunNpcShuffleContinuationTests() {
    std::printf("\n--- NPC跨重洗后的真实续招 ---\n");
    {
        Game Encounter = mfShuffleEncounter();
        auto& State = *Encounter.player.arts.battle;
        check(State.enemyCards.draw.empty() && State.enemyCards.discard.size() >= 10, "真实下一轮到达draw空且discard充足边界");
        checkEq(State.enemyCards.planned.size(), 3, "冻结前缀仅承诺两守势与明心，不预写未来弃牌UID");
        const auto OriginalPrefix = State.enemyCards.planned;
        const string Before = saveToJson(Encounter);
        const auto Preview = battleEnemyPreview(Encounter);
        check(Preview.spells.size() > OriginalPrefix.size() && Preview.damage > 0, "明心合法重洗抽到攻击后，普通修士继续使用已到手牌");
        check(Preview.spells.size() <= static_cast<std::size_t>(mfBattleEnemyActionLimit(Encounter, State)), "抽牌续招不突破完整修士七次行动上限");
        checkStr(saveToJson(Encounter), Before, "预演重洗与续招只改副本，不动活动RNG/牌堆/资源");
        check(State.enemyCards.planned == OriginalPrefix, "查询不把未来discard引用写回原计划");
        Game Restored; string Error;
        check(loadDetachedFromJson(Before, Restored, &Error), "保留原合法UID前缀的战斗仍可存读档");
        if (Restored.player.arts.battle) checkEq(battleEnemyPreview(Restored).damage, Preview.damage, "读档保持当前RNG下真实续招预览");
        // 模拟玩家本回合抽牌先消耗了共享RNG；NPC到实际拿到新牌才决策，不能依赖旧洗牌预测。
        randomOf(Encounter); randomOf(Encounter); randomOf(Encounter);
        const auto CurrentPreview = battleEnemyPreview(Encounter);
        const int HpBefore = State.hp, ShufflesBefore = State.enemyCards.shuffles;
        check(battleTurn(Encounter, "end").ok, "共享RNG已改变后仍能真实执行跨洗牌续招");
        checkEq(HpBefore - State.hp, CurrentPreview.damage, "当前意图与实际逐步来伤完全相同，不依赖开轮时旧RNG");
        check(State.enemyCards.shuffles > ShufflesBefore, "重洗只在真实动作中提交");
    }
    {
        Game Encounter = mfShuffleEncounter();
        auto& State = *Encounter.player.arts.battle;
        // 留足未洗抽牌：普通NPC用完前缀后仍须继续选择已经到手的合法牌。
        for (int Card = 0; Card < 3; ++Card) {
            State.enemyCards.draw.push_back(State.enemyCards.discard.back()); State.enemyCards.discard.pop_back();
        }
        mfSetPrefix(State, true);
        const auto Preview = battleEnemyPreview(Encounter);
        check(Preview.spells.size() > 1 && Preview.damage > 0,
            "普通NPC明心未跨重洗也可继续已到手的合法牌，不把短前缀误当行动上限");
        check(Preview.spells.size() <= static_cast<std::size_t>(mfBattleEnemyActionLimit(Encounter, State)),
            "未跨重洗续招仍遵守同一完整修士行动上限");
        const int HpBefore = State.hp;
        check(battleTurn(Encounter, "end").ok, "未跨重洗的旧前缀正常执行");
        checkEq(HpBefore - State.hp, Preview.damage, "未跨重洗的实际抽牌续招与当前完整意图一致");
    }
    {
        Game Encounter = mfShuffleEncounter(true);
        auto& State = *Encounter.player.arts.battle;
        mfSetPrefix(State, false);
        const string Before = saveToJson(Encounter);
        const auto Preview = battleEnemyPreview(Encounter);
        checkEq(Preview.spells.size(), 3, "Shadow仍严格执行原三步前缀，不增加普通NPC尾动作");
        checkEq(Preview.damage, 0, "Shadow指定守势与抽牌前缀不偷附攻击");
        checkStr(saveToJson(Encounter), Before, "Shadow预演重洗仍纯只读");
        const int HpBefore = State.hp;
        check(battleTurn(Encounter, "end").ok, "Shadow保持旧计划真实执行语义");
        checkEq(HpBefore - State.hp, Preview.damage, "Shadow旧前缀真实与预览仍一致");
    }
}
} // namespace nztest
