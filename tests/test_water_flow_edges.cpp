#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/hand_rules.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <array>
#include <set>
#include <stdexcept>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立双方真实水系牌组；流程：容量、NPC及留影水预算回归；返回：独立内存对局，无玩家文件或服务。
Game mfWaterEdgeEncounter(bool Shadow = false) {
    CreateOptions Options; Options.seed = 20261019;
    Game Encounter = createGame(Options);
    Encounter.pending.reset();
    auto& Actor = Encounter.player;
    Actor.name = "水流边界靶"; Actor.stage = Actor.highestStage = 12;
    Actor.lifespan = realmOf(12).life + Actor.lifeBonus;
    Actor.talent.clear(); Actor.traits.clear(); Actor.arts = Arts{};
    Actor.arts.main = "wave"; Actor.arts.manuals["wave"] = 5; Actor.arts.speed = 200;
    Actor.arts.spells = {"waterSeal", "stormSword", "wavehit", "waterfall", "waterDraw", "skim"};
    Actor.arts.knownSpells = Actor.arts.spells;
    Actor.health = mfPlayerHealthMaximum(Actor);
    Npc Opponent;
    Opponent.id = "water-flow-edge-opponent"; Opponent.name = "星河水流边界";
    Opponent.stage = 12; Opponent.sect = "xinghe"; Opponent.dao = "水";
    Opponent.combatProfileId = "xinghe-complete"; Opponent.lifespan = realmOf(12).life;
    if (Shadow) {
        Player Foe;
        Foe.name = Opponent.name; Foe.stage = Foe.highestStage = 12;
        Foe.lifespan = Opponent.lifespan; Foe.sect = Opponent.sect;
        Foe.talent.clear(); Foe.traits.clear(); Foe.arts = artNpcLoadout(Opponent);
        const Player Challenger = Actor;
        battleStartShadow(Encounter, Challenger, Foe, 20261019);
    } else {
        Encounter.npcs.push_back(Opponent);
        EnemySpec Target;
        Target.id = Opponent.id; Target.name = Opponent.name; Target.stage = 12;
        Target.sect = Opponent.sect; Target.style = "水"; Target.speed = 0;
        battleStart(Encounter, Target);
    }
    auto& Combat = *Encounter.player.arts.battle;
    Combat.max = Combat.hp = Combat.enemyMax = Combat.enemyHp = 100000000;
    return Encounter;
}

// 用途：查真实牌组的指定卡UID；流程：边界重排；返回：已有唯一UID，缺失时拒绝测试夹具。
string mfWaterEdgeCard(const vector<Card>& Deck, const string& SpellId, int Copy = 0) {
    for (const auto& Card : Deck) if (Card.spell == SpellId && Copy-- == 0) return Card.uid;
    throw std::logic_error("水边界夹具缺少真实牌：" + SpellId);
}

// 用途：在真实牌组中安排指定容量手牌；流程：预览与执行容量竞争；返回：无，四分区保持唯一守恒。
void mfWaterEdgeHand(Battle& Combat, std::size_t Count) {
    const auto First = mfWaterEdgeCard(Combat.deck, "waterSeal");
    Combat.hand = {First}; Combat.draw.clear(); Combat.discard.clear(); Combat.exhausted.clear();
    for (const auto& Card : Combat.deck) {
        if (Card.uid == First) continue;
        if (Combat.hand.size() < Count) Combat.hand.push_back(Card.uid);
        else Combat.draw.push_back(Card.uid);
    }
    if (Combat.hand.size() != Count) throw std::logic_error("水边界夹具真实牌数不足");
}

// 用途：检查四牌堆唯一守恒；流程：水补牌真实执行；返回：是否无丢牌、重复或超容量。
bool mfWaterEdgePartitioned(const vector<Card>& Deck, const vector<string>& Hand, const vector<string>& Draw,
        const vector<string>& Discard, const vector<string>& Exhausted) {
    std::set<string> Known, Placed;
    if (Hand.size() > BattleCards::MaximumHandSize) return false;
    for (const auto& Card : Deck) if (!Known.insert(Card.uid).second) return false;
    for (const auto* Pile : {&Hand, &Draw, &Discard, &Exhausted})
        for (const auto& Uid : *Pile) if (!Known.count(Uid) || !Placed.insert(Uid).second) return false;
    return Known == Placed;
}

