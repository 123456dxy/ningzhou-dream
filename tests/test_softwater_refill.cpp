#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/hand_rules.h"
#include "core/save.h"
#include "core/talents.h"
#include <algorithm>
#include <set>

namespace nztest {
namespace {
using namespace nz;

Player mfRefillPlayer(const string& Name) {
    Player Actor; Actor.name = Name; Actor.stage = Actor.highestStage = 6;
    Actor.lifespan = realmOf(6).life; Actor.talent.clear(); Actor.traits.clear();
    Actor.arts.main.clear(); Actor.arts.spells = {"softwater", "skim", "wood", "gather"};
    Actor.arts.knownSpells = Actor.arts.spells; Actor.arts.speed = 200;
    Actor.health = mfPlayerHealthMaximum(Actor);
    return Actor;
}

Game mfRefillEncounter() {
    Game State; State.seed = State.rng = 182713; State.player = mfRefillPlayer("补至八张回归");
    State.clock.ageBase = State.player.age;
    EnemySpec Enemy; Enemy.name = "补牌对手"; Enemy.stage = 0; Enemy.style = "土"; Enemy.speed = 0;
    battleStart(State, Enemy);
    auto& Combat = *State.player.arts.battle;
    Combat.enemyCards.enabled = false; Combat.enemyHp = Combat.enemyMax = 100000;
    Combat.intentKind = "guard"; Combat.intentScale = 0; Combat.mana = 12;
    Combat.hp = Combat.max / 2;
    return State;
}

// 用途：只用真实UID安排指定容量手牌；流程：补牌边界；返回：首张柔水UID，不能构造时为空且不取front。
string mfRefillHand(const vector<Card>& Deck, vector<string>& Hand, vector<string>& Draw,
                    vector<string>& Discard, vector<string>& Exhausted, int HandSize) {
    const auto Spell = std::find_if(Deck.begin(), Deck.end(), [](const Card& Entry) { return Entry.spell == "softwater"; });
    if (Spell == Deck.end() || HandSize < 1 || static_cast<std::size_t>(HandSize) > Deck.size()) return "";
    vector<string> Ordered{Spell->uid};
    for (const auto& Card : Deck) if (Card.uid != Spell->uid) Ordered.push_back(Card.uid);
    Hand.assign(Ordered.begin(), Ordered.begin() + HandSize);
    Draw.assign(Ordered.begin() + HandSize, Ordered.end());
    Discard.clear(); Exhausted.clear();
    return Spell->uid;
}

bool mfRefillPartitioned(const Battle& Combat) {
    std::set<string> Expected, Actual;
    for (const auto& Card : Combat.deck) if (!Expected.insert(Card.uid).second) return false;
    for (const auto* Pile : {&Combat.hand, &Combat.draw, &Combat.discard, &Combat.exhausted})
        for (const auto& Uid : *Pile) if (!Expected.count(Uid) || !Actual.insert(Uid).second) return false;
    return Expected == Actual && Combat.hand.size() <= BattleCards::MaximumHandSize;
}

void mfRefillEnemyPiles(EnemyCombat& Enemy, const string& Uid) {
    Enemy.planned = {Uid};
    Enemy.main.clear(); Enemy.manuals.clear(); Enemy.aux.clear(); Enemy.nodes.clear();
    Enemy.mana = 12;
}
} // namespace

void mfRunSoftwaterRefillTests() {
    std::printf("\n--- 柔水无痕补至八张真实集成 ---\n");
    const auto* Softwater = battleSpell("softwater");
    check(Softwater != nullptr, "柔水无痕保留在普通神通权威目录");
    if (!Softwater) return; // CE9327删除状态应安全RED，不解引用空指针或构造缺牌夹具。
    checkStr(Softwater->kind, "drawTo", "柔水无痕使用稳定补至目标张数类型");
    checkEq(Softwater->cost, 3, "柔水保留原raw3费用");
    checkEq(battleCost(Player{}, *Softwater), 2, "柔水实际费用仍为2灵力");
    checkEq(Softwater->cooldown, 2, "柔水保留两个完整自身回合冷却");
    checkEq(BattleCards::MaximumHandSize, 12, "补至八张不改变双方十二张手牌容量");
    checkEq(BattleCards::mfDrawToCount(4, 0, 8, 20), 4, "补牌报价按目标缺口报价");
    checkEq(BattleCards::mfDrawToCount(4, 0, 8, 2), 2, "补牌报价限制真实牌源");
    checkEq(BattleCards::mfDrawToCount(8, 0, 8, 20), 0, "已达目标不追加抽牌");
    checkEq(BattleCards::mfDrawToCount(11, 0, 8, 20), 0, "高于目标不弃牌也不补牌");
    checkEq(BattleCards::mfDrawToCount(4, 2, 8, 8), 2, "待抽牌先占用目标缺口");
    checkEq(BattleCards::mfDrawToCount(4, 2, 8, 3), 1, "待抽牌先占用有限牌源");
    checkEq(BattleCards::mfDrawToCount(4, 4, 8, 2), 0, "待抽已超牌源不产生负报价");
    checkEq(BattleCards::mfDrawToCount(0, -3, 99, 99), 12, "报价负待抽归零且目标受共享容量限制");
    checkEq(BattleCards::mfDrawToCount(0, 0, -1, 99), 0, "负目标报价为零");
    for (int HandSize : {1, 5, 8, 9, 12}) {
        auto State = mfRefillEncounter(); auto& Combat = *State.player.arts.battle;
        const auto Uid = mfRefillHand(Combat.deck, Combat.hand, Combat.draw, Combat.discard, Combat.exhausted, HandSize);
        check(!Uid.empty(), "从真实牌组安排不同手牌数量并含柔水");
        if (Uid.empty()) return;
        const vector<string> Retained(Combat.hand.begin() + 1, Combat.hand.end());
        const auto Before = saveToJson(State); const auto Preview = battlePreview(State.player, Combat, "softwater");
        const int ExpectedDraw = std::max(0, 8 - (HandSize - 1));
        checkStr(saveToJson(State), Before, "柔水只读预览不改手牌、共享RNG、费用或冷却");
        checkEq(Preview.draw, ExpectedDraw, "预览先移除施放牌，再按剩余手牌计算补至八张");
        checkEq(Preview.heal, 0, "柔水预览不再回血");
        checkEq(Preview.damage, 0, "柔水预览没有直接伤害");
        const int Hp = Combat.hp;
        check(battleTurn(State, Uid).ok, "不同手牌数量均可真实施放柔水");
        checkEq(Combat.hand.size(), HandSize - 1 + ExpectedDraw, "真实柔水手牌数量与预览一致");
        checkEq(Combat.hp, Hp, "柔水补牌本身不治疗缺失气血");
        checkEq(Combat.mana, 10, "柔水按原费用实际扣2灵力");
        check(Combat.cooldowns.contains("softwater") && Combat.cooldowns.at("softwater") == 3,
            "柔水冷却补偿施放当轮，内部记3");
        check(std::find(Combat.hand.begin(), Combat.hand.end(), Uid) == Combat.hand.end(),
            "正在施放的柔水不会在自己的补牌中抽回");
        check(std::all_of(Retained.begin(), Retained.end(), [&](const string& CardUid) {
            return std::find(Combat.hand.begin(), Combat.hand.end(), CardUid) != Combat.hand.end();
        }), "补牌及已达目标时均不弃置原有其他手牌");
        check(mfRefillPartitioned(Combat), "柔水真实执行后四牌堆互斥且唯一守恒");
    }
    {
        auto State = mfRefillEncounter(); auto& Combat = *State.player.arts.battle;
        const auto Uid = mfRefillHand(Combat.deck, Combat.hand, Combat.draw, Combat.discard, Combat.exhausted, 5);
        check(!Uid.empty(), "固定目标不受抽牌加成用例有真实柔水卡");
        if (Uid.empty()) return;
        State.player.arts.nodes = {"5-5"};
        check(talentMods(State.player).drawBonus > 0, "夹具确实具有正抽牌张数加成");
        checkEq(battlePreview(State.player, Combat, "softwater").draw, 4, "运筹不把补至八张抬高为九张");
        check(battleTurn(State, Uid).ok, "带抽牌加成时仍可施放柔水");
        checkEq(Combat.hand.size(), 8, "抽牌加成不改变柔水固定八张目标");
    }
    for (int UsedComboDraws : {0, 1, 2}) {
        auto State = mfRefillEncounter(); auto& Combat = *State.player.arts.battle;
        const auto Uid = mfRefillHand(Combat.deck, Combat.hand, Combat.draw, Combat.discard, Combat.exhausted, 5);
        check(!Uid.empty(), "相生固定目标用例有真实柔水卡"); if (Uid.empty()) return;
        State.player.arts.nodes = {"2-1", "2-2", "2-3", "2-4", "2-5", "5-5", "9-1", "9-2", "9-3", "9-4", "9-5"};
        Combat.last = "金"; Combat.comboDraws = UsedComboDraws;
        const auto Mods = talentMods(State.player);
        check(Mods.drawBonus > 0 && Mods.comboDraw > 0 && Mods.comboMana > 0,
            "满水与阵五夹具确有抽牌、相生抽牌及返灵加成");
        const auto Before = saveToJson(State); const auto Preview = battlePreview(State.player, Combat, "softwater");
        checkEq(Preview.draw, 4, "金生水相生抽牌与运筹均不抬高固定八张目标");
        checkStr(saveToJson(State), Before, "相生固定补牌预览不扣本轮额外抽牌预算");
        check(battleTurn(State, Uid).ok, "带满水与阵五相生时柔水真实施放");
        checkEq(Combat.hand.size(), 8, "相生与抽牌加成叠加后仍精确补至八张");
        checkEq(Combat.comboDraws, UsedComboDraws, "柔水不占用相生额外抽牌预算");
        check(Combat.mana > 10, "固定补牌仍保留金生水相生返灵");
        check(mfRefillPartitioned(Combat), "相生固定补牌后真实牌堆唯一守恒");
    }
    for (int Supply : {0, 1, 2}) {
        auto State = mfRefillEncounter(); auto& Combat = *State.player.arts.battle;
        const auto Uid = mfRefillHand(Combat.deck, Combat.hand, Combat.draw, Combat.discard, Combat.exhausted, 5);
        check(!Uid.empty(), "少牌用例仍包含真实柔水"); if (Uid.empty()) return;
        vector<string> Available = Combat.draw;
        if (Available.size() < static_cast<std::size_t>(Supply)) { check(false, "少牌用例备牌足够"); return; }
        Combat.draw.clear(); Combat.discard.clear(); Combat.exhausted.clear();
        for (std::size_t Index = 0; Index < Available.size(); ++Index) {
            if (Index == 0 && Supply >= 1) Combat.draw.push_back(Available[Index]);
            else if (Index == 1 && Supply >= 2) Combat.discard.push_back(Available[Index]);
            else Combat.exhausted.push_back(Available[Index]);
        }
        const auto Before = saveToJson(State); const auto Preview = battlePreview(State.player, Combat, "softwater");
        checkEq(Preview.draw, Supply, "剩余牌不足时预览只报告实际能补到的数量");
        checkStr(saveToJson(State), Before, "少牌及跨弃牌堆预览仍不提交重洗或RNG");
        check(battleTurn(State, Uid).ok, "抽牌堆不足时柔水仍真实完成");
        checkEq(Combat.hand.size(), 4 + Supply, "剩余牌抽完即停，不凭空补造牌");
        checkEq(Combat.shuffles, Supply == 2 ? 1 : 0, "需要时才按既有规则重洗可用弃牌");
        check(std::find(Combat.hand.begin(), Combat.hand.end(), Uid) == Combat.hand.end(), "少牌时不回收正在施放的柔水");
        check(mfRefillPartitioned(Combat), "少牌与重洗后四牌堆仍守恒");
    }
    {
        auto State = mfRefillEncounter(); auto& Combat = *State.player.arts.battle;
        const auto Uid = mfRefillHand(Combat.deck, Combat.hand, Combat.draw, Combat.discard, Combat.exhausted, 5);
        check(!Uid.empty(), "完整重洗用例含真实柔水"); if (Uid.empty()) return;
        Combat.discard = Combat.draw; Combat.draw.clear();
        const auto Before = saveToJson(State); const auto Rng = State.rng;
        checkEq(battlePreview(State.player, Combat, "softwater").draw, 4, "弃牌可供足量牌时预览补四张");
        checkStr(saveToJson(State), Before, "完整重洗预览不改真实牌序或RNG");
        check(battleTurn(State, Uid).ok, "柔水通过真实重洗抽满目标");
        checkEq(Combat.hand.size(), 8, "重洗后真实手牌补至八张");
        checkEq(Combat.shuffles, 1, "真实柔水动作仅重洗一次");
        check(State.rng != Rng, "多张弃牌的洗牌随机仅在真实动作中提交");
        check(mfRefillPartitioned(Combat), "完整重洗补牌仍唯一守恒");
    }
    for (bool Shadow : {false, true}) {
        Game State;
        if (Shadow) {
            auto Challenger = mfRefillPlayer("补牌挑战者"), Opponent = mfRefillPlayer("补牌影身");
            Opponent.arts.speed = 0; battleStartShadow(State, Challenger, Opponent, 71813);
        } else {
            State = mfRefillEncounter(); State.player.arts.battle.reset();
            Npc Opponent; Opponent.id = "refill-npc"; Opponent.name = "补牌星河修士";
            Opponent.stage = 6; Opponent.lifespan = realmOf(6).life; Opponent.sect = "xinghe"; Opponent.dao = "水";
            Opponent.spells = {"softwater"}; State.npcs.push_back(Opponent);
            State.player.health = mfPlayerHealthMaximum(State.player);
            EnemySpec Spec; Spec.id = Opponent.id; Spec.name = Opponent.name; Spec.stage = 6;
            Spec.sect = Opponent.sect; Spec.style = Opponent.dao; Spec.speed = 0;
            battleStart(State, Spec);
        }
        check(State.player.arts.battle.has_value(), "普通NPC及Shadow的真实补牌对局建立");
        if (!State.player.arts.battle) return;
        auto& Combat = *State.player.arts.battle; auto& Enemy = Combat.enemyCards;
        const auto Uid = mfRefillHand(Enemy.deck, Enemy.hand, Enemy.draw, Enemy.discard, Enemy.exhausted, 5);
        check(!Uid.empty(), "普通NPC和Shadow真实牌组都含柔水实例"); if (Uid.empty()) return;
        mfRefillEnemyPiles(Enemy, Uid);
        Enemy.played = mfBattleEnemyActionLimit(State, Combat) - 1; // 只余最后一次合法行动，隔离本术而不禁止正常NPC续招。
        const int DrawBefore = static_cast<int>(Enemy.draw.size());
        const auto Before = saveToJson(State); const auto Preview = battleEnemyPreview(State);
        check(Preview.spells.size() == 1 && Preview.spells[0] == "softwater", "双方真实调度在剩余一次行动中执行柔水");
        checkStr(saveToJson(State), Before, "NPC及Shadow补牌预演保持RNG与牌堆只读");
        check(battleTurn(State, "end").ok, "NPC及Shadow通过真实回合施放柔水");
        check(State.player.arts.battle.has_value(), "仅补牌的敌方镜像不结束战斗"); if (!State.player.arts.battle) return;
        const auto& After = *State.player.arts.battle;
        checkEq(static_cast<int>(After.enemyCards.draw.size()), DrawBefore - 4 - 5,
            "NPC及Shadow先补四张至八张，再按原规则下一轮抽五张");
        checkEq(After.enemyCards.hand.size(), 5, "镜像本术不改变正常下一轮五张起手");
        check(After.enemyCards.cooldowns.contains("softwater") && After.enemyCards.cooldowns.at("softwater") == 2,
            "NPC及Shadow柔水冷却按原完整回合推进");
    }
}
} // namespace nztest
