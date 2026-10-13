#pragma once

#include <array>
#include "core/model.h"

namespace nz::tide::rules {
inline constexpr int Version = 1;
inline constexpr int BattleDays = 60;
inline constexpr int MarketDays = 30;
inline constexpr int InventoryLimit = 1000000000;
inline constexpr int MaximumStock = 60;
inline constexpr int PointsPerStock = 3000;
inline constexpr int MinimumPayablePercent = 50;
// 类型：兽潮NPC出战节奏的不可变规则值；负责日出战概率、日击杀与本期上限，无资源或持久字段。
// BattleChanceDenominator表示每日仅有1/该值概率出战；未出战不抽击杀，已达本期上限不再投骰。
struct NpcBattlePace {
    int BattleChanceDenominator = 1;
    int MaximumDailyKills = 0;
    int MaximumEventKills = 0;
};
inline constexpr NpcBattlePace NormalNpcBattlePace{15, 1, 9};
inline constexpr NpcBattlePace EliteNpcBattlePace{10, 2, 12};
static_assert(NormalNpcBattlePace.BattleChanceDenominator > 0 && EliteNpcBattlePace.BattleChanceDenominator > 0);
struct RankBand { int maxRank, rewardYears, payablePercent; };
// 同分使用同一名次；maxRank=0是全部其余有效参战者，零分没有奖励资格。
inline constexpr std::array<RankBand,4> RankBands{{
    {1,30,50}, {3,20,60}, {10,10,75}, {0,3,90}
}};

struct Battlefield {
    int id, minStage, pointsPerKill;
    const char* name;
};
inline constexpr std::array<Battlefield, 4> Battlefields{{
    {0, 0, 1, "炼气战场"}, {1, 3, 3, "筑基战场"},
    {2, 6, 9, "金丹战场"}, {3, 9, 27, "元婴战场"}
}};

// 用途：查稳定战场编号；流程：入场、积分；用法：传编号；返回：定义指针或空。
const Battlefield* battlefield(int id);
// 用途：把修士境界映射到最高合法战场；流程：NPC 积分、默认入场；用法：传境界；返回：0..3。
int battlefieldForStage(int stage);
// 用途：按并列名次确定奖励年数；流程：金榜结算；用法：传有效名次；返回：3/10/20/30 年。
int rewardYears(int rank);
// 用途：以参加时境界折算冻结奖励；流程：金榜结算；用法：传境界与名次；返回：应发灵石。
int rewardStones(int entryStage, int rank);
// 用途：计算排名购价比例；流程：战后市场报价；用法：传名次与是否有积分；返回：实付正常价的 50..100%。
int payablePercent(int rank, bool participated);
// 用途：按全体积分计算有限库存；流程：战后市场生成；用法：传总分与品阶；返回：每种材料库存。
int stockPerItem(int totalScore, int grade);
// 用途：整数计算有底线的市场价格；流程：报价与成交共用；用法：传正常买价和实付比例；返回：不低于当前正常购价五折的向上取整单价。
int discountedPrice(int normalPrice, int payablePercent);
// 用途：为积分安全累加；流程：日模拟与击杀；用法：传原值和非负增量；返回：不溢出的积分。
int addScore(int current, int amount);
} // namespace nz::tide::rules

namespace nz::tide {
// 用途：依据冻结总分生成一次高阶库存；流程：期末结算内部使用；用法：仅未生成库存时调用；返回：无。
void initializeMarketStock(Game& game);
}
