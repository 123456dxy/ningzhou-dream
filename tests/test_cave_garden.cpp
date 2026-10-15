#include "harness.h"
#include "core/crafting/cave_garden.h"
#include "core/crafting/garden_rules.h"
#include "core/crafting/green_bottle.h"
#include "core/calendar.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/json.h"

namespace nztest {
namespace {
// 用途：建立无玩家文件的真实洞府夹具；流程：园艺回归；返回：独立Game。
nz::Game mfGardenTestGame() {
    nz::CreateOptions Options; Options.traitsExplicit = true; Options.seed = 3142;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear();
    State.player.cave = "wuling"; State.player.location = "wuling";
    State.player.traits.clear(); State.player.talent.clear(); State.player.stones = 100000;
    State.player.herbs = 100; State.player.arts.herbs = {{"银月花", 100}};
    return State;
}
// 用途：推进真实世界日并保持日期一致；流程：日tick测试；返回：无，重复日可另调用验证。
void mfGardenTestDay(nz::Game& State) {
    ++State.clock.days; State.year = State.clock.days / 360;
    State.player.age = State.clock.ageBase + State.year;
    nz::crafting::mfGardenDayTick(State);
}
}
// 用途：验证新版永久日增、12苗、独立批次、迁移与只读；流程：定向回归；返回：harness记录。
void mfRunCaveGardenTests() {
    using namespace nz; using namespace nz::crafting;
    std::printf("\n--- 永久品阶灵田 ---\n");
    Game State = mfGardenTestGame();
    const auto Fresh = saveToJson(State);
    const auto FreshInfo = mfGardenInfo(State);
    checkEq(FreshInfo.PlantsPerPlotLimit, 12, "每格容量12份");
    checkEq(FreshInfo.HarvestMultiplier, 3, "成熟仍每份产出3份");
    check(saveToJson(State) == Fresh, "查询不初始化/返现/消耗RNG");
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 12).empty(), "真实种植12份");
    checkEq(State.player.herbs, 88, "真实扣除12种苗");
    const auto Full = saveToJson(State);
    check(!mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty() && saveToJson(State) == Full, "第13份原子拒绝");
    check(!mfPlantGardenHerbs(State, "plot-8", "6001", 1).empty(), "未解锁地块不可种");
    check(!mfPlantGardenHerbs(State, "plot-2", "8001", 1).empty(), "妖丹不可种植");
    check(!mfDepositGardenReserve(State, "plot-1", 10).empty() && !mfWithdrawGardenReserve(State, "plot-1", 10).empty(), "旧存取命令均明确拒绝");
    State.player.qiSupply = 1;
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 20, "寻常洞府日增20且不受灵气配给影响");
    State.player.traits = {"greenBottle"}; State.player.arts.GreenBottleGrade = 1;
    State.player.location = "jiuyi";
    const auto StonesBefore = State.player.stones;
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 85, "外出仍永久日增20+45，无日费");
    checkEq(State.player.stones, StonesBefore, "生长不扣背包灵石");
    const auto Once = saveToJson(State); mfGardenDayTick(State);
    check(saveToJson(State) == Once, "同日重复tick幂等");
    State.player.caveTier = 2; State.player.qiSupply = 3;
    const auto Info = mfGardenInfo(State).Plots[0];
    checkEq(Info.NaturalUnitsPerDay, 60, "聚灵仙府自然60");
    checkEq(Info.BottleBonusUnitsPerDay, 45, "一阶绿瓶附加45");
    checkEq(Info.EffectiveUnitsPerDay, 105, "合计日增105");
    checkEq(Info.RequiredUnits, 18000, "一品成熟18000点");
    check(!mfHarvestGardenHerbs(State, "plot-1").empty(), "外出不能远程收获");

    State = mfGardenTestGame();
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "第一批种苗");
    mfGardenTestDay(State);
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "追加独立批次");
    checkEq(State.Garden.Plots[0].Cohorts.size(), 2, "追加批次不继承进度");
    checkEq(State.Garden.Plots[0].Cohorts[1].ProgressUnits, 0, "新批次进度0");
    State.Garden.Plots[0].Cohorts[0].ProgressUnits = 17980;
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 18000, "成熟进度精确封顶");
    checkEq(mfQuoteGardenHarvest(State, "plot-1").HarvestCount, 3, "预览仅成熟批次产3份");
    check(mfHarvestGardenHerbs(State, "plot-1").empty(), "真实收获成熟批次");
    checkEq(State.Garden.Plots[0].Cohorts.size(), 1, "未成熟批次保留");
    const auto Harvested = saveToJson(State);
    check(!mfHarvestGardenHerbs(State, "plot-1").empty() && saveToJson(State) == Harvested, "同批奖励不可重复收获");
    Game Restored; std::string Error;
    check(loadFromJson(Harvested, Restored, &Error), "新版灵田持久化可加载");
    check(json::Value::parse(saveToJson(Restored))["garden"].dump() == json::Value::parse(Harvested)["garden"].dump(), "灵田批次和规则版本往返一致");

    auto Old = json::Value::parse(saveToJson(mfGardenTestGame()));
    auto& OldGarden = Old.asObject()["garden"];
    OldGarden.asObject().erase("rulesVersion"); OldGarden.asObject().erase("pendingHerbReturns");
    auto& OldPlot = OldGarden.asObject()["plots"].asArray()[0];
    OldPlot.asObject().erase("legacyReserveStones"); OldPlot.set("reserveStones", 100);
    OldPlot.set("herbId", "6001");
    json::Value OldBatch(json::Object{}); OldBatch.set("count", 99); OldBatch.set("progressUnits", 1500);
    OldPlot.set("cohorts", json::Array{OldBatch});
    const int OldStones = Old["player"]["stones"].asInt();
    check(loadFromJson(Old.dump(), Restored, &Error), "旧99苗/储备存档可归一化迁移");
    checkEq(Restored.Garden.Plots[0].Cohorts[0].Count, 12, "迁移后每格最多12份");
    checkEq(Restored.Garden.Plots[0].Cohorts[0].ProgressUnits, 9000, "旧半熟进度按比例保留");
    checkEq(Restored.Garden.PendingHerbReturns["银月花"], 87, "超量苗记待返，不丢弃");
    checkEq(Restored.player.stones, OldStones, "加载/纯查询不会返还旧储备");
    const auto PendingSnapshot = saveToJson(Restored); static_cast<void>(mfGardenInfo(Restored));
    check(saveToJson(Restored) == PendingSnapshot, "查询不兑现待返资产");
    const int HerbsBefore = Restored.player.herbs;
    mfGardenTestDay(Restored);
    checkEq(Restored.player.stones, OldStones + 100, "真实日退还旧储备一次");
    checkEq(Restored.player.herbs, HerbsBefore + 87, "真实日退还超量种苗一次");
    check(Restored.Garden.PendingHerbReturns.empty() && Restored.Garden.Plots[0].ReserveStones == 0, "待返记录真实清空");
    const auto Refunded = saveToJson(Restored); mfGardenDayTick(Restored);
    check(saveToJson(Restored) == Refunded, "重复日不重复退还");
    auto CorruptOld = Old;
    CorruptOld.asObject()["garden"].asObject()["plots"].asArray()[0].asObject()["cohorts"].asArray()[0].set("progressUnits", 3001);
    check(!loadFromJson(CorruptOld.dump(), Restored, &Error) && saveToJson(Restored) == Refunded, "旧一品超阈值进度不能借迁移封顶洗成合法成熟苗");
    auto CorruptPending = json::Value::parse(Refunded);
    CorruptPending.asObject()["garden"].set("pendingHerbReturns", json::Object{{"一阶妖丹·金",1}});
    check(!loadFromJson(CorruptPending.dump(), Restored, &Error) && saveToJson(Restored) == Refunded, "旧待返种苗不接受妖丹或未知引用");
    Restored.Garden.Plots[0].ReserveStones = 25;
    Restored.player.stones = 1000000000;
    mfGardenTestDay(Restored);
    checkEq(Restored.Garden.Plots[0].ReserveStones, 25, "灵石背包满时保留旧待返余额");
    Restored.player.stones -= 10;
    mfGardenTestDay(Restored);
    checkEq(Restored.player.stones, 1000000000, "旧待返只补足现有容量");
    checkEq(Restored.Garden.Plots[0].ReserveStones, 15, "无法退还的余款继续保留");
    auto Missing = json::Value::parse(Harvested); Missing.asObject().erase("garden");
    check(loadFromJson(Missing.dump(), Restored, &Error) && Restored.Garden.Initialized, "缺灵田字段使用合法空默认");
}
}
