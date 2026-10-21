// 宁州一梦 · C++ 版 — 营生与坊市实现（对应 JS 版 economy.mjs）
#include "core/economy.h"
#include "core/combat/battle_scaling.h"
#include "core/shop.h"
#include "core/progression/manual_study.h"
#include "core/crafting/material_supply.h"
#include "core/crafting/forge_rules.h"
#include "core/crafting/alchemy_rules.h"
#include "core/crafting/craft_timing.h"
#include "core/crafting/craft_prices.h"
#include "core/crafting/cave_garden.h"
#include "core/crafting/market_cycle.h"
#include "core/exploration/beast_loot.h"
#include "core/renown.h"

#include <algorithm>
#include <cstdint>

#include "core/arts.h"
#include "core/cultivate.h"
#include "core/battle.h"
#include "core/sim.h"
#include "core/source_limits.h"
#include "core/sect_trial.h"
#include "core/reward_receipt.h"
#include "core/talents.h"
#include "data/reference_tables.h"

namespace nz {

const char* const ECO_CITIES[5] = {"wuling", "yifeng", "yunxi",
                                   "tianxing", "guangling"};

// ------------------------------------------------------------
// 任务表（对应 JS 的 ECO_TASKS）
// ------------------------------------------------------------
struct TaskDef {
    string id, name, desc;
    int days, fee, stage;
    bool sect;
};

// 用途：取得城市任务定义。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：对应目录或计算结果列表。
const vector<TaskDef>& ecoTasks() {
    static const vector<TaskDef> data = {
        {"errand", "城中跑腿", "替商铺递送书信、整理账簿。", 5, 12, 0, false},
        {"escort", "商路护送", "护送往来商旅，有小概率受伤。", 12, 32, 0, false},
        {"inspect", "灵阵巡检", "筑基修士协助检查城中灵阵。", 8, 24, 3, false},
        {"sect", "山门差事", "维护药田、巡守山门，增加贡献15。", 10, 25, 0, true},
    };
    return data;
}

// 用途：按ID查询城市任务。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
static const TaskDef* findTask(const string& id) {
    for (const auto& t : ecoTasks())
        if (t.id == id) return &t;
    return nullptr;
}



// 统一的功法查询（基础 + 拓展）
struct ManualView {
    string id, name, dao, effect;
    int stage = 0, price = 0;
};

// 用途：查找基础或扩展功法元信息。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const string& id, ManualView& out，按本模块入口调用。
// 返回值：找到为真并填充out；未知ID返回假。
static bool lookupManual(const string& id, ManualView& out) {
    for (const auto& m : data::BaseManualDef_TABLE()) {
        if (m.id != id) continue;
        out = {m.id, m.name, m.dao, m.effect, m.stage, m.price};
        return true;
    }
    for (const auto& m : data::ManualDef_TABLE()) {
        if (m.id != id) continue;
        out = {m.id, m.name, m.dao, m.effect, m.stage, m.price};
        return true;
    }
    ManualInfo Definition;
    if (findManualInfo(id, Definition)) {
        out = {Definition.id, Definition.name, Definition.dao, Definition.effect, Definition.stage, Definition.price};
        return true;
    }
    return false;
}

// 用途：确定角色可接触的丹药收购市场层级。
// 流程：月度坊市资金配置；与真实炼制品阶共用artTier，包含末小境的突破准备阶段。
// 返回值：一至六品；不改变炼制、丹炉或药材采购资格。
int ecoMarketTier(const Player& player) {
    return clampInt(artTier(player), 1, 6);
}

// 用途：计算有限的月度收购预算，避免高阶成品永远无法整枚售出。
// 流程：初始化或换月时取声望额度、本层级最高单枚丹价及标准法器售值的较高值，再乘基础三倍与大境界倍率。
// 返回值：当期行情下的月预算；只读，不给本月已花掉的资金充值。
int ecoMarketBudget(const Game& game) {
    return crafting::mfMarketCycleBudget(game);
}

// 用途：查询月度收购预算的境界乘区；流程：资金预览/换月；用法：传只读角色，小境界不改变倍率；返回：炼气至化神1/2/4/8/16，越界饱和，无状态写入。
int mfMarketRealmMultiplier(const Player& PlayerState) {
    const auto& Rules = crafting::mfCraftEconomyRules();
    const int MajorRealmIndex = clampInt(PlayerState.stage / Rules.MinorStagesPerMajorRealm, 0, Rules.MaximumMajorRealmIndex);
    return 1 << MajorRealmIndex;
}

// 用途：返回统一预算解释；流程：坊市和声望视图；用法：无参数；返回：与后端完整底座乘区相同的中文规则，无状态写入。
string mfMarketBudgetRule() {
    const auto& Rules = crafting::mfCraftEconomyRules();
    return "声望基础额度、当前可炼品阶最高单枚丹药和同阶标准法器收购价取较高值，基础乘 " +
        std::to_string(Rules.MonthlyBudgetMultiplier) + "，再按大境界翻倍（最高 " +
        std::to_string(1 << Rules.MaximumMajorRealmIndex) + " 倍），总预算上限 " +
        std::to_string(Rules.MaximumMonthlyBudget) +
        " 灵石作为原月额度，再乘50作为三年收购资金；每1080世界日刷新，不限制每月出售件数。切页、读档、换城或晋阶不补充本期余额。";
}
// ------------------------------------------------------------
// 初始化与每月刷新
// ------------------------------------------------------------
// 用途：补齐营生库存与委托初始化状态。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ensureEconomy(Game& g) {
    if (g.economy.month >= 0) return;
    g.economy.books.clear();
    for (const auto& [id, lv] : g.player.arts.manuals)
        g.economy.books.push_back(id);
    g.economy.contribution = 0;
    g.economy.taskCounts.clear();
    g.economy.huntCounts = 0;
    g.economy.commissionBrew = 0;
    g.economy.commissionForge = 0;
    g.economy.cityTasks = 0;
    g.economy.month = g.clock.days / 30;
    g.economy.orders.clear();
    g.economy.sold.clear();
    g.economy.marketCash = ecoMarketBudget(g);
    g.economy.MarketCycleIndex = g.clock.days / crafting::MarketCycleDurationDays;
    g.economy.seq = 0;
}

