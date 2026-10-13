#pragma once

#include "core/model.h"

#include <string>
#include <vector>

namespace nz::crafting {

// 以下报价/视图类型均为可复制快照，无外部资源或状态写权限；只在调用期间借用稳定Game。
struct GardenActionQuote {
    bool Enabled = false;
    std::string Reason;
    std::string PlotId;
    std::string HerbId;
    int SeedCount = 0;
    int HarvestCount = 0;
    int TransferStones = 0;
    int DiscardCount = 0;
};
struct GardenPlantOption {
    std::string HerbId;
    std::string Name;
    int Grade = 0;
    int OwnedCount = 0;
    int BaseGrowthDays = 0;
};
struct GardenCohortInfo {
    int Count = 0;
    int ProgressUnits = 0;
    bool Ready = false;
};
struct GardenPlotInfo {
    std::string PlotId;
    std::string HerbId;
    std::string Name;
    int Grade = 0;
    int SeedCount = 0;
    int GrowingCount = 0;
    int ReadyCount = 0;
    int ReserveStones = 0;
    int RequiredUnits = 0;
    int NaturalUnitsPerDay = 100;
    int SpiritUnitsPerDay = 100;
    int BottleUnitsPerDay = 100;
    int FullyAcceleratedUnitsPerDay = 100;
    int EffectiveUnitsPerDay = 100;
    int SpiritDailyCostStones = 0;
    int BottleDailyCostStones = 0;
    int DailyCostStones = 0;
    int EstimatedNaturalDays = 0;
    int EstimatedSpiritDays = 0;
    int EstimatedBottleDays = 0;
    int EstimatedFullyAcceleratedDays = 0;
    int EstimatedEffectiveDays = 0;
    bool AccelerationFunded = false;
    GardenActionQuote Harvest;
    GardenActionQuote Clear;
    std::vector<GardenCohortInfo> Cohorts;
};
struct GardenInfo {
    int PlotCountUnlocked = 3;
    int PlantsPerPlotLimit = 99;
    int HarvestMultiplier = 3;
    bool HasGreenBottle = false;
    std::string AccessReason;
    std::string GrowthCostRule;
    std::vector<GardenPlotInfo> Plots;
    std::vector<GardenPlantOption> PlantOptions;
};

// 用途：建立旧缺字段/新号合法空灵田；流程：建角及加载迁移命令；用法：传可写Game；返回：无，已初始化不重置；不在只读查询中调用。
void mfInitializeGarden(Game& State);
// 用途：检查洞府管理共同门槛；流程：种植、收获、储备和训练；返回：拒绝原因或空，无状态修改。
[[nodiscard]] std::string mfGardenAccessError(const Game& State);
// 用途：查询灵田完整成长/费用快照；流程：洞府只读页；返回：拥有的GardenInfo，不推进时间/RNG/生长/IO。
[[nodiscard]] GardenInfo mfGardenInfo(const Game& State);
// 用途：报价同格同药材播种；流程：预览与实际种植；参数：稳定plot-1..plot-8、目录HerbId、份数1..99；返回：资格/种苗扣除量，失败不写状态。
[[nodiscard]] GardenActionQuote mfQuoteGardenPlant(const Game& State, const std::string& PlotId,
    const std::string& HerbId, int Count);
// 用途：报价本格所有成熟批次收获；流程：预览/收获；返回：真正可增加的药材份数，未成熟或库存溢出拒绝，无写入。
[[nodiscard]] GardenActionQuote mfQuoteGardenHarvest(const Game& State, const std::string& PlotId);
// 用途：预检格内储备存取；流程：储备报价/命令；参数：正整数Amount、存入true/取出false；返回：余额与容量校验，无写入。
[[nodiscard]] GardenActionQuote mfQuoteGardenReserve(const Game& State, const std::string& PlotId,
    int Amount, bool IsDeposit);
// 用途：预检放弃本格全部种苗；流程：清理确认；返回：丢弃种苗份数，格内储备仍可取回，无写入。
[[nodiscard]] GardenActionQuote mfQuoteGardenClear(const Game& State, const std::string& PlotId);
// 用途：按新报价播种，原料从已有背包扣除；流程：真实洞府操作；返回：空成功或原因，拒绝不改原料/储备/时钟/RNG/工作。
[[nodiscard]] std::string mfPlantGardenHerbs(Game& State, const std::string& PlotId,
    const std::string& HerbId, int Count);
// 用途：按新报价收获成熟批次并清掉对应种苗；返回：空成功或原因，同一成熟批次不可重复收获；失败原子，不自动播种。
[[nodiscard]] std::string mfHarvestGardenHerbs(Game& State, const std::string& PlotId);
// 用途：从背包显式存入本格储备；返回：空成功或原因，所有预检通过再一次转账，不隐式跨格用钱。
[[nodiscard]] std::string mfDepositGardenReserve(Game& State, const std::string& PlotId, int Amount);
// 用途：显式取回本格储备到背包；返回：空成功或原因，失败不留下半笔转账。
[[nodiscard]] std::string mfWithdrawGardenReserve(Game& State, const std::string& PlotId, int Amount);
// 用途：放弃本格全部成熟/未熟种苗，保留灵石储备；返回：空成功或原因，拒绝不改状态，须前端明确确认损失。
[[nodiscard]] std::string mfClearGardenPlot(Game& State, const std::string& PlotId);
// 用途：推进真实世界日的后台生长和储备消耗；流程：日历唯一写入口，外出/秘境也生长；返回：无，相同日不重复，不扣主背包钱、不自动入库。
void mfGardenDayTick(Game& State);
// 用途：校验持久灵田状态不变量；流程：存档加载及动作预检；返回：拒绝原因或空，不改变或初始化输入。
[[nodiscard]] std::string mfGardenStateError(const Game& State);

} // namespace nz::crafting
