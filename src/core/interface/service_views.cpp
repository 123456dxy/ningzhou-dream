#include "core/interface/service_views.h"
namespace nz::api::view {
// 用途：序列化服务路线全部字段；参与异地服务与工坊入口；返回拥有数据的JSON，不执行赶路。
json::Value mfServiceRouteView(const Exploration::ServiceRouteQuote& Quote) {
    json::Value View(json::Object{});
    View.set("enabled", Quote.Enabled);
    View.set("alreadyHere", Quote.AlreadyHere);
    View.set("destination", Quote.Destination);
    View.set("destinationName", Quote.DestinationName);
    View.set("mode", Quote.Mode);
    View.set("distance", Quote.Distance);
    View.set("days", Quote.Days);
    View.set("cost", Quote.Cost);
    View.set("reason", Quote.Reason);
    View.set("targetTab", Quote.TargetTab);
    View.set("targetSub", Quote.TargetSub);
    return View;
}
// 用途：组合两类常用服务路线；参与state纯查询；返回无，写入JSON副本，不读写存档或活动状态。
void mfAppendServiceRoutes(const Game& State, json::Value& View) {
    json::Value Routes(json::Object{});
    Routes.set("market", mfServiceRouteView(Exploration::mfQuoteServiceRoute(State, Exploration::ServiceDestination::MaterialMarket)));
    Routes.set("intel", mfServiceRouteView(Exploration::mfQuoteServiceRoute(State, Exploration::ServiceDestination::Intel)));
    View.set("serviceRoutes", std::move(Routes));
}
// 用途：把实际材料需求传给领域导航；参与报价与买后响应；返回材料报价加路线，前端不计算最近城市。
json::Value mfMaterialSupplyView(const Game& State, const crafting::MaterialQuote& Materials, const string& CraftKind) {
    auto View = crafting::materialQuoteJson(Materials);
    View.set("travel", mfServiceRouteView(Exploration::mfQuoteMaterialRoute(State, Materials, CraftKind)));
    return View;
}
}
