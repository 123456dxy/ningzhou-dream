#include "harness.h"
#include "core/crafting/green_bottle.h"
#include "core/crafting/garden_rules.h"
#include "core/crafting/cave_garden.h"
#include "core/content/green_bottle_content.h"
#include "core/interface/green_bottle_api.h"
#include "core/crafting/craft_prices.h"
#include "core/foundation/state_limits.h"
#include "core/play.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/dungeon.h"
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
}
// 用途：验证冻结成本/周期、逐阶、白名单秘境、API原子性及持久字段；流程：本轮定向回归；返回：harness记录。
void mfRunGreenBottleTests() {
    using namespace nz; using namespace nz::crafting;
    std::printf("\n--- 小绿瓶永久品阶与专属秘境 ---\n");
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
    State = mfBottleGame(); State.eventSeq = StateLimits::MaxPersistedInteger;
    const auto FullJournal = saveToJson(State);
    check(!mfQuoteGreenBottleUpgrade(State).Enabled && !mfUpgradeGreenBottle(State, 0).empty() && saveToJson(State) == FullJournal,
        "纪事编号满时升级报价/执行均拒绝，不扣费或越持久上限");
    State.player.arts.GreenBottleGrade = 3;
    const auto FullDungeonJournal = saveToJson(State);
    check(!mfQuoteGreenBottleDungeon(State, 4).Enabled && !mfEnterGreenBottleDungeon(State, 4, 3).empty() && saveToJson(State) == FullDungeonJournal,
        "纪事编号满时开境报价/执行均拒绝，不消耗入口编号");
    State = mfBottleGame(); State.player.arts.GreenBottleGrade = 3;
    const std::string CapacityMaterial = content::mfGreenBottleMaterialForGrade(4)->Material.id;
    State.player.arts.materials[CapacityMaterial] = StateLimits::MaxPersistedInteger - 5;
    const auto NoCapacity = saveToJson(State);
    check(!mfQuoteGreenBottleDungeon(State, 4).Enabled && !mfEnterGreenBottleDungeon(State, 4, 3).empty() && saveToJson(State) == NoCapacity,
        "无法容纳完整十份时入口报价与执行一致拒绝");
    State.player.arts.materials[CapacityMaterial] = StateLimits::MaxPersistedInteger - 10;
    check(mfEnterGreenBottleDungeon(State, 4, 3).empty(), "恰好容纳十份时可进入");
    check(mfCompleteBottleRoute(State), "容量边界样本沿真实路线完成");
    State.player.arts.materials[CapacityMaterial] = StateLimits::MaxPersistedInteger - 5;
    const auto BadReturn = saveToJson(State);
    check(!dungeonStateError(State).empty(), "专属秘境在途保存拒绝无法完整兑现的目标库存");
    check(!dungeonLeave(State).empty() && saveToJson(State) == BadReturn, "离境再次校验容量，拒绝不部分发奖或清除在途探索");
    State.player.arts.materials[CapacityMaterial] = StateLimits::MaxPersistedInteger - 10;
    check(dungeonLeave(State).empty(), "恢复容量后可重试真实离境");
    checkEq(State.player.arts.materials[CapacityMaterial], StateLimits::MaxPersistedInteger, "真实完整十份刚好到持久库存上限");
    State = mfBottleGame();
    for (const auto& Input : {json::Value(json::Object{}), json::Value(json::Object{{"expectedGrade", true}}),
        json::Value(json::Object{{"expectedGrade", 1.5}}), json::Value(json::Object{{"expectedGrade", 0},{"grade", 6}})}) {
        const auto Before = saveToJson(State);
        check(!api::mfExecuteGreenBottleCommand(State, api::RouteId::CaveGreenBottleUpgrade, Input).empty() && saveToJson(State) == Before, "独立API严格校验必填整数与未知键，拒绝原子");
    }
    for (int Grade = 4; Grade <= 6; ++Grade) {
        State = mfBottleGame(); State.player.arts.GreenBottleGrade = Grade - 1;
        const auto* Material = content::mfGreenBottleMaterialForGrade(Grade);
        check(Material && mfQuoteGreenBottleDungeon(State, Grade).Enabled, "对应前一瓶阶解锁灵材秘境，无人物境界硬锁");
        const auto BeforeEntry = saveToJson(State); State.job = Job{};
        const auto Busy = saveToJson(State);
        check(!mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty() && saveToJson(State) == Busy, "忙碌时不生成专属入口");
        State.job.reset();
        check(mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty() && State.dungeons.run && State.dungeons.run->type == "bottle", "真实建立爬塔而非直接发材料");
        check(State.player.arts.materials[Material->Material.id] == 0, "入场不提前发专属材料");
        check(dungeonStateError(State).empty(), "专属秘境初始状态合法");
        check(loadFromJson(saveToJson(State), Restored, &Error), "专属秘境在途状态可存读档");
        check(mfCompleteBottleRoute(State), "通过真实路线/领奖和战斗收口单元边界完成秘境");
        checkEq(State.dungeons.run->materials[Material->Material.id], 10, "首领保底10份进入探索行囊");
        check(dungeonStateError(State).empty(), "完成后专属目标/奖励记录一致");
        const auto Completed = saveToJson(State);
        ++State.dungeons.run->materials[Material->Material.id];
        check(!dungeonStateError(State).empty(), "专属秘境保底不能伪造为多于十份");
        --State.dungeons.run->materials[Material->Material.id];
        check(saveToJson(State) == Completed, "专属十份白名单验证保持原奖励");
        check(dungeonLeave(State).empty() && State.player.arts.materials[Material->Material.id] == 10, "离境真实带回10份专属灵材");
        check(!dungeonLeave(State).empty() && State.player.arts.materials[Material->Material.id] == 10, "不可重复离境领材料");
        State.job.reset(); // 隔离下一次入口夹具，完整探索耗时的日历路径另由既有秘境回归覆盖。
        check(mfEnterGreenBottleDungeon(State, Grade, Grade - 1).empty(), "同一稳定入口选项可开新一轮真实秘境");
        check(dungeonLeave(State).empty() && State.player.arts.materials[Material->Material.id] == 10, "未通关离境没有额外专属材料");
        const auto Gear = artForgeInfo(State.player, Material->Material.id, "iron", "jade", "sword");
        check(Gear.valid && mfNormalMaterialBuyPrice(State, MaterialPriceKind::ForgeMaterial, Material->Material.id) > 0, "专属灵材可真实参与炼器并有统一参考报价");
        const auto Market = play::marketList(State);
        check(std::none_of(Market.begin(), Market.end(), [&](const auto& Item) { return Item.id == Material->Material.id; }), "专属灵材不进入普通坊市采购池");
        static_cast<void>(BeforeEntry);
    }
}
}