// 用途：刷新当月城市需求与任务上限。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoRefresh(Game& g) {
    Economy& e = g.economy;
    crafting::mfRefreshMarketCycle(g);
    const int month = g.clock.days / 30;
    if (month == e.month) return;
    e.month = month;
    e.orders.clear();
    e.taskCounts.clear();
    e.sold.clear();
}

// 用途：把已得功法抄本同步到学习书单。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoSyncBooks(Game& g) {
    auto& books = g.economy.books;
    auto& bookList = g.player.arts.bookList;
    for (const auto& id : bookList)
        if (std::find(books.begin(), books.end(), id) == books.end()) books.push_back(id);
    for (const auto& [id, lv] : g.player.arts.manuals) {
        if (std::find(g.economy.books.begin(), g.economy.books.end(), id) ==
            g.economy.books.end())
            g.economy.books.push_back(id);
    }
    for (const auto& id : books)
        if (std::find(bookList.begin(), bookList.end(), id) == bookList.end()) bookList.push_back(id);
}

// 用途：校验功法ID并仅发放一次抄本。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& id, const string& source，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool ecoAcquireBook(Game& g, const string& id, const string& source) {
    ManualView m;
    if (!lookupManual(id, m)) {
        logEvent(g, "self", source + "未能取得有效功法抄本，未发放该项奖励。");
        return false;
    }
    ecoSyncBooks(g);
    auto& books = g.economy.books;
    if (std::find(books.begin(), books.end(), id) != books.end()) {
        logEvent(g, "self", source + "：已持有「" + m.name + "」抄本，未重复发放。");
        return false;
    }
    books.push_back(id);
    g.player.arts.bookList.push_back(id);
    logEvent(g, "self", "获得功法抄本「" + m.name + "」（需修习），来源：" + source +
                         "；可在洞府道法 → 功法与洞府 → 已得功法抄本中修习。", true);
    return true;
}

