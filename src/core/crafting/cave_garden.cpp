#include "core/crafting/cave_garden.h"
#include "core/arts.h"
#include "core/cultivate.h"
#include "core/sim.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace nz::crafting {
namespace {
constexpr int MaximumPlots = 8;
constexpr int MaximumPlantsPerPlot = 99;
constexpr int InventoryLimit = 1000000000;
constexpr int HarvestYield = 3;
constexpr int UnitsPerBaseDay = 100;
constexpr std::array GrowthDurationDays{30, 60, 120, 240, 480, 960};
constexpr std::array UnlockedPlotCounts{3, 5, 8};
// 稳定内容ID表示妖丹分类，展示名称改动不影响能否种植；新妖丹加入目录时应同时补本分类。
constexpr std::array<std::string_view, 35> MonsterCoreIds{
    "8001","8002","8003","8004","8005","8006","8007","8008","8009","8010",
    "8011","8012","8013","8014","8015","8016","8017","8018","8019","8020",
    "8021","8022","8023","8024","8025","8590","8591","8592","8593","8594",
    "8595","8596","8597","8598","8599"};

// 用途：按稳定ID查可种草药；流程：播种/加载验证；返回：目录借用指针或空，不依赖展示名称。
const data::HerbDef* mfPlantableHerb(const std::string& HerbId) {
    const auto* Herb = findHerbById(HerbId);
    if (!Herb || Herb->grade < 1 || Herb->grade > 6 ||
        std::find(MonsterCoreIds.begin(), MonsterCoreIds.end(), HerbId) != MonsterCoreIds.end()) return nullptr;
    return Herb;
}
// 用途：解析稳定地块标识；流程：所有灵田入口；返回：0~7索引或空，未知ID明确拒绝。
std::optional<std::size_t> mfPlotIndex(const std::string& PlotId) {
    if (PlotId.size() != 6 || PlotId.substr(0, 5) != "plot-" || PlotId[5] < '1' || PlotId[5] > '8') return std::nullopt;
    return static_cast<std::size_t>(PlotId[5] - '1');
}
// 用途：读取洞府解锁格数；流程：视图和管理资格；返回：3/5/8，纯查询。
int mfUnlockedPlots(const Game& State) { return UnlockedPlotCounts[clampInt(State.player.caveTier, 0, 2)]; }
// 用途：读取某格快照；流程：纯报价；返回：合法已初始化地块借用或空，不偷偷初始化。
const GardenPlot* mfPlot(const Game& State, std::size_t PlotIndex) {
    return State.Garden.Initialized && PlotIndex < State.Garden.Plots.size() ? &State.Garden.Plots[PlotIndex] : nullptr;
}
// 用途：累计格内未收种苗数；流程：容量预检；返回：至多99份，合法状态下无溢出。
int mfSeedCount(const GardenPlot& Plot) {
    int Count = 0; for (const auto& Cohort : Plot.Cohorts) Count += Cohort.Count; return Count;
}
// 用途：计算药材成熟阈值；流程：报价/日tick；返回：基础日数×100成长点。
int mfRequiredUnits(const data::HerbDef& Herb) { return GrowthDurationDays[Herb.grade - 1] * UnitsPerBaseDay; }
// 用途：统计真正成熟种苗；流程：收获预览与执行；返回：可收苗数，不含未成熟批次。
int mfReadyCount(const GardenPlot& Plot, int RequiredUnits) {
    int Count = 0;
    for (const auto& Cohort : Plot.Cohorts) if (Cohort.ProgressUnits >= RequiredUnits) Count += Cohort.Count;
    return Count;
}
// 用途：读取统一管理及地块校验；流程：所有管理报价；返回：完整拒绝原因，失败不改变状态。
std::string mfManagementError(const Game& State, const std::string& PlotId) {
    if (const auto Error = mfGardenAccessError(State); !Error.empty()) return Error;
    if (const auto Error = mfGardenStateError(State); !Error.empty()) return Error;
    const auto Index = mfPlotIndex(PlotId);
    if (!Index) return "灵田地块不存在。";
    if (*Index >= static_cast<std::size_t>(mfUnlockedPlots(State))) return "该灵田地块尚未解锁，请先升级洞府。";
    return {};
}
// 用途：按剩余成长点向上取整预计天数；流程：只读时间预览；返回：最晚一批成熟天数，空格为0。
int mfEstimatedDays(const GardenPlot& Plot, int RequiredUnits, int UnitsPerDay) {
    int RemainingUnits = 0;
    for (const auto& Cohort : Plot.Cohorts) RemainingUnits = std::max(RemainingUnits, RequiredUnits - Cohort.ProgressUnits);
    return (RemainingUnits + UnitsPerDay - 1) / UnitsPerDay;
}
// 用途：按当前储备逐日隔离预演到最晚一批成熟；流程：真实可维持时间预览；返回：预计天数，不复制/推进活动Game或RNG。
int mfFundedDaysForecast(const GardenPlot& Plot, int RequiredUnits, int NaturalUnits, int FullUnits, int CostPerSeed) {
    GardenPlot Projection = Plot;
    const int MaximumDays = (RequiredUnits + NaturalUnits - 1) / NaturalUnits;
    for (int Day = 0; Day <= MaximumDays; ++Day) {
        int GrowingCount = 0; int ChargeableCount = 0;
        for (const auto& Cohort : Projection.Cohorts) {
            if (Cohort.ProgressUnits < RequiredUnits) GrowingCount += Cohort.Count;
            if (Cohort.ProgressUnits + NaturalUnits < RequiredUnits) ChargeableCount += Cohort.Count;
        }
        if (GrowingCount == 0) return Day;
        const int DayCost = ChargeableCount * CostPerSeed;
        const bool Funded = DayCost > 0 && Projection.ReserveStones >= DayCost;
        if (Funded) Projection.ReserveStones -= DayCost;
        const int GrowthUnits = Funded ? FullUnits : NaturalUnits;
        for (auto& Cohort : Projection.Cohorts) Cohort.ProgressUnits = std::min(RequiredUnits, Cohort.ProgressUnits + GrowthUnits);
    }
    return MaximumDays;
}
// 用途：构建格内下一日权威成长与储备报价；流程：只读视图/真实日tick共用；返回：不持有状态引用的报价值。
GardenPlotInfo mfPlotInfo(const Game& State, std::size_t PlotIndex) {
    GardenPlotInfo Info; Info.PlotId = "plot-" + std::to_string(PlotIndex + 1);
    Info.NaturalUnitsPerDay = UnitsPerBaseDay + clampInt(State.player.caveTier, 0, 2) * 10;
    Info.SpiritUnitsPerDay = Info.BottleUnitsPerDay = Info.FullyAcceleratedUnitsPerDay = Info.EffectiveUnitsPerDay = Info.NaturalUnitsPerDay;
    const auto* Plot = mfPlot(State, PlotIndex);
    if (!Plot) return Info;
    Info.ReserveStones = Plot->ReserveStones; Info.HerbId = Plot->HerbId;
    const auto* Herb = mfPlantableHerb(Plot->HerbId);
    if (!Herb) return Info;
    Info.Name = Herb->name; Info.Grade = Herb->grade; Info.RequiredUnits = mfRequiredUnits(*Herb);
    Info.SeedCount = mfSeedCount(*Plot); Info.ReadyCount = mfReadyCount(*Plot, Info.RequiredUnits);
    Info.GrowingCount = Info.SeedCount - Info.ReadyCount;
    int ChargeableSeeds = 0;
    for (const auto& Cohort : Plot->Cohorts) {
        Info.Cohorts.push_back({Cohort.Count, Cohort.ProgressUnits, Cohort.ProgressUnits >= Info.RequiredUnits});
        // 最后一个自然日已能成熟时，不再购买多余加速。
        if (Cohort.ProgressUnits + Info.NaturalUnitsPerDay < Info.RequiredUnits) ChargeableSeeds += Cohort.Count;
    }
    const int QiLevel = clampInt(State.player.qiSupply, 0, 1 + clampInt(State.player.caveTier, 0, 2));
    const bool HasBottle = hasTalent(State.player, "greenBottle");
    Info.SpiritUnitsPerDay += QiLevel * 30;
    Info.BottleUnitsPerDay += HasBottle ? 200 : 0;
    Info.FullyAcceleratedUnitsPerDay = Info.SpiritUnitsPerDay + (HasBottle ? 200 : 0);
    Info.SpiritDailyCostStones = ChargeableSeeds * Herb->grade * QiLevel;
    Info.BottleDailyCostStones = ChargeableSeeds * Herb->grade * (HasBottle ? 1 : 0);
    Info.DailyCostStones = Info.SpiritDailyCostStones + Info.BottleDailyCostStones;
    Info.AccelerationFunded = Info.DailyCostStones > 0 && Plot->ReserveStones >= Info.DailyCostStones;
    Info.EffectiveUnitsPerDay = Info.AccelerationFunded ? Info.FullyAcceleratedUnitsPerDay : Info.NaturalUnitsPerDay;
    Info.EstimatedNaturalDays = mfEstimatedDays(*Plot, Info.RequiredUnits, Info.NaturalUnitsPerDay);
    Info.EstimatedSpiritDays = mfEstimatedDays(*Plot, Info.RequiredUnits, Info.SpiritUnitsPerDay);
    Info.EstimatedBottleDays = mfEstimatedDays(*Plot, Info.RequiredUnits, Info.BottleUnitsPerDay);
    Info.EstimatedFullyAcceleratedDays = mfEstimatedDays(*Plot, Info.RequiredUnits, Info.FullyAcceleratedUnitsPerDay);
    Info.EstimatedEffectiveDays = mfFundedDaysForecast(*Plot, Info.RequiredUnits, Info.NaturalUnitsPerDay,
        Info.FullyAcceleratedUnitsPerDay, Herb->grade * (QiLevel + (HasBottle ? 1 : 0)));
    return Info;
}
// 用途：原子转移格内储备；流程：存入/取回命令；返回：空成功或拒绝原因，所有检查通过后转账。
std::string mfTransferReserve(Game& State, const std::string& PlotId, int Amount, bool IsDeposit) {
    const auto Quote = mfQuoteGardenReserve(State, PlotId, Amount, IsDeposit);
    if (!Quote.Enabled) return Quote.Reason;
    Game Candidate = State; mfInitializeGarden(Candidate);
    auto& Plot = Candidate.Garden.Plots[*mfPlotIndex(PlotId)];
    const int SignedTransfer = IsDeposit ? Amount : -Amount;
    Plot.ReserveStones += SignedTransfer; Candidate.player.stones -= SignedTransfer;
    logEvent(Candidate, "self", std::string(IsDeposit ? "存入" : "取回") + PlotId + "灵田储备" + std::to_string(Amount) + "灵石。", false);
    State = std::move(Candidate);
    return {};
}
}

