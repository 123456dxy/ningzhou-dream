// 宁州一梦 · C++ 版 — 即时经济动作
//
// 对应 JS economy.mjs 的 ecoInstant（七个「点一下立刻生效」的动作）：
//   sell-potion / sell-gear  坊市出售（含 marketCash 与每月需求上限）
//   book-buy                 城市书坊买功法
//   library                  宗门藏经阁用贡献兑换
//   cancel-job               中止当前安排
//   repair                   修炉
//   salvage / inscribe       法器分解 / 铭刻
//
// 这些之前完全没有：玩家炼了丹、打了法器都卖不掉，
// 贡献攒着没处花，抄本拿不到，丹炉坏了修不了。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"
#include "data/reference_tables.h"

namespace nz::shop {

// ============================================================
// 出售
// ============================================================
// 售价（对应 JS ecoSalePrice）
//   potion: 完整配方原料当前正常购价 × 1.5；标准产量摊到单枚，不计炉费或增产BUFF。
//   gear:   完整配料记录/同阶十份标准材料市价 × 1.5 × 明示的铭刻/声望/自炼售值加成。
// 原料正常购价已含行情、声望购买折扣和商贾优惠，成品不重复乘行情；详情见craft_prices.h。
// 用途：按物品类别计算真实出售价格。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const std::string& kind, const std::string& id，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int salePrice(const Game& g, const std::string& kind, const std::string& id);

// Read-only quotes share price/eligibility with the corresponding action.
// The API additionally applies its common job/event/battle/dungeon guards.
struct ActionQuote {
    bool enabled = false;
    std::string reason;
    int costStones = 0, returnStones = 0;
    // Preserve legacy failed-sale map insertion order in the mutating action.
    bool demandChecked = false, potionChecked = false;
};

// 类型：有限坊市收购能力，只负责当前资金/同类需求与地点，不依赖持有量或开炉资格。
// 临时炼器预览和现有物品出售共用；报价无时间/RNG/存档副作用，不充值旧档当月余额。
struct MarketSaleQuote {
    bool InCity = false;
    int RemainingCash = 0;
    int NextMonthBudget = 0;
    int AffordableCount = 0;
    std::string Reason;
};
// 类型：同一只读响应内的收购参数快照；只存数值，不借用Game，不跨请求或动作复用。
struct MarketSaleContext {
    bool InCity = false;
    int RemainingCash = 0;
    int NextMonthBudget = 0;
    int DemandLimit = 0;
};
// 用途：一次计算月预算并在本次目录视图中复用；返回：纯数值快照，无状态/RNG/文件副作用。
[[nodiscard]] MarketSaleContext mfMarketSaleContext(const Game& State);
// 用途：按后端单位售值查询资金和同类月需求；参数：potion/gear、稳定物品ID（临时法器可为空）、正价。
// 返回：与真实saleQuote共用的收购能力；不检查拥有量，未炼成的报价也能显示可收数量。
[[nodiscard]] MarketSaleQuote mfQuoteMarketSale(const Game& State, const std::string& Kind,
                                               const std::string& ItemId, int UnitSalePrice);
// 用途：批量只读报价共享本次响应快照；真实出售不接受外部上下文，始终重新校验活动状态。
[[nodiscard]] MarketSaleQuote mfQuoteMarketSale(const Game& State, const std::string& Kind,
                                               const std::string& ItemId, int UnitSalePrice,
                                               const MarketSaleContext& Context);
// 用途：预览出售资格需求与收益。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const std::string& kind, const std::string& id，按本模块入口调用。
// 返回值：ActionQuote结果，包含说明所述状态或计算数据。
ActionQuote saleQuote(const Game& g, const std::string& kind, const std::string& id);
// 用途：同次行囊响应共享收购上下文；真实sell仍调用三参接口重新检查，不接收请求提供的上下文。
ActionQuote saleQuote(const Game& State, const std::string& Kind, const std::string& ItemId,
                      const MarketSaleContext& Context);
// 用途：预览法器装备出售铭刻与拆解资格。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const std::string& action, const std::string& id，按本模块入口调用。
// 返回值：ActionQuote结果，包含说明所述状态或计算数据。
ActionQuote gearActionQuote(const Game& g, const std::string& action, const std::string& id);
// 用途：预览丹炉修理费用与资格。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：ActionQuote结果，包含说明所述状态或计算数据。
ActionQuote furnaceRepairQuote(const Game& g);


struct SellResult {
    bool ok = false;
    std::string error;
    std::string text;
    int gain = 0;
};
// 用途：检查出售条件后转移物品并发放灵石。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& kind, const std::string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult sell(Game& g, const std::string& kind, const std::string& id);

// 灵核 / 凝元丹 的买卖（对应 JS 的 trade('cores'/'pills')）
// 用途：计算灵核当前买入或卖出单价。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, bool buying，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int corePrice(const Game& g, bool buying);
// 用途：计算凝元丹当前买入或卖出单价。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, bool buying，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int pillPrice(const Game& g, bool buying);
// 用途：结算灵核与凝元丹的批量买卖。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& kind, bool buying, int count，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult tradeStock(Game& g, const std::string& kind, bool buying, int count);

// 洞府升档会保留所在地；迁府会保留已购档位。
struct CaveTierDef {
    int tier = 0, maxSupply = 1, minRenown = 0, cost = 0;
    std::string name, desc;
};
// 用途：取得洞府档位成本声望与供给上限。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：对应目录或计算结果列表。
const std::vector<CaveTierDef>& caveTierDefs();
// 用途：取得已购洞府的灵气配给上限。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& player，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int caveMaxSupply(const Player& player);
// 用途：检查洞府升档声望与灵石门槛。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& game，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string caveUpgradeError(const Game& game);
// 用途：支付已验证成本并解锁更高配给。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& game，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult caveUpgrade(Game& game);
// 用途：校验并设置灵石或灵气配给档位。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& game, const std::string& kind, int level，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult caveSupplySet(Game& game, const std::string& kind, int level);
// 用途：支付迁府费用并保留已购洞府等级。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& game，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult caveMove(Game& game);
// 用途：统一安置/迁府资格与费用；流程：洞府页面预览及caveMove；返回：成本、enabled和reason，无状态/RNG/时钟/IO写入。
[[nodiscard]] ActionQuote mfQuoteCaveMove(const Game& State);

// ============================================================
// 功法获取
// ============================================================
// 这部功法能不能在书坊买到 / 能不能在藏经阁换
struct ManualSource {
    bool valid = false;
    std::string id;
    std::string name;
    std::string city;      // 书坊所在城市，空表示不卖
    std::string sect;      // 藏经阁所属宗门，空表示不换
    int price = 0;         // 灵石
    int points = 0;        // 贡献
    int stage = 0;         // 境界要求
    std::string how;       // 人类可读的获取方式
};
// 用途：查询功法抄本的书坊与宗门获取途径。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const std::string& id，按本模块入口调用。
// 返回值：ManualSource结果，包含说明所述状态或计算数据。
ManualSource manualSource(const Game& g, const std::string& id);

// 元素与剑、体术属于本门；调牌、聚气等通用术法各宗共享。
struct LibrarySpellSource {
    bool valid = false, enabled = false, known = false;
    std::string id, name, dao, desc, sect, reason;
    int stage = 0, points = 0;
};
// 用途：预览本门神通传承门槛与贡献价格。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const std::string& id，按本模块入口调用。
// 返回值：LibrarySpellSource结果，包含说明所述状态或计算数据。
LibrarySpellSource librarySpellSource(const Game& g, const std::string& id);
// 用途：汇总本宗可传授的神通。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
std::vector<LibrarySpellSource> availableLibrarySpells(const Game& g);
// 用途：支付贡献并领悟当前本门神通。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult librarySpellExchange(Game& g, const std::string& id);

// 书坊购买（灵石）
// 用途：在对应书坊支付灵石购入未持有抄本。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult bookBuy(Game& g, const std::string& id);
// 藏经阁兑换（贡献）
// 用途：在本宗藏经阁支付贡献兑换抄本。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult libraryExchange(Game& g, const std::string& id);

// 列出当前可买/可换的功法
// 用途：筛出当前位置与持有状态可见的功法。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
std::vector<ManualSource> availableManuals(const Game& g);

// ============================================================
// 法器
// ============================================================
// 用途：移除法器占据的装备槽位。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult unequip(Game& g, const std::string& id);
// 用途：按确认报价回收法器原材料并支付手续费。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult salvage(Game& g, const std::string& id);    // 分解，返还 5×品阶灵石
// 用途：支付铭刻费用并增强指定法器。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult inscribe(Game& g, const std::string& id);   // 铭刻，30×品阶灵石 + 器道入门

// ============================================================
// 丹炉
// ============================================================
// 用途：支付修理费用恢复丹炉耐久。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult repairFurnace(Game& g);                     // 20 灵石回满耐久
// 换炉
// 用途：筛选当前境界可以使用的丹炉。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
std::vector<const data::FurnaceDef*> buyableFurnaces(const Game& g);
// 用途：支付价格更换可用丹炉并恢复耐久。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult buyFurnace(Game& g, const std::string& id);

// ============================================================
// 中止当前安排
// ============================================================
// 用途：中止当前工作并保留已消耗成本。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult cancelJob(Game& g);

} // namespace nz::shop
