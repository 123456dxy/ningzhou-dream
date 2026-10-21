#include "harness.h"
#include "core/calendar.h"
#include "core/crafting/market_cycle.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/foundation/state_limits.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include <limits>

namespace nztest {
namespace {
// 用途：创建独立城中交易样本；流程：账期与真实售丹；返回：无玩家文件/服务依赖的游戏副本。
nz::Game mfMarketCycleGame() {
    nz::CreateOptions Options; Options.name = "三年账期验证"; Options.seed = 20261015; Options.traitsExplicit = true;
    auto State = nz::createGame(Options);
    State.npcs.clear(); State.pending.reset(); State.job.reset(); State.dungeons.run.reset();
    State.player.arts.battle.reset(); State.player.location = "wuling"; State.player.stones = 100;
    State.player.traits.clear(); State.player.talent.clear(); State.world.market = 1.0;
    return State;
}
// 用途：将测试样本移动到一致的日历；流程：仅隔离账期边界；返回：无，不代替实际游戏推进。
void mfSetMarketTestDay(nz::Game& State, int Days) {
    State.clock.days = Days; State.year = Days / 360; State.player.age = State.clock.ageBase + State.year;
}
}

// 用途：验证三年资金、无限月销量与自然化毒；流程：定向回归；返回：无，失败写统一测试计数。
void mfRunMarketCycleTests() {
    using namespace nz;
    for (int Stage : {0, 3, 6, 9, 12}) {
        Game State = mfMarketCycleGame(); State.player.stage = Stage;
        const auto Before = saveToJson(State);
        const auto Base = crafting::mfMonthlyEquivalentMarketBudget(State);
        const int Expected = static_cast<int>(std::min<long long>(StateLimits::MaxPersistedInteger, static_cast<long long>(Base) * 50));
        checkEq(ecoMarketBudget(State), Expected, "各大境界三年资金为原月预算50倍且不溢出");
        shop::mfMarketSaleContext(State); shop::mfMarketSaleContext(State);
        check(saveToJson(State) == Before, "重复资金报价不充值、不改日历RNG或账户");
    }
    {
        Game State = mfMarketCycleGame(); State.economy.marketCash = 7;
        mfSetMarketTestDay(State, 30); ecoRefresh(State);
        checkEq(State.economy.marketCash, 7, "换月不补充三年资金");
        mfSetMarketTestDay(State, 1079); ecoRefresh(State);
        checkEq(State.economy.marketCash, 7, "三年边界前一天保留余额");
        checkEq(crafting::mfMarketDaysUntilRefresh(State), 1, "边界前正确预报剩余一天");
        mfSetMarketTestDay(State, 1080); ecoRefresh(State);
        checkEq(State.economy.marketCash, ecoMarketBudget(State), "第1080日真实刷新一次当前期资金");
        checkEq(State.economy.MarketCycleIndex, 1, "已兑现账期号持久记录");
        State.economy.marketCash -= 3; const int Remaining = State.economy.marketCash;
        ecoRefresh(State); checkEq(State.economy.marketCash, Remaining, "同一天反复刷新不重复发放");
        mfSetMarketTestDay(State, 1110); ecoRefresh(State);
        checkEq(State.economy.marketCash, Remaining, "新账期内换月继续保留已花资金");
        Game Restored; std::string Error;
        check(loadFromJson(saveToJson(State), Restored, &Error), "三年账期往返成功：" + Error);
        check(saveToJson(State) == saveToJson(Restored), "账期与剩余资金逐字段稳定往返");
        auto Legacy = json::Value::parse(saveToJson(State)); auto Ledger = Legacy["economy"];
        Ledger.asObject().erase("marketCycleIndex"); Legacy.set("economy", Ledger);
        check(loadFromJson(Legacy.dump(), Restored, &Error), "旧缺期号按已刷新月份归一");
        checkEq(Restored.economy.marketCash, Remaining, "旧档迁移不补充余额");
        for (const auto Invalid : {json::Value(-2), json::Value(2), json::Value(0.5), json::Value("bad"), json::Value(), json::Value(true)}) {
            auto Broken = json::Value::parse(saveToJson(State)); auto BrokenLedger = Broken["economy"];
            BrokenLedger.set("marketCycleIndex", Invalid); Broken.set("economy", BrokenLedger);
            const auto Saved = saveToJson(Restored);
            check(!loadFromJson(Broken.dump(), Restored, &Error), "负数/未来/小数/字符串账期明确拒绝");
            check(saveToJson(Restored) == Saved, "非法账期不能覆盖当前角色");
        }
    }
    {
        Game State = mfMarketCycleGame(); constexpr const char* Id = "5001";
        State.player.arts.potions[Id] = 100;
        const int Price = shop::salePrice(State, "potion", Id);
        State.economy.marketCash = 100 * Price;
        State.economy.sold["potion:5001"] = StateLimits::MaxPersistedInteger;
        const auto Quote = shop::mfQuoteMaxPotionSale(State, Id);
        checkEq(Quote.Count, 100, "持有100枚不再受原八枚月销量或旧已售账限制");
        check(shop::mfSellPotions(State, Id, 100, Price).ok, "真实一次售尽100枚");
        checkEq(State.player.arts.potions[Id], 0, "真实扣除100枚丹药");
        checkEq(State.economy.marketCash, 0, "只消耗资金，不存在月件数门槛");
        checkEq(State.economy.sold["potion:5001"], StateLimits::MaxPersistedInteger, "停用销量计数器不会继续累计溢出");
        const auto Saved = saveToJson(State);
        check(!shop::mfSellPotions(State, Id, 1).ok && saveToJson(State) == Saved, "库存/资金不足拒绝且不部分扣除");
    }
    {
        Game State = mfMarketCycleGame(); State.player.arts.nodes.clear();
        checkNear(calendarDetoxPerYear(State.player), 5, "普通角色基础每年化解丹毒5点");
        State.player.arts.poison = 10; State.clock.carry.erase("detox");
        for (int Day = 0; Day < 360; ++Day) {
            const int Decay = calendarTake(State, "detox", calendarDetoxPerYear(State.player) / 360.0);
            State.player.arts.poison = std::max(0, State.player.arts.poison - Decay);
        }
        checkEq(State.player.arts.poison, 5, "真实小数累积360日正好消散5点");
    }
}
} // namespace nztest