// 用途：让下一真实开轮抽到水印和四张守势，浪击仍在已知抽牌堆顶；流程：原生补牌前缀/重洗续招；返回：无。
void mfWaterEdgeNextNpcHand(Battle& Combat) {
    auto& Enemy = Combat.enemyCards;
    const auto Source = mfWaterEdgeCard(Enemy.deck, "waterSeal");
    const auto Release = mfWaterEdgeCard(Enemy.deck, "wavehit");
    Enemy.hand.clear(); Enemy.draw = {Release}; Enemy.discard.clear(); Enemy.exhausted.clear(); Enemy.planned.clear();
    for (int Copy = 0; Copy < 4; ++Copy) Enemy.draw.push_back(mfWaterEdgeCard(Enemy.deck, "defend", Copy));
    Enemy.draw.push_back(Source);
    for (const auto& Card : Enemy.deck)
        if (std::find(Enemy.draw.begin(), Enemy.draw.end(), Card.uid) == Enemy.draw.end()) Enemy.discard.push_back(Card.uid);
    Enemy.water = 2; Enemy.WaterFlowDraws = 0;
}
}

// 用途：核验容量竞争、NPC重洗及留影持久化；流程：定向/统一harness；返回：失败计入统一框架。
void mfRunWaterFlowEdgeTests() {
    using namespace nz;
    std::printf("\n--- 水流手牌、NPC与留影边界 ---\n");
    for (const int HandCount : {12, 11}) {
        auto Encounter = mfWaterEdgeEncounter();
        auto& Combat = *Encounter.player.arts.battle;
        Encounter.player.arts.nodes = {"9-1", "9-2", "9-3", "9-4", "9-5"};
        mfWaterEdgeHand(Combat, static_cast<std::size_t>(HandCount));
        Combat.last = "金"; Combat.water = 0; Combat.comboDraws = 0; Combat.WaterFlowDraws = 0;
        const string Uid = Combat.hand.front();
        const string Before = saveToJson(Encounter);
        const auto Forecast = battlePreview(Encounter.player, Combat, "waterSeal");
        checkEq(Forecast.draw, HandCount == 12 ? 1 : 2, "相生待抽先占位，原生水补牌只使用剩余容量");
        checkStr(saveToJson(Encounter), Before, "满手与十一手的水预览不动牌堆、预算或RNG");
        check(battleTurn(Encounter, Uid).ok, "容量竞争通过真实水印动作结算");
        checkEq(Combat.hand.size(), 12, "十二或十一手的相生水印只补到十二张");
        checkEq(Combat.comboDraws, 1, "独立相生抽牌正常占用自身预算");
        checkEq(Combat.WaterFlowDraws, HandCount == 12 ? 0 : 1, "无剩余容量不扣水预算，有一位置只扣一次");
        check(mfWaterEdgePartitioned(Combat.deck, Combat.hand, Combat.draw, Combat.discard, Combat.exhausted),
            "容量竞争后四牌堆仍唯一守恒");
    }
    {
        auto Encounter = mfWaterEdgeEncounter(); auto& Combat = *Encounter.player.arts.battle;
        mfWaterEdgeHand(Combat, 11); Combat.last.clear(); Combat.water = 0; Combat.WaterFlowDraws = 0;
        const auto Forecast = battlePreview(Encounter.player, Combat, "waterSeal");
        const string Uid = Combat.hand.front();
        checkEq(Forecast.draw, 1, "无相生时十一手只预览一张原生水补牌");
        check(battleTurn(Encounter, Uid).ok, "十一手原生水印正常执行");
        checkEq(Combat.hand.size(), 11, "十一手原生水印打出一张后只补回一张");
        checkEq(Combat.WaterFlowDraws, 1, "独立原生补牌只占一次共享预算");
    }
    {
        auto Encounter = mfWaterEdgeEncounter(); auto& Combat = *Encounter.player.arts.battle;
        mfWaterEdgeNextNpcHand(Combat);
        check(battleTurn(Encounter, "end").ok, "真实开轮抽到水印与守势夹具");
        const auto Prefix = Combat.enemyCards.planned;
        const auto WaterUid = mfWaterEdgeCard(Combat.enemyCards.deck, "waterSeal");
        const auto WaveUid = mfWaterEdgeCard(Combat.enemyCards.deck, "wavehit");
        check(!Prefix.empty() && Prefix.front() == WaterUid, "真实星河NPC以当前手牌水印蓄水开链");
        check(std::find(Combat.enemyCards.hand.begin(), Combat.enemyCards.hand.end(), WaveUid) == Combat.enemyCards.hand.end() &&
            std::find(Prefix.begin(), Prefix.end(), WaveUid) != Prefix.end(), "NPC前缀衔接水印补入的已知抽牌堆浪击");
        const string Before = saveToJson(Encounter);
        const auto First = battleEnemyPreview(Encounter), Repeated = battleEnemyPreview(Encounter);
        checkEq(First.damage, Repeated.damage, "水流跨洗牌的重复敌方预览一致");
        check(First.spells == Repeated.spells, "重复查询保持同一真实水连招");
        checkStr(saveToJson(Encounter), Before, "NPC水流预演不改活动RNG、补牌计数或牌堆");
        check(Combat.enemyCards.planned == Prefix, "预演不把重洗后的未来UID写回冻结前缀");
        check(First.spells.size() >= 2 && First.spells.size() <= static_cast<std::size_t>(mfBattleEnemyActionLimit(Encounter, Combat)),
            "水流前缀及普通重洗续招仍遵守实际NPC动作预算");
        Game Restored; string Error;
        check(loadDetachedFromJson(Before, Restored, &Error), "含水补牌已知抽牌UID前缀可存读：" + Error);
        if (Restored.player.arts.battle) checkEq(battleEnemyPreview(Restored).damage, First.damage, "读档保持相同水流意图");
        const auto ShufflesBefore = Combat.enemyCards.shuffles;
        const int HpBefore = Combat.hp;
        check(battleTurn(Encounter, "end").ok, "真实NPC执行水印补牌及浪击跨重洗");
        checkEq(HpBefore - Combat.hp, First.damage, "NPC水流真实来伤与当前只读意图一致");
        check(Combat.enemyCards.shuffles > ShufflesBefore, "水补牌跨重洗只在真实动作提交");
        checkEq(Combat.enemyCards.WaterFlowDraws, 0, "NPC完成自身回合后恢复下一轮水补牌预算");
        check(mfWaterEdgePartitioned(Combat.enemyCards.deck, Combat.enemyCards.hand, Combat.enemyCards.draw,
            Combat.enemyCards.discard, Combat.enemyCards.exhausted), "NPC水补牌及重洗后四牌堆唯一守恒");
    }
    {
        auto Encounter = mfWaterEdgeEncounter(true); auto& Combat = *Encounter.player.arts.battle;
        Combat.WaterFlowDraws = 1; Combat.enemyCards.WaterFlowDraws = 2;
        Combat.enemyCards.planned.clear();
        Game Restored; string Error;
        const auto Saved = saveToJson(Encounter);
        check(loadDetachedFromJson(Saved, Restored, &Error), "留影双方水预算同时往返：" + Error);
        if (Restored.player.arts.battle) {
            checkEq(Restored.player.arts.battle->WaterFlowDraws, 1, "留影读档保留玩家已用一次水预算");
            checkEq(Restored.player.arts.battle->enemyCards.WaterFlowDraws, 2, "留影读档保留敌方耗尽水预算");
        }
        const auto Stable = saveToJson(Restored);
        for (const auto Invalid : {json::Value(true), json::Value{}, json::Value(1.5), json::Value(-1), json::Value(3), json::Value("2")}) {
            auto Broken = json::Value::parse(Saved);
            auto PlayerObject = Broken["player"]; auto ArtsObject = PlayerObject["arts"]; auto BattleObject = ArtsObject["battle"];
            auto EnemyObject = BattleObject["enemyCards"]; EnemyObject.set("waterFlowDraws", Invalid);
            BattleObject.set("enemyCards", EnemyObject); ArtsObject.set("battle", BattleObject);
            PlayerObject.set("arts", ArtsObject); Broken.set("player", PlayerObject);
            check(!loadDetachedFromJson(Broken.dump(), Restored, &Error), "留影敌方水预算拒绝布尔/null/小数/越界/字符串");
            checkStr(saveToJson(Restored), Stable, "敌方非法预算拒绝不覆盖原留影和双方计数");
        }
        const auto Before = saveToJson(Encounter);
        static_cast<void>(battleEnemyPreview(Encounter));
        checkStr(saveToJson(Encounter), Before, "留影预算耗尽时预览也不重置双方计数");
        check(battleTurn(Encounter, "end").ok, "留影真实完整回合可推进");
        checkEq(Combat.WaterFlowDraws, 0, "留影玩家下一自身回合重置水预算");
        checkEq(Combat.enemyCards.WaterFlowDraws, 0, "留影对手下一自身回合重置水预算");
    }
}
} // namespace nztest

#ifdef NZ_WATER_FLOW_EDGE_STANDALONE
int main() {
    nztest::mfRunWaterFlowEdgeTests();
    return nztest::failures() == 0 ? 0 : 1;
}
#endif
