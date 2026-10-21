#include "harness.h"
#include "core/exploration/dungeon_encounters.h"
#include "core/content/game_ids.h"
#include "core/combat/beast_actions.h"
#include "core/combat/battle_scaling.h"
#include "core/save.h"
#include <algorithm>
#include <array>
#include <set>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立不接触世界文件的冻结入口；流程：遭遇报价/实际开战回归；返回：纯内存游戏值。
Game mfEncounterFixture(int EntryStage = 6) {
    Game State;
    State.seed = State.rng = 906211;
    State.player.name = "秘境遭遇回归";
    State.player.stage = State.player.highestStage = 12;
    State.player.lifespan = realmOf(12).life;
    State.player.talent.clear(); State.player.traits.clear();
    State.player.arts.main.clear(); State.player.arts.manuals.clear();
    State.player.arts.spells = {"gold", "water", "gather"};
    State.player.arts.knownSpells = State.player.arts.spells;
    State.player.health = mfPlayerHealthMaximum(State.player);
    DungeonSite Site;
    Site.id = "encounter-site"; Site.name = "遭遇测试秘境"; Site.stage = EntryStage; Site.type = "wild";
    State.dungeons.sites.push_back(Site);
    DungeonRun Run;
    Run.siteId = Site.id; Run.siteName = Site.name; Run.stage = 12; Run.type = Site.type;
    Run.deck = {{"encounter-card-1", "strike"}, {"encounter-card-2", "defend"}};
    State.dungeons.run = Run;
    return State;
}

// 用途：为指定遭遇类型寻找合法节点编号；流程：目录可达性回归；返回：最多512个稳定候选中的报价。
DungeonEncounter mfFindEncounter(const Game& State, DungeonEncounterKind Kind, const std::string& Type = "boss") {
    for (int NodeIndex = 0; NodeIndex < 512; ++NodeIndex) {
        DungeonNode Node;
        Node.id = "encounter-node-" + std::to_string(NodeIndex); Node.type = Type;
        auto Encounter = mfDescribeDungeonEncounter(State, *State.dungeons.run, Node);
        if (Encounter.Enabled && Encounter.Kind == Kind) return Encounter;
    }
    return {};
}

// 用途：核对一次报价与真实开战的同源字段；流程：五境界覆盖；返回：无，错误计入测试框架。
void mfCheckStartedEncounter(const Game& Original, const DungeonEncounter& Description) {
    Game State = Original;
    auto Enemy = Description.Enemy;
    Enemy.speed = 0; // 夹具保证玩家先手，避免开场死亡妨碍观察规格；并非调整秘境正式数值。
    battleStart(State, Enemy);
    check(State.player.arts.battle.has_value(), "有效秘境规格可以实际开战");
    if (!State.player.arts.battle) return;
    const auto& BattleState = *State.player.arts.battle;
    checkEq(BattleState.enemyStage, Enemy.stage, "实际对手境界取冻结入口报价");
    checkStr(BattleState.style, Enemy.style, "实际本命风格与预告同源");
    checkStr(BattleState.enemy, Description.Name, "实际对手名称与路线预告一致");
    check(BattleState.enemyBeast == Enemy.beast, "实际修士/妖兽身份与预告一致");
    checkStr(BattleState.dungeonSite, State.dungeons.run->siteId, "秘境奖励上下文未丢失");
    const auto Snapshot = saveToJson(State);
    const auto Preview = battleEnemyPreview(State);
    check(Preview.damage >= 0 && Preview.shield >= 0 && Preview.heal >= 0, "真实战斗意图可以执行只读预演");
    checkStr(saveToJson(State), Snapshot, "真实意图查询不消费活动RNG或改写存档");
    if (Enemy.beast) check(BeastCombat::mfPlanStateError(BattleState).empty(), "妖兽规格生成白名单合法冻结计划");
    else check(BattleState.enemyCards.enabled && !BattleState.enemySpells.empty(), "精英采用真实宗门神通牌组而非只有名称");
}
} // namespace