// ------------------------------------------------------------
// 制作天数与售价
// ------------------------------------------------------------
// 用途：计算既有能力影响后的炼制天数。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Player& p, const string& type, int grade，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int ecoCraftDays(const Player& PlayerState, const string& WireKind, int Grade) {
    const auto Kind = crafting::mfParseCraftKind(WireKind);
    if (!Kind) return 0;
    // 既有公共入口保留对品阶的钳位行为；新模块接口严格拒绝非法品阶。
    const int MaximumGrade = *Kind == crafting::CraftKind::Brewing ? 6 : 5;
    return crafting::mfCraftDurationDays(PlayerState, *Kind, clampInt(Grade, 1, MaximumGrade));
}

// 用途：按ID查询炼丹配方。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
static const data::RecipeDef* findRecipeEco(const string& id) {
    for (const auto& r : data::RecipeDef_TABLE())
        if (r.id == id) return &r;
    return nullptr;
}


// 用途：计算既有行情下药材或法器售价。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& type, const string& id，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int ecoSalePrice(const Game& g, const string& type, const string& id) {
    // 保留旧公共入口；丹药与法器均交权威市场报价，避免遗漏真实配方和声望。
    return shop::salePrice(g, type, id);
}

// ------------------------------------------------------------
// 委托（对应 ecoOrder）
// ------------------------------------------------------------
struct OrderInfo {
    string mode;      // brew / forge
    string id;        // 丹方 id 或 "forge"
    string name;
    int days = 0;
    int fee = 0;
    int cost = 0;
    int grade = 1;
    double chance = 0.5;
    // 炼器时的配料
    string mainId, auxId, coreId;
};

// 用途：生成当前境界的炼制委托要求。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& mode，按本模块入口调用。
// 返回值：OrderInfo结果，包含说明所述状态或计算数据。
static OrderInfo ecoBuildOrder(const Game& g, const string& mode) {
    OrderInfo o;
    o.mode = mode;
    const Player& p = g.player;
    const int stageGroup = p.stage / 3;

    if (mode == "brew") {
        const int wantGrade = std::min(
            artTier(p), 1 + stageGroup);
        vector<const data::RecipeDef*> pool;
        for (const auto& r : data::RecipeDef_TABLE())
            if (r.grade == wantGrade) pool.push_back(&r);
        if (pool.empty()) return o;
        const int idx = (g.economy.month + (g.seed % static_cast<uint32_t>(pool.size()))) %
                        static_cast<int>(pool.size());
        const data::RecipeDef* r = pool[static_cast<size_t>(idx)];
        const BrewInfo b = artBrewInfo(p, r->id);
        o.id = r->id;
        o.name = "代炼" + r->name;
        o.days = ecoCraftDays(p, "brew", r->grade);
        o.fee = 18 + r->grade * 12;
        o.cost = static_cast<int>(std::ceil(b.cost * 0.35));
        o.grade = r->grade;
        o.chance = b.chance;
        return o;
    }

    // 炼器委托
    const int wantGrade = std::min(1 + stageGroup, 5);
    vector<const data::MaterialDef*> pool;
    for (const auto& m : data::MaterialDef_TABLE())
        if (m.grade == wantGrade) pool.push_back(&m);
    if (pool.empty()) return o;
    const data::MaterialDef* main = pool[0];
    const data::MaterialDef* aux = pool.size() > 1 ? pool[1] : main;
    const data::MaterialDef* core = pool.size() > 2 ? pool[2] : main;
    const ForgeInfo b = artForgeInfo(p, main->id, aux->id, core->id, "sword");
    o.id = "forge";
    o.name = "代铸" + main->name + "剑";
    o.days = ecoCraftDays(p, "forge", b.grade);
    o.fee = 28 + b.grade * 20;
    o.cost = static_cast<int>(std::ceil(b.cost * 0.3));
    o.grade = b.grade;
    o.chance = b.chance;
    o.mainId = main->id;
    o.auxId = aux->id;
    o.coreId = core->id;
    return o;
}


