#include "core/shop.h"
#include "core/arts.h"
#include "core/sim.h"
#include "core/foundation/state_limits.h"

#include <algorithm>

namespace nz::shop {
namespace {

// 用途：检查批量交易的共同状态门槛；参与报价与执行；使用活动状态，返回失败中文原因或空串。
std::string mfPotionSaleStateError(const Game& State) {
    if (State.ended) return "这一世已经结束。";
    if (State.pending.has_value()) return "请先完成当前事件。";
    if (State.player.arts.battle.has_value()) return "斗法中无法交易。";
    if (State.job.has_value()) return "请先完成或中止当前安排。";
    if (State.dungeons.run.has_value()) return "请先退出秘境，再前往坊市出售。";
    // 共用现有资源上限，保证成交生成的余额与纪事仍满足存档数字校验，而非仅能装入int。
    if (State.player.stones < 0 || State.player.stones > StateLimits::MaxPersistedInteger ||
        State.economy.marketCash < 0 || State.economy.marketCash > StateLimits::MaxPersistedInteger)
        return "当前交易余额无效，无法出售。";
    if (State.eventSeq < 0 || State.eventSeq >= StateLimits::MaxPersistedInteger)
        return "纪事编号已达支持范围，无法提交交易。";
    return {};
}

// 用途：构造不带资源副作用的拒售结果；参与批量动作所有拒绝路径；返回带明确原因的失败值。
SellResult mfPotionSaleFailure(const std::string& Reason) {
    SellResult Result;
    Result.error = Reason;
    return Result;
}

} // namespace

// 用途：按持有量快速筛出无货丹药再计算坊市上下文；参与单种出售；返回纯报价，不推进任何活动状态。
PotionSaleQuote mfQuoteMaxPotionSale(const Game& State, const std::string& ItemId) {
    const auto Owned = State.player.arts.potions.find(ItemId);
    if (!findRecipe(ItemId) || Owned == State.player.arts.potions.end() || Owned->second <= 0)
        return mfQuoteMaxPotionSale(State, ItemId, MarketSaleContext{});
    return mfQuoteMaxPotionSale(State, ItemId, mfMarketSaleContext(State));
}

// 用途：复用既有出售价格/资格和坊市能力，计算单种丹药售尽上限；参与目录预览和真实动作复核。
// 使用：Context 仅为本次后端只读响应快照；返回最大可售数量、真实总收益及限制原因，无资源/时钟/RNG副作用。
PotionSaleQuote mfQuoteMaxPotionSale(const Game& State, const std::string& ItemId,
                                   const MarketSaleContext& Context) {
    PotionSaleQuote Quote;
    if (!findRecipe(ItemId)) {
        Quote.Reason = "没有这种丹药。";
        return Quote;
    }
    Quote.Reason = mfPotionSaleStateError(State);
    if (!Quote.Reason.empty()) return Quote;
    const auto Owned = State.player.arts.potions.find(ItemId);
    if (Owned == State.player.arts.potions.end() || Owned->second <= 0) {
        Quote.Reason = "没有这枚丹药。";
        return Quote;
    }
    const auto Sold = State.economy.sold.find("potion:" + ItemId);
    if (Sold != State.economy.sold.end() && Sold->second < 0) {
        Quote.Reason = "当前货品需求账无效，无法出售。";
        return Quote;
    }
    const auto SingleQuote = saleQuote(State, "potion", ItemId, Context);
    Quote.UnitPrice = SingleQuote.returnStones;
    if (!SingleQuote.enabled) {
        Quote.Reason = SingleQuote.reason;
        return Quote;
    }
    const auto MarketQuote = mfQuoteMarketSale(State, "potion", ItemId, Quote.UnitPrice, Context);
    const int BalanceCapacity = (StateLimits::MaxPersistedInteger - State.player.stones) / Quote.UnitPrice;
    Quote.Count = std::min({Owned->second, MarketQuote.AffordableCount, BalanceCapacity});
    if (Quote.Count <= 0) {
        Quote.Reason = MarketQuote.Reason.empty() ? "当前灵石余额或坊市收购额度不允许出售。" : MarketQuote.Reason;
        return Quote;
    }
    // 数量先受余额容量约束；64位乘法预演后才转换，避免高品丹药和大持有量溢出。
    Quote.TotalGain = static_cast<int>(static_cast<long long>(Quote.Count) * Quote.UnitPrice);
    Quote.Enabled = true;
    if (Quote.Count == Owned->second) Quote.LimitReason = "持有量";
    if (Quote.Count == MarketQuote.AffordableCount) {
        if (!Quote.LimitReason.empty()) Quote.LimitReason += "、";
        Quote.LimitReason += "坊市收购资金或本月需求";
    }
    if (Quote.Count == BalanceCapacity) {
        if (!Quote.LimitReason.empty()) Quote.LimitReason += "、";
        Quote.LimitReason += "灵石余额容量";
    }
    Quote.LimitReason += "限制，最多可售 " + std::to_string(Quote.Count) + " 枚。";
    return Quote;
}

// 用途：以当前报价验证指定丹药数量并一次提交交易；参与一键售尽和数量滑条动作。
// 使用：Count>0，ExpectedUnitPrice=-1或当前单价；返回真实总收益，所有预期拒绝路径保持原状态。
SellResult mfSellPotions(Game& State, const std::string& ItemId, int Count, int ExpectedUnitPrice) {
    if (Count <= 0) return mfPotionSaleFailure("出售数量必须为正整数。");
    if (ExpectedUnitPrice < -1) return mfPotionSaleFailure("预期出售单价无效。");
    const auto Quote = mfQuoteMaxPotionSale(State, ItemId);
    if (!Quote.Enabled) return mfPotionSaleFailure(Quote.Reason);
    if (ExpectedUnitPrice != -1 && ExpectedUnitPrice != Quote.UnitPrice)
        return mfPotionSaleFailure("出售报价已变化，请刷新后重试。");
    if (Count > Quote.Count)
        return mfPotionSaleFailure("当前最多可售 " + std::to_string(Quote.Count) + " 枚。" + Quote.LimitReason);

    const long long TotalGain = static_cast<long long>(Count) * Quote.UnitPrice;
    const long long NextStones = static_cast<long long>(State.player.stones) + TotalGain;
    const std::string SaleKey = "potion:" + ItemId;
    const auto Sold = State.economy.sold.find(SaleKey);
    const int SoldCount = Sold == State.economy.sold.end() ? 0 : Sold->second;
    const long long NextSoldCount = static_cast<long long>(SoldCount) + Count;
    if (TotalGain <= 0 || TotalGain > State.economy.marketCash || NextStones > StateLimits::MaxPersistedInteger ||
        SoldCount < 0 || NextSoldCount > StateLimits::MaxPersistedInteger)
        return mfPotionSaleFailure("当前交易数值超过支持范围。");

    // 仅预演受影响的两个账本和纪事，避免拷贝全州NPC；分配失败发生在提交前。
    auto NextPotions = State.player.arts.potions;
    auto NextSold = State.economy.sold;
    NextPotions.at(ItemId) -= Count;
    NextSold.insert_or_assign(SaleKey, static_cast<int>(NextSoldCount));
    Game JournalState;
    JournalState.year = State.year;
    JournalState.player.age = State.player.age;
    JournalState.clock.days = State.clock.days;
    JournalState.eventSeq = State.eventSeq;
    JournalState.logs = State.logs;
    const auto* Recipe = findRecipe(ItemId);
    SellResult Result;
    Result.ok = true;
    Result.gain = static_cast<int>(TotalGain);
    Result.text = "坊市收购「" + std::string(Recipe->name) + "」×" + std::to_string(Count) +
        "，灵石 +" + std::to_string(Result.gain) + "。";
    logEvent(JournalState, "self", Result.text, false, "", "丹药 −" + std::to_string(Count) +
        "；灵石 +" + std::to_string(Result.gain));

    // 以下仅交换已构造容器与写入已验证整数，不再进行可失败的分配或逐枚交易。
    State.player.arts.potions.swap(NextPotions);
    State.economy.sold.swap(NextSold);
    State.economy.marketCash -= Result.gain;
    State.player.stones = static_cast<int>(NextStones);
    State.player.arts.progress.wealth = std::max(State.player.arts.progress.wealth, State.player.stones);
    State.logs.swap(JournalState.logs);
    State.eventSeq = JournalState.eventSeq;
    return Result;
}

} // namespace nz::shop
