#pragma once

#include "core/model.h"
#include <string>

namespace nz::crafting {

// 类型：制作经济的只读平衡配置；材料目录price为本地基价，不采用原游戏sourcePrice。
// 比例和有限月预算集中于此契约，所有报价/真实结算共用；无持久字段或可变全局状态。
struct CraftEconomyRules {
    int BaseSellNumerator = 3;
    int BaseSellDenominator = 2;
    int BaseBuyMultiplier = 3;
    int MonthlyBudgetMultiplier = 3;
    int MaximumMonthlyBudget = 1000000000;
    int MaximumBudgetRenown = 250;
    int MinorStagesPerMajorRealm = 3;
    int MaximumMajorRealmIndex = 4;
};

// 用途：取得唯一制作经济平衡参数；返回：静态不可变配置借用，无时间/RNG/资源副作用。
[[nodiscard]] const CraftEconomyRules& mfCraftEconomyRules();

// 封闭的材料报价类别，仅在兼容HTTP/C++字符串边界转换；不使用显示文本判断业务。
enum class MaterialPriceKind { Herb, ForgeMaterial };

// 类型：制作经济的只读报价值；负责完整原料市场价值、基础成品价格和额外售值乘区。
// 无外部资源或状态写权限，可复制；Valid为真时所有价格为正且可由int表示。
// 参数快照由调用方保证读取期间不变；报价不保存引用、不推进时间/RNG、不写存档。
struct CraftPriceQuote {
    bool Valid = false;
    int MaterialCost = 0;
    int StandardOutput = 1;
    int BaseSellPrice = 0;
    int BuyPrice = 0;
    int SellPrice = 0;
    double SaleMultiplier = 1.0;
    double InscriptionMultiplier = 1.0;
    double RenownSaleMultiplier = 1.0;
    double CraftedSaleMultiplier = 1.0;
    bool UsesEstimatedMaterials = false;
    std::string Reason;
};

// 用途：将材料基价折算成当前正常采购单价；流程：普通材料铺、工坊和兽潮价格基准。
// 参数：稳定Game快照与非负基价（灵石/份）；返回：已含行情、声望和商贾优惠的整数单价。
// 错误：非正基价、非法行情或超范围金额返回0；副作用：无，线程条件同上，不保存借用。
[[nodiscard]] int mfNormalBuyPrice(const Game& GameState, int BasePrice);

// 用途：查询药材或炼器材料的正常采购价；流程：普通铺、工坊与战后坊市的同一基准。
// 参数：受控类别和目录稳定ID；返回：实际单价，未知ID/非法行情为0。
// 不检查普通铺高阶售卖资格、不改变活动状态；兼容play入口单向委托此函数，避免反向模块依赖。
[[nodiscard]] int mfNormalMaterialBuyPrice(const Game& GameState, MaterialPriceKind Kind, const std::string& MaterialId);

// 用途：按丹方完整原料价值查询单枚成品报价；流程：坊市、工坊收益、出售与预算。
// 参数：目录中的丹方稳定ID；标准产量当前为一枚，天赋/悟道增产不稀释售价。
// 返回：拥有的报价快照；未知ID、缺配料或越界金额使Valid为假并给Reason。
// 副作用：无；不检查当地产销资格和持有量，不能用Valid替代真实动作前置校验。
[[nodiscard]] CraftPriceQuote mfQuotePotionPrice(const Game& GameState, const std::string& RecipeId);

// 用途：按完整配料账查询现有法器买卖基价与额外加成；流程：装备预览和实际回收。
// 参数：新版从SourceMaterials读取冻结明细（预览可无装备ID）；旧配料账按GearState.id读取；旧无账物按同阶十份材料估价。
// 返回：独立报价；损坏账、非法品阶/行情/金额明确失败，无RNG/时间/资源或文件副作用。
[[nodiscard]] CraftPriceQuote mfQuoteGearPrice(const Game& GameState, const Gear& GearState);

// 用途：按当前三料选择预览自炼法器材料市价与售值；流程：工坊炼器预览，主/辅/灵核用量4/3/3。
// 参数：目录中的三种材料稳定ID，可重复；返回：完整配料报价及当前自炼额外加成。
// 错误：任何未知材料或越界价格均失败；副作用：无，不建立法器或配料账、不扣库存/灵石/时间。
[[nodiscard]] CraftPriceQuote mfQuoteForgePrice(const Game& GameState, const std::string& MainMaterialId,
                                               const std::string& AuxiliaryMaterialId, const std::string& CoreMaterialId);

// 用途：查询普通铺同品阶标准法器报价；流程：法器列表与实际购买，共用十份材料估值。
// 参数：品阶1..6；返回：独立报价，不打开高阶法器购买资格；错误和副作用同上。
[[nodiscard]] CraftPriceQuote mfQuoteStandardGearPrice(const Game& GameState, int Grade);

} // namespace nz::crafting
