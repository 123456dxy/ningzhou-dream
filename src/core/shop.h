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
//   potion: max(r.price, (材料价 + 8 + 品阶×6) × 1.6) × 行情
//   gear:   (craftCost × 1.45 + 品阶×20) × (铭刻?1.12) × 行情
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
ActionQuote saleQuote(const Game& g, const std::string& kind, const std::string& id);
ActionQuote gearActionQuote(const Game& g, const std::string& action, const std::string& id);
ActionQuote furnaceRepairQuote(const Game& g);


struct SellResult {
    bool ok = false;
    std::string error;
    std::string text;
    int gain = 0;
};
SellResult sell(Game& g, const std::string& kind, const std::string& id);

// 灵核 / 凝元丹 的买卖（对应 JS 的 trade('cores'/'pills')）
int corePrice(const Game& g, bool buying);
int pillPrice(const Game& g, bool buying);
SellResult tradeStock(Game& g, const std::string& kind, bool buying, int count);

// 洞府升档会保留所在地；迁府会保留已购档位。
struct CaveTierDef {
    int tier = 0, maxSupply = 1, minRenown = 0, cost = 0;
    std::string name, desc;
};
const std::vector<CaveTierDef>& caveTierDefs();
int caveMaxSupply(const Player& player);
std::string caveUpgradeError(const Game& game);
SellResult caveUpgrade(Game& game);
SellResult caveSupplySet(Game& game, const std::string& kind, int level);
SellResult caveMove(Game& game);

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
ManualSource manualSource(const Game& g, const std::string& id);

// 元素与剑、体术属于本门；调牌、聚气等通用术法各宗共享。
struct LibrarySpellSource {
    bool valid = false, enabled = false, known = false;
    std::string id, name, dao, desc, sect, reason;
    int stage = 0, points = 0;
};
LibrarySpellSource librarySpellSource(const Game& g, const std::string& id);
std::vector<LibrarySpellSource> availableLibrarySpells(const Game& g);
SellResult librarySpellExchange(Game& g, const std::string& id);

// 书坊购买（灵石）
SellResult bookBuy(Game& g, const std::string& id);
// 藏经阁兑换（贡献）
SellResult libraryExchange(Game& g, const std::string& id);

// 列出当前可买/可换的功法
std::vector<ManualSource> availableManuals(const Game& g);

// ============================================================
// 法器
// ============================================================
SellResult unequip(Game& g, const std::string& id);
SellResult salvage(Game& g, const std::string& id);    // 分解，返还 5×品阶灵石
SellResult inscribe(Game& g, const std::string& id);   // 铭刻，30×品阶灵石 + 器道入门

// ============================================================
// 丹炉
// ============================================================
SellResult repairFurnace(Game& g);                     // 20 灵石回满耐久
// 换炉
std::vector<const data::FurnaceDef*> buyableFurnaces(const Game& g);
SellResult buyFurnace(Game& g, const std::string& id);

// ============================================================
// 中止当前安排
// ============================================================
SellResult cancelJob(Game& g);

} // namespace nz::shop
