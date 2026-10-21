#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/interface/views.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

Game mfWaterEncounter() {
    CreateOptions Options;
    Options.seed = 20261019;
    Game State = createGame(Options);
    State.pending.reset();
    auto& Actor = State.player;
    Actor.stage = Actor.highestStage = 12;
    Actor.lifespan = realmOf(12).life + Actor.lifeBonus;
    Actor.talent.clear(); Actor.traits.clear();
    Actor.arts.spells = {"waterSeal", "stormSword", "wavehit", "waterfall", "waterDraw"};
    Actor.arts.knownSpells = Actor.arts.spells;
    Actor.health = mfPlayerHealthMaximum(Actor);
    EnemySpec Target;
    Target.name = "回流测试靶";
    battleStart(State, Target);
    auto& Combat = *Actor.arts.battle;
    Combat.enemyHp = Combat.enemyMax = 100000;
    Combat.mana = 12;
    return State;
}

string mfWaterCard(const Battle& Combat, const string& SpellId) {
    for (const auto& Card : Combat.deck) if (Card.spell == SpellId) return Card.uid;
    return "";
}

void mfWaterHand(Battle& Combat, const vector<string>& Uids) {
    Combat.hand = Uids;
    Combat.draw.clear(); Combat.discard.clear(); Combat.exhausted.clear();
    for (const auto& Card : Combat.deck)
        if (std::find(Uids.begin(), Uids.end(), Card.uid) == Uids.end()) Combat.draw.push_back(Card.uid);
}
}

// 用途：验证水系运转的真实牌堆、预览、回合预算和存档边界；返回：harness断言。
void mfRunWaterFlowIntegrationTests() {
    using namespace nz;
    std::printf("\n--- 水系生生不息真实集成 ---\n");
    auto State = mfWaterEncounter();
    auto& Combat = *State.player.arts.battle;
    const auto WaterUid = mfWaterCard(Combat, "waterSeal");
    const auto WaveUid = mfWaterCard(Combat, "wavehit");
    const auto StormUid = mfWaterCard(Combat, "stormSword");
    mfWaterHand(Combat, {WaterUid, WaveUid, StormUid});
    Combat.water = 2;
    const auto Before = saveToJson(State);
    const auto RandomBefore = State.rng;
    const auto Forecast = battlePreview(State.player, Combat, "waterSeal");
    checkEq(Forecast.draw, 1, "原生蓄水攻击预览补一张牌");
    check(saveToJson(State) == Before && State.rng == RandomBefore, "补牌预览不推进RNG或改变活动状态");
    const auto HandBefore = Combat.hand.size();
    check(battleTurn(State, WaterUid).ok, "真实水印可打出");
    checkEq(Combat.hand.size(), HandBefore, "攻击出一张后实际补回一张");
    checkEq(Combat.water, 4, "水印实际增加两层水势");
    checkEq(api::view::battle(State)["waterFlowDraws"].asInt(-1), 1, "视图下发共享水系补牌已用数");
    const auto WaveForecast = battlePreview(State.player, Combat, "wavehit");
    checkEq(WaveForecast.draw, 1, "四层浪击预览回流补牌");
    const int HpBefore = Combat.enemyHp;
    check(battleTurn(State, WaveUid).ok, "真实浪击可衔接水印");
    checkEq(HpBefore - Combat.enemyHp, WaveForecast.damage, "水势伤害真实执行与预览一致");
    checkEq(api::view::battle(State)["waterFlowDraws"].asInt(-1), 2, "两种水攻击共用两张预算");
    checkEq(battlePreview(State.player, Combat, "stormSword").draw, 0, "预算用完继续攻击但不再自补牌");
    check(battleTurn(State, StormUid).ok, "预算耗尽不阻止无冷却水攻击");
    checkEq(battleCooldown(Combat, "waterSeal"), 0, "水印保持无冷却");
    checkEq(battleCooldown(Combat, "wavehit"), 0, "浪击保持无冷却");
    checkEq(battlePreview(State.player, Combat, "waterDraw").draw, 2, "独立抽牌神通不占水攻击补牌预算");

    Game Restored;
    string Error;
    check(loadFromJson(saveToJson(State), Restored, &Error), "水预算真实状态可往返存档：" + Error);
    if (Restored.player.arts.battle)
        checkEq(api::view::battle(Restored)["waterFlowDraws"].asInt(-1), 2, "读档不重置本轮水补牌预算");
    auto Document = json::Value::parse(saveToJson(State));
    auto PlayerObject = Document["player"];
    auto ArtsObject = PlayerObject["arts"];
    auto BattleObject = ArtsObject["battle"];
    const auto Stable = saveToJson(Restored);
    for (const auto Invalid : {json::Value(-1), json::Value(3), json::Value(1.5), json::Value("2")}) {
        BattleObject.set("waterFlowDraws", Invalid);
        ArtsObject.set("battle", BattleObject); PlayerObject.set("arts", ArtsObject); Document.set("player", PlayerObject);
        check(!loadFromJson(Document.dump(), Restored, &Error), "负数/超预算/小数/字符串水补牌计数明确拒绝");
        check(saveToJson(Restored) == Stable, "非法预算读档拒绝不覆盖活动状态");
    }
    check(battleTurn(State, "end").ok, "真实回合可结束");
    checkEq(api::view::battle(State)["waterFlowDraws"].asInt(-1), 0, "下一自身回合恢复水补牌预算");
}
}
