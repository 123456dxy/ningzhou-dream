#include "harness.h"
#include "core/crafting/cave_garden.h"
#include "core/crafting/herb_acquisition.h"
#include "core/json.h"
#include "core/arts.h"
#include "core/save.h"
#include "core/sim.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace nztest {
namespace {
// 用途：保留优化前数字编码作为逐字节格式基线；流程：JSON响应/存档回归；返回：旧17位classic locale文本，无游戏状态或IO。
std::string mfLegacyNumberText(double Number) {
    if (!std::isfinite(Number)) return "null";
    std::ostringstream NumberStream;
    NumberStream.imbue(std::locale::classic());
    NumberStream << std::setprecision(std::numeric_limits<double>::max_digits10) << Number;
    return NumberStream.str();
}
// 用途：建立有多批次且最末一批未熟的灵田；流程：纯预报与真实日结算一致性；返回：隔离Game，合法品阶/种苗/储备，不读取玩家存档。
nz::Game mfGardenForecastFixture(const nz::data::HerbDef& Herb, int ReserveStones, bool HasBottle, int QiLevel) {
    nz::CreateOptions Options; Options.seed = 4271; Options.traitsExplicit = true;
    Options.lifeId = "time-performance-regression";
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear(); State.checkpoint = {};
    State.player.stage = 12; State.player.lifespan = 2000;
    State.player.cave = State.player.location = "wuling"; State.player.caveTier = 2;
    State.player.qiSupply = QiLevel; State.player.traits = HasBottle ? nz::vector<nz::string>{"greenBottle"} : nz::vector<nz::string>{};
    constexpr int BaseDays[] = {30, 60, 120, 240, 480, 960};
    auto& Plot = State.Garden.Plots[0]; Plot.HerbId = Herb.id; Plot.ReserveStones = ReserveStones;
    Plot.Cohorts = {{1, 0}, {1, BaseDays[Herb.grade - 1] * 100 - 100}, {3, 100}};
    return State;
}
}

