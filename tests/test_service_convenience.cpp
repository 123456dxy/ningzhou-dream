#include "harness.h"
#include "core/api.h"
#include "core/interface/views.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/society.h"
#include "core/exploration/service_navigation.h"
#include "core/interface/service_views.h"
#include "core/foundation/state_limits.h"
#include "core/travel.h"
#include "core/crafting/material_supply.h"
#include "data/reference_tables.h"

namespace nztest {
namespace {
using namespace nz;
// 用途：创建便利功能隔离角色；参与远程俸禄和服务路线回归；返回固定种子且无进行中工作的副本。
Game mfServiceFixture() {
    CreateOptions Options;
    Options.name = "便利功能验收";
    Options.seed = 20261014;
    Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.pending.reset();
    State.job.reset();
    State.player.arts.battle.reset();
    State.player.location = "zhushan";
    State.player.sect = "jinhong";
    State.player.stones = 1000;
    State.bonds.stipend = -1;
    State.bonds.payroll.sect = "jinhong";
    State.bonds.payroll.balance = 100;
    State.world.sects.at("jinhong").treasury = 60;
    return State;
}
}
// 用途：验证真实状态协议和异地领取结算；参与定向回归；返回无，失败记入自测计数。
void mfRunServiceConvenienceTests() {
    using namespace nz;
    Game State = mfServiceFixture();
    const auto Before = saveToJson(State);
    const auto View = api::view::state(State);
    check(View["bonds"]["stipendAction"]["enabled"].asBool(), "异地俸禄报价允许领取");
    checkEq(static_cast<int>(View["bonds"]["stipendAction"]["paid"].asNumber()), 60, "俸禄报价使用真实府库");
    check(View["serviceRoutes"]["market"]["enabled"].asBool(), "山门可导航最近坊市");
    check(View["serviceRoutes"]["intel"]["destination"].asString() == "tianji", "情报导航目的地稳定ID");
    checkStr(saveToJson(State), Before, "便利查询不写状态或共享RNG");
    const auto Result = socCollectStipend(State);
    check(Result.ok, "外出可以领取宗门俸禄");
    checkEq(Result.paid, 60, "真实实付与报价一致");
    checkEq(Result.owed, 40, "保留未付俸禄");
    checkEq(State.player.stones, 1060, "异地俸禄确实增加灵石");
    const auto PaidState = saveToJson(State);
    check(!socCollectStipend(State).ok, "同月重复领取拒绝");
    checkStr(saveToJson(State), PaidState, "重复领取不部分修改");
    {
        Game EmptyTreasury = mfServiceFixture();
        EmptyTreasury.world.sects.at("jinhong").treasury = 0;
        const auto BeforeEmpty = saveToJson(EmptyTreasury);
        check(!socCollectStipend(EmptyTreasury).ok, "府库空拒绝零支付");
        checkStr(saveToJson(EmptyTreasury), BeforeEmpty, "零支付不消耗月额");
        EmptyTreasury.world.sects.at("jinhong").treasury = 100;
        checkEq(socCollectStipend(EmptyTreasury).paid, 100, "同月府库恢复后仍能领取");
    }
    for (int CaseIndex = 0; CaseIndex < 8; ++CaseIndex) {
        Game Invalid = mfServiceFixture();
        if (CaseIndex == 0) Invalid.job = Job{};
        if (CaseIndex == 1) Invalid.pending = Pending{};
        if (CaseIndex == 2) Invalid.dungeons.run = DungeonRun{};
        if (CaseIndex == 3) Invalid.player.arts.battle = Battle{};
        if (CaseIndex == 4) Invalid.player.stones = StateLimits::MaxPersistedInteger - 10;
        if (CaseIndex == 5) Invalid.eventSeq = StateLimits::MaxPersistedInteger;
        if (CaseIndex == 6) Invalid.player.sect = "unknown-sect";
        if (CaseIndex == 7) Invalid.bonds.payroll.sect = "zhushan";
        const auto BeforeInvalid = saveToJson(Invalid);
        check(!mfQuoteStipend(Invalid).Enabled && !socCollectStipend(Invalid).ok,
            "俸禄非法/阻塞情形" + std::to_string(CaseIndex) + "一致拒绝");
        checkStr(saveToJson(Invalid), BeforeInvalid, "拒绝保持完整原状态");
    }
    {
        Game RouteState = mfServiceFixture();
        auto Quote = Exploration::mfQuoteServiceRoute(RouteState, Exploration::ServiceDestination::MaterialMarket);
        check(Quote.Enabled && Quote.Destination == "guangling", "竹山宗选择最近实际五城坊市广陵城");
        const auto Plan = travelPlan(RouteState, Quote.Destination);
        const auto* Option = travelOption(Plan, Quote.Mode);
        check(Option && Option->enabled && Option->days == Quote.Days && Option->cost == Quote.Cost,
            "导航完全复用实际交通报价");
        const int BeforeStones = RouteState.player.stones;
        check(travelBegin(RouteState, Quote.Destination, Quote.Mode).empty(), "推荐路线真实创建行程");
        check(RouteState.job && RouteState.job->destination == Quote.Destination && RouteState.job->remaining == Quote.Days,
            "导航没有直接传送或跳过时间");
        checkEq(RouteState.player.stones, BeforeStones - Quote.Cost, "真实行程费用与导航一致");
        check(!Exploration::mfQuoteServiceRoute(RouteState, Exploration::ServiceDestination::Intel).Enabled,
            "已有行程不创建第二次导航");
        RouteState.job.reset();
        RouteState.player.location = "wuling";
        Quote = Exploration::mfQuoteServiceRoute(RouteState, Exploration::ServiceDestination::MaterialMarket);
        check(Quote.AlreadyHere && !Quote.Enabled && Quote.Destination == "wuling", "已在坊市不重复赶路");
        RouteState.player.location = "zhushan";
        RouteState.player.stones = 0;
        Quote = Exploration::mfQuoteServiceRoute(RouteState, Exploration::ServiceDestination::MaterialMarket);
        check(Quote.Enabled && Quote.Mode == "walk" && Quote.Cost == 0, "余额不足仍可陆路步行");
        RouteState.player.location = "sea";
        check(!Exploration::mfQuoteServiceRoute(RouteState, Exploration::ServiceDestination::Intel).Enabled,
            "海路无免费步行时明确拒绝");
        RouteState.player.location = "zhushan";
        RouteState.player.stones = 1000;
        RouteState.dungeons.run = DungeonRun{};
        check(!Exploration::mfQuoteServiceRoute(RouteState, Exploration::ServiceDestination::Intel).Enabled,
            "秘境内服务导航拒绝");
    }
    {
        Game SupplyState = mfServiceFixture();
        SupplyState.player.arts.herbs.clear();
        SupplyState.player.herbs = 0;
        const auto& Recipe = data::RecipeDef_TABLE().front();
        const auto BeforeSupply = saveToJson(SupplyState);
        const auto Materials = crafting::materialQuote(SupplyState, "brew", Recipe.id, 1);
        auto Route = Exploration::mfQuoteMaterialRoute(SupplyState, Materials, "brew");
        check(Route.Enabled && Route.TargetTab == "workshop" && Route.TargetSub == "brew", "缺少低阶药材推荐城市并回炼丹页：" + Route.Reason);
        checkStr(saveToJson(SupplyState), BeforeSupply, "补料路线查询不修改任何资源");
        crafting::MaterialQuote Complete;
        Complete.valid = true;
        check(!Exploration::mfQuoteMaterialRoute(SupplyState, Complete, "brew").Enabled, "材料完整不多余导航");
        check(!Exploration::mfQuoteMaterialRoute(SupplyState, Materials, "unknown").Enabled, "未知制作类别拒绝");
        for (const auto& Herb : data::HerbDef_TABLE()) if (Herb.grade == 3) {
            crafting::MaterialQuote HighGrade;
            HighGrade.valid = true;
            crafting::MaterialRow Row;
            Row.kind = "herb";
            Row.id = Herb.id;
            Row.missing = 1;
            HighGrade.materials.push_back(Row);
            Route = Exploration::mfQuoteMaterialRoute(SupplyState, HighGrade, "brew");
            check(Route.Enabled && Route.Destination == "tianji" && Route.TargetSub == "intel", "高阶缺料明确引导情报，不冒充城市直购");
            break;
        }
    }
    {
        Game Saved = mfServiceFixture();
        Game Labels = mfServiceFixture();
        Labels.npcs.clear();
        for (int PersonIndex = 0; PersonIndex < 3; ++PersonIndex) {
            Npc Person;
            Person.id = "npc-filter-" + std::to_string(PersonIndex);
            Person.name = "分类仙友" + std::to_string(PersonIndex);
            Person.age = 16;
            Person.lifespan = 500;
            Person.health = 100;
            Person.alive = true;
            Person.stage = 6;
            Person.location = Labels.player.location;
            Person.dao = PersonIndex == 0 ? "神" : PersonIndex == 1 ? "木" : "气";
            if (PersonIndex == 1) Person.sect = "zhushan";
            Labels.npcs.push_back(Person);
        }
        const auto Nearby = api::view::state(Labels)["nearby"].asArray();
        checkEq(static_cast<int>(Nearby.size()), 3, "分类夹具显示三位实际仙友");
        for (const auto& Person : Nearby) {
            if (Person["id"].asString() == "npc-filter-0")
                checkStr(Person["buildId"].asString(), "mind-cycle", "神道不会因共享mind辅修被误分金虹");
            if (Person["id"].asString() == "npc-filter-1")
                checkStr(Person["buildId"].asString(), "wood-poison", "木宗长生主修按实际毒链归类");
            if (Person["id"].asString() == "npc-filter-2")
                checkStr(Person["buildId"].asString(), "dao-qi", "气道不会因共享zhou辅修被误分神识");
        }
        socCollectStipend(Saved);
        Game Loaded;
        string Error;
        check(loadFromJson(saveToJson(Saved), Loaded, &Error), "异地领取后既有存档格式可以读回" + Error);
        checkEq(Loaded.player.stones, Saved.player.stones, "存读档保留实付灵石");
        checkEq(Loaded.bonds.payroll.balance, Saved.bonds.payroll.balance, "存读档保留欠薪");
        check(!mfQuoteStipend(Loaded).Enabled, "重新读档不重复领取本月俸禄");
    }
}
}
