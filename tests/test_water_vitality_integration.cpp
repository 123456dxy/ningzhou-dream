#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/content/forge_content.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立真实出牌流程；流程：水功法满层集成；返回：不接触正式存档的独立战斗。
Game mfVitalityEncounter(int Level = 5) {
    CreateOptions Options; Options.seed = 20261019; Options.traitsExplicit = true;
    auto State = createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear();
    auto& Actor = State.player;
    Actor.stage = Actor.highestStage = 3;
    Actor.talent.clear(); Actor.traits.clear();
    Actor.arts.main = "wave"; Actor.arts.manuals = {{"wave", Level}};
    Actor.arts.aux.clear(); Actor.arts.nodes.clear(); Actor.arts.gear.clear(); Actor.arts.equipped.clear();
    Actor.arts.spells = {"waterSeal", "wavehit", "waterDraw", "water"};
    Actor.arts.knownSpells = Actor.arts.spells; Actor.arts.speed = 500;
    Actor.health = mfPlayerHealthMaximum(Actor);
    EnemySpec Target; Target.name = "水功法隔离木桩"; Target.beast = true; Target.speed = 1;
    battleStart(State, Target);
    auto& Field = *Actor.arts.battle;
    Field.max = 1000; Field.hp = 500; Field.enemyMax = Field.enemyHp = 100000;
    Field.enemyShield = 0; Field.mana = 12; Field.shield = 0;
    Field.poison = Field.poisonTurns = Field.burn = Field.burnTurns = 0;
    return State;
}

string mfVitalityHand(Battle& Field, const string& SpellId) {
    Field.hand.clear(); Field.draw.clear(); Field.discard.clear(); Field.exhausted.clear();
    for (const auto& Card : Field.deck) {
        if (Field.hand.empty() && Card.spell == SpellId) Field.hand.push_back(Card.uid);
        else Field.draw.push_back(Card.uid);
    }
    return Field.hand.empty() ? "" : Field.hand.front();
}
}

