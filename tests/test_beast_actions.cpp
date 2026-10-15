#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/beast_actions.h"
#include "core/combat/battle_scaling.h"
#include "core/json.h"
#include "core/save.h"
#include <algorithm>
#include <numeric>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立自然抽牌的纯内存妖兽战；流程：行动预算、真实结算及codec回归；返回：无天赋装备被动的同境隔离对局。
Game mfBeastEncounter(int Stage = 6, const string& Style = "体", int EnemySpeed = 0) {
    Game Encounter;
    Encounter.seed = Encounter.rng = 5147;
    auto& PlayerState = Encounter.player;
    PlayerState.name = "妖兽行动回归";
    PlayerState.stage = PlayerState.highestStage = Stage;
    PlayerState.lifespan = realmOf(Stage).life;
    PlayerState.talent.clear(); PlayerState.traits.clear();
    PlayerState.arts.main.clear(); PlayerState.arts.manuals.clear();
    PlayerState.arts.spells = {"sword", "wood", "skim"};
    PlayerState.arts.knownSpells = PlayerState.arts.spells;
    Encounter.clock.ageBase = PlayerState.age;
    EnemySpec Enemy;
    Enemy.name = "专属动作妖兽"; Enemy.kind = "spar";
    Enemy.stage = Stage; Enemy.style = Style; Enemy.beast = true; Enemy.speed = EnemySpeed;
    battleStart(Encounter, Enemy);
    return Encounter;
}

// 用途：汇总一轮冻结直伤；流程：预算与负载一致性检查；返回：整数，不改变计划。
int mfFrozenDamage(const Battle& State) {
    int Damage = 0;
    for (const auto& Action : State.BeastActions) Damage += Action.RawDamage;
    return Damage;
}

// 用途：定位隔离JSON的战斗对象；流程：恶意输入回归；返回：可修改JSON引用，不接触玩家档。
json::Value& mfBattleObject(json::Value& Object) {
    return Object.asObject().at("player").asObject().at("arts").asObject().at("battle");
}

// 用途：断言一次非法导入原子拒绝；流程：持久输入边界；返回：无，检查完整输出对象未改变。
void mfRejectUnchanged(const json::Value& Invalid, Game& Target, const string& Description) {
    const string Before = saveToJson(Target);
    string Error;
    check(!loadDetachedFromJson(Invalid.dump(), Target, &Error) && !Error.empty(), Description);
    checkStr(saveToJson(Target), Before, "非法妖兽计划不部分覆盖当前状态");
}
}

