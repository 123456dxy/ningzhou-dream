#include "harness.h"
#include "core/api.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/tide.h"
#include "data/reference_tables.h"
#include <filesystem>
#include <chrono>

namespace nztest {
namespace {
using namespace nz;

// 用途：构造独立坊市角色；流程：一键交易接口验收；用法：无参数；返回：没有在途安排的固定种子游戏。
Game mfQuickTradeFixture() {
    CreateOptions Options;
    Options.name = "一键交易验收";
    Options.seed = 20261014;
    Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.npcs.clear();
    State.pending.reset();
    State.job.reset();
    State.player.arts.battle.reset();
    State.player.location = "wuling";
    State.player.stones = 1000;
    State.economy.marketCash = 1000000;
    State.economy.sold.clear();
    State.player.arts.potions.clear();
    State.player.arts.potions[data::RecipeDef_TABLE().front().id] = 20;
    State.checkpoint = AutoCheckpoint{};
    return State;
}

// 用途：调用真实请求边界；流程：报价、出售与非法输入；用法：传路径、正文和方法；返回：真实状态码与正文。
http::Response mfQuickTradeRequest(const string& Path, const json::Value& Body = json::Object{}, const string& Method = "POST") {
    http::Request Request;
    Request.path = "/api/" + Path;
    Request.method = Method;
    Request.body = Body.dump();
    return api::handle(Request);
}

// 用途：只读取目录中指定丹药；流程：接口报价断言；用法：传完整state；返回：该丹药的JSON副本。
json::Value mfFirstPotion(const json::Value& State) {
    for (const auto& Item : State["pillCatalog"].asArray())
        if (Item["id"].asString() == data::RecipeDef_TABLE().front().id) return Item;
    return json::Value{};
}

// 用途：构造批量售药请求；流程：真实结算；用法：传数量和单价；返回：保持旧sell路径的请求对象。
json::Value mfPotionSaleRequest(int Count, int UnitPrice) {
    json::Value Body(json::Object{});
    Body.set("kind", json::Value("potion"));
    Body.set("id", json::Value(data::RecipeDef_TABLE().front().id));
    Body.set("count", json::Value(Count));
    Body.set("expectedUnitPrice", json::Value(UnitPrice));
    return Body;
}

// 类型：独立接口测试的绑定和文件路径守卫；拥有临时目录，退出恢复默认会话和路径；不读取玩家存档。
struct QuickTradeSessionScope {
    std::filesystem::path Directory;
    explicit QuickTradeSessionScope(api::Session& Session) {
        Directory = std::filesystem::temp_directory_path() /
            ("ningzhou-quick-trade-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(Directory);
        setSaveDirOverride(Directory.string());
        api::bindSession(&Session);
    }
    ~QuickTradeSessionScope() {
        api::bindSession(nullptr);
        setSaveDirOverride("");
        std::error_code Error;
        std::filesystem::remove_all(Directory, Error);
    }
};
}

// 用途：验证一键交易真实HTTP边界；流程：定向回归；用法：隔离运行；返回：无，由harness累计失败。
void mfRunTradeQuickActionsApiTests() {
    using namespace nz;
    api::Session Session;
    Session.started = true;
    Session.g = mfQuickTradeFixture();
    QuickTradeSessionScope Scope(Session);
    const string PotionId = data::RecipeDef_TABLE().front().id;
    const auto BeforeQuery = saveToJson(Session.g);
    const auto StateResponse = mfQuickTradeRequest("state", json::Object{}, "GET");
    const auto State = json::Value::parse(StateResponse.body)["state"];
    const auto Quote = mfFirstPotion(State)["maxSale"];
    check(Quote["enabled"].asBool(), "丹药目录提供一键售尽权威报价");
    check(Quote["count"].asNumber() > 1 && Quote["totalGain"].asNumber() > 0, "报价包含可售数量和总收益");
    checkStr(saveToJson(Session.g), BeforeQuery, "完整报价查询不修改游戏和随机数");

    const int Price = shop::salePrice(Session.g, "potion", PotionId);
    const int InitialStones = Session.g.player.stones;
    const auto SaleResponse = mfQuickTradeRequest("sell", mfPotionSaleRequest(3, Price));
    const auto Sale = json::Value::parse(SaleResponse.body);
    checkEq(SaleResponse.status, 200, "批量售出使用既有sell接口");
    checkEq(Session.g.player.arts.potions.at(PotionId), 17, "批量出售真实扣除三枚");
    checkEq(Session.g.player.stones - InitialStones, 3LL * Price, "批量出售真实收益与报价相符");
    checkEq(Session.g.economy.sold.at("potion:" + PotionId), 3, "批量出售累计同类需求三枚");
    checkEq(static_cast<int>(Sale["gain"].asNumber()), 3 * Price, "接口返回本笔实际总收益");

    for (const int Count : {0, -1, 21}) {
        Session.g = mfQuickTradeFixture();
        const string Before = saveToJson(Session.g);
        const auto Response = mfQuickTradeRequest("sell", mfPotionSaleRequest(Count, Price));
        check(Response.status >= 400, "非法或超过持有量的出售数量被拒绝");
        check(saveToJson(Session.g) == Before, "拒绝数量不部分出售或改写账本");
    }
    Session.g = mfQuickTradeFixture();
    const string BeforeStale = saveToJson(Session.g);
    check(mfQuickTradeRequest("sell", mfPotionSaleRequest(2, Price + 1)).status >= 400, "过期售药单价明确拒绝");
    check(saveToJson(Session.g) == BeforeStale, "过期报价不转移资源");

    for (const auto& InvalidCount : {json::Value(true), json::Value("3"), json::Value(1.5), json::Value{}, json::Value(2147483648.0)}) {
        Session.g = mfQuickTradeFixture();
        auto Body = mfPotionSaleRequest(1, Price);
        Body.set("count", InvalidCount);
        const auto Before = saveToJson(Session.g);
        check(mfQuickTradeRequest("sell", Body).status >= 400, "非整数类型不能绕过批量数量校验");
        check(saveToJson(Session.g) == Before, "错误类型售药请求不改变游戏");
    }
    Session.g = mfQuickTradeFixture();
    auto NullPrice = mfPotionSaleRequest(1, Price);
    NullPrice.set("expectedUnitPrice", json::Value{});
    const auto BeforeNullPrice = saveToJson(Session.g);
    check(mfQuickTradeRequest("sell", NullPrice).status >= 400, "显式null不能绕过旧单价校验");
    check(saveToJson(Session.g) == BeforeNullPrice, "非法单价不出售丹药");

    Session.g = mfQuickTradeFixture();
    Session.g.player.stones = 100000;
    Session.g.tide = BeastTide{};
    Session.g.tide.active = true;
    tide::ensureTide(Session.g);
    Session.g.clock.days = Session.g.tide.endDay;
    tide::dayTick(Session.g);
    Session.g.tide.marketStock = {{"material", "gold", 8}};
    const auto MarketState = json::Value::parse(mfQuickTradeRequest("state", json::Object{}, "GET").body)["state"];
    const auto Material = MarketState["tide"]["market"]["items"].asArray().front();
    const auto MaxPurchase = Material["maxPurchase"];
    check(MaxPurchase["enabled"].asBool(), "兽潮目录提供一键买满资格");
    checkEq(MaxPurchase["count"].asInt(), 8, "兽潮买满报价匹配剩余库存八份");
    json::Value Purchase(json::Object{});
    Purchase.set("eventId", json::Value(Session.g.tide.eventId));
    Purchase.set("kind", json::Value("material")); Purchase.set("id", json::Value("gold"));
    Purchase.set("count", MaxPurchase["count"]); Purchase.set("expectedUnitPrice", MaxPurchase["unitPrice"]);
    const int PurchaseStones = Session.g.player.stones;
    const int OriginalOwned = Session.g.player.arts.materials.count("gold") ? Session.g.player.arts.materials.at("gold") : 0;
    const auto Bought = mfQuickTradeRequest("tide/market/buy", Purchase);
    checkEq(Bought.status, 200, "兽潮买满沿用既有原子交易接口");
    checkEq(Session.g.player.stones, PurchaseStones - MaxPurchase["totalCost"].asInt(), "买满真实扣款与权威报价相同");
    checkEq(Session.g.player.arts.materials.at("gold"), OriginalOwned + 8, "买满真实到账八份材料");
    const string AfterPurchase = saveToJson(Session.g);
    check(mfQuickTradeRequest("tide/market/buy", Purchase).status >= 400, "重复买满提交被剩余库存拒绝");
    check(saveToJson(Session.g) == AfterPurchase, "重复提交不再次扣费");

    Session.g = mfQuickTradeFixture();
    json::Value Legacy(json::Object{});
    Legacy.set("kind", json::Value("potion")); Legacy.set("id", json::Value(PotionId));
    checkEq(mfQuickTradeRequest("sell", Legacy).status, 200, "旧单枚出售请求保持兼容");
    checkEq(Session.g.player.arts.potions.at(PotionId), 19, "旧请求默认仅售一枚");
}
}
