#include "harness.h"
#include "core/api.h"
#include "core/battle.h"
#include "core/combat/discard_rules.h"
#include "core/save.h"
#include "core/talents.h"
#include <algorithm>
#include <set>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立有真实弃牌凝盾/锐势和有效得盾蓄势的合法配装；流程：定向夹具；用法：选择常态或零取整边界；返回：无战斗的角色快照。
Player mfDiscardActor(const string& Name, bool RoundingBoundary = false) {
    Player Actor;
    Actor.name = Name; Actor.gender = "女";
    Actor.stage = Actor.highestStage = 3; Actor.age = 20;
    Actor.lifespan = realmOf(Actor.stage).life; Actor.health = 100;
    Actor.talent = "plain"; Actor.traits.clear(); Actor.body = 0;
    Actor.arts.spells = {"refresh", "gold"}; Actor.arts.knownSpells = Actor.arts.spells;
    if (RoundingBoundary) {
        Actor.arts.main = "earth"; Actor.arts.manuals = {{"earth", 5}, {"gold", 1}};
        Actor.arts.aux = {"gold"};
    } else {
        Actor.arts.main = "gold"; Actor.arts.manuals = {{"gold", 5}, {"earth", 5}, {"mountain", 5}};
        Actor.arts.aux = {"earth", "mountain"};
    }
    return Actor;
}

// 用途：通过正式入口生成双方对等的真实牌堆；流程：弃牌时机测试；用法：可选择零取整配装；返回：独立内存切磋，不接触任何账号。
Game mfDiscardBattle(bool RoundingBoundary = false) {
    Player Actor = mfDiscardActor("弃牌测试者", RoundingBoundary);
    const Player Opponent = mfDiscardActor("弃牌对手", RoundingBoundary);
    Actor.arts.speed = 200;
    Game Encounter;
    battleStartShadow(Encounter, Actor, Opponent, 74123);
    return Encounter;
}

// 用途：从真实唯一UID牌堆选择受控手牌，其余牌归抽牌堆；流程：确定性输入夹具；用法：传四个实际牌堆和神通序列；返回：请求是否都由真实牌满足。
bool mfArrangeHand(const vector<Card>& Deck, vector<string>& Hand, vector<string>& Draw,
                   vector<string>& Discard, vector<string>& Exhausted, const vector<string>& Wanted) {
    Hand.clear(); Draw.clear(); Discard.clear(); Exhausted.clear();
    for (const auto& SpellId : Wanted) {
        const auto Position = std::find_if(Deck.begin(), Deck.end(), [&](const auto& Entry) {
            return Entry.spell == SpellId && std::find(Hand.begin(), Hand.end(), Entry.uid) == Hand.end();
        });
        if (Position == Deck.end()) return false;
        Hand.push_back(Position->uid);
    }
    for (const auto& Entry : Deck)
        if (std::find(Hand.begin(), Hand.end(), Entry.uid) == Hand.end()) Draw.push_back(Entry.uid);
    return Hand.size() <= 8;
}

// 用途：校验受控夹具和真实抽牌后的守恒；流程：牌堆合法性断言；用法：传完整牌堆和四分区；返回：唯一、互斥、全部覆盖且手牌未超8。
bool mfPartitioned(const vector<Card>& Deck, const vector<string>& Hand, const vector<string>& Draw,
                   const vector<string>& Discard, const vector<string>& Exhausted) {
    std::set<string> Known, Seen;
    for (const auto& Entry : Deck) if (!Known.insert(Entry.uid).second) return false;
    for (const auto* Pile : {&Hand, &Draw, &Discard, &Exhausted})
        for (const auto& Uid : *Pile) if (!Known.count(Uid) || !Seen.insert(Uid).second) return false;
    return Hand.size() <= 8 && Seen == Known;
}

// 用途：安排玩家合法手牌并清除一次性状态；流程：动作夹具；用法：传已开战状态和实际神通；返回：无，断言牌堆有效。
void mfPlayerHand(Game& Encounter, const vector<string>& Wanted) {
    auto& State = *Encounter.player.arts.battle;
    check(mfArrangeHand(State.deck, State.hand, State.draw, State.discard, State.exhausted, Wanted), "玩家受控手牌来自真实牌堆");
    State.cooldowns.clear(); State.mana = battleManaCap(Encounter.player);
    State.shield = 0; State.charge = 0; State.discardPower = 0;
}