// ------------------------------------------------------------
// 五城判定与任务表访问
// ------------------------------------------------------------
// 用途：判断地点是否为五座城池。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const string& location，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool ecoIsCity(const string& location) {
    for (const char* c : ECO_CITIES)
        if (location == c) return true;
    return false;
}

// 用途：公开城市任务目录。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：匹配定义或实体指针；未找到时为空。
const vector<TaskDef>* ecoTaskList() { return &ecoTasks(); }

// ------------------------------------------------------------
// 行动查询（对应 ecoActionInfo）
// ------------------------------------------------------------
// 用途：计算营生行动的时间材料成本与成功率。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& type, const string& id, int batch，按本模块入口调用。
// 返回值：EcoActionInfo结果，包含说明所述状态或计算数据。
EcoActionInfo ecoActionInfo(const Game& g, const string& type,
                            const string& id, int batch) {
    EcoActionInfo info;
    const Player& p = g.player;

    if (type == "learn") {
        ManualView m;
        if (!lookupManual(id, m)) return info;
        const auto study = manualStudyInfo(p, id);
        info.valid = true;
        info.name = "修习" + m.name;
        info.days = study.days;
        info.cost = study.stones;
        return info;
    }
    if (type == "brew") {
        const auto Quote = crafting::mfQuoteAlchemy(g, id, batch);
        info.valid = Quote.Valid;
        info.name = Quote.Name;
        info.days = Quote.DurationDays;
        info.cost = Quote.StoneCost;
        info.chance = Quote.SuccessProbability;
        info.batch = batch;
        info.output = Quote.OutputPerSuccess;
        return info;
    }
    if (type == "forge") {
        // id 编码为 "main|aux|core|shape"
        const size_t p1 = id.find('|');
        if (p1 == string::npos) return info;
        const size_t p2 = id.find('|', p1 + 1);
        const size_t p3 = id.find('|', p2 + 1);
        if (p2 == string::npos || p3 == string::npos) return info;
        const string mainId = id.substr(0, p1);
        const string auxId = id.substr(p1 + 1, p2 - p1 - 1);
        const string coreId = id.substr(p2 + 1, p3 - p2 - 1);
        const string shapeId = id.substr(p3 + 1);
        const ForgeInfo b = artForgeInfo(p, mainId, auxId, coreId, shapeId);
        if (!b.valid) return info;
        info.valid = true;
        info.name = "铸炼" + b.mainName + b.shapeName;
        info.days = ecoCraftDays(p, "forge", b.grade);
        info.cost = b.cost;
        info.chance = b.chance;
        info.mainId = mainId; info.auxId = auxId;
        info.coreId = coreId; info.shapeId = shapeId;
        return info;
    }
    if (type == "task") {
        if (const auto* task = findSectTask(id)) {
            info.valid = true;
            info.name = task->name;
            info.days = task->days;
            info.fee = jsRound(task->fee * (1 + (p.stage / 3) * 0.35) *
                             (1.0 + renown::stipendBonus(p.renown)));
            return info;
        }
        const TaskDef* t = findTask(id);
        if (!t) return info;
        info.valid = true;
        info.name = t->name;
        info.days = t->days;
        info.cost = 0;
        info.fee = jsRound(t->fee * (1 + (p.stage / 3) * 0.35) *
                         (1.0 + renown::stipendBonus(p.renown)));
        return info;
    }
    if (type == "commission") {
        const OrderInfo o = ecoBuildOrder(g, id);
        if (o.id.empty()) return info;
        info.valid = true;
        info.name = o.name;
        info.days = o.days;
        info.cost = o.cost;
        info.fee = o.fee;
        info.chance = o.chance;
        info.orderId = o.id;
        info.mode = o.mode;
        info.mainId = o.mainId; info.auxId = o.auxId;
        info.coreId = o.coreId; info.shapeId = "sword";
        return info;
    }
    if (type == "hunt") {
        info.valid = true;
        info.name = "猎妖委托";
        info.days = 7;
        info.cost = 5;
        info.fee = 32 + (p.stage / 3) * 30;
        return info;
    }
    if (type == "meditate") {
        info.valid = daoIndex(id) >= 0;
        info.name = "参悟" + id + "道";
        info.days = 30;
        return info;
    }
    if (type == "mine") {
        info.valid = true;
        info.name = "寻矿";
        info.days = 7;
        return info;
    }
    if (type == "arena") {
        info.valid = true;
        info.name = "演武切磋";
        info.days = 2;
        info.cost = 10;
        return info;
    }
    if (type == "quest") {
        for (const auto& q : data::QuestDef_TABLE()) {
            if (q.id != id) continue;
            info.valid = true;
            info.name = q.name;
            info.days = 7;
            info.cost = q.price;
            return info;
        }
        return info;
    }
    return info;
}