// 用途：验收秘境遭遇的稳定目录、冻结难度、预告与真实机制；流程：独立定向回归。
// 使用方式：测试主入口调用；返回值：无，检查计入harness，不创建玩家文件或运行服务。
void mfRunDungeonEncounterTests() {
    std::printf("\n--- 秘境路线遭遇与实际机制 ---\n");
    constexpr std::array BeastKinds{
        DungeonEncounterKind::StoneGuard, DungeonEncounterKind::FlameBeast, DungeonEncounterKind::VenomBeast,
        DungeonEncounterKind::MindBeast, DungeonEncounterKind::RendingBeast, DungeonEncounterKind::ChargingBeast
    };
    {
        auto State = mfEncounterFixture();
        DungeonNode Node; Node.id = "stable-node"; Node.type = "battle";
        const auto Snapshot = saveToJson(State);
        const auto First = mfDescribeDungeonEncounter(State, *State.dungeons.run, Node);
        for (int Repetition = 0; Repetition < 5; ++Repetition) {
            const auto Again = mfDescribeDungeonEncounter(State, *State.dungeons.run, Node);
            check(Again.Enabled && Again.Id == First.Id && Again.Name == First.Name && Again.Hint == First.Hint,
                "重复查看同一节点不会重抽对手或预告");
        }
        checkStr(saveToJson(State), Snapshot, "遭遇目录查询完全不改变存档、时间和RNG");
        State.player.stage = 0; State.dungeons.run->stage = 0;
        const auto LowPlayer = mfDescribeDungeonEncounter(State, *State.dungeons.run, Node);
        checkEq(LowPlayer.Enemy.stage, 5, "普通战比六阶入口低一个小境界，与当前玩家/run.stage无关");
        checkStr(LowPlayer.Id, First.Id, "改变人物或临时牌组境界不重抽节点类型");
        State.dungeons.sites.clear();
        const auto Missing = mfDescribeDungeonEncounter(State, *State.dungeons.run, Node);
        check(!Missing.Enabled && !Missing.Reason.empty(), "缺少冻结入口时明确禁用，绝不回退当前人物境界");
    }
    {
        auto State = mfEncounterFixture();
        for (const auto InvalidStage : {-1, 13}) {
            State.dungeons.sites.front().stage = InvalidStage;
            DungeonNode Node; Node.id = "invalid-stage"; Node.type = "boss";
            const auto Invalid = mfDescribeDungeonEncounter(State, *State.dungeons.run, Node);
            check(!Invalid.Enabled && !Invalid.Reason.empty(), "非法入口境界明确拒绝而非静默夹取");
        }
        State.dungeons.sites.front().stage = 6;
        for (const std::string Type : {"event", "merchant", "camp", "chest", "unknown", ""}) {
            DungeonNode Node; Node.id = "non-combat"; Node.type = Type;
            const auto Invalid = mfDescribeDungeonEncounter(State, *State.dungeons.run, Node);
            check(!Invalid.Enabled && !Invalid.Reason.empty(), "非斗法节点不能生成对手");
        }
    }
    for (const int Stage : {0, 3, 6, 9, 12}) {
        auto State = mfEncounterFixture(Stage);
        for (const auto Kind : BeastKinds) {
            const auto Description = mfFindEncounter(State, Kind);
            check(Description.Enabled && Description.Kind == Kind, "五境界六型妖兽均有稳定可达节点");
            if (!Description.Enabled) continue;
            checkEq(Description.Enemy.stage, Stage, "首领采用入口冻结境界");
            checkEq(Description.Enemy.health, 115, "首领沿用明确115健康系数，不另抬伤害");
            check(Description.Enemy.id.empty() && Description.Enemy.beast && Description.Enemy.sect.empty(),
                "妖兽不借用真实NPC身份，不触发NPC服丹");
            check(content::parseDaoId(Description.Enemy.style).has_value() && !Description.Hint.empty(),
                "妖兽机制风格来自真实大道白名单且有预告");
            mfCheckStartedEncounter(State, Description);
        }
        DungeonNode Elite; Elite.id = "sect-elite"; Elite.type = "elite";
        const auto Description = mfDescribeDungeonEncounter(State, *State.dungeons.run, Elite);
        check(Description.Enabled && Description.Kind == DungeonEncounterKind::SectDisciple && !Description.Enemy.beast &&
            content::parseSectId(Description.Enemy.sect).has_value() && Description.Enemy.id.empty(),
            "精英使用六宗真实普通构筑，不伪造完整传承或世界NPC身份");
        checkEq(Description.Enemy.health, 90, "精英沿用90健康系数");
        mfCheckStartedEncounter(State, Description);
    }
    {
        auto State = mfEncounterFixture(6);
        std::set<std::string> SeenSects;
        for (int NodeIndex = 0; NodeIndex < 512; ++NodeIndex) {
            DungeonNode Node; Node.id = "sect-node-" + std::to_string(NodeIndex); Node.type = "elite";
            SeenSects.insert(mfDescribeDungeonEncounter(State, *State.dungeons.run, Node).Enemy.sect);
        }
        checkEq(SeenSects.size(), 6, "精英路线可稳定遇到六宗不同真实构筑");
        constexpr std::array ExpectedActions{
            BeastCombat::ActionKind::Carapace, BeastCombat::ActionKind::FlameBreath, BeastCombat::ActionKind::VenomBreath,
            BeastCombat::ActionKind::Roar, BeastCombat::ActionKind::Rend, BeastCombat::ActionKind::Maul
        };
        for (std::size_t KindIndex = 0; KindIndex < BeastKinds.size(); ++KindIndex) {
            auto Description = mfFindEncounter(State, BeastKinds[KindIndex]);
            auto Started = State; Description.Enemy.speed = 0;
            battleStart(Started, Description.Enemy);
            if (!Started.player.arts.battle) { check(false, "机制观察必须有真实开场"); continue; }
            auto& BattleState = *Started.player.arts.battle;
            BattleState.turn = 1; // 固定本命技能轮，直接使用正式计划器，不伪造行动载荷。
            BeastCombat::mfPlanActions(Started);
            const auto Special = std::find_if(BattleState.BeastActions.begin(), BattleState.BeastActions.end(), [&](const auto& Action) {
                return Action.Kind == ExpectedActions[KindIndex];
            });
            check(Special != BattleState.BeastActions.end(), "路线预告的本命特性确实进入真实妖兽计划");
            if (Special == BattleState.BeastActions.end()) continue;
            if (BeastKinds[KindIndex] == DungeonEncounterKind::StoneGuard) check(Special->ShieldGain > 0, "凝甲守卫真实增加护盾");
            if (BeastKinds[KindIndex] == DungeonEncounterKind::FlameBeast) check(Special->BurnDamage > 0, "焰息异兽真实附加灼烧");
            if (BeastKinds[KindIndex] == DungeonEncounterKind::VenomBeast) check(Special->PoisonDamage > 0, "瘴毒异兽真实附加中毒");
            if (BeastKinds[KindIndex] == DungeonEncounterKind::MindBeast) check(Special->WeakenLayers == 1, "摄魂异兽真实扰灵而非虚构扣蓝");
            if (BeastKinds[KindIndex] == DungeonEncounterKind::RendingBeast) check(Special->PiercePercent == 25, "迅爪掠食者真实穿盾25%");
            if (BeastKinds[KindIndex] == DungeonEncounterKind::ChargingBeast) check(Special->RawDamage > 0, "蓄势猛兽真实采用重爪");
            Battle Projected = BattleState;
            BeastCombat::mfExecuteActions(Started.player, Projected, false);
            check(BeastCombat::mfPlanStateError(BattleState).empty(), "本命机制计划符合现有持久化白名单");
            if (BeastKinds[KindIndex] == DungeonEncounterKind::FlameBeast) check(Projected.enemyCards.burn > 0, "焰息实际执行产生玩家灼烧池");
            if (BeastKinds[KindIndex] == DungeonEncounterKind::VenomBeast) check(Projected.playerPoison > 0, "毒息实际执行产生玩家中毒");
            if (BeastKinds[KindIndex] == DungeonEncounterKind::MindBeast) check(Projected.enemyWeaken == 1, "扰灵实际执行削弱玩家后续攻击");
        }
    }
}
} // namespace nztest
