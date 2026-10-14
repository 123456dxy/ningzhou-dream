#include "core/simulation/tide_rules.h"
#include "core/society.h"
#include <algorithm>
#include <limits>

namespace nz::tide::rules {
// 用途：解析稳定战场编号；流程：入场与积分；用法：传编号；返回：合法定义指针或空。
const Battlefield* battlefield(int id) {
    return id >= 0 && id < static_cast<int>(Battlefields.size()) ? &Battlefields[id] : nullptr;
}
// 用途：选择境界对应战场；流程：默认入场和NPC模拟；用法：传境界；返回：0..3。
int battlefieldForStage(int stage) { return clampInt(stage / 3, 0, 3); }
// 用途：确定排名奖励年数；流程：金榜结算；用法：传名次；返回：保底或高排名年数。
int rewardYears(int rank) {
    if (rank<=0) return RankBands.back().rewardYears;
    for (const auto& band : RankBands) if (band.maxRank==0 || rank<=band.maxRank) return band.rewardYears;
    return RankBands.back().rewardYears;
}
// 用途：按集中排名档位读取声望奖励；参与期末发奖与只读预览，返回应发整数声望，不改变状态。
int mfRewardRenown(int Rank) {
    if (Rank <= 0) return 0;
    for (const auto& Band : RankBands)
        if (Band.maxRank == 0 || Rank <= Band.maxRank) return Band.RenownReward;
    return 0;
}
// 用途：按冻结入场境界折算俸禄奖励；流程：金榜结算；用法：传境界与名次；返回：应发灵石。
int rewardStones(int entryStage, int rank) {
    Person reference;
    reference.stage = clampInt(entryStage, 0, 12);
    return socStipend(reference) * 12 * rewardYears(rank);
}
// 用途：计算排名实付比例；流程：市场报价；用法：传名次和正分资格；返回：正常价的50..100%。
int payablePercent(int rank, bool participated) {
    if (!participated || rank <= 0) return 100;
    for (const auto& band : RankBands) if (band.maxRank==0 || rank<=band.maxRank) return band.payablePercent;
    return RankBands.back().payablePercent;
}
// 用途：按总分配置有限库存；流程：期末生成；用法：传总分和品阶；返回：有上限的每项库存。
int stockPerItem(int totalScore, int grade) {
    if (grade < MinimumStockGrade || grade > MaximumStockGrade) return 0;
    const int ScoreSteps = std::max(0, totalScore) / PointsPerStock;
    const int PreviousTierSteps = (grade - MinimumStockGrade) * (StockTierUnlockCount - 1);
    return clampInt(ScoreSteps - PreviousTierSteps, 0, MaximumStock);
}
// 用途：给出阶梯库存的有效品阶门槛；参与报价拒绝与界面说明，返回全州积分要求，无副作用。
int mfStockUnlockScore(int Grade) {
    if (Grade < MinimumStockGrade || Grade > MaximumStockGrade) return 0;
    return ((Grade - MinimumStockGrade) * (StockTierUnlockCount - 1) + 1) * PointsPerStock;
}
// 用途：用整数计算不少于正常价一半的价格；流程：报价和成交；用法：传正常价与比例；返回：安全单价。
int discountedPrice(int normalPrice, int percent) {
    if (normalPrice <= 0) return 0;
    const long long price = normalPrice;
    const int floorPrice = static_cast<int>((price * MinimumPayablePercent + 99) / 100);
    const int ranked = static_cast<int>((price * clampInt(percent, MinimumPayablePercent, 100) + 99) / 100);
    return std::max({1, floorPrice, ranked});
}
// 用途：积分安全累加；流程：NPC和玩家击杀；用法：传原值与增量；返回：不溢出的非负总量。
int addScore(int current, int amount) {
    const long long sum = static_cast<long long>(std::max(0, current)) + std::max(0, amount);
    return static_cast<int>(std::min(sum, static_cast<long long>(InventoryLimit)));
}
} // namespace nz::tide::rules
