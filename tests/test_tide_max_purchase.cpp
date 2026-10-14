#include "harness.h"
#include "core/arts.h"
#include "core/play.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/foundation/state_limits.h"
#include "core/simulation/tide_rules.h"
#include "core/tide.h"
#include <algorithm>
#include <limits>

namespace nztest {
namespace {
// 用途：构造本期战后坊市的独立内存快照；参与买满报价和结算回归。
// 使用：无参数，不读取或写入玩家存档；返回：城中、有灵石、药材和器材各八份的游戏。
nz::Game mfMaxPurchaseFixture() {
    nz::Game State;
    State.player.name = "兽潮买满测试";
    State.player.location = "wuling";
    State.player.stones = nz::tide::rules::InventoryLimit;
    State.player.herbs = 0;
    State.clock.days = 100;
    State.tide.eventId = "max-purchase-fixture";
    State.tide.startDay = 40;
    State.tide.endDay = 100;
    State.tide.lastProcessedDay = 100;
    State.tide.marketExpiresDay = 130;
    State.tide.settled = true;
    State.tide.totalScore = 6000;
    State.tide.playerJoined = true;
    State.tide.playerScore = 100;
    State.tide.playerRank = 1;
    State.tide.marketStock = {{"herb", "6201", 8}, {"material", "gold", 8}};
    return State;
}

// 用途：通过真实目录定位一个商品报价；参与买满回归，使用稳定类别与 ID 查询。
// 返回：对应快照；未找到时返回不可用默认值，让调用断言明确失败。
nz::tide::MarketItem mfMaxPurchaseItem(const nz::Game& State,
                                       const std::string& Kind = "material",
                                       const std::string& MaterialId = "gold") {
    for (const auto& Item : nz::tide::marketItems(State))
        if (Item.kind == Kind && Item.id == MaterialId) return Item;
    return {};
}

// 用途：证明拒绝购买不改变完整活动事实；参与陈旧报价与非法数据边界回归。
// 使用：传独立游戏和原报价数量；返回：无，断言失败原因和资源、时钟、RNG 原子性。
void mfCheckMaxPurchaseRejected(nz::Game& State, const nz::tide::MarketItem& Item,
                                int PurchaseCount, const std::string& Label) {
    const auto Before = nz::saveToJson(State);
    const auto Result = nz::tide::buyMarket(State, State.tide.eventId, Item.kind,
                                          Item.id, PurchaseCount, Item.unitPrice);
    check(!Result.ok && !Result.error.empty(), Label + "明确拒绝");
    checkStr(nz::saveToJson(State), Before, Label + "失败不改变资源、库存、时钟和随机数");
}

// 用途：用依赖类型记录新报价合同的红绿过程；参与定向测试和统一自测。
// 使用：以真实 MarketItem 实例化；返回：无，旧合同明确失败，新合同进入真实规则断言。
template <class MarketEntry>
void mfCheckMaxPurchaseContract() {
    if constexpr (!requires(const MarketEntry& Item) {
        Item.MaxPurchaseCount;
        Item.MaxPurchaseCost;
        Item.MaxPurchaseReason;
    }) {
        check(false, "战后坊市应提供后端一键买满数量、总价与拒绝原因");
    } else {
        using namespace nz;
        {
            auto State = mfMaxPurchaseFixture();
            const auto Before = saveToJson(State);
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            checkEq(Quote.MaxPurchaseCount, 8, "充足余额时买满数等于真实有限库存");
            checkEq(Quote.MaxPurchaseCost, Quote.unitPrice * 8, "买满总价使用现有权威折后单价");
            check(Quote.MaxPurchaseReason.empty(), "可成交买满报价没有错误原因");
            checkEq(Quote.basePrice, play::normalMaterialBuyPrice(State, Quote.kind, Quote.id),
                "买满与普通材料市场价同源");
            checkStr(saveToJson(State), Before, "查询买满报价不改游戏、时钟、RNG 或存档事实");
            const MarketEntry RepeatedQuote = mfMaxPurchaseItem(State);
            checkEq(RepeatedQuote.MaxPurchaseCount, Quote.MaxPurchaseCount, "重复查询买满数量确定一致");
            const int StonesBefore = State.player.stones;
            const auto Result = tide::buyMarket(State, State.tide.eventId, Quote.kind, Quote.id,
                                                Quote.MaxPurchaseCount, Quote.unitPrice);
            check(Result.ok, "买满报价可通过原有购买入口真实成交");
            checkEq(Result.cost, Quote.MaxPurchaseCost, "真实扣款等于预览总价");
            checkEq(State.player.stones, StonesBefore - Quote.MaxPurchaseCost, "买满一次扣除精确总价");
            checkEq(State.player.arts.materials.at("gold"), 8, "买满真实添加全部炼器材料");
            checkEq(State.tide.marketStock[1].stock, 0, "买满耗尽对应库存");
            const MarketEntry SoldOutQuote = mfMaxPurchaseItem(State);
            check(SoldOutQuote.MaxPurchaseCount == 0 && SoldOutQuote.MaxPurchaseCost == 0 &&
                !SoldOutQuote.MaxPurchaseReason.empty(), "已售罄买满数与总价为零且明确原因");
            mfCheckMaxPurchaseRejected(State, Quote, Quote.MaxPurchaseCount, "重复提交原买满数量");
        }
        {
            auto State = mfMaxPurchaseFixture();
            const MarketEntry FullQuote = mfMaxPurchaseItem(State);
            State.player.stones = FullQuote.unitPrice * 5 + FullQuote.unitPrice - 1;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            checkEq(Quote.MaxPurchaseCount, 5, "余额允许五份时买满向下取整");
            checkEq(Quote.MaxPurchaseCost, Quote.unitPrice * 5, "余额限制下总价精确");
            mfCheckMaxPurchaseRejected(State, FullQuote, FullQuote.MaxPurchaseCount, "余额下降后的陈旧买满数量");
            State.player.stones = Quote.unitPrice - 1;
            const MarketEntry InsufficientQuote = mfMaxPurchaseItem(State);
            check(InsufficientQuote.MaxPurchaseCount == 0 && !InsufficientQuote.MaxPurchaseReason.empty(),
                "连一份也买不起时明确余额原因");
        }
        {
            auto State = mfMaxPurchaseFixture();
            State.player.arts.materials["gold"] = tide::rules::InventoryLimit - 3;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            checkEq(Quote.MaxPurchaseCount, 3, "炼器材料买满遵守单物品上限");
            check(tide::buyMarket(State, State.tide.eventId, Quote.kind, Quote.id,
                Quote.MaxPurchaseCount, Quote.unitPrice).ok, "容量限制的买满报价仍能真实成交");
            checkEq(State.player.arts.materials.at("gold"), tide::rules::InventoryLimit,
                "买满后单物品恰好封顶而不溢出");
        }
        {
            auto State = mfMaxPurchaseFixture();
            const auto* Herb = findHerbById("6201");
            check(Herb != nullptr, "测试药材为真实目录材料");
            if (Herb) {
                State.player.arts.herbs[Herb->name] = tide::rules::InventoryLimit - 2;
                const MarketEntry SingleQuote = mfMaxPurchaseItem(State, "herb", "6201");
                checkEq(SingleQuote.MaxPurchaseCount, 2, "药材买满遵守单药材及分类总数上限");
                State.player.arts.herbs.clear();
                State.player.arts.herbs["另一种药材"] = tide::rules::InventoryLimit - 3;
                const MarketEntry TypedQuote = mfMaxPurchaseItem(State, "herb", "6201");
                checkEq(TypedQuote.MaxPurchaseCount, 3, "买满考虑全部已分类药材总容量");
                State.player.herbs = tide::rules::InventoryLimit - 1;
                const MarketEntry AggregateQuote = mfMaxPurchaseItem(State, "herb", "6201");
                checkEq(AggregateQuote.MaxPurchaseCount, 1, "买满取分类与汇总药材容量的较小值");
                check(tide::buyMarket(State, State.tide.eventId, "herb", "6201",
                    AggregateQuote.MaxPurchaseCount, AggregateQuote.unitPrice).ok,
                    "汇总容量限制的药材买满报价可真实成交");
                checkEq(State.player.herbs, tide::rules::InventoryLimit, "药材买满同时更新汇总药材数");
                checkEq(State.player.arts.herbs.at(Herb->name), 1, "药材买满使用名称背包键");
                const MarketEntry FullHerbQuote = mfMaxPurchaseItem(State, "herb", "6201");
                check(FullHerbQuote.MaxPurchaseCount == 0 && !FullHerbQuote.MaxPurchaseReason.empty(),
                    "汇总药材已满时拒绝并给出原因");
            }
        }
        for (const int InvalidCount : {-1, tide::rules::InventoryLimit + 1}) {
            auto State = mfMaxPurchaseFixture();
            State.player.arts.materials["gold"] = InvalidCount;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            check(Quote.MaxPurchaseCount == 0 && !Quote.MaxPurchaseReason.empty(),
                "非法单物品库存不产生可买满报价");
            mfCheckMaxPurchaseRejected(State, Quote, 1, "非法单物品库存交易");
            State = mfMaxPurchaseFixture();
            State.player.herbs = InvalidCount;
            const MarketEntry HerbQuote = mfMaxPurchaseItem(State, "herb", "6201");
            check(HerbQuote.MaxPurchaseCount == 0 && !HerbQuote.MaxPurchaseReason.empty(),
                "非法汇总药材库存不产生可买满报价");
            mfCheckMaxPurchaseRejected(State, HerbQuote, 1, "非法汇总药材交易");
        }
        {
            auto State = mfMaxPurchaseFixture();
            State.player.arts.herbs["无效数量药材"] = -1;
            const MarketEntry Quote = mfMaxPurchaseItem(State, "herb", "6201");
            check(Quote.MaxPurchaseCount == 0 && !Quote.MaxPurchaseReason.empty(),
                "其他分类药材负库存也会阻止药材买满");
            mfCheckMaxPurchaseRejected(State, Quote, 1, "分类药材记录无效");
        }
        for (const int InvalidStones : {-1, tide::rules::InventoryLimit + 1}) {
            auto State = mfMaxPurchaseFixture();
            State.player.stones = InvalidStones;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            check(Quote.MaxPurchaseCount == 0 && !Quote.MaxPurchaseReason.empty(),
                "非法灵石余额不能获得买满报价");
            mfCheckMaxPurchaseRejected(State, Quote, 1, "非法余额买入");
        }
        for (const int InvalidStock : {-1, tide::rules::MaximumStock + 1}) {
            auto State = mfMaxPurchaseFixture();
            State.tide.marketStock[1].stock = InvalidStock;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            check(!Quote.enabled && Quote.MaxPurchaseCount == 0 && !Quote.MaxPurchaseReason.empty(),
                "非法兽潮库存不会被截断成可买满库存");
            mfCheckMaxPurchaseRejected(State, Quote, 1, "非法兽潮库存买入");
        }
        for (const double InvalidMarket : {std::numeric_limits<double>::quiet_NaN(),
                                           std::numeric_limits<double>::infinity(), -1.0}) {
            auto State = mfMaxPurchaseFixture();
            State.world.market = InvalidMarket;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            check(Quote.MaxPurchaseCount == 0 && Quote.MaxPurchaseCost == 0 &&
                !Quote.MaxPurchaseReason.empty(), "非法行情不产生可成交买满价格");
            mfCheckMaxPurchaseRejected(State, Quote, 1, "非法市场价格交易");
        }
        {
            auto State = mfMaxPurchaseFixture();
            State.world.market = 10000000.0;
            State.tide.marketStock[1].stock = tide::rules::MaximumStock;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            check(Quote.unitPrice > 0 && Quote.MaxPurchaseCount > 0 &&
                Quote.MaxPurchaseCost <= State.player.stones,
                "极大合法单价由余额限制数量，乘法不溢出");
            check(tide::buyMarket(State, State.tide.eventId, Quote.kind, Quote.id,
                Quote.MaxPurchaseCount, Quote.unitPrice).ok,
                "极大合法单价的买满报价可以真实成交");
        }
        {
            auto State = mfMaxPurchaseFixture();
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            State.clock.days = State.tide.marketExpiresDay;
            const MarketEntry ClosedQuote = mfMaxPurchaseItem(State);
            check(ClosedQuote.MaxPurchaseCount == 0 && !ClosedQuote.MaxPurchaseReason.empty(),
                "市场到期后买满明确不可用");
            mfCheckMaxPurchaseRejected(State, Quote, Quote.MaxPurchaseCount, "市场到期陈旧报价");
        }
        for (const int InvalidSequence : {-1, StateLimits::MaxPersistedInteger}) {
            auto State = mfMaxPurchaseFixture();
            State.eventSeq = InvalidSequence;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            check(!Quote.enabled && Quote.MaxPurchaseCount == 0 && !Quote.MaxPurchaseReason.empty(),
                "纪事编号非法或已满时买满报价明确不可用");
            mfCheckMaxPurchaseRejected(State, Quote, 1, "纪事编号无法安全提交时购买");
        }
        {
            CreateOptions Options;
            Options.name = "兽潮交易存档边界";
            Options.seed = 20261014;
            Options.traitsExplicit = true;
            auto State = createGame(Options);
            State.npcs.clear();
            State.pending.reset();
            State.job.reset();
            State.dungeons.run.reset();
            State.player.arts.battle.reset();
            State.player.location = "wuling";
            State.player.stones = tide::rules::InventoryLimit;
            State.player.herbs = 0;
            State.player.arts.herbs.clear();
            State.player.arts.materials.clear();
            State.clock.days = 100;
            State.tide = mfMaxPurchaseFixture().tide;
            State.tide.rulesVersion = tide::rules::Version;
            State.eventSeq = StateLimits::MaxPersistedInteger - 1;
            const MarketEntry Quote = mfMaxPurchaseItem(State);
            check(Quote.enabled && Quote.MaxPurchaseCount > 0, "最后一个合法纪事编号允许买满报价");
            const auto BeforeLogCount = State.logs.size();
            const auto Result = tide::buyMarket(State, State.tide.eventId, Quote.kind, Quote.id,
                                                Quote.MaxPurchaseCount, Quote.unitPrice);
            check(Result.ok, "最后一个合法纪事编号可以完成买满结算");
            checkEq(State.eventSeq, StateLimits::MaxPersistedInteger, "成功交易纪事编号恰好达到存档上限");
            checkEq(State.logs.size(), BeforeLogCount + 1, "一键买满只提交一条纪事");
            Game Restored;
            string Error;
            check(loadFromJson(saveToJson(State), Restored, &Error), "边界交易完整存档可重新载入：" + Error);
            checkEq(Restored.player.stones, State.player.stones, "边界存读档保留交易后灵石");
            checkEq(Restored.player.arts.materials["gold"], State.player.arts.materials.at("gold"),
                "边界存读档保留买满材料");
            checkEq(Restored.eventSeq, StateLimits::MaxPersistedInteger, "边界存读档保留最大合法纪事编号");
            const MarketEntry NextQuote = mfMaxPurchaseItem(State, "herb", "6201");
            check(!NextQuote.enabled && !NextQuote.MaxPurchaseReason.empty(), "纪事达到上限后其他材料也不可再买");
            mfCheckMaxPurchaseRejected(State, NextQuote, 1, "合法最后纪事之后的继续购买");
        }
    }
}
} // namespace

// 用途：验证兽潮逐件一键买满的纯报价与真实成交；参与统一自测与本轮定向回归。
// 使用：由测试入口调用，不接触玩家服务或存档；返回：无，通过 harness 记录结果。
void mfRunTideMaxPurchaseTests() {
    std::printf("\n--- 兽潮材料一键买满报价与边界 ---\n");
    mfCheckMaxPurchaseContract<nz::tide::MarketItem>();
}
} // namespace nztest