// 用途：锁定满层回春在真实神通/法器/预览的接入；返回：统一harness断言。
void mfRunWaterVitalityIntegrationTests() {
    using namespace nz;
    std::printf("\n--- 浪击诀满层回春真实集成 ---\n");
    for (const bool FullHealth : {false, true}) {
        auto State = mfVitalityEncounter();
        auto& Field = *State.player.arts.battle;
        if (FullHealth) Field.hp = Field.max;
        const auto Uid = mfVitalityHand(Field, "waterSeal");
        auto NoMastery = State.player; NoMastery.arts.manuals["wave"] = 4;
        const auto Base = battlePreview(NoMastery, Field, "waterSeal");
        const int Healing = Base.damage / 2;
        const auto Before = saveToJson(State);
        const auto Forecast = battlePreview(State.player, Field, "waterSeal");
        check(saveToJson(State) == Before, "回春预览不推进RNG或写活动状态");
        checkEq(Forecast.heal, FullHealth ? 0 : Healing, "回春50%实际损血与缺血共用报价");
        checkEq(Forecast.damage, Base.damage, "回春不增加伤害，满血也不追加伤害");
        checkEq(Forecast.RawDamage, Base.RawDamage, "回春不改变打出伤害");
        check(Forecast.text.find("浪击诀回春") != string::npos, "命中说明显示功法治疗事实");
        const int OwnBefore = Field.hp, TargetBefore = Field.enemyHp;
        check(battleTurn(State, Uid).ok, "真实出牌可触发满层回春");
        checkEq(Field.hp - OwnBefore, Forecast.heal, "真实回春与预览一致");
        checkEq(TargetBefore - Field.enemyHp, Forecast.damage, "真实伤害与预览一致且无额外段");
        checkEq(Field.poison, 0, "回春不附加毒或其他状态");
    }
    {
        auto State = mfVitalityEncounter(); auto& Field = *State.player.arts.battle;
        Field.enemyShield = 10000;
        const auto Forecast = battlePreview(State.player, Field, "waterSeal");
        checkEq(Forecast.damage, 0, "全额盾挡不产生实际损血");
        checkEq(Forecast.heal, 0, "盾挡不触发满层吸血");
        const auto Support = battlePreview(State.player, Field, "waterDraw");
        checkEq(Support.heal, 0, "独立抽牌不会触发回春");
    }
    {
        auto State = mfVitalityEncounter(); auto& Field = *State.player.arts.battle;
        auto Auxiliary = State.player; Auxiliary.arts.main = "tuna";
        Auxiliary.arts.manuals["tuna"] = 1; Auxiliary.arts.aux = {"wave"};
        const auto Forecast = battlePreview(Auxiliary, Field, "waterSeal");
        checkEq(Forecast.heal, 0, "辅修浪击诀五层不获得主修满层效果");
    }
    {
        auto State = mfVitalityEncounter(); auto& Field = *State.player.arts.battle;
        const auto* Definition = content::mfFindForgeEffect("damage");
        Gear Item; Item.id = "vitality-artifact"; Item.name = "回春测试法器"; Item.grade = 2;
        Item.slot = "weapon"; Item.shape = "box";
        Item.element = Definition && !Definition->Elements.empty() ? Definition->Elements.front() : "神";
        Item.polarity = "天阳"; Item.crafted = true; Item.ForgeVersion = 2; Item.EffectId = "damage";
        Item.EffectPower = 1; Item.SourceMaterials = {{"iron", 10}}; Item.ForgeEnergy = 10;
        Item.dimensions = {100, 100, 100, 100, 100};
        State.player.arts.gear = {Item}; State.player.arts.equipped["weapon"] = Item.id;
        const string Id = "artifact:" + Item.id;
        const auto Forecast = battlePreview(State.player, Field, Id);
        check(Forecast.valid, "新版法器可预览");
        checkEq(Forecast.RawDamage, 40, "受伤时二阶法器基础伤害仍为固定40");
        checkEq(Forecast.heal, 20, "新版法器主动直伤同样触发满层回春");
        const int OwnBefore = Field.hp, TargetBefore = Field.enemyHp;
        check(battleTurn(State, Id).ok, "新版法器可真实催动");
        checkEq(Field.hp - OwnBefore, Forecast.heal, "新版法器真实回春与预览一致");
        checkEq(TargetBefore - Field.enemyHp, Forecast.damage, "新版法器真实命中与预览一致");
    }
    {
        auto Template = mfVitalityEncounter();
        Player Challenger = Template.player, Opponent = Template.player;
        Challenger.name = "对称测试受击者"; Opponent.name = "水功法留影";
        Challenger.arts.battle.reset(); Opponent.arts.battle.reset();
        Challenger.arts.speed = 500; Opponent.arts.speed = 0;
        Game State; battleStartShadow(State, Challenger, Opponent, 20261019);
        auto& Field = *State.player.arts.battle;
        Field.max = Field.hp = 10000; Field.enemyMax = 1000; Field.enemyHp = 500;
        Field.shield = Field.enemyShield = 0;
        auto& Enemy = Field.enemyCards;
        Enemy.hand.clear(); Enemy.draw.clear(); Enemy.discard.clear(); Enemy.exhausted.clear(); Enemy.planned.clear();
        for (const auto& Card : Enemy.deck) {
            if (Enemy.hand.empty() && Card.spell == "waterSeal") Enemy.hand.push_back(Card.uid);
            else Enemy.draw.push_back(Card.uid);
        }
        Enemy.planned = Enemy.hand; Enemy.mana = 12;
        const int Before = Field.enemyHp;
        check(battleTurn(State, "end").ok, "水功法留影可执行真实冻结行动");
        const int ActualLoss = 10000 - Field.hp;
        check(ActualLoss > 0, "留影攻击实际损血大于零");
        checkEq(Field.enemyHp - Before, ActualLoss / 2, "敌方镜像按相同50%实际伤害治疗");
    }
    {
        ManualInfo Info;
        check(findManualInfo("wave", Info) && Info.effect.find("50%") != string::npos &&
            Info.effect.find("5层") != string::npos && Info.effect.find("转为") == string::npos,
            "当前功法目录写明五层50%回血且删除转伤");
        check(battleSpell("softwater") && battleSpell("softwater")->kind == "drawTo", "柔水无痕恢复为补牌而不再治疗");
        for (const auto& Build : artBuilds())
            if (Build.id == "water-wave")
                check(std::find(Build.spells.begin(), Build.spells.end(), "softwater") != Build.spells.end(),
                    "水系推荐构筑携带补至八张的柔水无痕");
    }
}
} // namespace nztest
