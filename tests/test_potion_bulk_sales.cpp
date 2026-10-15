#include "harness.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/foundation/state_limits.h"

#include <limits>

namespace nztest {
namespace {
using namespace nz;
constexpr const char* PotionId = "5001";

// 用途：建立独立城中出售状态；参与批量丹药规则测试；返回自有副本，不接触存档或服务。
Game mfPotionSaleState() {
    CreateOptions Options;
    Options.seed = 2026101421;
    Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.npcs.clear();
    State.pending.reset();
    State.job.reset();
    State.dungeons.run.reset();
    State.player.arts.battle.reset();
    State.player.location = "wuling";
    State.player.stones = 100;
    State.player.renown = 0;
    State.player.traits.clear();
    State.player.arts.potions.clear();
    State.player.arts.potions[PotionId] = 5;
    State.player.arts.progress.wealth = 100;
    State.economy.sold.clear();
    State.world.market = 1.0;
    State.economy.marketCash = 10000;
    return State;
}

// 用途：确认拒绝动作原子且保持全部游戏状态；参与非法数量/过期报价回归；返回无，记录断言。
void mfCheckSaleRejected(Game& State, const std::string& ItemId, int Count,
                         const std::string& Label, int ExpectedPrice = -1) {
    const std::string Before = saveToJson(State);
    const auto Result = shop::mfSellPotions(State, ItemId, Count, ExpectedPrice);
    check(!Result.ok && !Result.error.empty(), Label + "明确拒绝");
    checkStr(saveToJson(State), Before, Label + "不改变丹药、账本、灵石、纪事、时间或随机数");
}
}

// 用途：验证丹药售尽共享报价、真实结算与边界；参与定向自测；返回无，失败写入统一测试计数。
void mfRunPotionBulkSalesTests() {
    using namespace nz;
    std::printf("\n--- 丹药批量售尽报价与交易 ---\n");
    {
        Game State = mfPotionSaleState();
        const std::string Before = saveToJson(State);
        const auto Quote = shop::mfQuoteMaxPotionSale(State, PotionId);
        const auto Shared = shop::mfQuoteMaxPotionSale(State, PotionId, shop::mfMarketSaleContext(State));
        check(Quote.Enabled, "城中有丹药与资金可以报价售尽");
        checkEq(Quote.Count, 5, "持有量五枚限制可售数量");
        checkEq(Quote.UnitPrice, shop::salePrice(State, "potion", PotionId), "单位售值复用现有材料价格规则");
        checkEq(Quote.TotalGain, Quote.Count * Quote.UnitPrice, "总售值为允许数量乘同源单价");
        check(Quote.Count == Shared.Count && Quote.UnitPrice == Shared.UnitPrice && Quote.Reason == Shared.Reason,
              "共享目录上下文与单次报价一致");
        check(!Quote.LimitReason.empty(), "报价说明最大数量限制来源");
        checkStr(saveToJson(State), Before, "重复纯报价不改变完整活动状态");
        const auto LogCount = State.logs.size();
        const int EventSequence = State.eventSeq;
        const int MarketCash = State.economy.marketCash;
        const auto Result = shop::mfSellPotions(State, PotionId, Quote.Count, Quote.UnitPrice);
        check(Result.ok && Result.gain == Quote.TotalGain, "售尽按报价一次成功结算");
        checkEq(State.player.arts.potions.at(PotionId), 0, "一次扣除售尽丹药数量");
        check(State.economy.sold.empty(), "取消月需求后成交不再新增销量限制账");
        checkEq(State.economy.marketCash, MarketCash - Quote.TotalGain, "坊市收购资金扣除实际总售值");
        checkEq(State.player.stones, 100 + Quote.TotalGain, "背包余额增加实际总售值");
        checkEq(State.player.arts.progress.wealth, State.player.stones, "财富成就按新的历史最高余额更新");
        checkEq(State.logs.size(), LogCount + 1, "批量交易仅写一则纪事");
        checkEq(State.eventSeq, EventSequence + 1, "批量交易事件号仅增加一次");
        check(!shop::mfQuoteMaxPotionSale(State, PotionId).Enabled, "售尽后报价立即失效");
        mfCheckSaleRejected(State, PotionId, Quote.Count, "重复售尽请求", Quote.UnitPrice);
    }
    {
        Game State = mfPotionSaleState();
        State.player.arts.potions[PotionId] = 20;
        const auto Quote = shop::mfQuoteMaxPotionSale(State, PotionId);
        checkEq(Quote.Count, 20, "持有20枚且资金充足可全部出售");
        check(shop::mfSellPotions(State, PotionId, Quote.Count).ok, "取消八枚限制后20枚可一次出售");
        checkEq(State.player.arts.potions.at(PotionId), 0, "真实售尽20枚");
        mfCheckSaleRejected(State, PotionId, 1, "售尽后库存不足的出售");
    }
    {
        Game State = mfPotionSaleState();
        const int Price = shop::salePrice(State, "potion", PotionId);
        State.economy.marketCash = 3 * Price + Price / 2;
        const auto Quote = shop::mfQuoteMaxPotionSale(State, PotionId);
        checkEq(Quote.Count, 3, "收购资金只够三枚时向下取整");
        mfCheckSaleRejected(State, PotionId, 4, "超出当前资金可售数量");
        check(shop::mfSellPotions(State, PotionId, Quote.Count).ok, "资金允许的三枚成功出售");
        checkEq(State.economy.marketCash, Price / 2, "不足一枚的资金尾数保留");
    }
    {
        Game State = mfPotionSaleState();
        State.economy.sold["potion:" + std::string(PotionId)] = 6;
        checkEq(shop::mfQuoteMaxPotionSale(State, PotionId).Count, 5, "旧月销量不限制现有五枚丹药");
        check(shop::mfSellPotions(State, PotionId, 3).ok, "原剩余需求两枚不再阻止出售三枚");
        checkEq(State.economy.sold.at("potion:" + std::string(PotionId)), 6, "旧停用需求账保留但不再累计");
    }
    {
        Game State = mfPotionSaleState();
        const int Price = shop::salePrice(State, "potion", PotionId);
        State.player.stones = StateLimits::MaxPersistedInteger - 2 * Price;
        checkEq(shop::mfQuoteMaxPotionSale(State, PotionId).Count, 2, "灵石余额容量限制最大售数");
        mfCheckSaleRejected(State, PotionId, 3, "灵石余额乘加溢出边界");
        check(shop::mfSellPotions(State, PotionId, 2).ok, "精确达到存档灵石上限可以结算");
        checkEq(State.player.stones, StateLimits::MaxPersistedInteger, "极值出售余额保持在存档可读范围");
        Game Restored;
        std::string Error;
        check(loadFromJson(saveToJson(State), Restored, &Error), "合法上限出售后的真实存档可以读回：" + Error);
        checkEq(Restored.player.stones, StateLimits::MaxPersistedInteger, "上限出售存读档完整保留灵石");
        checkEq(Restored.player.arts.progress.wealth, StateLimits::MaxPersistedInteger, "财富极值同样保持在可存读范围");
        mfCheckSaleRejected(State, PotionId, 1, "达到存档灵石上限后出售");
    }
    {
        Game State = mfPotionSaleState();
        mfCheckSaleRejected(State, PotionId, 0, "零数量");
        mfCheckSaleRejected(State, PotionId, -1, "负数量");
        mfCheckSaleRejected(State, PotionId, std::numeric_limits<int>::max(), "极大数量");
        mfCheckSaleRejected(State, "unknown-potion", 1, "未知丹方ID");
        check(State.economy.sold.find("potion:unknown-potion") == State.economy.sold.end(), "拒绝未知ID不插入需求键");
        check(State.player.arts.potions.find("unknown-potion") == State.player.arts.potions.end(), "拒绝未知ID不插入库存键");
        const int Price = shop::salePrice(State, "potion", PotionId);
        mfCheckSaleRejected(State, PotionId, 2, "过期单价", Price + 1);
        mfCheckSaleRejected(State, PotionId, 2, "非法预期单价哨兵", -2);
        State.world.market = 1.5;
        mfCheckSaleRejected(State, PotionId, 2, "行情变化后旧报价", Price);
    }
    {
        Game State = mfPotionSaleState();
        check(shop::mfSellPotions(State, PotionId, 2).ok, "滑条允许出售小于售尽上限的正数量");
        checkEq(State.player.arts.potions.at(PotionId), 3, "滑条部分出售准确扣除");
        State.player.arts.progress.wealth = 100000;
        check(shop::mfSellPotions(State, PotionId, 1).ok, "一次单枚出售同样支持新入口");
        checkEq(State.player.arts.progress.wealth, 100000, "出售不降低已记录财富最高值");
    }
    for (const std::string Scenario : {"outside", "ended", "event", "battle", "job", "dungeon", "empty", "zero-cash",
                                       "negative-stones", "negative-cash", "negative-owned", "event-overflow",
                                       "stones-save-overflow", "cash-save-overflow"}) {
        Game State = mfPotionSaleState();
        if (Scenario == "outside") State.player.location = "yishan";
        if (Scenario == "ended") State.ended = true;
        if (Scenario == "event") State.pending.emplace();
        if (Scenario == "battle") State.player.arts.battle.emplace();
        if (Scenario == "job") State.job.emplace();
        if (Scenario == "dungeon") State.dungeons.run.emplace();
        if (Scenario == "empty") State.player.arts.potions.clear();
        if (Scenario == "zero-cash") State.economy.marketCash = 0;
        if (Scenario == "negative-stones") State.player.stones = -1;
        if (Scenario == "negative-cash") State.economy.marketCash = -1;
        if (Scenario == "negative-owned") State.player.arts.potions[PotionId] = -1;
        if (Scenario == "event-overflow") State.eventSeq = StateLimits::MaxPersistedInteger;
        if (Scenario == "stones-save-overflow") State.player.stones = StateLimits::MaxPersistedInteger + 1;
        if (Scenario == "cash-save-overflow") State.economy.marketCash = StateLimits::MaxPersistedInteger + 1;
        const auto Quote = shop::mfQuoteMaxPotionSale(State, PotionId);
        check(!Quote.Enabled && Quote.Count == 0 && !Quote.Reason.empty(), Scenario + " 报价明确不可售");
        mfCheckSaleRejected(State, PotionId, 1, Scenario + " 出售");
    }
    {
        Game State = mfPotionSaleState();
        State.player.arts.potions["5512"] = StateLimits::MaxPersistedInteger;
        State.economy.marketCash = StateLimits::MaxPersistedInteger;
        State.player.renown = StateLimits::MaxPersistedInteger;
        const auto Quote = shop::mfQuoteMaxPotionSale(State, "5512");
        check(Quote.Enabled && Quote.TotalGain > 0 && Quote.Count > 0, "大持有量高品丹药报价保持正值且不溢出");
        check(shop::mfSellPotions(State, "5512", Quote.Count, Quote.UnitPrice).ok, "大持有量按有限报价成交");
        Game Restored;
        std::string Error;
        check(loadFromJson(saveToJson(State), Restored, &Error), "大持有量高品丹药成交后的完整存档可读：" + Error);
        checkEq(Restored.player.arts.potions.at("5512"), State.player.arts.potions.at("5512"), "高品大量出售后存读完整保留剩余数量");
    }
    {
        Game State = mfPotionSaleState();
        State.eventSeq = StateLimits::MaxPersistedInteger - 1;
        check(shop::mfSellPotions(State, PotionId, 1).ok, "最后一个合法纪事编号允许成交");
        checkEq(State.eventSeq, StateLimits::MaxPersistedInteger, "成功纪事编号最大为存档上限");
        Game Restored;
        std::string Error;
        check(loadFromJson(saveToJson(State), Restored, &Error), "纪事编号合法上限的完整存档可读：" + Error);
        mfCheckSaleRejected(State, PotionId, 1, "纪事编号已到存档上限");
    }
    {
        const Game State = mfPotionSaleState();
        Game Restored;
        std::string Error;
        check(loadFromJson(saveToJson(State), Restored, &Error), "现有存档无需新增字段即可读回：" + Error);
        const auto Quote = shop::mfQuoteMaxPotionSale(Restored, PotionId);
        check(Quote.Enabled && Quote.Count == 5, "旧库存和旧当月余额直接用于售尽报价");
        check(shop::mfSellPotions(Restored, PotionId, Quote.Count, Quote.UnitPrice).ok, "读回存档按当前报价成功售尽");
    }
}
} // namespace nztest
