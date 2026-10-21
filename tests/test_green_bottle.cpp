#include "harness.h"
#include "core/crafting/green_bottle.h"
#include "core/crafting/garden_rules.h"
#include "core/crafting/cave_garden.h"
#include "core/content/green_bottle_content.h"
#include "core/exploration/dungeon_encounters.h"
#include "core/interface/green_bottle_api.h"
#include "core/crafting/craft_prices.h"
#include "core/foundation/state_limits.h"
#include "core/play.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/dungeon.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <limits>

namespace nztest {
namespace {
// 用途：建立独立有瓶洞府；流程：规则/API回归；返回：临时Game，不访问运行服务或玩家文件。
nz::Game mfBottleGame() {
    nz::CreateOptions Options; Options.traitsExplicit = true; Options.seed = 71377;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear();
    State.player.traits = {"greenBottle"}; State.player.cave = State.player.location = "wuling";
    State.player.stones = 1000000; State.player.health = 100;
    State.player.caveTier = 2; State.player.qiSupply = 0;
    return State;
}
// 用途：用真实路线/领奖及战斗收口边界验证通关材料；流程：结算单元测试；返回：完成真，战斗胜利使用显式夹具，不代表自然实战胜率。
bool mfCompleteBottleRoute(nz::Game& State) {
    int Guard = 0;
    while (State.dungeons.run && State.dungeons.run->phase != "complete" && ++Guard < 100) {
        if (State.player.arts.battle) {
            State.player.arts.battle->hp = State.player.arts.battle->max;
            State.player.arts.battle->enemyHp = 0;
            nz::battleFinish(State, true);
        } else if (State.dungeons.run->phase == "reward") {
            if (!nz::dungeonClaim(State, "skip").empty()) return false;
        } else {
            const auto& Run = *State.dungeons.run;
            const auto Node = std::find_if(Run.nodes.begin(), Run.nodes.end(), [&](const nz::DungeonNode& Entry) { return nz::dungeonNodeAvailable(Run, Entry.id); });
            if (Node == Run.nodes.end()) return false;
            const auto NodeId = Node->id;
            if (!nz::dungeonChoose(State, NodeId).empty()) return false;
        }
    }
    return State.dungeons.run && State.dungeons.run->cleared && State.dungeons.run->phase == "complete";
}

// 用途：构造更新前没有奖励数量字段的真实形状；流程：旧在途与已通关保存兼容回归；使用：还原旧十份合同、原人物境界与旧普通怪预告，再删除新增字段；返回：独立旧档JSON。
nz::json::Value mfLegacyBottleSave(const nz::Game& State) {
    nz::Game LegacyState = State;
    auto& LegacyRun = *LegacyState.dungeons.run;
    LegacyRun.TargetRewardCount = 10;
    for (auto& Site : LegacyState.dungeons.sites) {
        if (Site.id != LegacyRun.siteId) continue;
        Site.TargetRewardCount = 10;
        // 旧专属入口随当时人物境界生成；令人物与run仍为同一原境界，不制造历史上不可能的越级夹具。
        LegacyRun.stage = Site.stage;
        LegacyState.player.stage = LegacyState.player.highestStage = Site.stage;
        LegacyState.player.lifespan = nz::realmOf(Site.stage).life;
        // 通关夹具已按高境界战斗回满；还原旧境界时同步收回超出该境界上限的绝对气血。
        LegacyState.player.health = std::min(LegacyState.player.health, nz::mfPlayerHealthMaximum(LegacyState.player));
    }
    // 旧普通战比入口低一个小境界；新入口普通怪与首领同境，旧存档预告也必须保持旧规则。
    for (auto& Node : LegacyRun.nodes) {
        const auto Encounter = nz::mfDescribeDungeonEncounter(LegacyState, LegacyRun, Node);
        if (!Encounter.Enabled) continue;
        Node.name = Encounter.Name;
        Node.text = "第" + std::to_string(Node.row + 1) + "层 · " + Node.name;
    }
    if (LegacyRun.targetGranted) LegacyRun.materials[LegacyRun.targetId] = 10;
    auto Stored = nz::json::Value::parse(nz::saveToJson(LegacyState));
    auto& Dungeons = Stored.asObject().at("dungeons");
    auto& Run = Dungeons.asObject().at("run");
    Run.asObject().erase("targetRewardCount");
    for (auto& Site : Dungeons.asObject().at("sites").asArray()) {
        if (Site["id"].asString() != Run["siteId"].asString()) continue;
        Site.asObject().erase("targetRewardCount");
    }
    return Stored;
}
}
// 用途：验证冻结成本/周期、逐阶、白名单秘境、API原子性及持久字段；流程：本轮定向回归；返回：harness记录。
void mfRunGreenBottleTests() {
    using namespace nz; using namespace nz::crafting;
    std::printf("\n--- 小绿瓶永久品阶与专属秘境 ---\n");
    constexpr int ExpectedMaterialCounts[3] = {50, 100, 200};
    constexpr int ExpectedDays[7][6] = {
        {300,750,1875,4688,11719,29297}, {172,429,1072,2679,6697,16742},
        {125,311,776,1940,4850,12123}, {86,215,536,1340,3349,8371},
        {59,146,363,908,2269,5671}, {38,94,235,586,1465,3663}, {24,60,149,371,926,2313}
    };
    for (int Grade = 0; Grade <= 6; ++Grade) {
        const auto Actual = mfGreenBottleMaturityDays(2, Grade);
        for (std::size_t Index = 0; Index < Actual.size(); ++Index)
            checkEq(Actual[Index], ExpectedDays[Grade][Index], "冻结成熟周期 瓶" + std::to_string(Grade) + " 药" + std::to_string(Index + 1));
    }
    Game State = mfBottleGame();
    State.player.stage = State.player.highestStage = 12; State.player.lifespan = realmOf(12).life;
    for (const auto& Definition : content::mfGreenBottleMaterials()) State.player.arts.materials[Definition.Material.id] = 1000;
    const auto Snapshot = saveToJson(State);
    static_cast<void>(api::view::mfGreenBottleView(State));
    check(saveToJson(State) == Snapshot, "升级及秘境纯查询不生成入口/消耗RNG/返现");
    check(mfGreenBottleDungeonOptions(State).empty(), "零阶未解锁专属秘境");
    for (int ExpectedGrade = 0; ExpectedGrade < 6; ++ExpectedGrade) {
        const auto Quote = mfQuoteGreenBottleUpgrade(State);
        const int StonesBefore = State.player.stones;
        check(Quote.Enabled && Quote.TargetGrade == ExpectedGrade + 1, "升级报价只升下一级");
        const int MaterialBefore = Quote.Materials.empty() ? 0 : State.player.arts.materials[Quote.Materials.front().Id];
        check(mfUpgradeGreenBottle(State, ExpectedGrade).empty(), "逐阶真实升级");
        checkEq(State.player.stones, StonesBefore - GreenBottleUpgradeStones[ExpectedGrade], "真实扣款匹配冻结费用");
        if (!Quote.Materials.empty()) checkEq(State.player.arts.materials[Quote.Materials.front().Id], MaterialBefore - Quote.Materials.front().Required, "升级精确扣专属50/100/200份");
        const auto Once = saveToJson(State);
        check(!mfUpgradeGreenBottle(State, ExpectedGrade).empty() && saveToJson(State) == Once, "重复/过期点击不跨级消费");
        if (ExpectedGrade + 1 == 3) check(mfQuoteGreenBottleDungeon(State, 4).Enabled, "三阶即可进入四阶材料秘境，不形成3到4死锁");
    }
    check(!mfQuoteGreenBottleUpgrade(State).Enabled && mfQuoteGreenBottleDungeon(State, 6).Enabled, "六阶不可再升级但仍保留六阶秘境");
    const auto Full = saveToJson(State);
    check(!mfUpgradeGreenBottle(State, 6).empty() && saveToJson(State) == Full, "满阶拒绝不扣款");
    Game Restored; std::string Error;
    check(loadFromJson(Full, Restored, &Error) && Restored.player.arts.GreenBottleGrade == 6, "永久阶位真实存读档保留");
    auto Missing = json::Value::parse(Full); Missing.asObject()["player"].asObject()["arts"].asObject().erase("greenBottleGrade");
    check(loadFromJson(Missing.dump(), Restored, &Error) && Restored.player.arts.GreenBottleGrade == 0, "旧档缺字段默认零阶，不补领任何升级");
    for (const auto& Invalid : {json::Value(-1),json::Value(7),json::Value(1.5),json::Value(true),json::Value("3")}) {
        auto Corrupt = json::Value::parse(Full); Corrupt.asObject()["player"].asObject()["arts"].set("greenBottleGrade", Invalid);
        const auto Before = saveToJson(Restored);
        check(!loadFromJson(Corrupt.dump(), Restored, &Error) && saveToJson(Restored) == Before, "非法瓶阶类型/范围拒绝并保留目标存档");
    }
    State = mfBottleGame(); State.player.stones = 1999;
    const auto Poor = saveToJson(State);
    check(!mfUpgradeGreenBottle(State, 0).empty() && saveToJson(State) == Poor, "余额不足不部分扣费");
    State.player.stones = 100000; State.player.arts.GreenBottleGrade = 3;
    State.player.arts.materials["meteor"] = 1000;
    const auto Ordinary = saveToJson(State);
    check(!mfUpgradeGreenBottle(State, 3).empty() && saveToJson(State) == Ordinary, "普通高阶材料不能替代专属门槛");
    State.player.traits.clear(); State.player.arts.GreenBottleGrade = 0;
    check(!mfQuoteGreenBottleUpgrade(State).Enabled && mfGreenBottleDungeonOptions(State).empty() && api::view::mfGreenBottleView(State).asObject().size() == 1, "无天赋不显示/不允许升级或专属入口");
    State = mfBottleGame(); State.player.stage = State.player.highestStage = 12; State.player.lifespan = realmOf(12).life;
    State.eventSeq = StateLimits::MaxPersistedInteger;
    const auto FullJournal = saveToJson(State);
    check(!mfQuoteGreenBottleUpgrade(State).Enabled && !mfUpgradeGreenBottle(State, 0).empty() && saveToJson(State) == FullJournal,
        "纪事编号满时升级报价/执行均拒绝，不扣费或越持久上限");
    State.player.arts.GreenBottleGrade = 3;
    const auto FullDungeonJournal = saveToJson(State);
    check(!mfQuoteGreenBottleDungeon(State, 4).Enabled && !mfEnterGreenBottleDungeon(State, 4, 3).empty() && saveToJson(State) == FullDungeonJournal,
        "纪事编号满时开境报价/执行均拒绝，不消耗入口编号");
    for (int Grade = 4; Grade <= 6; ++Grade) {
        State = mfBottleGame(); State.player.arts.GreenBottleGrade = Grade - 1;
        State.player.stage = State.player.highestStage = 12; State.player.lifespan = realmOf(12).life;
        const int ExpectedCount = ExpectedMaterialCounts[Grade - 4];
        const std::string CapacityMaterial = content::mfGreenBottleMaterialForGrade(Grade)->Material.id;
        State.player.arts.materials[CapacityMaterial] = StateLimits::MaxPersistedInteger - ExpectedCount + 1;
        const auto NoCapacity = saveToJson(State);
        check(!mfQuoteGreenBottleDungeon(State, Grade).Enabled && !mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty() && saveToJson(State) == NoCapacity,
              "无法容纳完整升阶材料时入口报价与执行一致拒绝，不能按旧十份低估容量");
        State.player.arts.materials[CapacityMaterial] = StateLimits::MaxPersistedInteger - ExpectedCount;
        check(mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty(), "恰好容纳整次50/100/200份时可进入");
        const bool CapacityRouteCompleted = mfCompleteBottleRoute(State);
        check(CapacityRouteCompleted, "容量边界样本沿真实路线完成");
        if (!CapacityRouteCompleted) continue;
        State.player.arts.materials[CapacityMaterial] = StateLimits::MaxPersistedInteger - ExpectedCount + 1;
        const auto BadReturn = saveToJson(State);
        check(!dungeonStateError(State).empty(), "专属秘境在途保存拒绝无法完整兑现的目标库存");
        check(!dungeonLeave(State).empty() && saveToJson(State) == BadReturn, "离境再次校验完整容量，拒绝不部分发奖或清除在途探索");
        State.player.arts.materials[CapacityMaterial] = StateLimits::MaxPersistedInteger - ExpectedCount;
        check(dungeonLeave(State).empty(), "恢复完整容量后可重试真实离境");
        checkEq(State.player.arts.materials[CapacityMaterial], StateLimits::MaxPersistedInteger, "真实完整50/100/200份刚好到持久库存上限");
    }
    State = mfBottleGame();
    for (const auto& Input : {json::Value(json::Object{}), json::Value(json::Object{{"expectedGrade", true}}),
        json::Value(json::Object{{"expectedGrade", 1.5}}), json::Value(json::Object{{"expectedGrade", 0},{"grade", 6}})}) {
        const auto Before = saveToJson(State);
        check(!api::mfExecuteGreenBottleCommand(State, api::RouteId::CaveGreenBottleUpgrade, Input).empty() && saveToJson(State) == Before, "独立API严格校验必填整数与未知键，拒绝原子");
    }
    for (int Grade = 4; Grade <= 6; ++Grade) {
        const int ExpectedStage = (Grade - 2) * 3;
        const int ExpectedCount = ExpectedMaterialCounts[Grade - 4];
        State = mfBottleGame(); State.player.arts.GreenBottleGrade = Grade - 1;
        State.player.stage = State.player.highestStage = ExpectedStage - 1;
        State.player.lifespan = realmOf(State.player.stage).life;
        const auto BelowSnapshot = saveToJson(State);
        const auto BelowQuote = mfQuoteGreenBottleDungeon(State, Grade);
        checkEq(BelowQuote.Stage, ExpectedStage, "四五六阶灵材秘境报价固定金丹/元婴/化神难度");
        checkEq(BelowQuote.RewardCount, ExpectedCount, "未满足境界时仍明示一次通关完整材料奖励");
        check(!BelowQuote.Enabled && !mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty() && saveToJson(State) == BelowSnapshot,
              "低于固定入口境界时预览与实际动作一致拒绝且不生成入口");
        vector<int> PlayerStages{ExpectedStage};
        if (ExpectedStage < 12) PlayerStages.push_back(12);
        for (const int PlayerStage : PlayerStages) {
            State = mfBottleGame(); State.player.arts.GreenBottleGrade = Grade - 1;
            State.player.stage = State.player.highestStage = PlayerStage; State.player.lifespan = realmOf(PlayerStage).life;
            const auto Quote = mfQuoteGreenBottleDungeon(State, Grade);
            check(Quote.Enabled && Quote.Stage == ExpectedStage && Quote.RewardCount == ExpectedCount,
                  "满足对应前一瓶阶与人物境界后报价固定难度和足量奖励");
            const auto BeforeView = saveToJson(State);
            const auto BottleView = api::view::mfGreenBottleView(State);
            const auto ViewEntry = std::find_if(BottleView["dungeons"].asArray().begin(), BottleView["dungeons"].asArray().end(),
                [&](const auto& Entry) { return Entry["grade"].asInt() == Grade; });
            check(ViewEntry != BottleView["dungeons"].asArray().end() && (*ViewEntry)["rewardCount"].asInt() == ExpectedCount &&
                  (*ViewEntry)["stage"].asInt() == ExpectedStage, "洞府API展示同一固定难度与足量奖励报价");
            check(saveToJson(State) == BeforeView, "固定入口与奖励查询不写状态或推进RNG");
            check(mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty(), "满足境界边界后真实进入专属秘境");
            if (!State.dungeons.run) continue;
            const auto& Run = *State.dungeons.run;
            const auto Site = std::find_if(State.dungeons.sites.begin(), State.dungeons.sites.end(), [&](const auto& Entry) { return Entry.id == Run.siteId; });
            check(Site != State.dungeons.sites.end() && Site->stage == ExpectedStage && Site->TargetRewardCount == ExpectedCount,
                  "真实入口冻结固定境界及50/100/200份合同");
            checkEq(Run.stage, PlayerStage, "人物境界继续独立冻结用于卡组与人物一致性");
            checkEq(Run.TargetRewardCount, ExpectedCount, "在途探索冻结与入口相同的完整材料数量");
            checkEq(Run.nodes.size(), (9 + ExpectedStage / 3 - 1) * 3 + 1, "高境打低阶瓶秘境路线不随人物境界增层");
            vector<string> CheckedNodeTypes;
            for (const auto& Node : Run.nodes) {
                if (Node.type != "battle" && Node.type != "elite" && Node.type != "boss") continue;
                if (std::find(CheckedNodeTypes.begin(), CheckedNodeTypes.end(), Node.type) != CheckedNodeTypes.end()) continue;
                CheckedNodeTypes.push_back(Node.type);
                const auto Encounter = mfDescribeDungeonEncounter(State, Run, Node);
                check(Encounter.Enabled && Encounter.Enemy.stage == ExpectedStage,
                      "瓶秘境普通/精英/首领全部固定对应境界，不随高境人物抬高或降回上个大境界");
            }
            checkEq(CheckedNodeTypes.size(), 3, "固定难度探针覆盖普通、精英和首领三种真实节点");
            check(dungeonStateError(State).empty(), "固定难度与独立人物境界的在途状态合法");
            const bool Loaded = loadFromJson(saveToJson(State), Restored, &Error);
            check(Loaded, "新固定难度秘境在途可真实存读档：" + Error);
            if (Grade == 4 && PlayerStage == 12) {
                const string EntrySave = saveToJson(State);
                for (const auto& Invalid : {json::Value(-1), json::Value(0), json::Value(11), json::Value(51),
                                           json::Value(.5), json::Value(true), json::Value("50"), json::Value{}}) {
                    for (const int TargetSide : {0, 1, 2}) {
                        auto Broken = json::Value::parse(EntrySave);
                        auto& DungeonObject = Broken.asObject().at("dungeons");
                        auto& RunObject = DungeonObject.asObject().at("run");
                        if (TargetSide != 0) RunObject.set("targetRewardCount", Invalid);
                        if (TargetSide != 1)
                            for (auto& SiteObject : DungeonObject.asObject().at("sites").asArray())
                                if (SiteObject["id"].asString() == RunObject["siteId"].asString()) SiteObject.set("targetRewardCount", Invalid);
                        const auto BeforeLoad = saveToJson(Restored);
                        check(!loadFromJson(Broken.dump(), Restored, &Error) && saveToJson(Restored) == BeforeLoad,
                              "入口/在途奖励合同拒绝非法类型、越界或伪造11份且不覆盖目标存档");
                    }
                }
                auto Mismatch = json::Value::parse(EntrySave);
                Mismatch.asObject().at("dungeons").asObject().at("run").set("targetRewardCount", 10);
                const auto BeforeLoad = saveToJson(Restored);
                check(!loadFromJson(Mismatch.dump(), Restored, &Error) && saveToJson(Restored) == BeforeLoad,
                      "旧十份与新五十份虽各自合法，但入口与在途不能冻结成不同合同");
            }
            check(dungeonLeave(State).empty() && State.player.arts.materials[Quote.MaterialId] == 0,
                  "不通关直接退出固定入口不提前发足量材料");
        }
    }
    for (int Grade = 4; Grade <= 6; ++Grade) {
        State = mfBottleGame(); State.player.arts.GreenBottleGrade = Grade - 1;
        State.player.stage = State.player.highestStage = 12; State.player.lifespan = realmOf(12).life;
        const int ExpectedCount = ExpectedMaterialCounts[Grade - 4];
        const auto* Material = content::mfGreenBottleMaterialForGrade(Grade);
        check(Material && mfQuoteGreenBottleDungeon(State, Grade).Enabled, "对应前一瓶阶且满足境界后解锁专属秘境");
        const auto BeforeEntry = saveToJson(State); State.job = Job{};
        const auto Busy = saveToJson(State);
        check(!mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty() && saveToJson(State) == Busy, "忙碌时不生成专属入口");
        State.job.reset();
        check(mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty() && State.dungeons.run && State.dungeons.run->type == "bottle", "真实建立爬塔而非直接发材料");
        check(State.player.arts.materials[Material->Material.id] == 0, "入场不提前发专属材料");
        check(dungeonStateError(State).empty(), "专属秘境初始状态合法");
        const bool NewEntryLoaded = loadFromJson(saveToJson(State), Restored, &Error);
        check(NewEntryLoaded, "专属秘境在途状态可存读档：" + Error);
        if (Grade == 4) {
            Game LegacyInProgress;
            const bool LegacyLoaded = loadFromJson(mfLegacyBottleSave(State).dump(), LegacyInProgress, &Error);
            check(LegacyLoaded, "旧未通关在途档缺奖励字段按十份继续：" + Error);
            if (LegacyLoaded && LegacyInProgress.dungeons.run) {
                checkEq(LegacyInProgress.dungeons.run->TargetRewardCount, 10, "旧在途不会因新报价被补升到50份合同");
                const bool LegacyRouteCompleted = mfCompleteBottleRoute(LegacyInProgress);
                check(LegacyRouteCompleted, "旧未通关秘境仍可按原路线完成");
                if (LegacyRouteCompleted) {
                    checkEq(LegacyInProgress.dungeons.run->materials[Material->Material.id], 10, "旧在途首领仍兑现原十份承诺");
                    check(dungeonLeave(LegacyInProgress).empty() && LegacyInProgress.player.arts.materials[Material->Material.id] == 10,
                          "旧在途真实离境仍只带回十份");
                }
            }
        }
        const bool RouteCompleted = mfCompleteBottleRoute(State);
        check(RouteCompleted, "通过真实路线/领奖和战斗收口单元边界完成秘境");
        if (!RouteCompleted) continue;
        checkEq(State.dungeons.run->materials[Material->Material.id], ExpectedCount, "首领一次发够升阶所需50/100/200份进入探索行囊");
        check(dungeonStateError(State).empty(), "完成后专属目标/奖励记录一致");
        const auto Completed = saveToJson(State);
        Game LegacyCompleted;
        const bool LegacyLoaded = loadFromJson(mfLegacyBottleSave(State).dump(), LegacyCompleted, &Error);
        check(LegacyLoaded, "旧已通关档保留原冻结十份可合法载入：" + Error);
        if (LegacyLoaded && LegacyCompleted.dungeons.run) {
            checkEq(LegacyCompleted.dungeons.run->TargetRewardCount, 10, "旧已通关档缺数量字段仍默认十份");
            checkEq(LegacyCompleted.dungeons.run->materials[Material->Material.id], 10, "读旧档不凭空补足本次新增升阶材料");
            LegacyCompleted.player.arts.materials[Material->Material.id] = StateLimits::MaxPersistedInteger - 10;
            check(dungeonStateError(LegacyCompleted).empty(), "旧十份合同入库容量仍按十份检查");
            check(dungeonLeave(LegacyCompleted).empty(), "旧已通关档真实离境只兑现原十份");
            checkEq(LegacyCompleted.player.arts.materials[Material->Material.id], StateLimits::MaxPersistedInteger,
                    "旧十份容量边界不被新50/100/200份要求阻断");
        }
        ++State.dungeons.run->materials[Material->Material.id];
        check(!dungeonStateError(State).empty(), "专属秘境保底不能伪造为超过本次冻结数量");
        --State.dungeons.run->materials[Material->Material.id];
        check(saveToJson(State) == Completed, "专属冻结奖励白名单校验不修改奖励");
        check(dungeonLeave(State).empty() && State.player.arts.materials[Material->Material.id] == ExpectedCount, "离境真实一次带回完整升阶灵材");
        check(!dungeonLeave(State).empty() && State.player.arts.materials[Material->Material.id] == ExpectedCount, "不可重复离境领材料");
        State.job.reset(); // 隔离下一次入口夹具，完整探索耗时的日历路径另由既有秘境回归覆盖。
        const auto UpgradeQuote = mfQuoteGreenBottleUpgrade(State);
        check(UpgradeQuote.Enabled && UpgradeQuote.Materials.size() == 1 && UpgradeQuote.Materials.front().Required == ExpectedCount && UpgradeQuote.Materials.front().Missing == 0,
              "通关一次后下一阶报价材料完整，不需要反复探索补十份");
        check(mfUpgradeGreenBottle(State, Grade - 1).empty(), "单次通关所得材料可直接用于真实升阶");
        checkEq(State.player.arts.GreenBottleGrade, Grade, "一次通关即可恢复对应下一瓶阶");
        checkEq(State.player.arts.materials[Material->Material.id], 0, "一次通关所得完整材料被升阶精确消耗");
        const auto Upgraded = saveToJson(State);
        check(!mfUpgradeGreenBottle(State, Grade - 1).empty() && saveToJson(State) == Upgraded, "通关后升级重复点击仍拒绝原子");
        check(mfEnterGreenBottleDungeon(State, Grade, Grade).empty(), "升级后仍可通过稳定入口开新一轮真实秘境");
        check(dungeonLeave(State).empty() && State.player.arts.materials[Material->Material.id] == 0, "未通关离境不给50/100/200份专属材料");
        const auto Gear = artForgeInfo(State.player, Material->Material.id, "iron", "jade", "sword");
        check(Gear.valid && mfNormalMaterialBuyPrice(State, MaterialPriceKind::ForgeMaterial, Material->Material.id) > 0, "专属灵材可真实参与炼器并有统一参考报价");
        const auto Market = play::marketList(State);
        check(std::none_of(Market.begin(), Market.end(), [&](const auto& Item) { return Item.id == Material->Material.id; }), "专属灵材不进入普通坊市采购池");
        static_cast<void>(BeforeEntry);
    }
}
}