// 用途：验证编码优化逐字节兼容及单日结算与成熟预报等价；流程：核心时间性能回归；返回：无，失败记入测试框架。无生产服务、账号或文件IO。
void mfRunTimePerformanceTests() {
    using namespace nz;
    using namespace nz::crafting;
    std::printf("\n--- 时间性能优化行为等价 ---\n");
    vector<double> Numbers = {0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 1.0 / 360.0,
        static_cast<double>(std::numeric_limits<int>::min()),
        static_cast<double>(std::numeric_limits<int>::max()),
        static_cast<double>(std::numeric_limits<int>::min()) - 1.0,
        static_cast<double>(std::numeric_limits<int>::max()) + 1.0,
        1e17, -1e17, 1e30, 1e-30, std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()};
    constexpr std::int64_t IntegerMinimum = std::numeric_limits<int>::min();
    constexpr std::int64_t IntegerSpan = static_cast<std::int64_t>(std::numeric_limits<int>::max()) - IntegerMinimum;
    for (int SampleIndex = 0; SampleIndex <= 128; ++SampleIndex)
        Numbers.push_back(static_cast<double>(IntegerMinimum + IntegerSpan * SampleIndex / 128));
    for (std::size_t NumberIndex = 0; NumberIndex < Numbers.size(); ++NumberIndex) {
        const auto Number = Numbers[NumberIndex];
        checkStr(json::Value(Number).dump(), mfLegacyNumberText(Number),
            "JSON数字新旧编码逐字节一致 " + std::to_string(NumberIndex));
    }
    json::Value Nested(json::Object{}); Nested.set("负零", -0.0);
    Nested.set("整数", json::Array{json::Value(30), json::Value(-2147483647), json::Value(1000000000)});
    Nested.set("余数", 1.0 / 360.0);
    std::string ParseError;
    const auto RoundTrip = json::Value::parse(Nested.dump(1), &ParseError);
    check(ParseError.empty() && RoundTrip.dump(1) == Nested.dump(1), "嵌套中文JSON缩进及负零/余数精度往返保持");
    check(std::signbit(RoundTrip["负零"].asNumber()), "精确整数快路保留负零符号");

    for (const auto& Definition : data::BaseManualDef_TABLE()) {
        ManualInfo Info;
        check(findManualInfo(Definition.id, Info), "不可变功法目录涵盖基础功法 " + Definition.id);
        check(Info.found && Info.id == Definition.id && Info.name == Definition.name && Info.dao == Definition.dao &&
              Info.stage == Definition.stage && Info.price == Definition.price && Info.coefficients == Definition.coefficients,
            "功法目录基础字段保持原始定义 " + Definition.id);
    }
    for (const auto& Definition : data::ManualDef_TABLE()) {
        ManualInfo Info;
        check(findManualInfo(Definition.id, Info), "不可变功法目录涵盖扩展功法 " + Definition.id);
    }
    ManualInfo Unknown; Unknown.id = "保留输出"; Unknown.name = "拒绝未知功法时不清空调用方";
    Unknown.price = 123; Unknown.effect = "旧接口拒绝语义";
    check(!findManualInfo("not-a-manual", Unknown) && Unknown.id == "保留输出" && Unknown.price == 123 &&
          Unknown.effect == "旧接口拒绝语义", "未知功法查询保持调用方输出不变");

    for (int Grade = 1; Grade <= 6; ++Grade) {
        const data::HerbDef* Herb = nullptr;
        for (const auto& Candidate : data::HerbDef_TABLE())
            if (Candidate.grade == Grade && mfIsPlantHerb(Candidate.id)) { Herb = &Candidate; break; }
        check(Herb != nullptr, "灵田等价回归有合法 " + std::to_string(Grade) + " 阶草药");
        if (!Herb) continue;
        for (const int ReserveStones : {0, 1, 2000, 1000000}) {
            for (const bool HasBottle : {false, true}) {
                for (const int QiLevel : {0, 3}) {
                    auto State = mfGardenForecastFixture(*Herb, ReserveStones, HasBottle, QiLevel);
                    const auto Snapshot = saveToJson(State);
                    const auto Info = mfGardenInfo(State).Plots[0];
                    check(saveToJson(State) == Snapshot, "成熟预报不改变日历、RNG、种苗或储备");
                    const auto PreviousCohorts = State.Garden.Plots[0].Cohorts;
                    const auto PreviousRng = State.rng;
                    const int PreviousBackpack = State.player.stones;
                    ++State.clock.days; mfGardenDayTick(State);
                    const auto& Result = State.Garden.Plots[0];
                    checkEq(Result.ReserveStones, ReserveStones - (Info.AccelerationFunded ? Info.DailyCostStones : 0),
                        "下一日真实储备扣费与权威报价一致");
                    bool ProgressMatches = Result.Cohorts.size() == PreviousCohorts.size();
                    for (std::size_t BatchIndex = 0; BatchIndex < Result.Cohorts.size(); ++BatchIndex)
                        ProgressMatches = ProgressMatches && Result.Cohorts[BatchIndex].ProgressUnits ==
                            std::min(Info.RequiredUnits, PreviousCohorts[BatchIndex].ProgressUnits + Info.EffectiveUnitsPerDay);
                    check(ProgressMatches, "所有播种批次真实成长与权威报价一致，含临成熟批次");
                    check(State.rng == PreviousRng && State.player.stones == PreviousBackpack,
                        "日结算不推进RNG且不挪用背包灵石");
                    const auto DaySnapshot = saveToJson(State); mfGardenDayTick(State);
                    check(saveToJson(State) == DaySnapshot, "重复同日结算不重复收费或成长");
                    int ElapsedDays = 1;
                    while (ElapsedDays < Info.EstimatedEffectiveDays) {
                        ++State.clock.days; mfGardenDayTick(State); ++ElapsedDays;
                    }
                    bool AllMature = true;
                    for (const auto& Batch : State.Garden.Plots[0].Cohorts)
                        AllMature = AllMature && Batch.ProgressUnits == Info.RequiredUnits;
                    check(AllMature, "储备耗尽、分批成熟的真实完成日与只读预报一致");
                }
            }
        }
    }
}
} // namespace nztest
