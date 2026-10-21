#pragma once
#include "core/model.h"
#include "core/social/tianji_steward.h"
#include <span>
#include <string_view>

namespace nz::crafting {
// 不可变货架配置；RequiredStage是购买资格，丹药服用资格仍由arts持有。
struct TianjiOfferDefinition {
    string OfferId;
    string ItemKind;
    string ItemId;
    string Name;
    string Description;
    int Grade = 0;
    int FixedUnitPrice = 0;
    int RequiredStage = 0;
};
// 无外部资源的纯报价快照；无库存刷新、随机、时钟或存档副作用。
struct TianjiShopQuote {
    bool Enabled = false;
    string Reason;
    string NpcId;
    string OfferId;
    int Count = 0;
    int FixedUnitPrice = 0;
    int TotalCost = 0;
    int OwnedCount = 0;
    int MaxPurchaseCount = 0;
};
[[nodiscard]] std::span<const TianjiOfferDefinition> mfTianjiOffers();
[[nodiscard]] const TianjiOfferDefinition* mfFindTianjiOffer(std::string_view OfferId);
[[nodiscard]] TianjiShopQuote mfQuoteTianjiPurchase(const Game& State, const string& NpcId, const string& OfferId, int Count);
// 用途：当前位置、身份、货品、数量、余额、容量、过期价重验后候选提交；拒绝不改变状态。
// 仅游戏串行写上下文调用；不IO、不推进时钟/RNG。成功生成一条纪事，后续保存由API边界负责。
[[nodiscard]] string mfBuyTianjiOffer(Game& State, const string& NpcId, const string& OfferId, int Count, int ExpectedCost);
}
