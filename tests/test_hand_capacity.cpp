#include "harness.h"

#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/hand_rules.h"
#include "core/save.h"
#include "core/sim.h"

#include <algorithm>
#include <set>

namespace nztest {
namespace {
using namespace nz;

// 用途：创建带抽牌、换手神通的合法独立对局；流程：容量及存档回归；用法：无参数；返回：内存游戏，不读写玩家文件。
Game mfHandCapacityEncounter() {
    CreateOptions Options;
    Options.seed = 215012;
    Game Encounter = createGame(Options);
    Encounter.pending.reset();
    Encounter.player.stage = Encounter.player.highestStage = 6;
    Encounter.player.lifespan = realmOf(6).life + Encounter.player.lifeBonus;
    Encounter.player.arts.spells = {"skim", "refresh", "gather"};
    ensureArts(Encounter);
    Encounter.player.health = mfPlayerHealthMaximum(Encounter.player);
    EnemySpec Opponent;
    Opponent.name = "手牌容量对手";
    Opponent.stage = 6;
    Opponent.sect = "jinhong";
    Opponent.style = "金";
    Opponent.speed = 0;
    battleStart(Encounter, Opponent);
    return Encounter;
}

// 用途：只重排真实唯一牌UID形成指定容量手牌；流程：满手及非法导入边界；用法：传完整四分区与数量，可指定首牌；返回：是否能由真实牌堆满足。
bool mfArrangeCapacityHand(const vector<Card>& Deck, vector<string>& Hand, vector<string>& Draw,
                           vector<string>& Discard, vector<string>& Exhausted,
                           int CardCount, const string& FirstSpell = {}) {
    if (CardCount < 0 || static_cast<std::size_t>(CardCount) > Deck.size()) return false;
    vector<string> Ordered;
    if (!FirstSpell.empty()) {
        const auto First = std::find_if(Deck.begin(), Deck.end(), [&](const Card& Entry) {
            return Entry.spell == FirstSpell;
        });
        if (First == Deck.end()) return false;
        Ordered.push_back(First->uid);
    }
    for (const Card& Entry : Deck)
        if (Ordered.empty() || Entry.uid != Ordered.front()) Ordered.push_back(Entry.uid);
    Hand.assign(Ordered.begin(), Ordered.begin() + CardCount);
    Draw.assign(Ordered.begin() + CardCount, Ordered.end());
    Discard.clear();
    Exhausted.clear();
    return true;
}

// 用途：检查容量以及四牌堆唯一守恒；流程：真实抽牌、换手和敌方开轮；用法：传只读完整牌堆与四分区；返回：是否全部合法。
bool mfCapacityPartitioned(const vector<Card>& Deck, const vector<string>& Hand,
                           const vector<string>& Draw, const vector<string>& Discard,
                           const vector<string>& Exhausted) {
    if (Hand.size() > BattleCards::MaximumHandSize) return false;
    std::set<string> Known, Placed;
    for (const Card& Entry : Deck) if (!Known.insert(Entry.uid).second) return false;
    for (const auto* Pile : {&Hand, &Draw, &Discard, &Exhausted})
        for (const string& Uid : *Pile)
            if (!Known.count(Uid) || !Placed.insert(Uid).second) return false;
    return Known == Placed;
}
} // namespace

// 用途：验证十二张容量覆盖双方真实动作、预览与持久化；流程：定向及统一回归；用法：测试框架调用；返回：无，累计断言结果。
void mfRunHandCapacityTests() {
    using namespace nz;
    std::printf("\n-- 十二张手牌容量与双方存档边界 --\n");
    checkEq(BattleCards::MaximumHandSize, 12, "手牌容量扩展为十二张，区别于携带神通槽位");
    {
        Game Encounter = mfHandCapacityEncounter();
        Battle& State = *Encounter.player.arts.battle;
        checkEq(State.hand.size(), 5, "扩大容量不额外增加玩家起手牌");
        checkEq(State.enemyCards.hand.size(), 5, "扩大容量不额外增加NPC起手牌");
        battleDraw(Encounter, 100);
        checkEq(State.hand.size(), BattleCards::MaximumHandSize, "玩家大量抽牌恰好截断在十二张");
        check(mfCapacityPartitioned(State.deck, State.hand, State.draw, State.discard, State.exhausted),
              "玩家大量抽牌后四牌堆唯一且守恒");
        const string Before = saveToJson(Encounter);
        battleDraw(Encounter, 100);
        battleDraw(Encounter, 0);
        battleDraw(Encounter, -1);
        checkStr(saveToJson(Encounter), Before, "满手或非正抽牌数量不改变牌堆与随机数");
    }
    for (const string SpellId : {string("skim"), string("refresh")}) {
        Game Encounter = mfHandCapacityEncounter();
        Battle& State = *Encounter.player.arts.battle;
        check(mfArrangeCapacityHand(State.deck, State.hand, State.draw, State.discard, State.exhausted,
                                   BattleCards::MaximumHandSize, SpellId), "从真实牌堆构造满十二张手牌");
        State.mana = battleManaCap(Encounter.player);
        const string PlayedUid = State.hand.front();
        check(battleTurn(Encounter, PlayedUid).ok, "满手情况下抽牌或换手通过真实动作执行");
        checkEq(State.hand.size(), BattleCards::MaximumHandSize, "抽牌和换手都补到十二张且不溢出");
        check(std::find(State.hand.begin(), State.hand.end(), PlayedUid) == State.hand.end(),
              "正在施放的抽牌或换手卡不会被自己立即抽回");
        check(mfCapacityPartitioned(State.deck, State.hand, State.draw, State.discard, State.exhausted),
              "满容量抽牌或换手后四牌堆仍互斥且守恒");
    }
    {
        Game Encounter = mfHandCapacityEncounter();
        Battle& State = *Encounter.player.arts.battle;
        State.drawNext = State.enemyCards.drawNext = 100;
        const string BeforePreview = saveToJson(Encounter);
        const EnemyPreview First = battleEnemyPreview(Encounter);
        const EnemyPreview Repeated = battleEnemyPreview(Encounter);
        checkStr(saveToJson(Encounter), BeforePreview, "敌方重复预演不改变手牌、抽牌请求或随机数");
        checkEq(First.damage, Repeated.damage, "同一十二张容量规则下敌方重复预演稳定");
        const int HealthBefore = State.hp;
        check(battleTurn(Encounter, "end").ok, "双方额外抽牌经真实回合推进");
        checkEq(HealthBefore - State.hp, First.damage, "敌方真实行动伤害与只读镜像预演一致");
        checkEq(State.hand.size(), BattleCards::MaximumHandSize, "玩家下一轮大量抽牌不突破十二张");
        checkEq(State.enemyCards.hand.size(), BattleCards::MaximumHandSize, "NPC镜像下一轮同样可持有十二张");
        check(mfCapacityPartitioned(State.enemyCards.deck, State.enemyCards.hand, State.enemyCards.draw,
                                   State.enemyCards.discard, State.enemyCards.exhausted),
              "NPC扩容抽牌保持四牌堆唯一守恒");
    }
    {
        Game Encounter = mfHandCapacityEncounter();
        Battle& State = *Encounter.player.arts.battle;
        EnemyCombat& Enemy = State.enemyCards;
        check(mfArrangeCapacityHand(State.deck, State.hand, State.draw, State.discard, State.exhausted, 8) &&
              mfArrangeCapacityHand(Enemy.deck, Enemy.hand, Enemy.draw, Enemy.discard, Enemy.exhausted, 8),
              "旧八张容量存档夹具沿用真实牌UID");
        Enemy.planned.clear();
        Game Restored;
        string Error;
        check(loadFromJson(saveToJson(Encounter), Restored, &Error), "原八张手牌存档继续可加载：" + Error);
        check(Restored.player.arts.battle && Restored.player.arts.battle->hand.size() == 8 &&
              Restored.player.arts.battle->enemyCards.hand.size() == 8,
              "读取旧战斗不额外发牌或更改已冻结手牌");
        check(mfArrangeCapacityHand(State.deck, State.hand, State.draw, State.discard, State.exhausted,
                                   BattleCards::MaximumHandSize), "保存夹具持有十二张真实玩家手牌");
        check(mfArrangeCapacityHand(Enemy.deck, Enemy.hand, Enemy.draw, Enemy.discard, Enemy.exhausted,
                                   BattleCards::MaximumHandSize), "保存夹具持有十二张真实NPC手牌");
        Enemy.planned.clear();
        const string Saved = saveToJson(Encounter);
        const bool Loaded = loadFromJson(Saved, Restored, &Error);
        check(Loaded, "双方十二张手牌存档可加载：" + Error);
        if (Loaded) {
            checkEq(Restored.player.arts.battle->hand.size(), BattleCards::MaximumHandSize,
                    "读档保留玩家十二张手牌");
            checkEq(Restored.player.arts.battle->enemyCards.hand.size(), BattleCards::MaximumHandSize,
                    "读档保留NPC十二张手牌");
            checkStr(saveToJson(Restored), Saved, "十二张手牌全部字段存读往返稳定");
        }
        const string Unchanged = saveToJson(Restored);
        Game InvalidPlayer = Encounter;
        Battle& InvalidPlayerState = *InvalidPlayer.player.arts.battle;
        check(mfArrangeCapacityHand(InvalidPlayerState.deck, InvalidPlayerState.hand, InvalidPlayerState.draw,
                                   InvalidPlayerState.discard, InvalidPlayerState.exhausted,
                                   BattleCards::MaximumHandSize + 1), "十三张非法玩家手牌仍使用真实不重复UID");
        check(!loadFromJson(saveToJson(InvalidPlayer), Restored, &Error), "玩家十三张手牌存档明确拒绝");
        checkStr(saveToJson(Restored), Unchanged, "玩家超容量导入拒绝不覆盖当前状态");
        Game InvalidEnemy = Encounter;
        EnemyCombat& InvalidEnemyState = InvalidEnemy.player.arts.battle->enemyCards;
        check(mfArrangeCapacityHand(InvalidEnemyState.deck, InvalidEnemyState.hand, InvalidEnemyState.draw,
                                   InvalidEnemyState.discard, InvalidEnemyState.exhausted,
                                   BattleCards::MaximumHandSize + 1), "十三张非法NPC手牌仍使用真实不重复UID");
        check(!loadFromJson(saveToJson(InvalidEnemy), Restored, &Error), "NPC十三张手牌存档明确拒绝");
        checkStr(saveToJson(Restored), Unchanged, "NPC超容量导入拒绝不覆盖当前状态");
    }
}
} // namespace nztest
