// 宁州一梦 · C++ 版 — 即时经济动作（实现）
#include "core/shop.h"

#include <algorithm>
#include <cmath>

#include "core/arts.h"
#include "core/battle.h"
#include "core/cultivate.h"
#include "core/economy.h"
#include "core/renown.h"
#include "core/sim.h"
#include "core/talents.h"
#include "data/reference_tables.h"

namespace nz::shop {

using std::string;
using std::vector;

namespace {

// 行情系数（与 JS 一致：直接乘 world.market）
double market(const Game& g) { return g.world.market; }

// 丹方的材料成本（按药材基础价 price，不是 sourcePrice）
double materialCost(const data::RecipeDef& r) {
    double sum = 0;
    for (const auto& [herbName, n] : parseIngredients(r.ingredients)) {
        const data::HerbDef* h = findHerbByName(herbName);
        if (!h) continue;
        sum += (h->price > 0 ? h->price : h->sourcePrice) * n;
    }
    return sum;
}

const Gear* findGear(const Game& g, const string& id) {
    for (const auto& gr : g.player.arts.gear)
        if (gr.id == id) return &gr;
    return nullptr;
}

bool isEquipped(const Game& g, const string& id) {
    for (const auto& [slot, gid] : g.player.arts.equipped)
        if (gid == id) return true;
    return false;
}

SellResult fail(const string& msg) {
    SellResult r;
    r.error = msg;
    return r;
}

SellResult ok(const string& msg, int gain = 0) {
    SellResult r;
    r.ok = true;
    r.text = msg;
    r.gain = gain;
    return r;
}

} // namespace

// ============================================================
// 出售
// ============================================================
int salePrice(const Game& g, const string& kind, const string& id) {
    if (kind == "potion") {
        const data::RecipeDef* r = findRecipe(id);
        if (!r) return 0;
        const double mats = materialCost(*r);
        // JS: Math.round(Math.max(r.price, (materials+8+r.grade*6)*1.6) * market)
        const double base = std::max(static_cast<double>(r->value),
                                     (mats + 8 + r->grade * 6) * 1.6);
        return jsRound(base * market(g));
    }
    if (kind == "gear") {
        const Gear* gr = findGear(g, id);
        if (!gr) return 0;
        const double base = (gr->craftCost > 0 ? gr->craftCost
                                               : 60.0 * gr->grade * gr->grade) *
                                1.45 +
                            gr->grade * 20.0;
        return jsRound(base * (gr->inscribed ? 1.12 : 1.0) * market(g) *
                        (1.0 + renown::sellBonus(g.player.renown)) *
                        (gr->crafted ? 1.0 + talentMods(g.player).craftedSale : 1.0));
    }
    return 0;
}

SellResult sell(Game& g, const string& kind, const string& id) {
    Player& p = g.player;
    Arts& a = p.arts;
    Economy& e = g.economy;

    if (g.ended) return fail("这一世已经结束。");
    if (g.pending.has_value()) return fail("请先完成当前事件。");
    if (a.battle.has_value()) return fail("斗法中无法交易。");
    if (!ecoIsCity(p.location)) return fail("此处没有坊市。请前往五城之一。");

    const int price = salePrice(g, kind, id);
    if (price <= 0) return fail("此物无法出售。");
    if (e.marketCash < price)
        return fail("坊市收购资金不足（余 " + std::to_string(e.marketCash) +
                    "），请下月再来。");

    const string key = kind + ":" + id;
    const int limit = renown::sellLimit(g.player.renown);
    if (e.sold[key] >= limit)
        return fail("本月此类货品需求已满（" + std::to_string(limit) +
                    " 件），请下月出售。");

    string label;
    if (kind == "potion") {
        if (a.potions[id] <= 0) return fail("没有这枚丹药。");
        const data::RecipeDef* r = findRecipe(id);
        label = r ? r->name : id;
        a.potions[id] -= 1;
    } else if (kind == "gear") {
        const Gear* gr = findGear(g, id);
        if (!gr) return fail("没有这件法器。");
        if (isEquipped(g, id)) return fail("请先卸下装备再出售。");
        label = gr->name;
        a.gear.erase(std::remove_if(a.gear.begin(), a.gear.end(),
                                    [&](const Gear& x) { return x.id == id; }),
                     a.gear.end());
    } else {
        return fail("此物无法出售。");
    }

    e.sold[key] += 1;
    e.marketCash -= price;
    p.stones += price;
    a.progress.wealth = std::max(a.progress.wealth, p.stones);

    const string text = "坊市收购" + label + "，灵石 +" + std::to_string(price) + "。";
    logEvent(g, "self", text, false, "", "灵石 +" + std::to_string(price));
    return ok(text, price);
}

// ============================================================
// 灵核 / 凝元丹
// ============================================================
int corePrice(const Game& g, bool buying) {
    // 灵核稀有：买价随境界涨，卖价打七折
    const double base = 120.0 + g.player.stage * 30.0;
    return jsRound(base * market(g) * (buying ? 1.0 : 0.7));
}

int pillPrice(const Game& g, bool buying) {
    const double base = 60.0 + g.player.stage * 12.0;
    return jsRound(base * market(g) * (buying ? 1.0 : 0.7));
}

SellResult tradeStock(Game& g, const string& kind, bool buying, int count) {
    Player& p = g.player;
    if (g.ended) return fail("这一世已经结束。");
    if (g.pending.has_value()) return fail("请先处理当前事件。");
    if (p.arts.battle.has_value()) return fail("斗法中无法交易。");
    if (count < 1 || count > 1000) return fail("交易数量须为 1 至 1000。");
    if (!ecoIsCity(p.location)) return fail("此处没有坊市。请前往五城之一。");

    if (kind == "cores") {
        if (buying) {
            const int unit = corePrice(g, true);
            const int total = unit * count;
            if (p.stones < total)
                return fail("灵石不足：需要 " + std::to_string(total) + "。");
            if (g.world.coreStock < count)
                return fail("坊市灵核存量不足（余 " +
                            std::to_string(g.world.coreStock) + "）。");
            p.stones -= total;
            g.world.coreStock -= count;
            p.cores += count;
            const string t = "购入灵核 ×" + std::to_string(count) + "。";
            logEvent(g, "self", t, false, "", "灵石 −" + std::to_string(total));
            return ok(t, -total);
        }
        if (p.cores < count) return fail("灵核不足。");
        const int unit = corePrice(g, false);
        p.cores -= count;
        g.world.coreStock += count;
        p.stones += unit * count;
        const string t = "售出灵核 ×" + std::to_string(count) + "。";
        logEvent(g, "self", t, false, "", "灵石 +" + std::to_string(unit * count));
        return ok(t, unit * count);
    }

    if (kind == "pills") {
        if (buying) {
            const int unit = pillPrice(g, true);
            const int total = unit * count;
            if (p.stones < total)
                return fail("灵石不足：需要 " + std::to_string(total) + "。");
            if (g.world.pillStock < count)
                return fail("坊市凝元丹存量不足。");
            p.stones -= total;
            g.world.pillStock -= count;
            p.pills += count;
            const string t = "购入凝元丹 ×" + std::to_string(count) + "。";
            logEvent(g, "self", t, false, "", "灵石 −" + std::to_string(total));
            return ok(t, -total);
        }
        if (p.pills < count) return fail("凝元丹不足。");
        const int unit = pillPrice(g, false);
        p.pills -= count;
        g.world.pillStock += count;
        p.stones += unit * count;
        const string t = "售出凝元丹 ×" + std::to_string(count) + "。";
        logEvent(g, "self", t, false, "", "灵石 +" + std::to_string(unit * count));
        return ok(t, unit * count);
    }
    return fail("没有这种货品。");
}

// ============================================================
// 洞府档位与配给
// ============================================================
const vector<CaveTierDef>& caveTierDefs() {
    static const vector<CaveTierDef> tiers = {
        {0, 1, 0, 0, "寻常洞府", "可设一档灵气配给。高级灵气须先升级洞府。"},
        {1, 2, 20, 120, "灵脉洞府", "可设二档灵气配给；在洞府闭关时，气场令修炼速度额外+10%。"},
        {2, 3, 60, 400, "聚灵仙府", "可设三档灵气配给；在洞府闭关时，气场令修炼速度额外+20%。"},
    };
    return tiers;
}

int caveMaxSupply(const Player& p) {
    return caveTierDefs()[clampInt(p.caveTier, 0, 2)].maxSupply;
}

namespace {
string caveBusyError(const Game& g) {
    if (g.ended) return "这一世已经结束。";
    if (g.pending) return "请先处理当前事件。";
    if (g.player.arts.battle) return "斗法中无法改动洞府或配给。";
    if (g.job) return "请先完成或中止当前安排，再改动洞府或配给。";
    return "";
}
}

string caveUpgradeError(const Game& g) {
    const string busy = caveBusyError(g);
    if (!busy.empty()) return busy;
    if (g.player.cave.empty()) return "请先在所在地安置洞府。";
    if (!cultivate::atCave(g.player)) return "请先回到洞府再升级。";
    if (g.player.caveTier >= 2) return "洞府已是最高档的聚灵仙府。";
    const auto& next = caveTierDefs()[clampInt(g.player.caveTier, 0, 1) + 1];
    if (g.player.renown < next.minRenown)
        return "升级为「" + next.name + "」需声望 " + std::to_string(next.minRenown) + "。";
    if (g.player.stones < next.cost)
        return "升级为「" + next.name + "」需 " + std::to_string(next.cost) + " 灵石。";
    return "";
}

SellResult caveUpgrade(Game& g) {
    const string error = caveUpgradeError(g);
    if (!error.empty()) return fail(error);
    const auto& next = caveTierDefs()[g.player.caveTier + 1];
    g.player.stones -= next.cost;
    g.player.caveTier = next.tier;
    const string text = "洞府升级为「" + next.name + "」，解锁" + std::to_string(next.maxSupply) + "档灵气配给。";
    logEvent(g, "self", text, true, "", "灵石 −" + std::to_string(next.cost));
    return ok(text, -next.cost);
}

SellResult caveSupplySet(Game& g, const string& kind, int level) {
    const string busy = caveBusyError(g);
    if (!busy.empty()) return fail(busy);
    if (kind != "stone" && kind != "qi") return fail("没有这种配给。");
    if (level < 0 || level > cultivate::maxSupplyLevel()) return fail("配给档位须为 0 至 3。");
    if (g.player.cave.empty() && kind == "qi" && level > 0) return fail("请先安置洞府再设置灵气配给。");
    if (kind == "qi" && level > caveMaxSupply(g.player)) {
        const auto& required = caveTierDefs()[level - 1];
        return fail("此档配给需「" + required.name + "」；升级需声望 " + std::to_string(required.minRenown) + "。");
    }
    if (kind == "stone") g.player.stoneSupply = level;
    else g.player.qiSupply = level;
    const int cost = cultivate::supplyCost(g.player, kind, level);
    const string text = string(kind == "qi" ? "灵气" : "灵石") + "配给调整为「" + cultivate::supplyName(kind, level) + "」" +
        (cost > 0 ? "，每月 " + std::to_string(cost) + " 灵石。" : "，不再投入灵石。");
    logEvent(g, "self", text);
    return ok(text);
}

SellResult caveMove(Game& g) {
    const string busy = caveBusyError(g);
    if (!busy.empty()) return fail(busy);
    if (!findLoc(g.player.location)) return fail("所在地无法安置洞府。");
    if (g.player.cave == g.player.location) return fail("洞府已经在这里了。");
    const int cost = 40 + (g.player.stage / 3) * 30;
    if (g.player.stones < cost) return fail("迁府需要 " + std::to_string(cost) + " 灵石。");
    g.player.stones -= cost;
    g.player.cave = g.player.location;
    const string text = "在" + string(findLoc(g.player.location)->name) + "安下「" + caveTierDefs()[clampInt(g.player.caveTier, 0, 2)].name + "」，已购档位保留。";
    logEvent(g, "self", text, true, "", "灵石 −" + std::to_string(cost));
    return ok(text, -cost);
}

// ============================================================
// 功法获取
// ============================================================
namespace {
string learningBusyError(const Game& g) {
    if (g.ended || g.player.health <= 0) return "此世已结束或伤势过重。";
    if (g.pending || g.job || g.player.arts.battle || g.dungeons.run) return "请先完成当前安排、事件或斗法。";
    return "";
}
bool sectDaoAllowed(const string& sect, const string& dao) {
    if (sect == "jinhong") return dao == "金" || dao == "剑";
    if (sect == "xinghe") return dao == "水" || dao == "剑";
    if (sect == "zhushan") return dao == "木";
    if (sect == "lihuo") return dao == "火";
    if (sect == "huachen") return dao == "土" || dao == "体";
    return false;
}
string manualSect(const Game& g, const string& dao) {
    if (dao == "金" || dao == "剑") return "jinhong";
    if (dao == "水") return "xinghe";
    if (dao == "木") return "zhushan";
    if (dao == "火") return "lihuo";
    if (dao == "土" || dao == "体") return "huachen";
    return findSect(g.player.sect) ? g.player.sect : "";
}
bool ownedManual(const Game& g, const string& id) {
    const auto& books = g.economy.books;
    const auto& list = g.player.arts.bookList;
    const auto studied = g.player.arts.manuals.find(id);
    return std::find(books.begin(), books.end(), id) != books.end() ||
        std::find(list.begin(), list.end(), id) != list.end() ||
        (studied != g.player.arts.manuals.end() && studied->second > 0);
}
int libraryPoints(const Game& g, int stage, bool spell = false) {
    return std::max(1, static_cast<int>(std::ceil(((spell ? 18 : 25) + stage * (spell ? 12 : 18)) *
        (1.0 - renown::libraryDiscount(g.player.renown)))));
}
bool commonSpell(const SpellDef& spell) {
    static const vector<string> ids = {"skim", "refresh", "purge", "scheme", "insightdraw"};
    return spell.dao == "气" || std::find(ids.begin(), ids.end(), spell.id) != ids.end();
}
}

ManualSource manualSource(const Game& g, const string& id) {
    ManualSource ms;

    // 先查拓展功法表：只有这张表带 source（所属宗门）。
    // 顺序很关键 —— BaseManualDef_TABLE 里其实也包含这 26 部，
    // 先查它的话每部都会被当成「书坊有售」，藏经阁那条路就成了死代码，
    // 宗门贡献也就永远花不出去。
    for (const auto& m : data::ManualDef_TABLE()) {
        if (m.id != id) continue;
        ms.valid = true;
        ms.id = m.id;
        ms.name = m.name;
        ms.stage = m.stage;
        ms.price = m.price;
        if (!m.source.empty() && (!findSect(m.source) ||
            !(sectDaoAllowed(m.source, m.dao) || (m.source == "huachen" && m.id == "wheel")))) return ManualSource{};
        ms.sect = m.source.empty() ? manualSect(g, m.dao) : m.source;
        if (m.source.empty()) ms.city = "wuling";
        // 名声高的人，藏经阁给折扣
        ms.points = libraryPoints(g, m.stage);
        ms.how = ms.sect.empty() ? "武陵城书坊购入" : string(findSect(ms.sect)->name) + "藏经阁兑换（" + std::to_string(ms.points) + " 贡献）";
        if (!ms.city.empty() && !ms.sect.empty()) ms.how += "；武陵城书坊亦有抄本";
        return ms;
    }

    // 其余是入门功法：书坊用灵石买
    for (const auto& m : data::BaseManualDef_TABLE()) {
        if (m.id != id) continue;
        ms.valid = true;
        ms.id = m.id;
        ms.name = m.name;
        ms.stage = m.stage;
        ms.price = m.price;
        ms.city = "wuling";                 // 书坊设在武陵城
        ms.sect = manualSect(g, m.dao);
        ms.points = libraryPoints(g, m.stage);
        ms.how = "武陵城书坊购入（" + std::to_string(m.price) + " 灵石）";
        if (!ms.sect.empty()) ms.how += "；" + string(findSect(ms.sect)->name) + "藏经阁可兑";
        return ms;
    }
    return ms;
}

vector<ManualSource> availableManuals(const Game& g) {
    vector<ManualSource> out;
    // BaseManualDef_TABLE 已含全部 40 部，遍历它即可，不必再叠 ManualDef
    for (const auto& m : data::BaseManualDef_TABLE()) {
        ManualSource ms = manualSource(g, m.id);
        if (ms.valid && (ownedManual(g, m.id) || (!ms.city.empty() && g.player.location == ms.city) ||
            (!ms.sect.empty() && ms.sect == g.player.sect && g.player.location == ms.sect))) out.push_back(ms);
    }
    return out;
}

SellResult bookBuy(Game& g, const string& id) {
    if (const string error = learningBusyError(g); !error.empty()) return fail(error);
    Player& p = g.player;
    Economy& e = g.economy;
    const ManualSource ms = manualSource(g, id);
    if (!ms.valid) return fail("没有这部功法。");
    if (ms.city.empty()) return fail("这部功法书坊没有，需从宗门藏经阁或机缘获取。");
    if (ownedManual(g, id))
        return fail("已经拥有此功法。");
    if (p.location != ms.city)
        return fail("请前往" +
                    string(findLoc(ms.city) ? findLoc(ms.city)->name : ms.city) +
                    "的书坊。");
    if (p.stage < ms.stage) return fail("境界未达到获取要求。");
    if (p.stones < ms.price) return fail("灵石不足。");

    p.stones -= ms.price;
    e.books.push_back(id);
    if (std::find(p.arts.bookList.begin(), p.arts.bookList.end(), id) ==
        p.arts.bookList.end())
        p.arts.bookList.push_back(id);

    const string t = "在书坊购入《" + ms.name + "》抄本。";
    logEvent(g, "self", t, true, "", "灵石 −" + std::to_string(ms.price));
    return ok(t, -ms.price);
}

SellResult libraryExchange(Game& g, const string& id) {
    if (const string error = learningBusyError(g); !error.empty()) return fail(error);
    Player& p = g.player;
    Economy& e = g.economy;
    const ManualSource ms = manualSource(g, id);
    if (!ms.valid) return fail("没有这部功法。");
    if (ms.sect.empty()) return fail("这部功法不在藏经阁。");
    if (p.sect != ms.sect)
        return fail("仅可在所属宗门藏经阁兑换。");
    if (p.location != ms.sect)
        return fail("请回到山门藏经阁。");
    if (ownedManual(g, id))
        return fail("已经拥有此功法。");
    if (p.stage < ms.stage) return fail("境界未达到获取要求。");
    if (e.contribution < ms.points)
        return fail("宗门贡献不足：需要 " + std::to_string(ms.points) +
                    "，你有 " + std::to_string(e.contribution) + "。");

    e.contribution -= ms.points;
    e.books.push_back(id);
    if (std::find(p.arts.bookList.begin(), p.arts.bookList.end(), id) ==
        p.arts.bookList.end())
        p.arts.bookList.push_back(id);

    const string t = "在藏经阁以 " + std::to_string(ms.points) + " 贡献换得《" +
                     ms.name + "》。";
    logEvent(g, "sect", t, true, "", "贡献 −" + std::to_string(ms.points));
    return ok(t, -ms.points);
}

LibrarySpellSource librarySpellSource(const Game& g, const string& id) {
    LibrarySpellSource source;
    const auto* spell = battleSpell(id);
    if (!spell || id == "strike" || id == "defend" || !findSect(g.player.sect)) return source;
    if (!commonSpell(*spell) && !sectDaoAllowed(g.player.sect, spell->dao)) return source;
    source.valid = true;
    source.id = spell->id; source.name = spell->name; source.dao = spell->dao;
    source.sect = g.player.sect; source.stage = spell->stage;
    source.desc = battleSpellDescription(*spell);
    source.points = libraryPoints(g, spell->stage, true);
    const auto& known = g.player.arts.knownSpells;
    source.known = std::find(known.begin(), known.end(), id) != known.end() ||
        std::find(g.player.arts.spells.begin(), g.player.arts.spells.end(), id) != g.player.arts.spells.end();
    source.reason = learningBusyError(g);
    if (source.reason.empty() && g.player.location != source.sect) source.reason = "请回到本宗藏经阁。";
    if (source.reason.empty() && source.known) source.reason = "已经领悟此神通。";
    if (source.reason.empty() && g.player.stage < source.stage) source.reason = "至少需要" + realmName(source.stage) + "。";
    if (source.reason.empty() && g.economy.contribution < source.points) source.reason = "宗门贡献不足。";
    source.enabled = source.reason.empty();
    return source;
}

vector<LibrarySpellSource> availableLibrarySpells(const Game& g) {
    vector<LibrarySpellSource> sources;
    for (const auto& spell : allSpells()) {
        auto source = librarySpellSource(g, spell.id);
        if (source.valid) sources.push_back(std::move(source));
    }
    return sources;
}

SellResult librarySpellExchange(Game& g, const string& id) {
    const auto source = librarySpellSource(g, id);
    if (!source.valid) return fail("此神通不在本门传承中，通用攻击与防御也无需兑换。");
    if (!source.enabled) return fail(source.reason);
    g.economy.contribution -= source.points;
    g.player.arts.knownSpells.push_back(id);
    const string text = "在藏经阁以" + std::to_string(source.points) + "贡献领悟「" + source.name + "」，可在神通页自行装备。";
    logEvent(g, "sect", text, true, "", "贡献 −" + std::to_string(source.points));
    return ok(text, -source.points);
}

// ============================================================
// 法器
// ============================================================
SellResult unequip(Game& g, const string& id) {
    Arts& a = g.player.arts;
    for (auto it = a.equipped.begin(); it != a.equipped.end(); ++it) {
        if (it->second != id) continue;
        const string slot = it->first;
        a.equipped.erase(it);
        return ok("卸下了" + slot + "位的法器。");
    }
    return fail("这件法器并未装备。");
}

SellResult salvage(Game& g, const string& id) {
    Player& p = g.player;
    Arts& a = p.arts;
    const Gear* gr = findGear(g, id);
    if (!gr) return fail("没有这件法器。");
    if (isEquipped(g, id)) return fail("请先卸下装备再分解。");

    const int back = 5 * gr->grade;
    const string name = gr->name;
    a.gear.erase(std::remove_if(a.gear.begin(), a.gear.end(),
                                [&](const Gear& x) { return x.id == id; }),
                 a.gear.end());
    p.stones += back;
    const string t = "分解「" + name + "」，回收灵石 +" + std::to_string(back) + "。";
    logEvent(g, "self", t, false, "", "灵石 +" + std::to_string(back));
    return ok(t, back);
}

SellResult inscribe(Game& g, const string& id) {
    Player& p = g.player;
    Arts& a = p.arts;
    if (!artHas(a, "器", 1))
        return fail("需先领悟「器道入门」才能铭刻。");
    for (auto& gr : a.gear) {
        if (gr.id != id) continue;
        if (gr.inscribed) return fail("这件法器已经铭刻过了。");
        const int cost = 30 * gr.grade;
        if (p.stones < cost)
            return fail("铭刻需 " + std::to_string(cost) + " 灵石。");
        p.stones -= cost;
        gr.inscribed = true;
        const string t = "为「" + gr.name + "」铭刻符文，效果 +12%。";
        logEvent(g, "self", t, true, "", "灵石 −" + std::to_string(cost));
        return ok(t, -cost);
    }
    return fail("没有这件法器。");
}

// ============================================================
// 丹炉
// ============================================================
SellResult repairFurnace(Game& g) {
    Player& p = g.player;
    Arts& a = p.arts;
    if (a.durability >= 40) return fail("丹炉完好，无需修理。");
    if (p.stones < 20) return fail("修炉需 20 灵石。");
    p.stones -= 20;
    a.durability = 40;
    const string t = "修补丹炉，耐久回满。";
    logEvent(g, "self", t, false, "", "灵石 −20");
    return ok(t, -20);
}

vector<const data::FurnaceDef*> buyableFurnaces(const Game& g) {
    vector<const data::FurnaceDef*> out;
    // 按境界解锁：1 + stage/3 品以下可买
    const int maxGrade = std::min(6, 1 + g.player.stage / 3);
    for (const auto& f : data::FurnaceDef_TABLE())
        if (f.grade <= maxGrade) out.push_back(&f);
    return out;
}

SellResult buyFurnace(Game& g, const string& id) {
    Player& p = g.player;
    Arts& a = p.arts;
    if (a.furnace == id) return fail("这已经是当前丹炉。");
    for (const auto& f : data::FurnaceDef_TABLE()) {
        if (f.id != id) continue;
        const int maxGrade = std::min(6, 1 + p.stage / 3);
        if (f.grade > maxGrade)
            return fail("境界不足，暂时用不了这尊丹炉。");
        const int price = f.price > 0 ? f.price : 100 * f.grade * f.grade;
        if (p.stones < price)
            return fail("灵石不足：需要 " + std::to_string(price) + "。");
        p.stones -= price;
        a.furnace = f.id;
        a.durability = 40;
        const string t = "换用「" + string(f.name) + "」，耐久回满。";
        logEvent(g, "self", t, true, "", "灵石 −" + std::to_string(price));
        return ok(t, -price);
    }
    return fail("没有这尊丹炉。");
}

// ============================================================
// 中止安排
// ============================================================
SellResult cancelJob(Game& g) {
    if (!g.job.has_value()) return fail("当前没有进行中的安排。");
    const Job job = *g.job;
    const string name = job.name;
    g.job.reset();
    string t = "中止「" + name + "」。已投入的材料与灵石不返还。";
    if (job.actionType == "travel") {
        const string origin = job.origin.empty() ? g.player.location : job.origin;
        const LocationDef* place = findLoc(origin);
        t = "中止「" + name + "」，仍停留在出发地" +
            (place ? string(place->name) : origin) + "。";
        if (job.cost > 0)
            t += "已支付的" + string(job.travelMode == "boat" || job.travelMode.empty() ? "船资" : "御器费用") +
                 std::to_string(job.cost) + "灵石不返还。";
        else t += "步行未支付灵石。";
    }
    logEvent(g, "self", t, false);
    return ok(t);
}

} // namespace nz::shop