// 用途：显式初始化合法空灵田；流程：建角/旧档加载/命令；返回：无，现代状态不重置。
void mfInitializeGarden(Game& State) {
    if (State.Garden.Initialized) return;
    State.Garden.Plots.resize(MaximumPlots);
    State.Garden.LastGrowthDay = State.clock.days;
    State.Garden.Initialized = true;
}
// 用途：统一洞府管理资格；流程：种田、储备及训练；返回：拒绝原因或空，支持合法野外洞府，不写状态。
std::string mfGardenAccessError(const Game& State) {
    if (State.ended || State.player.health <= 0) return "这一世已经结束。";
    if (State.pending) return "请先处理当前事件。";
    if (State.player.arts.battle) return "请先完成斗法。";
    if (State.job) return "请先完成或中止当前安排。";
    if (State.dungeons.run) return "请先离开秘境。";
    if (State.player.cave.empty()) return "请先安置洞府。";
    if (!cultivate::atCave(State.player)) return "请先通过地图赶路回到洞府所在地。";
    return {};
}
// 用途：只读汇总灵田、持有种子和后台日费用；返回：值快照，不推进任何状态。
GardenInfo mfGardenInfo(const Game& State) {
    GardenInfo Info; Info.PlotCountUnlocked = mfUnlockedPlots(State);
    Info.HasGreenBottle = hasTalent(State.player, "greenBottle"); Info.AccessReason = mfGardenAccessError(State);
    if (const auto Error = mfGardenStateError(State); !Error.empty()) { Info.AccessReason = Error; return Info; }
    Info.GrowthCostRule = "每格同药材最多99份；每份成熟收获3份。天然每天100成长点，洞府每档+10；格内储备全额负担时灵气每档+30、绿瓶额外+200。日费=尚需加速种苗数×品阶×(灵气档位+绿瓶1)，储备不足自然生长且不扣费。外出照常生长，不自动扣主背包灵石。";
    for (int PlotIndex = 0; PlotIndex < Info.PlotCountUnlocked; ++PlotIndex) {
        auto PlotInfo = mfPlotInfo(State, static_cast<std::size_t>(PlotIndex));
        PlotInfo.Harvest = mfQuoteGardenHarvest(State, PlotInfo.PlotId);
        PlotInfo.Clear = mfQuoteGardenClear(State, PlotInfo.PlotId);
        Info.Plots.push_back(std::move(PlotInfo));
    }
    for (const auto& [HerbName, OwnedCount] : State.player.arts.herbs) {
        const auto* Herb = findHerbByName(HerbName);
        if (OwnedCount > 0 && Herb && mfPlantableHerb(Herb->id))
            Info.PlantOptions.push_back({Herb->id, Herb->name, Herb->grade, OwnedCount, GrowthDurationDays[Herb->grade - 1]});
    }
    return Info;
}
// 用途：完整预检同药材批次播种；返回：真实种苗消耗，无写入或隐式扣钱。
GardenActionQuote mfQuoteGardenPlant(const Game& State, const std::string& PlotId, const std::string& HerbId, int Count) {
    GardenActionQuote Quote; Quote.PlotId = PlotId; Quote.HerbId = HerbId; Quote.SeedCount = Count;
    Quote.Reason = mfManagementError(State, PlotId);
    const auto* Herb = mfPlantableHerb(HerbId);
    if (Quote.Reason.empty() && !Herb) Quote.Reason = "此材料不能种植；妖丹不属于草药种苗。";
    if (Quote.Reason.empty() && (Count < 1 || Count > MaximumPlantsPerPlot)) Quote.Reason = "种植数量须为1至99份。";
    const auto Index = mfPlotIndex(PlotId); const auto* Plot = Index ? mfPlot(State, *Index) : nullptr;
    if (Quote.Reason.empty() && Plot && !Plot->HerbId.empty() && Plot->HerbId != HerbId) Quote.Reason = "同一格只可种同一种草药，请先收获或清理。";
    if (Quote.Reason.empty() && Plot && Count > MaximumPlantsPerPlot - mfSeedCount(*Plot)) Quote.Reason = "本格种苗总数不能超过99份。";
    if (Quote.Reason.empty()) {
        const auto Owned = State.player.arts.herbs.find(Herb->name);
        if (Owned == State.player.arts.herbs.end() || Owned->second < Count || State.player.herbs < Count) Quote.Reason = "背包中此草药种苗数量不足。";
    }
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：收获成熟批次报价；返回：库存容量与真实份数预检，不改变批次。
GardenActionQuote mfQuoteGardenHarvest(const Game& State, const std::string& PlotId) {
    GardenActionQuote Quote; Quote.PlotId = PlotId; Quote.Reason = mfManagementError(State, PlotId);
    if (!mfGardenStateError(State).empty()) return Quote;
    const auto Index = mfPlotIndex(PlotId); const auto* Plot = Index ? mfPlot(State, *Index) : nullptr;
    const auto* Herb = Plot ? mfPlantableHerb(Plot->HerbId) : nullptr;
    if (Herb) { Quote.HerbId = Herb->id; Quote.HarvestCount = mfReadyCount(*Plot, mfRequiredUnits(*Herb)) * HarvestYield; }
    if (Quote.Reason.empty() && Quote.HarvestCount == 0) Quote.Reason = "本格尚无成熟草药。";
    if (Quote.Reason.empty()) {
        const auto Owned = State.player.arts.herbs.find(Herb->name);
        const int OwnedCount = Owned == State.player.arts.herbs.end() ? 0 : Owned->second;
        if (OwnedCount > InventoryLimit - Quote.HarvestCount || State.player.herbs > InventoryLimit - Quote.HarvestCount) Quote.Reason = "收获后草药库存超过上限。";
    }
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：存取储备报价；返回：完整转账金额/容量预检，无写入。
GardenActionQuote mfQuoteGardenReserve(const Game& State, const std::string& PlotId, int Amount, bool IsDeposit) {
    GardenActionQuote Quote; Quote.PlotId = PlotId; Quote.TransferStones = Amount; Quote.Reason = mfManagementError(State, PlotId);
    if (Quote.Reason.empty() && (Amount < 1 || Amount > InventoryLimit)) Quote.Reason = "储备转账数量须为1至十亿灵石。";
    const auto Index = mfPlotIndex(PlotId); const auto* Plot = Index ? mfPlot(State, *Index) : nullptr;
    const int Reserve = Plot ? Plot->ReserveStones : 0;
    if (Quote.Reason.empty()) {
        if (IsDeposit && (State.player.stones < Amount || Reserve > InventoryLimit - Amount)) Quote.Reason = "背包灵石不足或本格储备超过上限。";
        if (!IsDeposit && (Reserve < Amount || State.player.stones > InventoryLimit - Amount)) Quote.Reason = "本格储备不足或取回后背包灵石超过上限。";
    }
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：预览清理丢弃份数；返回：明确损失，不返种子且不删储备。
GardenActionQuote mfQuoteGardenClear(const Game& State, const std::string& PlotId) {
    GardenActionQuote Quote; Quote.PlotId = PlotId; Quote.Reason = mfManagementError(State, PlotId);
    if (!mfGardenStateError(State).empty()) return Quote;
    const auto Index = mfPlotIndex(PlotId); const auto* Plot = Index ? mfPlot(State, *Index) : nullptr;
    Quote.DiscardCount = Plot ? mfSeedCount(*Plot) : 0;
    if (Quote.Reason.empty() && Quote.DiscardCount == 0) Quote.Reason = "本格没有需要清理的种苗。";
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：提交播种候选快照；返回：空成功或完整拒绝，材料和批次一次提交。
std::string mfPlantGardenHerbs(Game& State, const std::string& PlotId, const std::string& HerbId, int Count) {
    const auto Quote = mfQuoteGardenPlant(State, PlotId, HerbId, Count); if (!Quote.Enabled) return Quote.Reason;
    Game Candidate = State; mfInitializeGarden(Candidate);
    auto& Plot = Candidate.Garden.Plots[*mfPlotIndex(PlotId)]; const auto* Herb = mfPlantableHerb(HerbId);
    Plot.HerbId = HerbId;
    auto NewBatch = std::find_if(Plot.Cohorts.begin(), Plot.Cohorts.end(), [](const GardenCohort& Batch) { return Batch.ProgressUnits == 0; });
    if (NewBatch == Plot.Cohorts.end()) Plot.Cohorts.push_back({Count, 0}); else NewBatch->Count += Count;
    Candidate.player.arts.herbs[Herb->name] -= Count; Candidate.player.herbs -= Count;
    logEvent(Candidate, "self", "在" + PlotId + "播种「" + Herb->name + "」" + std::to_string(Count) + "份，每份成熟可收3份；新种苗从零成长。", false);
    State = std::move(Candidate); return {};
}
// 用途：提交真实成熟收获；返回：空成功或拒绝，成熟批次只领取一次，未熟批次保留。
std::string mfHarvestGardenHerbs(Game& State, const std::string& PlotId) {
    const auto Quote = mfQuoteGardenHarvest(State, PlotId); if (!Quote.Enabled) return Quote.Reason;
    Game Candidate = State; auto& Plot = Candidate.Garden.Plots[*mfPlotIndex(PlotId)]; const auto* Herb = mfPlantableHerb(Plot.HerbId);
    const int RequiredUnits = mfRequiredUnits(*Herb); const std::string HerbName = Herb->name;
    Candidate.player.arts.herbs[HerbName] += Quote.HarvestCount; Candidate.player.herbs += Quote.HarvestCount;
    std::erase_if(Plot.Cohorts, [RequiredUnits](const GardenCohort& Batch) { return Batch.ProgressUnits >= RequiredUnits; });
    if (Plot.Cohorts.empty()) Plot.HerbId.clear();
    logEvent(Candidate, "self", "收获" + PlotId + "「" + HerbName + "」" + std::to_string(Quote.HarvestCount) + "份。", true, "", "草药 +" + std::to_string(Quote.HarvestCount));
    State = std::move(Candidate); return {};
}
// 用途：存入储备的明确命令；返回：空成功或原因，不从主背包自动取钱。
std::string mfDepositGardenReserve(Game& State, const std::string& PlotId, int Amount) { return mfTransferReserve(State, PlotId, Amount, true); }
// 用途：取回储备的明确命令；返回：空成功或原因，不改变成长批次。
std::string mfWithdrawGardenReserve(Game& State, const std::string& PlotId, int Amount) { return mfTransferReserve(State, PlotId, Amount, false); }
// 用途：清理全部种苗但保留储备；返回：空成功或原因，不返种子，不清除其他格。
std::string mfClearGardenPlot(Game& State, const std::string& PlotId) {
    const auto Quote = mfQuoteGardenClear(State, PlotId); if (!Quote.Enabled) return Quote.Reason;
    Game Candidate = State; auto& Plot = Candidate.Garden.Plots[*mfPlotIndex(PlotId)]; Plot.Cohorts.clear(); Plot.HerbId.clear();
    logEvent(Candidate, "self", "清理" + PlotId + "，放弃" + std::to_string(Quote.DiscardCount) + "份种苗；格内灵石储备保留。", false);
    State = std::move(Candidate); return {};
}
// 用途：真实世界日唯一后台结算；返回：无，同日幂等，成熟封顶且自然即可成熟时不收加速费。
void mfGardenDayTick(Game& State) {
    if (!State.Garden.Initialized || State.clock.days <= State.Garden.LastGrowthDay) return;
    if (!mfGardenStateError(State).empty()) return;
    State.Garden.LastGrowthDay = State.clock.days;
    if (State.ended || State.player.health <= 0 || State.player.age >= State.player.lifespan) return;
    for (std::size_t PlotIndex = 0; PlotIndex < State.Garden.Plots.size(); ++PlotIndex) {
        const auto Info = mfPlotInfo(State, PlotIndex); if (Info.RequiredUnits == 0 || Info.GrowingCount == 0) continue;
        auto& Plot = State.Garden.Plots[PlotIndex];
        if (Info.AccelerationFunded) Plot.ReserveStones -= Info.DailyCostStones;
        for (auto& Cohort : Plot.Cohorts) Cohort.ProgressUnits = std::min(Info.RequiredUnits, Cohort.ProgressUnits + Info.EffectiveUnitsPerDay);
        const int NewlyReady = mfReadyCount(Plot, Info.RequiredUnits) - Info.ReadyCount;
        if (NewlyReady > 0) logEvent(State, "self", Info.PlotId + "的「" + Info.Name + "」" + std::to_string(NewlyReady) + "份种苗成熟，可回洞府收获。", false);
    }
}
// 用途：校验持久状态、批次容量、成长阈值与目录引用；返回：明确拒绝或空，不迁移/修复输入。
std::string mfGardenStateError(const Game& State) {
    const auto& Garden = State.Garden;
    if (!Garden.Initialized) return Garden.Plots.empty() && Garden.LastGrowthDay == -1 ? "" : "未初始化灵田包含非法持久状态。";
    if (Garden.Plots.size() != MaximumPlots || Garden.LastGrowthDay < 0 || Garden.LastGrowthDay > State.clock.days) return "灵田格数或成长日期无效。";
    const bool HasGardenAssets = std::any_of(Garden.Plots.begin(), Garden.Plots.end(), [](const GardenPlot& Plot) {
        return !Plot.Cohorts.empty() || Plot.ReserveStones > 0;
    });
    if (HasGardenAssets && (State.player.cave.empty() || !findLoc(State.player.cave)))
        return "有种苗或储备的灵田必须归属于已安置的合法洞府。";
    for (std::size_t PlotIndex = 0; PlotIndex < Garden.Plots.size(); ++PlotIndex) {
        const auto& Plot = Garden.Plots[PlotIndex];
        if (Plot.ReserveStones < 0 || Plot.ReserveStones > InventoryLimit || Plot.Cohorts.size() > MaximumPlantsPerPlot) return "灵田储备或批次数无效。";
        if (PlotIndex >= static_cast<std::size_t>(mfUnlockedPlots(State)) && (!Plot.Cohorts.empty() || Plot.ReserveStones > 0))
            return "未解锁灵田不能保存种苗或灵石储备。";
        if (Plot.Cohorts.empty()) { if (!Plot.HerbId.empty()) return "空灵田不应保存药材引用。"; continue; }
        const auto* Herb = mfPlantableHerb(Plot.HerbId); if (!Herb) return "灵田药材引用无效或属于不可种妖丹。";
        int SeedCount = 0;
        for (const auto& Cohort : Plot.Cohorts) {
            if (Cohort.Count < 1 || Cohort.Count > MaximumPlantsPerPlot || Cohort.ProgressUnits < 0 || Cohort.ProgressUnits > mfRequiredUnits(*Herb)) return "灵田批次数量或成长点无效。";
            SeedCount += Cohort.Count;
        }
        if (SeedCount > MaximumPlantsPerPlot) return "灵田种苗总量超过99份。";
    }
    return {};
}
} // namespace nz::crafting
