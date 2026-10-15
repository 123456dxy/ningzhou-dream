#include "core/crafting/cave_garden.h"
#include "core/crafting/garden_rules.h"
#include "core/crafting/green_bottle.h"
#include "core/crafting/herb_acquisition.h"
#include "core/arts.h"
#include "core/cultivate.h"
#include "core/sim.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <type_traits>

namespace nz::crafting {
namespace {
constexpr int MaximumPlots = 8;
constexpr int InventoryLimit = 1000000000;
constexpr std::array UnlockedPlotCounts{3, 5, 8};
constexpr std::array LegacyMaturityUnits{3000, 6000, 12000, 24000, 48000, 96000};
// 用途：查可种植草药；流程：种植/校验；返回：静态定义或空，妖丹明确排除。
const data::HerbDef* mfPlantableHerb(const std::string& HerbId) {
    return mfIsPlantHerb(HerbId) ? findHerbById(HerbId) : nullptr;
}
// 用途：解析地块白名单；流程：管理入口；返回：0..7或空，拒绝未知/伪造ID。
std::optional<std::size_t> mfPlotIndex(const std::string& PlotId) {
    if (PlotId.size() != 6 || !PlotId.starts_with("plot-") || PlotId[5] < '1' || PlotId[5] > '8') return {};
    return static_cast<std::size_t>(PlotId[5] - '1');
}
// 用途：读取解锁地块数；流程：报价/校验；返回：洞府档位对应3/5/8。
int mfUnlockedPlots(const Game& State) { return UnlockedPlotCounts[clampInt(State.player.caveTier, 0, 2)]; }
// 用途：取得现有地块只读快照；流程：报价；返回：借用指针或空，不隐式初始化。
const GardenPlot* mfPlot(const Game& State, std::size_t Index) {
    return State.Garden.Initialized && Index < State.Garden.Plots.size() ? &State.Garden.Plots[Index] : nullptr;
}
// 用途：统计地块种苗总数；流程：容量/显示；返回：合法状态下0..12。
int mfSeedCount(const GardenPlot& Plot) {
    int Count = 0;
    for (const auto& Batch : Plot.Cohorts) Count += Batch.Count;
    return Count;
}
// 用途：查询成熟阈值；流程：预览/真实日结算；返回：固定品阶成长点。
int mfRequiredUnits(const data::HerbDef& Herb) { return GardenMaturityUnits[Herb.grade - 1]; }
// 用途：统计成熟种苗；流程：收获和预览；返回：真正达到阈值的份数。
int mfReadyCount(const GardenPlot& Plot, int Required) {
    int Count = 0;
    for (const auto& Batch : Plot.Cohorts) if (Batch.ProgressUnits >= Required) Count += Batch.Count;
    return Count;
}
// 用途：统一管理资格；流程：全部园艺动作；返回：拒绝原因或空，不改变状态。
std::string mfManagementError(const Game& State, const std::string& PlotId) {
    if (const auto Error = mfGardenAccessError(State); !Error.empty()) return Error;
    if (const auto Error = mfGardenStateError(State); !Error.empty()) return Error;
    const auto Index = mfPlotIndex(PlotId);
    if (!Index) return "灵田地块不存在。";
    return *Index >= static_cast<std::size_t>(mfUnlockedPlots(State)) ? "该灵田地块尚未解锁，请先升级洞府。" : "";
}
// 用途：按最晚批次向上取整天数；流程：只读预报；返回：最晚成熟天数，空格0。
int mfEstimatedDays(const GardenPlot& Plot, int Required, int DailyUnits) {
    int Remaining = 0;
    for (const auto& Batch : Plot.Cohorts) Remaining = std::max(Remaining, Required - Batch.ProgressUnits);
    return (Remaining + DailyUnits - 1) / DailyUnits;
}
// 用途：生成真实日与视图共用快照；流程：生长/报价；返回：只读值，不推进日历或RNG。
GardenPlotInfo mfPlotInfo(const Game& State, std::size_t Index) {
    GardenPlotInfo Info;
    Info.PlotId = "plot-" + std::to_string(Index + 1);
    Info.NaturalUnitsPerDay = GardenNaturalDailyUnits[clampInt(State.player.caveTier, 0, 2)];
    Info.BottleBonusUnitsPerDay = mfGreenBottleDailyBonus(State.player);
    Info.EffectiveUnitsPerDay = Info.NaturalUnitsPerDay + Info.BottleBonusUnitsPerDay;
    const auto* Plot = mfPlot(State, Index);
    if (!Plot) return Info;
    Info.HerbId = Plot->HerbId;
    const auto* Herb = mfPlantableHerb(Plot->HerbId);
    if (!Herb) return Info;
    Info.Name = Herb->name; Info.Grade = Herb->grade; Info.RequiredUnits = mfRequiredUnits(*Herb);
    Info.SeedCount = mfSeedCount(*Plot); Info.ReadyCount = mfReadyCount(*Plot, Info.RequiredUnits);
    Info.GrowingCount = Info.SeedCount - Info.ReadyCount;
    for (const auto& Batch : Plot->Cohorts) {
        const int Remaining = std::max(0, Info.RequiredUnits - Batch.ProgressUnits);
        Info.Cohorts.push_back({Batch.Count, Batch.ProgressUnits, Remaining == 0,
            (Remaining + Info.EffectiveUnitsPerDay - 1) / Info.EffectiveUnitsPerDay});
    }
    Info.EstimatedNaturalDays = mfEstimatedDays(*Plot, Info.RequiredUnits, Info.NaturalUnitsPerDay);
    Info.EstimatedEffectiveDays = mfEstimatedDays(*Plot, Info.RequiredUnits, Info.EffectiveUnitsPerDay);
    return Info;
}
// 用途：只提交园艺真实写集；流程：播种/收获/清理/旧资产兑现；返回：无，容器swap及标量写入不分配，Game的其他可抛optional不参与。
void mfCommitGarden(Game& State, Game& Candidate) noexcept {
    static_assert(noexcept(State.Garden.Plots.swap(Candidate.Garden.Plots)));
    static_assert(noexcept(State.Garden.PendingHerbReturns.swap(Candidate.Garden.PendingHerbReturns)));
    static_assert(noexcept(State.player.arts.herbs.swap(Candidate.player.arts.herbs)));
    static_assert(noexcept(State.logs.swap(Candidate.logs)));
    State.Garden.Plots.swap(Candidate.Garden.Plots);
    State.Garden.PendingHerbReturns.swap(Candidate.Garden.PendingHerbReturns);
    State.player.arts.herbs.swap(Candidate.player.arts.herbs);
    State.logs.swap(Candidate.logs);
    State.Garden.RulesVersion = Candidate.Garden.RulesVersion;
    State.Garden.Initialized = Candidate.Garden.Initialized;
    State.Garden.LastGrowthDay = Candidate.Garden.LastGrowthDay;
    State.player.herbs = Candidate.player.herbs;
    State.player.stones = Candidate.player.stones;
    State.eventSeq = Candidate.eventSeq;
}
// 用途：先验证旧规则语义再归一化；流程：候选读档迁移；返回：合法真，不将损坏进度或非法地块修成合法资产。
bool mfLegacyGardenValid(const Game& State) {
    const auto& Garden = State.Garden;
    if (Garden.RulesVersion != 1 || !Garden.PendingHerbReturns.empty()) return false;
    if (!Garden.Initialized) return Garden.Plots.empty() && Garden.LastGrowthDay == -1;
    if (Garden.Plots.size() != MaximumPlots || Garden.LastGrowthDay < 0 || Garden.LastGrowthDay > State.clock.days) return false;
    for (std::size_t Index = 0; Index < Garden.Plots.size(); ++Index) {
        const auto& Plot = Garden.Plots[Index];
        if (Plot.ReserveStones < 0 || Plot.ReserveStones > InventoryLimit || Plot.Cohorts.size() > 99) return false;
        if ((!Plot.Cohorts.empty() || Plot.ReserveStones > 0) && (State.player.cave.empty() || !findLoc(State.player.cave))) return false;
        if (Index >= static_cast<std::size_t>(mfUnlockedPlots(State)) && !Plot.Cohorts.empty()) return false;
        if (Plot.Cohorts.empty()) { if (!Plot.HerbId.empty()) return false; continue; }
        const auto* Herb = mfPlantableHerb(Plot.HerbId);
        if (!Herb) return false;
        int Count = 0;
        for (const auto& Batch : Plot.Cohorts) {
            if (Batch.Count < 1 || Batch.Count > 99 || Batch.ProgressUnits < 0 || Batch.ProgressUnits > LegacyMaturityUnits[Herb->grade - 1]) return false;
            Count += Batch.Count;
        }
        if (Count > 99) return false;
    }
    return true;
}
// 用途：兑现已验证旧资产到隔离候选；流程：真实日/园艺动作；返回：无，任何分配或纪事失败不触及活动状态。
void mfApplyLegacyGardenReturns(Game& Candidate) {
    for (auto& Plot : Candidate.Garden.Plots) {
        const int Returned = std::min(Plot.ReserveStones, std::max(0, InventoryLimit - Candidate.player.stones));
        if (Returned <= 0) continue;
        logEvent(Candidate, "self", "旧灵田储备退回背包：" + std::to_string(Returned) + "灵石。", false);
        Candidate.player.stones += Returned; Plot.ReserveStones -= Returned;
    }
    for (auto& [HerbName, Pending] : Candidate.Garden.PendingHerbReturns) {
        const auto Found = Candidate.player.arts.herbs.find(HerbName);
        const int Owned = Found == Candidate.player.arts.herbs.end() ? 0 : Found->second;
        const int Returned = std::min({Pending, InventoryLimit - Candidate.player.herbs, InventoryLimit - Owned});
        if (Returned <= 0) continue;
        logEvent(Candidate, "self", "灵田容量调整退回「" + HerbName + "」" + std::to_string(Returned) + "份。", false);
        Candidate.player.arts.herbs[HerbName] += Returned; Candidate.player.herbs += Returned; Pending -= Returned;
    }
    std::erase_if(Candidate.Garden.PendingHerbReturns, [](const auto& Entry) { return Entry.second == 0; });
}
}
// 用途：初始化空灵田；流程：建号/缺字段默认；返回：无，已初始化不重置。
void mfInitializeGarden(Game& State) {
    if (State.Garden.Initialized) return;
    State.Garden.RulesVersion = GardenRulesVersion;
    State.Garden.Plots.resize(MaximumPlots);
    State.Garden.LastGrowthDay = State.clock.days;
    State.Garden.Initialized = true;
}
// 用途：归一化旧成长比例和12份容量；流程：候选读档迁移；返回：无，超额成熟苗按三倍、未熟苗按原种数登记待返，不直接发资源。
void mfMigrateGardenRules(Game& State) {
    auto& Garden = State.Garden;
    if (Garden.RulesVersion >= GardenRulesVersion) return;
    if (!mfLegacyGardenValid(State)) return;
    if (Garden.Initialized) {
        for (auto& Plot : Garden.Plots) {
            const auto* Herb = mfPlantableHerb(Plot.HerbId);
            if (!Herb) continue;
            const int OldRequired = LegacyMaturityUnits[Herb->grade - 1];
            const int NewRequired = mfRequiredUnits(*Herb);
            int Capacity = GardenMaximumPlantsPerPlot;
            std::vector<GardenCohort> Kept;
            for (const auto& Batch : Plot.Cohorts) {
                const int Keep = std::min(Capacity, Batch.Count);
                const int Excess = Batch.Count - Keep;
                if (Excess > 0) Garden.PendingHerbReturns[Herb->name] += Excess * (Batch.ProgressUnits >= OldRequired ? GardenHarvestMultiplier : 1);
                if (Keep > 0) Kept.push_back({Keep, static_cast<int>(std::min<std::int64_t>(NewRequired,
                    static_cast<std::int64_t>(Batch.ProgressUnits) * NewRequired / OldRequired))});
                Capacity -= Keep;
            }
            Plot.Cohorts = std::move(Kept);
            if (Plot.Cohorts.empty()) Plot.HerbId.clear();
        }
    }
    Garden.RulesVersion = GardenRulesVersion;
}
// 用途：仅在真实动作退还旧资产；流程：日tick/成功园艺命令；返回：无，余额或背包容量不足时保留余款，重复调用不会重复返还。
void mfRefundLegacyGardenAssets(Game& State) {
    const bool CanReturnStones = State.player.stones < InventoryLimit && std::any_of(State.Garden.Plots.begin(), State.Garden.Plots.end(), [](const GardenPlot& Plot) { return Plot.ReserveStones > 0; });
    const bool CanReturnHerbs = State.player.herbs < InventoryLimit && std::any_of(State.Garden.PendingHerbReturns.begin(), State.Garden.PendingHerbReturns.end(), [&](const auto& Entry) {
        const auto Found = State.player.arts.herbs.find(Entry.first);
        return Found == State.player.arts.herbs.end() || Found->second < InventoryLimit;
    });
    if (!CanReturnStones && !CanReturnHerbs) return;
    if (!mfGardenStateError(State).empty()) return;
    Game Candidate = State;
    mfApplyLegacyGardenReturns(Candidate);
    mfCommitGarden(State, Candidate);
}
// 用途：统一洞府管理门槛；流程：训练/种植/升瓶/专属秘境；返回：错误或空，无状态补全。
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
// 用途：只读灵田快照；流程：洞府页；返回：成长及预计天数，不退还旧资产或初始化状态。
GardenInfo mfGardenInfo(const Game& State) {
    GardenInfo Info; Info.PlotCountUnlocked = mfUnlockedPlots(State);
    Info.HasGreenBottle = hasTalent(State.player, "greenBottle"); Info.AccessReason = mfGardenAccessError(State);
    if (const auto Error = mfGardenStateError(State); !Error.empty()) { Info.AccessReason = Error; return Info; }
    Info.GrowthCostRule = Info.HasGreenBottle
        ? "自然成长随洞府档位为20/40/60点；总日增为自然成长与永久瓶阶附加之和，外出照常生长，无每日费用。"
        : "灵田自然成长随洞府档位为每日20/40/60点，外出照常生长，无每日费用。";
    for (int Index = 0; Index < Info.PlotCountUnlocked; ++Index) {
        auto PlotInfo = mfPlotInfo(State, static_cast<std::size_t>(Index));
        PlotInfo.Harvest = mfQuoteGardenHarvest(State, PlotInfo.PlotId);
        PlotInfo.Clear = mfQuoteGardenClear(State, PlotInfo.PlotId);
        Info.Plots.push_back(std::move(PlotInfo));
    }
    const int Natural = GardenNaturalDailyUnits[clampInt(State.player.caveTier, 0, 2)];
    for (const auto& [Name, Count] : State.player.arts.herbs) {
        const auto* Herb = findHerbByName(Name);
        if (Count > 0 && Herb && mfPlantableHerb(Herb->id))
            Info.PlantOptions.push_back({Herb->id, Herb->name, Herb->grade, Count, (mfRequiredUnits(*Herb) + Natural - 1) / Natural});
    }
    return Info;
}
// 用途：报价同格播种；流程：预览/执行共用；返回：严格1..12及持有量校验，不消耗种子。
GardenActionQuote mfQuoteGardenPlant(const Game& State, const std::string& PlotId, const std::string& HerbId, int Count) {
    GardenActionQuote Quote; Quote.PlotId = PlotId; Quote.HerbId = HerbId; Quote.SeedCount = Count;
    Quote.Reason = mfManagementError(State, PlotId);
    const auto* Herb = mfPlantableHerb(HerbId);
    if (Quote.Reason.empty() && !Herb) Quote.Reason = "此材料不能种植；妖丹不属于草药种苗。";
    if (Quote.Reason.empty() && (Count < 1 || Count > GardenMaximumPlantsPerPlot)) Quote.Reason = "种植数量须为1至12份。";
    const auto Index = mfPlotIndex(PlotId); const auto* Plot = Index ? mfPlot(State, *Index) : nullptr;
    if (Quote.Reason.empty() && Plot && !Plot->HerbId.empty() && Plot->HerbId != HerbId) Quote.Reason = "同一格只可种同一种草药。";
    if (Quote.Reason.empty() && Plot && Count > GardenMaximumPlantsPerPlot - mfSeedCount(*Plot)) Quote.Reason = "本格种苗总数不能超过12份。";
    if (Quote.Reason.empty()) {
        const auto Found = State.player.arts.herbs.find(Herb->name);
        if (Found == State.player.arts.herbs.end() || Found->second < Count || State.player.herbs < Count) Quote.Reason = "背包中此草药种苗数量不足。";
    }
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：报价全部成熟收获；流程：预览/实际命令；返回：真实产量和背包边界。
GardenActionQuote mfQuoteGardenHarvest(const Game& State, const std::string& PlotId) {
    GardenActionQuote Quote; Quote.PlotId = PlotId; Quote.Reason = mfManagementError(State, PlotId);
    const auto Index = mfPlotIndex(PlotId); const auto* Plot = Index ? mfPlot(State, *Index) : nullptr;
    const auto* Herb = Plot ? mfPlantableHerb(Plot->HerbId) : nullptr;
    if (Herb) { Quote.HerbId = Herb->id; Quote.HarvestCount = mfReadyCount(*Plot, mfRequiredUnits(*Herb)) * GardenHarvestMultiplier; }
    if (Quote.Reason.empty() && Quote.HarvestCount == 0) Quote.Reason = "本格尚无成熟草药。";
    if (Quote.Reason.empty()) {
        const auto Found = State.player.arts.herbs.find(Herb->name);
        const int Owned = Found == State.player.arts.herbs.end() ? 0 : Found->second;
        if (Owned > InventoryLimit - Quote.HarvestCount || State.player.herbs > InventoryLimit - Quote.HarvestCount) Quote.Reason = "收获后草药库存超过上限。";
    }
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：旧储备接口明确拒绝；流程：旧调用兼容；返回：禁用报价，不允许任何存取或隐式退钱。
GardenActionQuote mfQuoteGardenReserve(const Game&, const std::string& PlotId, int Amount, bool) {
    static_cast<void>(Amount);
    GardenActionQuote Quote; Quote.PlotId = PlotId;
    Quote.Reason = "灵田储备机制已移除；旧余额将在真实园艺或日结算时自动退还。"; return Quote;
}
// 用途：预览清理种苗；流程：确认/实际清理；返回：真实丢弃数。
GardenActionQuote mfQuoteGardenClear(const Game& State, const std::string& PlotId) {
    GardenActionQuote Quote; Quote.PlotId = PlotId; Quote.Reason = mfManagementError(State, PlotId);
    const auto Index = mfPlotIndex(PlotId); const auto* Plot = Index ? mfPlot(State, *Index) : nullptr;
    Quote.DiscardCount = Plot ? mfSeedCount(*Plot) : 0;
    if (Quote.Reason.empty() && Quote.DiscardCount == 0) Quote.Reason = "本格没有需要清理的种苗。";
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：播种候选后一次提交；流程：真实园艺；返回：空成功或拒绝，新批次不继承旧进度。
std::string mfPlantGardenHerbs(Game& State, const std::string& PlotId, const std::string& HerbId, int Count) {
    const auto Quote = mfQuoteGardenPlant(State, PlotId, HerbId, Count); if (!Quote.Enabled) return Quote.Reason;
    Game Candidate = State; mfInitializeGarden(Candidate);
    auto& Plot = Candidate.Garden.Plots[*mfPlotIndex(PlotId)]; const auto* Herb = mfPlantableHerb(HerbId);
    Plot.HerbId = HerbId;
    auto Batch = std::find_if(Plot.Cohorts.begin(), Plot.Cohorts.end(), [](const GardenCohort& Entry) { return Entry.ProgressUnits == 0; });
    if (Batch == Plot.Cohorts.end()) Plot.Cohorts.push_back({Count, 0}); else Batch->Count += Count;
    Candidate.player.arts.herbs[Herb->name] -= Count; Candidate.player.herbs -= Count;
    mfRefundLegacyGardenAssets(Candidate);
    logEvent(Candidate, "self", "在" + PlotId + "播种「" + Herb->name + "」" + std::to_string(Count) + "份；每份成熟可收3份。", false);
    mfCommitGarden(State, Candidate); return {};
}
// 用途：只收成熟批次；流程：真实收获；返回：空成功或拒绝，拒绝原子，未熟批次保留。
std::string mfHarvestGardenHerbs(Game& State, const std::string& PlotId) {
    const auto Quote = mfQuoteGardenHarvest(State, PlotId); if (!Quote.Enabled) return Quote.Reason;
    Game Candidate = State; auto& Plot = Candidate.Garden.Plots[*mfPlotIndex(PlotId)]; const auto* Herb = mfPlantableHerb(Plot.HerbId);
    const int Required = mfRequiredUnits(*Herb); const std::string Name = Herb->name;
    Candidate.player.arts.herbs[Name] += Quote.HarvestCount; Candidate.player.herbs += Quote.HarvestCount;
    std::erase_if(Plot.Cohorts, [Required](const GardenCohort& Batch) { return Batch.ProgressUnits >= Required; });
    if (Plot.Cohorts.empty()) Plot.HerbId.clear();
    mfRefundLegacyGardenAssets(Candidate);
    logEvent(Candidate, "self", "收获「" + Name + "」" + std::to_string(Quote.HarvestCount) + "份。", true);
    mfCommitGarden(State, Candidate); return {};
}
// 用途：拒绝已删除存入能力；流程：旧API兼容；返回：固定拒绝，不改状态。
std::string mfDepositGardenReserve(Game& State, const std::string& PlotId, int Amount) { return mfQuoteGardenReserve(State, PlotId, Amount, true).Reason; }
// 用途：拒绝已删除取出能力；流程：旧API兼容；返回：固定拒绝，不改状态。
std::string mfWithdrawGardenReserve(Game& State, const std::string& PlotId, int Amount) { return mfQuoteGardenReserve(State, PlotId, Amount, false).Reason; }
// 用途：清理指定地块全部种苗；流程：真实清理；返回：空成功或拒绝，不影响其他地块。
std::string mfClearGardenPlot(Game& State, const std::string& PlotId) {
    const auto Quote = mfQuoteGardenClear(State, PlotId); if (!Quote.Enabled) return Quote.Reason;
    Game Candidate = State; auto& Plot = Candidate.Garden.Plots[*mfPlotIndex(PlotId)]; Plot.Cohorts.clear(); Plot.HerbId.clear();
    mfRefundLegacyGardenAssets(Candidate);
    logEvent(Candidate, "self", "清理" + PlotId + "，放弃" + std::to_string(Quote.DiscardCount) + "份种苗。", false);
    mfCommitGarden(State, Candidate); return {};
}
// 用途：真实日后台成长；流程：calendar唯一日tick；返回：无，同日幂等，不扣任何日费，不读取灵气配给。
void mfGardenDayTick(Game& State) {
    if (!State.Garden.Initialized || State.clock.days <= State.Garden.LastGrowthDay || !mfGardenStateError(State).empty()) return;
    if (State.ended || State.player.health <= 0 || State.player.age >= State.player.lifespan) { State.Garden.LastGrowthDay = State.clock.days; return; }
    mfRefundLegacyGardenAssets(State);
    State.Garden.LastGrowthDay = State.clock.days;
    for (std::size_t Index = 0; Index < State.Garden.Plots.size(); ++Index) {
        const auto Info = mfPlotInfo(State, Index);
        if (Info.RequiredUnits == 0 || Info.GrowingCount == 0) continue;
        auto& Plot = State.Garden.Plots[Index];
        for (auto& Batch : Plot.Cohorts) Batch.ProgressUnits = std::min(Info.RequiredUnits, Batch.ProgressUnits + Info.EffectiveUnitsPerDay);
        const int NewlyReady = mfReadyCount(Plot, Info.RequiredUnits) - Info.ReadyCount;
        if (NewlyReady > 0) logEvent(State, "self", Info.PlotId + "的「" + Info.Name + "」" + std::to_string(NewlyReady) + "份成熟。", false);
    }
}
// 用途：校验新版状态及待返资产；流程：加载/动作；返回：错误或空，不修复输入。
std::string mfGardenStateError(const Game& State) {
    const auto& Garden = State.Garden;
    if (const auto Error = mfGreenBottleStateError(State.player); !Error.empty()) return Error;
    if (Garden.RulesVersion != GardenRulesVersion) return "灵田规则版本无效。";
    if (!Garden.Initialized) return Garden.Plots.empty() && Garden.LastGrowthDay == -1 && Garden.PendingHerbReturns.empty() ? "" : "未初始化灵田包含非法持久状态。";
    if (Garden.Plots.size() != MaximumPlots || Garden.LastGrowthDay < 0 || Garden.LastGrowthDay > State.clock.days) return "灵田格数或日期无效。";
    if (Garden.PendingHerbReturns.size() > MaximumPlots) return "旧灵田待返草药记录过多。";
    int PendingTotal = 0;
    for (const auto& [Name, Count] : Garden.PendingHerbReturns) {
        const auto* Herb = findHerbByName(Name);
        if (!Herb || !mfPlantableHerb(Herb->id) || Count < 1 || Count > 2376) return "旧灵田待返草药记录无效。";
        PendingTotal += Count;
    }
    if (PendingTotal > MaximumPlots * 99 * GardenHarvestMultiplier) return "旧灵田待返草药总量无效。";
    for (std::size_t Index = 0; Index < Garden.Plots.size(); ++Index) {
        const auto& Plot = Garden.Plots[Index];
        if (Plot.ReserveStones < 0 || Plot.ReserveStones > InventoryLimit || Plot.Cohorts.size() > GardenMaximumPlantsPerPlot) return "旧灵田待返余额或批次数无效。";
        if ((!Plot.Cohorts.empty() || Plot.ReserveStones > 0) && (State.player.cave.empty() || !findLoc(State.player.cave))) return "灵田资产必须归属合法洞府。";
        if (Index >= static_cast<std::size_t>(mfUnlockedPlots(State)) && !Plot.Cohorts.empty()) return "未解锁灵田不能保存种苗。";
        if (Plot.Cohorts.empty()) { if (!Plot.HerbId.empty()) return "空灵田不应保存药材引用。"; continue; }
        const auto* Herb = mfPlantableHerb(Plot.HerbId); if (!Herb) return "灵田药材无效或属于妖丹。";
        int Count = 0;
        for (const auto& Batch : Plot.Cohorts) {
            if (Batch.Count < 1 || Batch.Count > GardenMaximumPlantsPerPlot || Batch.ProgressUnits < 0 || Batch.ProgressUnits > mfRequiredUnits(*Herb)) return "灵田批次数量或成长点无效。";
            Count += Batch.Count;
        }
        if (Count > GardenMaximumPlantsPerPlot) return "灵田种苗超过12份。";
    }
    return {};
}
}