// ------------------------------------------------------------
// 行动校验（对应 ecoActionError）
// ------------------------------------------------------------
// 用途：检查营生行动资源地点和状态门槛。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& type, const string& id, int batch，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string ecoActionError(const Game& g, const string& type, const string& id, int batch) {
    const Player& p = g.player;
    const Arts& a = p.arts;
    const Economy& e = g.economy;

    // 不归 economy 管的行动返回空字符串，交给上层继续判断
    const bool handled =
        (type == "learn" || type == "brew" || type == "forge" ||
         type == "task" || type == "commission" || type == "hunt" ||
         type == "meditate" || type == "mine" || type == "arena" ||
         type == "quest");
    if (!handled) return "";
    if (g.ended) return "这一世已经结束。";
    if (g.pending.has_value()) return "请先处理当前事件。";
    if (a.battle.has_value()) return "请先完成斗法。";
    if (g.job.has_value()) return "请先完成或中止当前安排。";
    if (type == "arena") {
        const string AccessError = crafting::mfGardenAccessError(g);
        if (!AccessError.empty()) return AccessError;
    }

    if (type == "commission" && id != "brew" && id != "forge")
        return "没有此类委托。";

    const EcoActionInfo info = ecoActionInfo(g, type, id, batch);
    if (!info.valid) return "行动不存在。";
    if (p.stones < info.cost) return "本次行动灵石不足。";

    if (type == "learn") {
        ManualView m;
        if (!lookupManual(id, m)) return "无此功法。";
        if (std::find(e.books.begin(), e.books.end(), m.id) == e.books.end() &&
            std::find(a.bookList.begin(), a.bookList.end(), m.id) == a.bookList.end())
            return "尚未获得功法，请从书坊、藏经阁或机缘获取。";
        if (p.stage < m.stage) return "境界不足。";
        auto it = a.manuals.find(m.id);
        if (it != a.manuals.end() && it->second >= 5)
            return "功法已修满五层。";
        return "";
    }
    if (type == "brew") {
        return crafting::mfQuoteAlchemy(g, id, batch).Reason;
    }
    if (type == "forge") return artAnnualError(g, "forge", id);
    if (type == "task") {
        if (const auto* task = findSectTask(id)) {
            if (!findSect(p.sect) || p.location != p.sect)
                return "请回所属山门接差事。";
            if (p.stage < task->stage) return "境界不足。";
            if (sectTasksTakenThisMonth(g) >= 3)
                return "本月宗门差事已满三次（所有差事共用额度）。";
            const auto sect = g.world.sects.find(p.sect);
            if (sect == g.world.sects.end() || sect->second.treasury < info.fee)
                return "宗门府库不足。";
            return "";
        }
        const TaskDef* t = findTask(id);
        if (!t) return "行动不存在。";
        if (t->sect) {
            if (p.sect.empty() || p.location != p.sect)
                return "请回所属山门接差事。";
        } else if (!ecoIsCity(p.location)) {
            return "请到五城任务堂。";
        }
        if (p.stage < t->stage) return "境界不足。";
        auto it = e.taskCounts.find(t->id);
        if (e.month == g.clock.days / 30 && it != e.taskCounts.end() && it->second >= 3)
            return "本月此任务已满三次。";
        if (t->sect) {
            auto s = g.world.sects.find(p.sect);
            if (s != g.world.sects.end() && s->second.treasury < info.fee)
                return "宗门府库不足。";
        }
        return "";
    }
    if (type == "commission") {
        if (!ecoIsCity(p.location)) return "请到城中工坊接单。";
        auto it = e.orders.find(id);
        if (it != e.orders.end() && it->second >= 3)
            return "本月三件此类委托已经接满。";
        if (id == "brew") {
            const BrewInfo b = artBrewInfo(p, info.orderId);
            const data::RecipeDef* r = findRecipeEco(info.orderId);
            const data::FurnaceDef* f = nullptr;
            for (const auto& x : data::FurnaceDef_TABLE())
                if (x.id == a.furnace) f = &x;
            if (!f || !r || f->grade < r->grade || f->capacity < b.count ||
                a.durability < 1)
                return "丹炉品阶、容量或耐久不足。";
        } else {
            const ForgeInfo fb = artForgeInfo(p, info.mainId, info.auxId,
                                              info.coreId, info.shapeId);
            if (!fb.valid) return "配料无效。";
            if (fb.grade > 1 + p.stage / 3 ||
                fb.grade > 1 + artLevel(a.exp.count("器") ? a.exp.at("器") : 0))
                return "器道不足以承接此单。";
        }
        return "";
    }
    if (type == "hunt") {
        if (!ecoIsCity(p.location)) return "请从城中猎妖任务堂出发。";
        if (p.health < HealthGateMinimum) return "猎妖请先休养到30气血。";
        auto it = e.orders.find("hunt");
        if (it != e.orders.end() && it->second >= 3)
            return "本月猎妖委托已满三次。";
        return "";
    }
    if (type == "mine" && g.world.herbs < 3)
        return "附近可开采原料不足，请等待年初补充。";
    return artAnnualError(g, type, id);
}