// 用途：安排敌方合法手牌及已公开的行动前缀；流程：意图夹具；用法：计划为空即当轮调息；返回：无，不注入额外神通或UID。
void mfEnemyHand(Game& Encounter, const vector<string>& Wanted, const string& PlannedSpell = "") {
    auto& State = *Encounter.player.arts.battle;
    auto& Enemy = State.enemyCards;
    check(mfArrangeHand(Enemy.deck, Enemy.hand, Enemy.draw, Enemy.discard, Enemy.exhausted, Wanted), "敌方受控手牌来自真实牌堆");
    Enemy.planned.clear(); Enemy.cooldowns.clear(); Enemy.mana = 12;
    Enemy.charge = 0; Enemy.discardPower = 0; State.enemyShield = 0;
    if (!PlannedSpell.empty()) {
        for (const auto& Uid : Enemy.hand) {
            const auto Position = std::find_if(Enemy.deck.begin(), Enemy.deck.end(), [&](const auto& Entry) { return Entry.uid == Uid; });
            if (Position != Enemy.deck.end() && Position->spell == PlannedSpell) { Enemy.planned.push_back(Uid); break; }
        }
        check(!Enemy.planned.empty(), "敌方计划引用已在手中的真实卡牌");
    }
}

// 用途：反复调用直接及整轮预览并检查活动状态；流程：只读契约；用法：传隔离战斗；返回：公开敌方预告，不推进RNG、时间或牌堆。
EnemyPreview mfReadOnlyIntent(Game& Encounter) {
    const auto RngBefore = Encounter.rng;
    const int DaysBefore = Encounter.clock.days;
    const string SavedBefore = saveToJson(Encounter);
    const auto First = battleEnemyPreview(Encounter);
    const auto Second = battleEnemyPreview(Encounter);
    battlePreview(Encounter.player, *Encounter.player.arts.battle, "refresh");
    check(First.damage == Second.damage && First.shield == Second.shield && First.spells == Second.spells,
          "重复弃牌预演返回一致伤害、护盾与招式");
    check(Encounter.rng == RngBefore && Encounter.clock.days == DaysBefore && saveToJson(Encounter) == SavedBefore,
          "主动换手与回合末预览不改RNG、时间、牌堆或可保存状态");
    return First;
}

// 用途：检查真实动作后双方四个牌堆守恒；流程：换牌/回合末验证；用法：传仍在斗法的状态；返回：无，记录断言。
void mfCheckPartitions(const Game& Encounter) {
    const auto& State = *Encounter.player.arts.battle;
    const auto& Enemy = State.enemyCards;
    check(mfPartitioned(State.deck, State.hand, State.draw, State.discard, State.exhausted) &&
          mfPartitioned(Enemy.deck, Enemy.hand, Enemy.draw, Enemy.discard, Enemy.exhausted),
          "真实动作后双方牌堆唯一、互斥、守恒且未超过手牌上限");
}
}

