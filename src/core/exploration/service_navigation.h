#pragma once
#include "core/model.h"
#include "core/crafting/material_supply.h"

namespace nz::Exploration {
enum class ServiceDestination { MaterialMarket, Intel };
// 类型：服务赶路报价；可复制值，无外部资源。目的地与方式来自目录和 travelPlan，不执行移动/交易或保存。
struct ServiceRouteQuote {
    bool Enabled = false;
    bool AlreadyHere = false;
    string Destination, DestinationName, Mode, Reason, TargetTab, TargetSub;
    int Distance = 0;
    int Days = 0;
    int Cost = 0;
};
// 用途：寻找最近实际可达的坊市或天机阁；参与异地服务入口；传只读人生和封闭类别，返回真实交通报价。
[[nodiscard]] ServiceRouteQuote mfQuoteServiceRoute(const Game& State, ServiceDestination Service);
// 用途：按配方真实缺料寻找供应点；参与工坊报价；传有效材料报价和制作类别，返回城市/高阶情报路线，不自动购买。
[[nodiscard]] ServiceRouteQuote mfQuoteMaterialRoute(const Game& State, const crafting::MaterialQuote& Materials, const string& CraftKind);
} // namespace nz::Exploration