// ------------------------------------------------------------
// 开始工作（对应 ecoBegin）
// ------------------------------------------------------------
// 用途：扣除已验证成本并建立进行中的工作。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& type, const string& id, int batch，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoBegin(Game& g, const string& type, const string& id, int batch) {
    if (!ecoActionError(g, type, id, batch).empty()) return;
    ecoRefresh(g);
    Player& p = g.player;
    Arts& a = p.arts;
    Economy& e = g.economy;
    const EcoActionInfo info = ecoActionInfo(g, type, id, batch);
    const auto AlchemyQuote = type == "brew" ? crafting::mfQuoteAlchemy(g, id, batch) : crafting::AlchemyQuote{};

    Job job;
    job.actionType = type;
    job.name = info.name;
    job.total = info.days;
    job.remaining = info.days;
    job.started = g.clock.days;
    job.cost = info.cost;
    job.paid = true;
    job.destination = id;        // 用 destination 携带行动 id
    job.kind = type;
    job.gift = info.batch;
    job.batch = info.batch;
    job.craftOutput = info.output;
    job.chance = info.chance;
    job.fee = info.fee;
    p.stones -= info.cost;

    if (type == "brew") {
        for (const auto& Ingredient : AlchemyQuote.Ingredients) {
            a.herbs[Ingredient.Name] -= Ingredient.Required;
            p.herbs = std::max(0, p.herbs - Ingredient.Required);
        }
        a.durability -= AlchemyQuote.DurabilityCost;
    } else if (type == "forge") {
        for (const auto& material : artForgeMaterials(p, info.mainId, info.auxId, info.coreId))
            a.materials[material.id] -= material.required;
        const ForgeInfo b = artForgeInfo(p, info.mainId, info.auxId,
                                         info.coreId, info.shapeId);
        job.sect = b.shapeSlot;   // 暂存器型槽位
    } else if (type == "task") {
        e.taskCounts[id]++;
        if (findSectTask(id)) {
            job.sect = p.sect;
            job.origin = p.location;
            g.world.sects[p.sect].treasury -= info.fee;
        }
    } else if (type == "commission") {
        e.orders[id]++;
        job.kind = id;
        job.sect = info.orderId;
        if (id == "brew") {
            const data::RecipeDef* r = findRecipeEco(info.orderId);
            const data::FurnaceDef* f = nullptr;
            for (const auto& x : data::FurnaceDef_TABLE())
                if (x.id == a.furnace) f = &x;
            if (!(artHas(a, "丹", 4) && f && r && f->grade > r->grade))
                a.durability--;
        }
    } else if (type == "hunt") {
        e.orders["hunt"]++;
    } else if (type == "mine") {
        g.world.herbs -= 3;
    }

    g.job = job;
    logEvent(g, "self",
             "安排" + info.name + "，需 " + std::to_string(job.total) +
                 " 天，已投入 " + std::to_string(job.cost) + " 灵石。");
}