// 用途：验证主动/余牌弃置时机的真实收益；流程：统一定向测试入口；用法：无外部存档调用；返回：无，失败由harness汇总。
void mfRunDiscardTimingTests() {
    std::printf("\n-- 主动换手全额凝盾与回合末半额凝盾 --\n");
    checkEq(nz::DiscardRules::RoundEndShieldPercent, 50, "回合末凝盾规则固定为主动弃牌的50%");
    {
        auto Encounter = mfDiscardBattle();
        mfPlayerHand(Encounter, {"refresh", "strike", "strike", "defend"});
        mfEnemyHand(Encounter, {});
        const auto Mods = talentMods(Encounter.player);
        auto& State = *Encounter.player.arts.battle;
        const int ExpectedShield = jsRound(State.max * Mods.discardShield * 3);
        const auto RngBefore = Encounter.rng;
        const auto Preview = battlePreview(Encounter.player, State, "refresh");
        check(Encounter.rng == RngBefore && State.hand.size() == 4 && State.shield == 0, "主动换手预览不提前弃牌或消耗RNG");
        checkEq(Preview.shield, ExpectedShield, "主动换手按其余3张牌发完整凝盾");
        check(battleTurn(Encounter, "refresh").ok, "真实主动换手可以执行");
        checkEq(State.shield, Preview.shield, "主动换手预览护盾与执行一致");
        checkNear(State.discardPower, std::min(.30, Mods.discardPower * 3), "主动换手发放完整锐势");
        checkEq(State.charge, 1, "主动换手实际获得护盾后仅触发一次得盾蓄势");
        checkEq(State.hand.size(), 4, "换手仅弃其余3张后真实抽4张");
        mfCheckPartitions(Encounter);
        Game Reloaded;
        string Error;
        check(loadDetachedFromJson(saveToJson(Encounter), Reloaded, &Error) && saveToJson(Reloaded) == saveToJson(Encounter),
              "弃牌时机不增加存档字段，主动换手后的状态可无损往返");
    }
    {
        auto Encounter = mfDiscardBattle();
        mfPlayerHand(Encounter, {"strike", "strike", "defend"});
        mfEnemyHand(Encounter, {"strike"}, "strike");
        const auto Mods = talentMods(Encounter.player);
        auto& State = *Encounter.player.arts.battle;
        const int HalfShield = jsRound(State.max * Mods.discardShield * 3 * .5);
        auto WithoutRemainingCards = Encounter;
        mfPlayerHand(WithoutRemainingCards, {});
        const int UnblockedDamage = battleEnemyPreview(WithoutRemainingCards).damage;
        check(UnblockedDamage > HalfShield, "来伤足够观察半额凝盾差异，避免被零伤害遮蔽");
        const auto Intent = mfReadOnlyIntent(Encounter);
        checkEq(Intent.damage, UnblockedDamage - HalfShield, "玩家回合末只用余牌半额凝盾抵挡当轮来伤");
        const int HpBefore = State.hp;
        check(battleTurn(Encounter, "end").ok, "有余牌时可真实结束回合");
        checkEq(HpBefore - State.hp, Intent.damage, "玩家回合末半盾预告与真实扣血一致");
        checkNear(State.discardPower, std::min(.30, Mods.discardPower * 3), "玩家回合末锐势仍全额发放");
        checkEq(State.charge, 1, "回合末半盾实际增加时仍触发一次蓄势");
        checkEq(State.shield, 0, "当轮余牌盾在下一玩家回合清零");
        mfCheckPartitions(Encounter);
    }
    {
        auto Encounter = mfDiscardBattle();
        mfPlayerHand(Encounter, {});
        mfEnemyHand(Encounter, {"strike", "strike", "defend"});
        const auto Mods = talentMods(*Encounter.player.arts.battle->shadowPlayer);
        auto& State = *Encounter.player.arts.battle;
        const int ExpectedShield = jsRound(State.enemyMax * Mods.discardShield * 3 * .5);
        const auto Intent = mfReadOnlyIntent(Encounter);
        checkEq(Intent.shield, ExpectedShield, "敌方余牌回合末护盾预告也是半额");
        check(battleTurn(Encounter, "end").ok, "双方调息的真实回合可以结束");
        checkEq(State.enemyShield, Intent.shield, "敌方半额凝盾预览与真实存留一致");
        checkNear(State.enemyCards.discardPower, std::min(.30, Mods.discardPower * 3), "敌方回合末锐势保持全额");
        checkEq(State.enemyCards.charge, 1, "敌方余牌实际得盾只触发一次蓄势");
        mfCheckPartitions(Encounter);
    }
    {
        auto Encounter = mfDiscardBattle();
        mfPlayerHand(Encounter, {});
        mfEnemyHand(Encounter, {"refresh", "strike", "strike", "defend"}, "refresh");
        auto& State = *Encounter.player.arts.battle;
        const auto Mods = talentMods(*State.shadowPlayer);
        const int ActiveShield = jsRound(State.enemyMax * Mods.discardShield * 3);
        const int EndShield = jsRound(State.enemyMax * Mods.discardShield * 4 * .5);
        const auto Intent = mfReadOnlyIntent(Encounter);
        checkEq(Intent.shield, ActiveShield + EndShield, "敌方主动换手全盾与新抽4张的回合末半盾分别计算");
        check(battleTurn(Encounter, "end").ok, "敌方真实主动换手可以执行");
        checkEq(State.enemyShield, Intent.shield, "敌方含换手抽牌的整轮凝盾预览与执行一致");
        checkEq(State.enemyCards.charge, 2, "敌方主动和回合末两次有效得盾各结算一次蓄势");
        checkNear(State.enemyCards.discardPower, .30, "敌方主动和回合末锐势全额累积并遵守上限");
        mfCheckPartitions(Encounter);
    }
    {
        auto Encounter = mfDiscardBattle();
        mfPlayerHand(Encounter, {}); mfEnemyHand(Encounter, {});
        const auto Intent = mfReadOnlyIntent(Encounter);
        checkEq(Intent.damage + Intent.shield, 0, "双方无余牌且无行动时预演没有弃牌收益");
        check(battleTurn(Encounter, "end").ok, "空手回合可结束");
        const auto& State = *Encounter.player.arts.battle;
        check(State.shield == 0 && State.enemyShield == 0 && State.charge == 0 && State.enemyCards.charge == 0 &&
              State.discardPower == 0 && State.enemyCards.discardPower == 0,
              "无牌不发护盾、蓄势或锐势");
        mfCheckPartitions(Encounter);
    }
    {
        auto Encounter = mfDiscardBattle(true);
        mfPlayerHand(Encounter, {"strike"}); mfEnemyHand(Encounter, {"strike"});
        const auto Mods = talentMods(Encounter.player);
        auto& State = *Encounter.player.arts.battle;
        check(Mods.shieldCharge >= 1 && jsRound(State.max * Mods.discardShield * .5) == 0,
              "边界配装有得盾蓄势但单张余牌半盾取整为0");
        const auto Intent = mfReadOnlyIntent(Encounter);
        checkEq(Intent.shield, 0, "敌方零取整余牌盾预演为0");
        check(battleTurn(Encounter, "end").ok, "零取整边界回合可结束");
        check(State.shield == 0 && State.enemyShield == 0 && State.charge == 0 && State.enemyCards.charge == 0,
              "半盾取整为0时双方都不虚增护盾或蓄势");
        checkNear(State.discardPower, Mods.discardPower, "零盾边界仍发玩家完整锐势");
        checkNear(State.enemyCards.discardPower, Mods.discardPower, "零盾边界仍发敌方完整锐势");
        mfCheckPartitions(Encounter);
    }
    for (const bool AlreadyAtCap : {false, true}) {
        auto Encounter = mfDiscardBattle();
        mfPlayerHand(Encounter, {"refresh", "strike", "strike", "strike", "defend", "defend", "defend", "defend"});
        mfEnemyHand(Encounter, {});
        auto& State = *Encounter.player.arts.battle;
        const int Cap = jsRound(State.max * .30);
        State.shield = AlreadyAtCap ? Cap : Cap - 1;
        const auto Preview = battlePreview(Encounter.player, State, "refresh");
        checkEq(Preview.shield, AlreadyAtCap ? 0 : 1, "主动换手预览只报告30%护盾上限内的实际增量");
        check(battleTurn(Encounter, "refresh").ok, "接近或达到护盾上限时换手仍可执行");
        checkEq(State.shield, Cap, "主动大量弃牌不突破30%气血护盾上限");
        checkEq(State.charge, AlreadyAtCap ? 0 : 1, "护盾已满无实际增量时不虚增蓄势");
        checkNear(State.discardPower, .30, "护盾受限时锐势仍按全部7张累计至自身上限");
        mfCheckPartitions(Encounter);
    }
    {
        auto Encounter = mfDiscardBattle();
        mfPlayerHand(Encounter, {}); mfEnemyHand(Encounter, {"gold"}, "gold");
        auto& State = *Encounter.player.arts.battle;
        State.intentName = "金芒刺";
        State.enemyHp = 1; State.poison = 1; State.poisonDamage = 1; State.poisonTurns = 1;
        const string Before = saveToJson(Encounter);
        const auto View = api::battleView(Encounter);
        check(View["intentSpells"].asArray().empty() && View["enemyActionCount"].asInt() == 0 && View["intentDamage"].asInt() == 0,
              "持续伤害将先击败修士时没有敌方动作或来伤");
        checkStr(View["intentName"].asString(), "本轮不行动", "敌人将被持续伤害击败时标题不残留开轮攻击链");
        check(View["intentDescription"].asString().find("持续伤害") != string::npos && saveToJson(Encounter) == Before,
              "不行动原因仍由真实预演说明，读取标题不改状态");
        check(battleTurn(Encounter, "end").ok && battleShadowOutcome(Encounter) == "win", "真实持续伤害在对手行动前结束战斗");
        checkStr(api::battleView(Encounter)["intentName"].asString(), "斗法已结束", "已结束的修士战报不再显示待执行攻击");
    }
    {
        auto Encounter = mfDiscardBattle();
        mfPlayerHand(Encounter, {}); mfEnemyHand(Encounter, {});
        Encounter.player.arts.battle->intentName = "金芒刺";
        const auto View = api::battleView(Encounter);
        check(View["enemyActionCount"].asInt() == 0 && View["intentDamage"].asInt() == 0 &&
              View["intentName"].asString() == "本轮不行动", "无可施放前缀时动作数、来伤与不行动标题一致");
    }
    {
        Game Encounter;
        Encounter.player = mfDiscardActor("妖兽标题测试者");
        EnemySpec Opponent;
        Opponent.name = "标题测试妖兽"; Opponent.stage = 3; Opponent.style = "体"; Opponent.beast = true;
        battleStart(Encounter, Opponent);
        const string BeastIntentName = Encounter.player.arts.battle->intentName;
        const auto View = api::battleView(Encounter);
        check(View["enemyBeast"].asBool() && View["intentSpells"].asArray().empty() &&
              View["intentName"].asString() == BeastIntentName,
              "妖兽的神通列表自然为空，仍保留自身动作标题");
    }
}
} // namespace nztest
