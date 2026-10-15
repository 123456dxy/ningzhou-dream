#pragma once
#include "core/model.h"

namespace nz::crafting {
inline constexpr int MarketCycleDurationDays = 1080;
inline constexpr int MarketCycleBudgetMultiplier = 50;

// 用途：保留改版前月额度作为三年资金的统一基数；流程：预算报价；返回：已含声望/境界/行情的有限灵石数，无副作用。
[[nodiscard]] int mfMonthlyEquivalentMarketBudget(const Game& State);
// 用途：计算三年一轮的收购总资金；流程：开局/真实换期/预览；返回：原月额度50倍，按持久化余额上限饱和，不充值活动账户。
[[nodiscard]] int mfMarketCycleBudget(const Game& State);
// 用途：查询距下次固定三年边界的世界日；流程：坊市显示；返回：1..1080，不改时钟、账本或RNG。
[[nodiscard]] int mfMarketDaysUntilRefresh(const Game& State);
// 用途：在动作边界兑现新的三年资金；流程：经济月度刷新；返回：无，同一期重复调用不充值，跳期只建当前预算不累积历史额度。
void mfRefreshMarketCycle(Game& State);
} // namespace nz::crafting