// ------------------------------------------------------------
// 完成工作（对应 ecoComplete）
// ------------------------------------------------------------
// 用途：结算完成的制作学习任务与采集工作。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const Job& job，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoComplete(Game& g, const Job& job) {
    if (job.actionType == "forge" && crafting::mfCompleteForge(g, job)) return;
    Player& p = g.player;
    Arts& a = p.arts;
    Economy& e = g.economy;
    const string type = job.actionType;
    const string id = job.destination;

    // 参悟大道（30 天）：效果与 artAnnual 的 meditate 分支一致
    if (type == "meditate") {
        artAnnual(g, "meditate", id);
        return;
    }

    if (type == "brew") {
        int success = 0;

        for (int i = 0; i < std::max(1, job.gift); ++i)
            if (randomOf(g) < job.chance) success++;
        const int quantity = success * clampInt(job.craftOutput, 1, 4);
        a.potions[id] += quantity;
        a.progress.brew += success;
        p.alchemy = std::min(500, p.alchemy + std::max(1, job.gift));
        artGain(p, "丹", std::max(1, job.gift) * 2);
        const data::RecipeDef* r = findRecipeEco(id);
        logEvent(g, "self",
                 "炼制" + (r ? r->name : id) + "结束：" +
                     std::to_string(std::max(1, job.gift)) + "炉中 " +
                     std::to_string(success) + "炉成丹。",
                 false, "",
                 "成丹 +" + std::to_string(quantity) + " · 用时" +
                     std::to_string(job.total) + "天");
    } else if (type == "forge") {
        artGain(p, "器", 4);
        const bool win = randomOf(g) < job.chance;
        if (win) {
            const size_t p1 = id.find('|');
            const size_t p2 = id.find('|', p1 + 1);
            const size_t p3 = id.find('|', p2 + 1);
            const string mainId = id.substr(0, p1);
            const string auxId = id.substr(p1 + 1, p2 - p1 - 1);
            const string coreId = id.substr(p2 + 1, p3 - p2 - 1);
            const string shapeId = id.substr(p3 + 1);
            const ForgeInfo b = artForgeInfo(p, mainId, auxId, coreId, shapeId);
            Gear gear;
            gear.id = "forge-" + std::to_string(g.clock.days) + "-" +
                      std::to_string(e.seq++);
            gear.name = job.name;
            if (gear.name.rfind("铸炼", 0) == 0)
                gear.name = gear.name.substr(string("铸炼").size());
            gear.slot = b.shapeSlot;
            gear.shape = b.shapeId;
            gear.grade = b.grade;
            gear.bonus = b.bonus;
            gear.element = b.mainElement;
            gear.polarity = b.mainPolarity;
            gear.inscribed = false;
            gear.craftCost = job.cost;
            gear.crafted = true;
            a.gear.push_back(gear);
            crafting::recordForgeMaterials(g, gear.id, mainId, auxId, coreId);
            a.progress.forge++;
        }
        logEvent(g, "self",
                 job.name + (win ? "成功" : "失败，投入资材耗尽") + "，用时" +
                     std::to_string(job.total) + "天。");
    } else if (type == "learn") {
        ManualView m;
        if (!lookupManual(id, m)) return;
        a.manuals[id] = std::min(5, std::max(0, a.manuals[id]) + 1);
        artGain(p, m.dao, 5);
        logEvent(g, "self",
                 job.name + "至第" + std::to_string(a.manuals[id]) + "层，用时" +
                     std::to_string(job.total) + "天。");
    } else if (type == "task") {
        const RewardReceipt receipt(g);
        const auto* sectTask = findSectTask(id);
        if (sectTask) {
            const bool legacy = id == "sect" && job.sect.empty() && job.origin.empty();
            if (!findSect(p.sect) || p.location != p.sect ||
                (!legacy && (job.sect != p.sect || job.origin != p.sect))) {
                logEvent(g, "sect", "「" + job.name + "」未交付：已离开接单山门或改变所属宗门，未发放工钱、贡献或道法经验。");
                return;
            }
            e.contribution += sectTask->contribution;
            if (sectTask->daoExp > 0) artGain(p, findSect(p.sect)->element, sectTask->daoExp);
        }
        p.stones += jsRound(static_cast<double>(job.fee));
        e.cityTasks++;
        if (id == "escort" && randomOf(g) < 0.15)
            p.health = std::max(1, p.health - mfHealthPercentAmount(mfPlayerHealthMaximum(p), 8));
        logEvent(g, "self",
                 "完成" + job.name + "：" + receipt.changes(g) + "。");
    } else if (type == "commission") {
        const bool win = randomOf(g) < job.chance;
        if (win) {
            p.stones += job.fee;
            if (job.kind == "brew") {
                e.commissionBrew++;
                p.alchemy = std::min(500, p.alchemy + 1);
                artGain(p, "丹", 2);
                if (e.commissionBrew >= 3)
                    ecoAcquireBook(g, "pill", "工坊丹师授业");
            } else {
                e.commissionForge++;
                artGain(p, "器", 4);
                if (e.commissionForge >= 3)
                    ecoAcquireBook(g, "forge", "工坊器师授业");
            }
        }
        logEvent(g, "self",
                 job.name +
                     (win ? "交付成功，工费 +" + std::to_string(job.fee) +
                                " 灵石"
                          : "失败，无工费；客户材料不进入个人行囊") +
                     "。");
    } else if (type == "hunt") {
        EnemySpec enemy;
        enemy.name = "任务妖兽";
        enemy.stage = p.stage;
        enemy.kind = "hunt";
        enemy.beast = true;
        enemy.style = exploration::mfRollBeastMaterialElement(g);
        enemy.huntFee = job.fee;
        battleStart(g, enemy);
    } else if (type == "meditate") {
        artGain(p, id, jsRound(4 + jsSqrt(p.insight) * 0.5));
        p.insight = std::min(std::max(INSIGHT_MAX, p.insight), p.insight + 1);
        a.focus = id;
        logEvent(g, "self", "参悟" + id + "道" + std::to_string(job.total) +
                                "天，经验有所增长。");
    } else {
        // mine / arena / quest 等：退还投入后走年度洞府结算
        p.stones += job.cost;
        artAnnual(g, type, id);
    }

    ecoSyncBooks(g);
    a.progress.wealth = std::max(a.progress.wealth, p.stones);
}

// 用途：结算猎妖胜负和对应悬赏。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, bool win，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoHuntEnd(Game& g, bool win) {
    Player& p = g.player;
    Arts& a = p.arts;
    Economy& e = g.economy;
    // 切磋、机缘与兽潮都不会变成城市猎妖委托。
    if (!a.battle.has_value() || a.battle->kind != "hunt" || a.battle->huntFee <= 0)
        return;
    const int fee = a.battle->huntFee;

    if (win) {
        p.stones += fee;
        e.huntCounts++;
        logEvent(g, "self", "猎妖交付：赏金 +" + std::to_string(fee) + " 灵石。");
        if (e.huntCounts >= 1) ecoAcquireBook(g, "bull", "猎妖任务堂奖励");
        if (e.huntCounts >= 5) ecoAcquireBook(g, "longlife", "猎妖五胜传承");
    } else {
        logEvent(g, "self", "猎妖未完成，未获得赏金。请养伤后再接单。");
    }
    a.battle->huntFee = 0;
}

} // namespace nz
