#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/elemental_sword.h"
#include "core/json.h"
#include "core/save.h"
#include "core/talents.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立无天赋装备干扰的合法五剑对局；流程：强化真实动作与存档回归；使用：指定功法层数和主辅修；返回：独立内存游戏。
Game mfEnhancementEncounter(int ManualLevel = 5, bool IsAuxiliary = false) {
    Game Encounter;
    Encounter.seed = Encounter.rng = 20261015;
    auto& PlayerState = Encounter.player;
    PlayerState.name = "相生强化定向测试";
    PlayerState.stage = PlayerState.highestStage = 12;
    PlayerState.lifespan = realmOf(12).life;
    Encounter.clock.ageBase = PlayerState.age;
    PlayerState.talent.clear(); PlayerState.traits.clear();
    PlayerState.arts.main = IsAuxiliary ? "tuna" : "fiveSword";
    PlayerState.arts.manuals = {{"fiveSword", ManualLevel}, {"tuna", 1}};
    if (IsAuxiliary) PlayerState.arts.aux = {"fiveSword"};
    PlayerState.arts.spells = {"earthSword", "metalSword", "waterSword", "woodSword", "fireSword", "gather", "skim", "gold"};
    PlayerState.arts.knownSpells = PlayerState.arts.spells;
    Npc Opponent;
    Opponent.id = "enhancement-opponent"; Opponent.name = "强化计量对手";
    Opponent.stage = 12; Opponent.sect = "zhushan"; Opponent.dao = "木";
    Encounter.npcs.push_back(Opponent);
    EnemySpec Enemy;
    Enemy.name = Opponent.name; Enemy.id = Opponent.id; Enemy.stage = 12;
    Enemy.sect = Opponent.sect; Enemy.style = Opponent.dao; Enemy.speed = 0;
    battleStart(Encounter, Enemy);
    auto& State = *PlayerState.arts.battle;
    State.mana = battleManaCap(PlayerState);
    State.enemyCards.main = "tuna"; State.enemyCards.aux.clear();
    State.enemyCards.manuals = {{"tuna", 1}}; State.enemyCards.nodes.clear();
    State.enemyCards.planned.clear();
    State.draw.insert(State.draw.end(), State.hand.begin(), State.hand.end());
    State.hand.clear();
    return Encounter;
}

// 用途：从既有合法牌组指定一张手牌；流程：强化顺序定向探针；使用：只搬运同一UID，不增牌；返回：可提交的手牌UID，缺牌为空。
string mfHoldEnhancementCard(Battle& State, const string& SpellId) {
    for (const auto& CardState : State.deck) {
        if (CardState.spell != SpellId) continue;
        for (auto* Pile : {&State.hand, &State.draw, &State.discard})
            Pile->erase(std::remove(Pile->begin(), Pile->end(), CardState.uid), Pile->end());
        State.hand.push_back(CardState.uid);
        return CardState.uid;
    }
    return "";
}

// 用途：准备敌方同构相生单招；流程：NPC镜像预览与执行；使用：从玩家合法牌组复制后冻结一张金剑；返回：无，不运行任何服务。
void mfPlanEnhancementEnemy(Game& Encounter) {
    auto& State = *Encounter.player.arts.battle;
    auto& Enemy = State.enemyCards;
    Enemy.main = "fiveSword"; Enemy.aux.clear(); Enemy.manuals = {{"fiveSword", 5}};
    Enemy.nodes.clear(); Enemy.deck = State.deck;
    Enemy.hand.clear(); Enemy.draw.clear(); Enemy.discard.clear(); Enemy.exhausted.clear(); Enemy.planned.clear();
    for (const auto& CardState : Enemy.deck) Enemy.draw.push_back(CardState.uid);
    const auto CardState = std::find_if(Enemy.deck.begin(), Enemy.deck.end(), [](const Card& Entry) { return Entry.spell == "metalSword"; });
    if (CardState != Enemy.deck.end()) {
        Enemy.draw.erase(std::remove(Enemy.draw.begin(), Enemy.draw.end(), CardState->uid), Enemy.draw.end());
        Enemy.hand = Enemy.planned = {CardState->uid};
    }
    Enemy.mana = 12; Enemy.last = "土"; Enemy.ElementalSwordSequence = 1;
    Enemy.ElementalSwordEnhancementLayers = 4;
}
} // namespace

