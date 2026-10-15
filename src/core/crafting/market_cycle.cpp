#include "core/crafting/market_cycle.h"
#include "core/crafting/craft_prices.h"
#include "core/economy.h"
#include "core/foundation/state_limits.h"
#include "core/renown.h"
#include "core/shop.h"
#include "data/reference_tables.h"
#include <algorithm>
#include <cstdint>

namespace nz::crafting {
// 用途：集中保留原月预算底座；流程：三年预算纯查询；返回：灵石整数，现有价格/声望/境界乘区均保持。
int mfMonthlyEquivalentMarketBudget(const Game& State) {
    const auto& Rules = mfCraftEconomyRules();
    int BaseBudget = renown::marketCashCap(clampInt(State.player.renown, 0, Rules.MaximumBudgetRenown));
    const int MarketTier = ecoMarketTier(State.player);
    for (const auto& Recipe : data::RecipeDef_TABLE())
        if (Recipe.grade <= MarketTier) BaseBudget = std::max(BaseBudget, shop::salePrice(State, "potion", Recipe.id));
    for (int Grade = 1; Grade <= MarketTier; ++Grade) {
        const auto Quote = mfQuoteStandardGearPrice(State, Grade);
        if (Quote.Valid) BaseBudget = std::max(BaseBudget, Quote.SellPrice);
    }
    const auto Budget = static_cast<std::int64_t>(BaseBudget) * Rules.MonthlyBudgetMultiplier * mfMarketRealmMultiplier(State.player);
    return static_cast<int>(std::min<std::int64_t>(Budget, Rules.MaximumMonthlyBudget));
}

// 用途：将统一原月额度扩大50倍；流程：真实刷新与只读报价；返回：不超过通用持久整数上限的本期资金。
int mfMarketCycleBudget(const Game& State) {
    return static_cast<int>(std::min<std::int64_t>(StateLimits::MaxPersistedInteger,
        static_cast<std::int64_t>(mfMonthlyEquivalentMarketBudget(State)) * MarketCycleBudgetMultiplier));
}

// 用途：显示固定世界日账期的剩余天数；流程：报价视图；返回：下个1080日边界距离，不保存任何引用。
int mfMarketDaysUntilRefresh(const Game& State) {
    return MarketCycleDurationDays - std::max(0, State.clock.days) % MarketCycleDurationDays;
}

// 用途：在真实动作中刷新一次三年账期；流程：开局后的经济推进；返回：无，不读取随机数、不补发被跳过的历史账期。
void mfRefreshMarketCycle(Game& State) {
    const int CurrentCycle = std::max(0, State.clock.days) / MarketCycleDurationDays;
    if (State.economy.MarketCycleIndex < 0) {
        // 旧直接构造状态按其已刷新月份确定旧期；只补期号，不在同期凭空充值。
        State.economy.MarketCycleIndex = std::max(0, State.economy.month) / 36;
    }
    if (State.economy.MarketCycleIndex >= CurrentCycle) return;
    const int Budget = mfMarketCycleBudget(State);
    State.economy.marketCash = Budget;
    State.economy.MarketCycleIndex = CurrentCycle;
    State.economy.sold.clear();
}
} // namespace nz::crafting
