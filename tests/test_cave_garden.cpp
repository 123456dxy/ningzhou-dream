#include "harness.h"
#include "core/crafting/cave_garden.h"
#include "core/calendar.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/talents.h"
#include "core/json.h"

namespace nztest {
namespace {
// 用途：隔离灵田夹具；流程：种植/日历/读档回归；返回：在洞府、有真实药材的独立Game，不使用玩家账号。
nz::Game mfGardenTestGame() {
    nz::CreateOptions Options; Options.traitsExplicit = true; Options.seed = 3142;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear();
    State.player.cave = "wuling"; State.player.location = "wuling";
    State.player.traits.clear(); State.player.stones = 100000;
    State.player.herbs = 100; State.player.arts.herbs = {{"银月花", 100}};
    return State;
}
// 用途：推进独立世界日并测试日tick；流程：后台自然/收费生长；返回：无，保持日历年月年龄一致。
void mfGardenTestDay(nz::Game& State) {
    ++State.clock.days; State.year = State.clock.days / 360;
    State.player.age = State.clock.ageBase + State.year;
    nz::crafting::mfGardenDayTick(State);
}
}
// 用途：验证批次、储备、离家生长、纯预览和存档原子性；流程：核心回归；返回：无，失败记测试计数。
void mfRunCaveGardenTests() {
    using namespace nz; using namespace nz::crafting;
    std::printf("\n--- 洞府灵田批次与自动储备 ---\n");
    Game State = mfGardenTestGame();
    const string Fresh = saveToJson(State);
    const auto FreshView = mfGardenInfo(State);
    checkEq(FreshView.PlotCountUnlocked, 3, "寻常洞府解锁三格灵田");
    check(saveToJson(State) == Fresh, "读取灵田不初始化/生长/改RNG");
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 99).empty(), "同格可播种已有草药99份");
    checkEq(State.player.herbs, 1, "播种精确扣真实药材总量");
    checkEq(State.Garden.Plots[0].Cohorts[0].Count, 99, "种苗持久化为真实批次");
    const string Full = saveToJson(State);
    check(!mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "满99种苗不能补第100份");
    check(!mfPlantGardenHerbs(State, "plot-1", "8001", 1).empty(), "妖丹不能种植");
    check(!mfPlantGardenHerbs(State, "plot-8", "6001", 1).empty(), "未解锁地块不能播种");
    check(!mfDepositGardenReserve(State, "plot-1", -1).empty(), "负储备转账拒绝");
    check(saveToJson(State) == Full, "非法种植/储备均原子不变");
    check(!mfQuoteGardenReserve(State, "plot-1", 1000, true).Enabled, "无小绿瓶存入报价不可用");
    check(!mfDepositGardenReserve(State, "plot-1", 1000).empty(), "无小绿瓶真实存入拒绝");
    check(saveToJson(State) == Full, "无小绿瓶存入拒绝保留整档时间RNG及资源");
    State.player.traits = {"greenBottle"};
    check(mfDepositGardenReserve(State, "plot-1", 1000).empty(), "有小绿瓶格内储备从背包明确存入");
    State.player.location = "yifeng";
    const int Backpack = State.player.stones;
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 300, "离家绿瓶自动100+200成长");
    checkEq(State.Garden.Plots[0].ReserveStones, 901, "99份一阶种苗只扣格内99灵石");
    checkEq(State.player.stones, Backpack, "后台成长不偷扣主背包灵石");
    const string Once = saveToJson(State); mfGardenDayTick(State);
    check(saveToJson(State) == Once, "同一世界日重复tick不重复扣费或生长");
    const auto OffsiteView = mfGardenInfo(State);
    check(!OffsiteView.AccessReason.empty(), "外出可看进度但管理动作禁用");
    checkEq(OffsiteView.Plots[0].BottleDailyCostStones, 99, "权威视图明确绿瓶日费99");
    State.Garden.Plots[0].ReserveStones = 98;
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 400, "储备不足全额时仅自然成长100");
    checkEq(State.Garden.Plots[0].ReserveStones, 98, "不足全额不部分扣储备");
    State.player.location = "wuling";
    check(mfClearGardenPlot(State, "plot-1").empty(), "清理种苗有明确动作");
    check(State.Garden.Plots[0].Cohorts.empty(), "清理后不能收获已放弃苗");
    checkEq(State.Garden.Plots[0].ReserveStones, 98, "清理不删除储备灵石");
    State.player.location = "yifeng";
    const string BeforeMoveQuote = saveToJson(State);
    const auto MoveQuote = shop::mfQuoteCaveMove(State);
    check(!MoveQuote.enabled && MoveQuote.reason.find("灵田") != string::npos, "有储备迁府报价明确被灵田阻断");
    check(!shop::caveMove(State).ok, "有储备迁府被拒绝");
    check(saveToJson(State) == BeforeMoveQuote, "迁府报价和拒绝执行都不修改状态");
    State.player.location = "wuling";
    check(mfWithdrawGardenReserve(State, "plot-1", 98).empty(), "可取回全部格内储备");
    State.player.location = "yifeng";
    check(shop::mfQuoteCaveMove(State).enabled, "空灵田且储备清零后可迁府");
    const int MovePrice = shop::mfQuoteCaveMove(State).costStones;
    const int MoveStones = State.player.stones;
    check(shop::caveMove(State).ok, "符合报价的迁府真实成功");
    checkEq(State.player.stones, MoveStones - MovePrice, "迁府真实扣款与报价一致");
    checkStr(State.player.cave, "yifeng", "迁府仍用合法地图地点ID");

    State = mfGardenTestGame();
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "首批种苗播种");
    for (int Day = 0; Day < 10; ++Day) mfGardenTestDay(State);
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "同格补种独立新批次");
    checkEq(static_cast<int>(State.Garden.Plots[0].Cohorts.size()), 2, "不同播种日保存两批");
    checkEq(State.Garden.Plots[0].Cohorts[1].ProgressUnits, 0, "新批次不能继承旧10天成长");
    for (int Day = 0; Day < 20; ++Day) mfGardenTestDay(State);
    const auto Partial = mfGardenInfo(State).Plots[0];
    checkEq(Partial.ReadyCount, 1, "先播一批30天成熟");
    checkEq(Partial.GrowingCount, 1, "补播批次仍需10天");
    checkEq(Partial.Harvest.HarvestCount, 3, "部分收获只计算成熟种苗乘3");
    State.player.location = "yifeng";
    const string BeforeOffsite = saveToJson(State);
    const auto OffsiteReady = mfGardenInfo(State).Plots[0];
    checkEq(OffsiteReady.Harvest.HarvestCount, 3, "外出只读页仍显示真实成熟可收3份");
    checkEq(OffsiteReady.Clear.DiscardCount, 2, "外出只读页仍显示清理将损失2份种苗");
    check(!OffsiteReady.Harvest.Enabled && !OffsiteReady.Clear.Enabled, "外出可读数量但收获清理被禁用");
    check(!mfHarvestGardenHerbs(State, "plot-1").empty(), "外出不能远程收获");
    check(saveToJson(State) == BeforeOffsite, "外出成熟预览及拒绝收获不改状态");
    State.player.location = "wuling";
    const int HerbsBefore = State.player.herbs;
    check(mfHarvestGardenHerbs(State, "plot-1").empty(), "可以单独收获成熟批次");
    checkEq(State.player.herbs, HerbsBefore + 3, "实际部分收获与预览一致");
    checkEq(static_cast<int>(State.Garden.Plots[0].Cohorts.size()), 1, "未成熟新批次保留");
    const string Harvested = saveToJson(State);
    check(!mfHarvestGardenHerbs(State, "plot-1").empty(), "不能重复领取同一成熟批次");
    check(saveToJson(State) == Harvested, "重复收获拒绝不改库存");
    Game Restored; string Error;
    check(loadFromJson(Harvested, Restored, &Error), "灵田批次存读档有效：" + Error);
    check(saveToJson(Restored) == Harvested, "现代灵田存读档精确保留状态");
    Game HomeLess = State;
    HomeLess.player.cave.clear();
    const string HomeLessSave = saveToJson(HomeLess);
    const string BeforeInvalidLoad = saveToJson(Restored);
    check(!mfGardenStateError(HomeLess).empty(), "有种苗却没有洞府的持久状态明确拒绝");
    check(saveToJson(HomeLess) == HomeLessSave, "洞府归属语义检查纯只读不修补输入");
    check(!loadFromJson(HomeLessSave, Restored, &Error), "缺洞府但有种苗的存档拒绝导入以免管理死锁");
    check(saveToJson(Restored) == BeforeInvalidLoad, "非法洞府归属导入不替换当前合法存档");
    Game ReserveOnly = mfGardenTestGame();
    ReserveOnly.player.cave.clear(); ReserveOnly.Garden.Plots[0].ReserveStones = 1;
    check(!loadFromJson(saveToJson(ReserveOnly), Restored, &Error), "没有洞府但只有储备的存档同样拒绝导入");
    Game LockedReserve = mfGardenTestGame();
    LockedReserve.Garden.Plots[7].ReserveStones = 1;
    const string LockedSave = saveToJson(LockedReserve);
    check(!mfGardenStateError(LockedReserve).empty(), "未解锁空格有储备也违反状态约束");
    check(saveToJson(LockedReserve) == LockedSave, "未解锁格储备检查不修改输入");
    check(!loadFromJson(LockedSave, Restored, &Error), "未解锁格藏灵石的存档拒绝导入");
    check(saveToJson(Restored) == BeforeInvalidLoad, "非法格内储备导入仍保留当前合法存档");
    auto Corrupt = json::Value::parse(Harvested);
    Corrupt.asObject()["garden"].asObject()["plots"].asArray()[0].set("herbId", json::Value("missing"));
    check(!loadFromJson(Corrupt.dump(), Restored, &Error), "非法种植ID拒绝导入");
    auto Old = json::Value::parse(Harvested); Old.asObject().erase("garden");
    check(loadFromJson(Old.dump(), Restored, &Error), "旧缺灵田字段存档可加载");
    check(Restored.Garden.Initialized && Restored.Garden.Plots.size() == 8, "旧档初始化空格而不补发历史药材");

    State = mfGardenTestGame(); State.player.qiSupply = 1;
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "无瓶灵气配给成长夹具");
    State.Garden.Plots[0].ReserveStones = 20;
    const string BeforeNoBottleView = saveToJson(State);
    const auto NoBottleView = mfGardenInfo(State);
    check(!NoBottleView.HasGreenBottle && !NoBottleView.Plots[0].AccelerationFunded,
          "无瓶不启用付费自动加速");
    checkEq(NoBottleView.Plots[0].DailyCostStones, 0, "无瓶灵田不产生储备日费");
    checkEq(NoBottleView.Plots[0].SpiritUnitsPerDay, 100, "无瓶引灵方案不提供免费灵气增量");
    checkEq(NoBottleView.Plots[0].FullyAcceleratedUnitsPerDay, 100, "无瓶完整成长速度仍为自然值");
    checkEq(NoBottleView.Plots[0].EffectiveUnitsPerDay, 100, "无瓶仅按自然成长，不读取未付款配给加成");
    checkEq(NoBottleView.Plots[0].EstimatedEffectiveDays, 30, "无瓶实际预报按自然成长30天");
    check(saveToJson(State) == BeforeNoBottleView, "无瓶预报不消耗旧储备或修改状态");
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 100, "无瓶真实日不因配置灵气档位免费加速");
    checkEq(State.Garden.Plots[0].ReserveStones, 20, "无瓶真实日不消耗残留储备");

    State = mfGardenTestGame(); State.player.caveTier = 2; State.player.qiSupply = 3;
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "高档洞府无瓶自然成长夹具");
    State.player.location = "yifeng"; State.player.stones = 0;
    const auto OutsideNoBottle = mfGardenInfo(State);
    checkEq(OutsideNoBottle.Plots[0].EffectiveUnitsPerDay, 120, "无瓶外出无钱仅保留两档洞府自然加成");
    checkEq(OutsideNoBottle.Plots[0].EstimatedEffectiveDays, 25, "无瓶高档洞府预报不计未付费的三档灵气");
    State.player.traits = {"greenBottle"};
    checkEq(mfGardenInfo(State).Plots[0].EffectiveUnitsPerDay, 120, "有瓶空储备与无瓶享有相同自然成长");
    State.player.traits.clear(); mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 120, "外出无瓶实际结算与自然成长预报一致");

    State = mfGardenTestGame(); State.player.qiSupply = 1;
    State.player.traits = {"greenBottle"};
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "引灵和绿瓶共同报价夹具");
    check(mfDepositGardenReserve(State, "plot-1", 2).empty(), "存入恰好一天组合加速费用");
    checkEq(mfGardenInfo(State).Plots[0].EstimatedEffectiveDays, 28,
            "预计实际成熟时间考虑两灵石只够加速一天，后续自然成长");
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 330, "引灵一档和绿瓶成长100+30+200");
    checkEq(State.Garden.Plots[0].ReserveStones, 0, "组合加速费用一天恰好2灵石");
    State.Garden.Plots[0].Cohorts[0].ProgressUnits = 2900;
    check(mfDepositGardenReserve(State, "plot-1", 20).empty(), "临成熟可保留储备");
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 3000, "自然足够成熟当日封顶");
    checkEq(State.Garden.Plots[0].ReserveStones, 20, "自然已足够成熟不收加速费用");
    mfGardenTestDay(State);
    checkEq(State.Garden.Plots[0].ReserveStones, 20, "已成熟批次后续天数不收费");
    checkEq(State.player.herbs, 99, "成熟不自动入库无限产出");
    State = mfGardenTestGame();
    check(mfPlantGardenHerbs(State, "plot-1", "6001", 1).empty(), "真实日历生长夹具播种");
    Job Rest; Rest.actionType = "plan"; Rest.plan = "rest"; Rest.name = "休养";
    Rest.total = Rest.remaining = 1; Rest.started = State.clock.days; Rest.paid = true;
    State.job = Rest;
    const auto DayRun = calendarRun(State, 1);
    check(DayRun.ok && State.clock.days == 1, "世界日历真正推进一天");
    checkEq(State.Garden.Plots[0].Cohorts[0].ProgressUnits, 100, "真实日历自动接入灵田日tick");
}
} // namespace nztest