// 用途：验证妖兽境界预算、专属动作、逐段生存与冻结存档；流程：本地战斗改版定向回归。
// 使用：统一测试入口调用；返回：无，失败计入harness；所有状态仅在内存，没有玩家路径和活动端口。
void mfRunBeastActionTests() {
    std::printf("\n--- 妖兽专属多行动与冻结计划 ---\n");
    const int Stages[]{0,3,6,9,12};
    const int Limits[]{1,2,3,3,4};
    const int Energies[]{2,3,4,5,6};
    const double Caps[]{2.10,2.10,2.90,3.00,3.10};
    for (int Index = 0; Index < 5; ++Index) {
        Game Encounter = mfBeastEncounter(Stages[Index]);
        check(Encounter.player.arts.battle.has_value(), "各大境界可建立真实妖兽战");
        if (!Encounter.player.arts.battle) continue;
        auto& State = *Encounter.player.arts.battle;
        checkEq(BeastCombat::mfActionLimit(Stages[Index]), Limits[Index], "妖兽行动上限随境界分档");
        checkEq(BeastCombat::mfEnergyBudget(Stages[Index]), Energies[Index], "妖力预算与行动次数独立");
        check(!State.enemyCards.enabled && !State.BeastActions.empty(), "妖兽使用专属计划，没有人类神通牌堆");
        check(State.BeastActions.size() <= static_cast<std::size_t>(Limits[Index]), "冻结计划不超过境界行动上限");
        int Cost = 0;
        for (const auto& Action : State.BeastActions) Cost += BeastCombat::mfActionCost(Action.Kind);
        check(Cost <= Energies[Index], "冻结动作总妖力不越预算");
        check(State.intentRaw <= jsRound(mfBattleDamageBasis(Stages[Index]) * Caps[Index] * 1.06), "整轮直伤有统一上限，不按段数倍增");
        checkEq(State.intentRaw, mfFrozenDamage(State), "冻结直伤分配后合计等于整轮预告");
        check(BeastCombat::mfPlanStateError(State).empty(), "真实计划通过领域校验");
        const int HpBefore = State.hp;
        const auto Preview = battleEnemyPreview(Encounter);
        const string Saved = saveToJson(Encounter);
        battleEnemyPreview(Encounter); battleEnemyPreview(Encounter);
        checkStr(saveToJson(Encounter), Saved, "重复只读预告不抽牌、不掷伤害、不推进状态");
        checkEq(Preview.BeastActions.size(), State.BeastActions.size(), "预告包含全部逐段行动");
        check(battleTurn(Encounter, "end").ok && Encounter.player.arts.battle.has_value(), "各境界可真实结束首轮");
        if (Encounter.player.arts.battle)
            checkEq(HpBefore - Encounter.player.arts.battle->hp, Preview.damage, "逐段来伤预告与真实回合完全一致");
        Game Duplicate = mfBeastEncounter(Stages[Index]);
        Game Original = mfBeastEncounter(Stages[Index]);
        checkStr(saveToJson(Duplicate), saveToJson(Original), "相同种子真实开战产生相同牌组与兽行计划");
    }
    for (const string Style : {"体", "火", "木", "神", "金", "水", "土"}) {
        Game Encounter = mfBeastEncounter(6, Style);
        auto& State = *Encounter.player.arts.battle;
        for (int Round = 0; Round < 4; ++Round) {
            // 规则夹具每轮补满气血，确保观察完四轮特性；自然胜负由独立均匀矩阵验收。
            State.hp = State.max;
            check(BeastCombat::mfPlanStateError(State).empty(), "各风格每轮计划都可合法保存");
            if (Round == 1) {
                const auto Expected = Style == "火" ? BeastCombat::ActionKind::FlameBreath :
                    Style == "木" ? BeastCombat::ActionKind::VenomBreath : Style == "神" ? BeastCombat::ActionKind::Roar :
                    Style == "金" ? BeastCombat::ActionKind::Rend : Style == "水" || Style == "土" ? BeastCombat::ActionKind::Carapace : BeastCombat::ActionKind::Maul;
                check(std::any_of(State.BeastActions.begin(), State.BeastActions.end(), [&](const auto& Action) { return Action.Kind == Expected; }),
                    "本命特殊轮展示妖兽专属动作");
            }
            const auto Preview = battleEnemyPreview(Encounter);
            const int HpBefore = State.hp;
            check(battleTurn(Encounter, "end").ok && Encounter.player.arts.battle.has_value(), "专属风格动作按真实回合推进");
            if (!Encounter.player.arts.battle) break;
            checkEq(HpBefore - State.hp, Preview.damage, "吐息毒火/扰灵/凝甲与整轮损血预告同源");
            if (Round == 1 && Style == "火") check(State.enemyCards.burn > 0 && State.enemyCards.burnTurns == 3, "烈焰吐息真实施加玩家灼烧");
            if (Round == 1 && Style == "木") check(State.playerPoison > 0 && State.playerPoisonTurns == 3, "毒息真实施加玩家中毒");
            if (Round == 1 && Style == "神") check(State.enemyWeaken > 0, "咆哮扰灵只削弱后续直接攻击");
        }
        if (Encounter.player.arts.battle) checkEq(State.enemyCards.charge, 1, "第四轮蓄势保留到下一真实攻击");
    }
    {
        Game Encounter = mfBeastEncounter();
        auto& State = *Encounter.player.arts.battle;
        const auto FirstAttack = std::find_if(State.BeastActions.begin(), State.BeastActions.end(), [](const auto& Action) { return Action.RawDamage > 0; });
        State.shield = FirstAttack->RawDamage + 1;
        const int ShieldBefore = State.shield;
        Battle Projected = State;
        const auto Outcome = BeastCombat::mfExecuteActions(Encounter.player, Projected, false);
        int Blocked = 0;
        for (const auto& Action : Outcome.Actions) Blocked += Action.Blocked;
        checkEq(Blocked, ShieldBefore, "第一击扣除护盾后下一击继续消耗余盾，不能重复使用同一面盾");
        check(Outcome.Actions.front().Damage == 0 && Outcome.Actions.back().Damage > 0, "多段攻击逐段计算实际盾挡与损血");
        const int HpBefore = State.hp;
        const auto Preview = battleEnemyPreview(Encounter);
        check(battleTurn(Encounter, "end").ok, "带盾多行动真实回合成功");
        checkEq(HpBefore - State.hp, Preview.damage, "带盾多段预演和实际来伤一致");
    }
    {
        Game Encounter = mfBeastEncounter();
        Encounter.player.arts.nodes = {"6-5"};
        Battle State = *Encounter.player.arts.battle;
        State.enemyHp = 1; State.shield = State.max;
        const auto Outcome = BeastCombat::mfExecuteActions(Encounter.player, State, false);
        check(State.enemyHp == 0 && Outcome.Actions.front().CounterDamage == 1, "首段盾挡反震可击杀妖兽");
        check(std::count_if(Outcome.Actions.begin(), Outcome.Actions.end(), [](const auto& Item) { return Item.Executed; }) == 1,
            "妖兽被反震击杀后不能继续凝甲或第二击");
        State = *Encounter.player.arts.battle; State.hp = 1; State.shield = 0;
        const auto Death = BeastCombat::mfExecuteActions(Encounter.player, State, false);
        check(State.hp == 0 && Death.Damage == 1 && Death.Actions.front().Executed && !Death.Actions.back().Executed,
            "玩家首段阵亡后余下动作终止且实际损血不超过剩余气血");
    }
    {
        Game Encounter = mfBeastEncounter(12);
        auto& State = *Encounter.player.arts.battle;
        State.turn = 3;
        BeastCombat::mfPlanActions(Encounter);
        const auto Breathing = BeastCombat::mfExecuteActions(Encounter.player, State, false);
        check(State.intentRaw > 0 && Breathing.Damage > 0 && State.enemyShield > 0 && State.enemyCards.charge == 1,
            "第四轮以较低伤害经营凝甲蓄势，明确预告攻击，不空过回合");
        State.turn = 4;
        Game WithoutCharge = Encounter; WithoutCharge.player.arts.battle->enemyCards.charge = 0;
        BeastCombat::mfPlanActions(Encounter); BeastCombat::mfPlanActions(WithoutCharge);
        check(State.intentRaw > WithoutCharge.player.arts.battle->intentRaw && BeastCombat::mfPlanStateError(State).empty(),
            "蓄势在下一轮冻结时提高攻击但仍受整轮伤害预算约束");
        BeastCombat::mfExecuteActions(Encounter.player, State, false);
        checkEq(State.enemyCards.charge, 0, "蓄势只被第一段攻击消耗，不给每一击重复加成");
    }
    {
        Game Faster = mfBeastEncounter(6, "火", 10000);
        const auto& State = *Faster.player.arts.battle;
        check(!State.playerFirst && State.hp < State.max, "妖兽先手真实执行专属多行动");
        check(std::any_of(State.log.begin(), State.log.end(), [](const string& Text) { return Text.find("扑击") != string::npos; }),
            "先手战报记录妖兽专属动作，不伪装人类神通");
    }
    {
        for (const string Style : {"火", "木"}) {
            auto Encounter = mfBeastEncounter(9, Style); auto& State = *Encounter.player.arts.battle;
            State.turn = 1; BeastCombat::mfPlanActions(Encounter);
            auto Action = std::find_if(State.BeastActions.begin(), State.BeastActions.end(), [](const auto& Item) {
                return Item.BurnDamage > 0 || Item.PoisonDamage > 0;
            });
            check(Action != State.BeastActions.end(), "本命吐息轮包含精确持续伤害载荷");
            if (Action == State.BeastActions.end()) continue;
            checkEq(std::max(Action->BurnDamage, Action->PoisonDamage), jsRound(mfBattleDamageBasis(9) * .10), "新计划按10%基底冻结每跳伤害");
            if (Action->BurnDamage) Action->BurnDamage = jsRound(mfBattleDamageBasis(9) * .04);
            else Action->PoisonDamage = jsRound(mfBattleDamageBasis(9) * .04);
            const auto Saved = saveToJson(Encounter); Game Loaded; string Error;
            check(loadDetachedFromJson(Saved, Loaded, &Error), "旧4%每跳冻结载荷仍可合法读档：" + Error);
            checkStr(saveToJson(Loaded), Saved, "旧吐息不在读档中改成新10%或重掷计划");
            checkEq(battleEnemyPreview(Loaded).damage, battleEnemyPreview(Encounter).damage, "旧吐息冻结读档后预演仍一致");
        }
    }
    {
        Game Encounter = mfBeastEncounter(9, "火");
        auto& State = *Encounter.player.arts.battle;
        State.turn = 1; BeastCombat::mfPlanActions(Encounter);
        const string Saved = saveToJson(Encounter);
        Game Loaded; string Error;
        const bool Success = loadDetachedFromJson(Saved, Loaded, &Error);
        if (!Success) std::printf("  妖兽codec拒绝原因：%s\n", Error.c_str());
        check(Success, "多动作冻结计划通过持久化往返");
        if (Loaded.player.arts.battle) {
            checkEq(Loaded.rng, Encounter.rng, "加载冻结计划不重掷RNG");
            checkEq(battleEnemyPreview(Loaded).damage, battleEnemyPreview(Encounter).damage, "读档不改变逐段伤害预告");
            checkStr(saveToJson(Loaded), Saved, "全部冻结动作载荷完整往返");
        }
        const auto OriginalObject = json::Value::parse(Saved);
        for (const auto& Invalid : {json::Value(), json::Value(true), json::Value(1)}) {
            auto Object = OriginalObject; mfBattleObject(Object).set("beastActions", Invalid);
            mfRejectUnchanged(Object, Loaded, "拒绝妖兽计划null/布尔/数字冒充数组");
        }
        for (const string Field : {"rawDamage", "shieldGain", "healGain", "poisonDamage", "burnDamage", "weakenLayers", "chargeGain", "piercePercent"}) {
            auto Object = OriginalObject;
            mfBattleObject(Object).asObject().at("beastActions").asArray().front().set(Field, -1);
            mfRejectUnchanged(Object, Loaded, "拒绝妖兽行动负数载荷");
        }
        for (const string Id : {"fire", "unknown-beast-action"}) {
            auto Object = OriginalObject;
            mfBattleObject(Object).asObject().at("beastActions").asArray().front().set("id", Id);
            mfRejectUnchanged(Object, Loaded, "拒绝人类神通或未知妖兽行动ID");
        }
        {
            auto Object = OriginalObject; mfBattleObject(Object).set("enemyBeast", false);
            mfRejectUnchanged(Object, Loaded, "修士/留影身份不能夹带妖兽计划");
        }
        {
            auto Object = OriginalObject;
            mfBattleObject(Object).asObject().at("beastActions").asArray().front().set("rawDamage", 999999);
            mfRejectUnchanged(Object, Loaded, "拒绝超整轮预算的冻结直伤");
        }
        {
            auto Object = OriginalObject;
            mfBattleObject(Object).asObject().at("beastActions").asArray().front().set("hiddenPower", 1);
            mfRejectUnchanged(Object, Loaded, "拒绝未定义的妖兽属性键");
        }
        {
            auto Object = OriginalObject;
            auto& BattleObject = mfBattleObject(Object);
            BattleObject.asObject().erase("beastActions"); BattleObject.set("intentKind", "attack");
            BattleObject.set("intentName", "旧档冻结扑击"); BattleObject.set("intentRaw", 37);
            const bool LegacyLoaded = loadDetachedFromJson(Object.dump(), Loaded, &Error);
            check(LegacyLoaded && Loaded.player.arts.battle && Loaded.player.arts.battle->BeastActions.empty(),
                "旧档缺计划保留当前单意图，不在加载时生成或重掷");
            if (LegacyLoaded && Loaded.player.arts.battle) {
                const int HpBefore = Loaded.player.arts.battle->hp;
                checkEq(battleEnemyPreview(Loaded).damage, 37, "旧档当轮预览保持37点冻结伤害");
                check(battleTurn(Loaded, "end").ok && Loaded.player.arts.battle.has_value(), "旧档当前行动仍可真实结算");
                if (Loaded.player.arts.battle) {
                    checkEq(HpBefore - Loaded.player.arts.battle->hp, 37, "旧档真实来伤与冻结预告一致");
                    check(!Loaded.player.arts.battle->BeastActions.empty(), "旧意图执行后下一真实边界采用新妖兽计划");
                }
            }
        }
    }
}
} // namespace nztest