// 用途：验证五行剑功法强化的范围、资源边界、镜像及持久化；流程：本轮定向回归；使用：由测试主入口调用；返回：无，断言写入harness失败计数。
void mfRunElementalEnhancementTests() {
    std::printf("\n--- 五行剑相生强化 ---\n");
    for (const int ManualLevel : {1, 5}) {
        for (const bool IsAuxiliary : {false, true}) {
            Game Encounter = mfEnhancementEncounter(ManualLevel, IsAuxiliary);
            auto& State = *Encounter.player.arts.battle;
            State.last = "土"; State.ElementalSwordSequence = 1;
            const auto Modifiers = talentMods(Encounter.player);
            const double EffectPerLayer = ManualLevel * .04 * (IsAuxiliary ? .5 : 1.0);
            checkNear(Modifiers.ElementalSwordMastery, ManualLevel * .2 * (IsAuxiliary ? .5 : 1.0),
                      "功法有效系数遵守主修全额与辅修减半");
            State.ElementalSwordEnhancementLayers = 5;
            checkNear(ElementalSword::mfEnhancementMultiplier(Modifiers, State), 1.0 + 5 * EffectPerLayer,
                      "五层强化只按当前有效功法层数放大附加部分");
            State.ElementalSwordEnhancementLayers = 0;
            const string CardUid = mfHoldEnhancementCard(State, "metalSword");
            const string SavedBefore = saveToJson(Encounter);
            const auto Preview = battlePreview(Encounter.player, State, "metalSword");
            checkStr(saveToJson(Encounter), SavedBefore, "强化预览不增加活动层数、不推进RNG或牌堆");
            const int ExpectedRaw = jsRound(mfBattleDamageBasis(12) * .65 * (1.0 + .15 * (1.0 + EffectPerLayer)));
            checkEq(Preview.RawDamage, ExpectedRaw, "本次接链先获得第一层强化再计算相生额外伤害");
            const int HealthBefore = State.enemyHp;
            check(battleTurn(Encounter, CardUid).ok, "实际手牌入口接受相生金剑");
            checkEq(State.ElementalSwordEnhancementLayers, 1, "实际相生只积累一层强化");
            checkEq(HealthBefore - State.enemyHp, Preview.damage, "强化后实际气血伤害与预览一致");
            checkEq(State.swordQi, 1, "五行剑诀不额外送剑气，金剑只保留通用剑招一层");
            checkEq(State.ElementalSwordManaReturns, ManualLevel == 5 && !IsAuxiliary ? 1 : 0,
                    "强化不放大返灵，整数返灵仍按有效功法系数取整");
        }
    }
    {
        Game Encounter = mfEnhancementEncounter();
        Encounter.player.arts.main = "tuna";
        Encounter.player.arts.aux.clear();
        auto& State = *Encounter.player.arts.battle;
        State.last = "金"; State.ElementalSwordSequence = 1;
        check(battleTurn(Encounter, mfHoldEnhancementCard(State, "waterSword")).ok,
              "没有装备五行剑诀也可正常使用五行剑神通");
        checkEq(State.ElementalSwordEnhancementLayers, 0, "只学过但未装备功法不会获得相生强化");
        checkEq(State.swordQi, 2, "水剑不依赖功法保留通用剑气和天然额外一层");
        checkEq(State.ElementalSwordManaReturns, 0, "未装备功法不获得其返灵");
    }
    {
        Game Encounter = mfEnhancementEncounter();
        auto& State = *Encounter.player.arts.battle;
        const auto Modifiers = talentMods(Encounter.player);
        for (const auto& SpellId : {"earthSword", "metalSword", "waterSword", "woodSword", "fireSword", "earthSword"}) {
            check(battleTurn(Encounter, mfHoldEnhancementCard(State, SpellId)).ok, "真实五剑连续顺序积累强化");
        }
        checkEq(State.ElementalSwordEnhancementLayers, 5, "完整五次接链恰好达到五层强化上限");
        checkEq(State.swordQi, 7, "六次普通剑招与水剑天然额外剑气累计七层");
        checkEq(State.ElementalSwordManaReturns, 3, "达到满强化仍最多返灵三次每轮");
        checkEq(State.comboDraws, 2, "达到满强化仍共用两张相生补牌上限");
        State.mana = battleManaCap(Encounter.player);
        check(battleTurn(Encounter, mfHoldEnhancementCard(State, "metalSword")).ok, "满强化后仍可继续正常相生");
        checkEq(State.ElementalSwordEnhancementLayers, 5, "第六次接链不突破强化上限");
        checkEq(State.ElementalSwordManaReturns, 3, "后续接链不绕过返灵限额");
        checkEq(State.comboDraws, 2, "后续接链不绕过补牌限额");
        check(battleTurn(Encounter, mfHoldEnhancementCard(State, "fireSword")).ok, "错序火剑仍能正常打出");
        checkEq(State.ElementalSwordSequence, 1, "有效五行错序只重开五剑顺序");
        checkEq(State.ElementalSwordEnhancementLayers, 5, "有效五行错序不会删除已获得的战斗强化");
        check(battleTurn(Encounter, mfHoldEnhancementCard(State, "gather")).ok, "强化间穿插中性回灵牌");
        checkEq(State.ElementalSwordEnhancementLayers, 5, "中性牌不清已得强化");
        checkEq(State.ElementalSwordSequence, 1, "中性牌不打断独立五剑顺序");
        State.enemyCards.planned.clear();
        check(battleTurn(Encounter, "end").ok, "强化对局跨真实回合");
        checkEq(State.ElementalSwordEnhancementLayers, 5, "新回合保留本场五层强化");
        checkNear(ElementalSword::mfGenerationDamageMultiplier(Modifiers, State, *battleSpell("metalSword"), true), 1.30,
                  "满强化将基础15%相生额外部分加倍为30%");
        checkNear(ElementalSword::mfGenerationDamageMultiplier(Modifiers, State, *battleSpell("gold"), true), 1.15,
                  "非五剑普通金道神通不获得五行剑强化增伤");
        checkNear(ElementalSword::mfGenerationDamageMultiplier(Modifiers, State, *battleSpell("fireSword"), false), 1.0,
                  "未相生时强化不放大基础伤害");
        auto ExtraGeneration = Modifiers;
        ExtraGeneration.combo = .15;
        checkNear(ElementalSword::mfGenerationDamageMultiplier(ExtraGeneration, State, *battleSpell("metalSword"), true), 1.60,
                  "附加相生系数也作为有界额外部分放大，最大总倍率1.60");
        checkNear(ElementalSword::mfDamageMultiplier(*battleSpell("fireSword"), true, 2.0), 1.50,
                  "火剑只将额外25%加倍，不把整段基础伤害翻倍");
        EnemySpec Enemy; Enemy.id = Encounter.npcs.front().id; Enemy.name = Encounter.npcs.front().name;
        Enemy.stage = 12; Enemy.sect = "zhushan"; Enemy.style = "木"; Enemy.speed = 0;
        battleStart(Encounter, Enemy);
        checkEq(Encounter.player.arts.battle->ElementalSwordEnhancementLayers, 0, "新战斗重置玩家强化");
        checkEq(Encounter.player.arts.battle->enemyCards.ElementalSwordEnhancementLayers, 0, "新战斗重置对手强化");
    }
    for (const bool HasGeneratingCombo : {false, true}) {
        Game Encounter = mfEnhancementEncounter();
        auto& State = *Encounter.player.arts.battle;
        State.hp = State.max / 2; State.enemyShield = 0;
        State.ElementalSwordEnhancementLayers = 5; State.ElementalSwordSequence = 1;
        State.last = HasGeneratingCombo ? "水" : "火";
        const string CardUid = mfHoldEnhancementCard(State, "woodSword");
        const auto Preview = battlePreview(Encounter.player, State, "woodSword");
        const int HealthBefore = State.hp;
        check(battleTurn(Encounter, CardUid).ok, "木剑通过真实动作测试相生与非相生吸血");
        checkEq(State.hp - HealthBefore, jsRound(Preview.damage * (HasGeneratingCombo ? .50 : .25)),
                "满强化木剑仅在相生时吸血50%，非相生保留原25%");
        checkEq(State.hp - HealthBefore, Preview.heal, "木剑强化吸血与预览一致");
    }
    {
        Game Encounter = mfEnhancementEncounter(); auto& State = *Encounter.player.arts.battle;
        State.ElementalSwordEnhancementLayers = 5; State.ElementalSwordSequence = 1; State.last = "水";
        State.hp = State.max / 2; State.enemyShield = State.enemyMax;
        const int HealthBefore = State.hp;
        check(battleTurn(Encounter, mfHoldEnhancementCard(State, "woodSword")).ok, "强化木剑可以攻击全挡护盾");
        checkEq(State.hp, HealthBefore, "全被护盾吸收的强化木剑不产生虚假吸血");
    }
    {
        Game Encounter = mfEnhancementEncounter(); auto& State = *Encounter.player.arts.battle;
        State.ElementalSwordEnhancementLayers = 5; State.ElementalSwordSequence = 1; State.last = "水";
        State.hp = State.max;
        check(battleTurn(Encounter, mfHoldEnhancementCard(State, "woodSword")).ok, "满血强化木剑正常施放");
        checkEq(State.hp, State.max, "强化吸血不突破自身气血上限");
        State.last = "火"; State.ElementalSwordSequence = 1; State.guard = .15;
        const auto Preview = battlePreview(Encounter.player, State, "earthSword");
        checkNear(Preview.guardPct, 5.0, "土剑单次强化10%仍被总守势20%限制为实际5%");
        check(battleTurn(Encounter, mfHoldEnhancementCard(State, "earthSword")).ok, "满强化土剑进入真实守势结算");
        checkNear(State.guard, .20, "强化土剑不能越过20%总守势上限");
    }
    {
        Game Encounter = mfEnhancementEncounter(); auto& State = *Encounter.player.arts.battle;
        mfPlanEnhancementEnemy(Encounter);
        const string SavedBefore = saveToJson(Encounter);
        const auto Preview = battleEnemyPreview(Encounter);
        checkStr(saveToJson(Encounter), SavedBefore, "NPC强化意图不提交模拟得到的第五层");
        const int HealthBefore = State.hp;
        check(battleTurn(Encounter, "end").ok, "NPC使用相同五剑强化真实施术路径");
        checkEq(HealthBefore - State.hp, Preview.damage, "NPC强化来伤与共享预览一致");
        checkEq(State.enemyCards.ElementalSwordEnhancementLayers, 5, "NPC实际接链得到第五层强化");
        checkEq(State.ElementalSwordEnhancementLayers, 0, "敌方强化不会串到玩家状态");
    }
    {
        Game Encounter = mfEnhancementEncounter();
        Encounter.player.arts.battle->ElementalSwordEnhancementLayers = 5;
        Encounter.player.arts.battle->enemyCards.ElementalSwordEnhancementLayers = 3;
        const string Saved = saveToJson(Encounter);
        Game Loaded; string Error;
        const bool RoundTripLoaded = loadDetachedFromJson(Saved, Loaded, &Error);
        check(RoundTripLoaded, "双方强化字段可合法往返：" + Error);
        if (Loaded.player.arts.battle) {
            checkEq(Loaded.player.arts.battle->ElementalSwordEnhancementLayers, 5, "往返保留玩家五层强化");
            checkEq(Loaded.player.arts.battle->enemyCards.ElementalSwordEnhancementLayers, 3, "往返保留对手三层强化");
        }
        auto Legacy = json::Value::parse(Saved);
        auto& LegacyBattle = Legacy.asObject().at("player").asObject().at("arts").asObject().at("battle");
        LegacyBattle.asObject().erase("elementalSwordEnhancementLayers");
        LegacyBattle.asObject().at("enemyCards").asObject().erase("elementalSwordEnhancementLayers");
        const bool LegacyLoaded = loadDetachedFromJson(Legacy.dump(), Loaded, &Error);
        check(LegacyLoaded, "旧档缺少双方强化字段仍可加载：" + Error);
        if (Loaded.player.arts.battle) {
            checkEq(Loaded.player.arts.battle->ElementalSwordEnhancementLayers, 0, "旧档玩家强化默认零");
            checkEq(Loaded.player.arts.battle->enemyCards.ElementalSwordEnhancementLayers, 0, "旧档对手强化默认零");
        }
        for (const auto& Invalid : {json::Value(-1), json::Value(6), json::Value(.5), json::Value(true), json::Value{}, json::Value("5")}) {
            for (const bool IsEnemy : {false, true}) {
                auto Broken = json::Value::parse(Saved);
                auto& Combat = Broken.asObject().at("player").asObject().at("arts").asObject().at("battle");
                (IsEnemy ? Combat.asObject().at("enemyCards") : Combat).set("elementalSwordEnhancementLayers", Invalid);
                const string BeforeLoad = saveToJson(Loaded);
                check(!loadDetachedFromJson(Broken.dump(), Loaded, &Error), "双方强化字段拒绝越界、非整数或错误类型");
                checkStr(saveToJson(Loaded), BeforeLoad, "拒绝非法强化档不部分覆盖原游戏");
            }
        }
    }
}
} // namespace nztest
