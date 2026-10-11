// 宁州一梦 · C++ 版 — 游戏接口（前端通过 HTTP 调用这里的规则）
//
// 这一层是「浏览器界面」与「C++ 规则」之间的桥：
//   前端发 POST /api/<动作>  ->  这里解析参数、调用 core 函数  ->  返回 JSON
//
// 设计原则：这一层只做「翻译」，不写任何规则。
// 所有数值都来自 core，保证和自测覆盖的是同一套代码。
#include "core/api.h"
#include "core/api_exploration.h"
#include "core/dungeon.h"
#include "core/checkpoint.h"
#include "core/source_limits.h"
#include "core/talents.h"
#include "core/sect_trial.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/adventure.h"
#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/json.h"
#include "core/conflict.h"
#include "core/cultivate.h"
#include "core/economy.h"
#include "core/friendship.h"
#include "core/tide.h"
#include "core/legacy.h"
#include "core/play.h"
#include "core/renown.h"
#include "core/reward_receipt.h"
#include "core/roster.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/society.h"
#include "core/sword.h"
#include "core/npc_learning.h"
#include "core/location_services.h"
#include "core/travel.h"
#include "data/reference_tables.h"

namespace nz::api {

using json::Value;

namespace {
Session& defaultSession() { static Session s; return s; }
Session* currentSession = nullptr;
Session& cur() { return currentSession ? *currentSession : defaultSession(); }
} // namespace

void bindSession(Session* s) { currentSession = s; }

bool restoreGame(Session& s, const std::string& text, std::string* err) {
    Game loaded;
    if (!loadFromJson(text, loaded, err)) return false;
    s.g = std::move(loaded);
    checkpointEnable(s.g);
    s.started = true;
    return true;
}

Game& game() {
    return cur().g;
}

// 这一份 game() 里是否真的开过局。
// 冷启动（服务刚起、还没点「新的一世」）时是 false ——
// 前端要靠它区分「空壳默认值」和「真的存档」，
// 否则会把默认值当存档渲染，一读 S.clock 就崩。
bool& started() {
    return cur().started;
}

namespace {

// ------------------------------------------------------------
// 小工具
// ------------------------------------------------------------
std::string strOf(const Value& v, const char* key,
                  const std::string& def = "") {
    const Value& x = v[key];
    return x.isString() ? x.asString() : def;
}

int intOf(const Value& v, const char* key, int def = 0) {
    const Value& x = v[key];
    if (!x.isNumber()) return def;
    const double n = x.asNumber();
    if (!std::isfinite(n) || n < std::numeric_limits<int>::min() ||
        n > std::numeric_limits<int>::max()) return def;
    return static_cast<int>(n);
}

uint32_t seedOf(const Value& v, uint32_t def = 20261004) {
    const Value& x = v["seed"];
    return x.isNumber() ? static_cast<uint32_t>(x.asNumber()) : def;
}

bool readOnlyRoute(const std::string& path) {
    for (const char* route : {"state", "slots", "export", "arts/spells",
         "arts/nodes", "social-actions", "sects", "achievements", "legacy",
         "legacy/draft", "conflict/preview", "forge-options", "forge-preview", "eco-options",
         "npc-bag", "tide", "roster", "build-options", "build-check"})
        if (path == std::string("/api/") + route) return true;
    return false;
}

Value num(int v) { return Value(v); }
Value num(double v) { return Value(v); }
Value num(long long v) { return Value(static_cast<double>(v)); }
Value str(const std::string& s) { return Value(s); }
Value str(const char* s) { return Value(std::string(s ? s : "")); }
std::string decimal(double value) {
    std::ostringstream out; out << std::fixed << std::setprecision(1) << value;
    return out.str();
}
Value previewView(const Player& player, const SpellDef& spell) {
    const Preview pv = battleReferencePreview(player, spell);
    const Battle opening = battleReferenceState(player);
    Value o(json::Object{});
    o.set("valid", Value(pv.valid)); o.set("reference", Value(true));
    o.set("context", str("按当前" + realmName(player.stage) + "、已装备功法/悟道/法器与开局准备计算；气血上限" +
        std::to_string(opening.max) + "、开局灵力" + std::to_string(opening.mana) +
        "；目标无护盾/减伤，无相生或额外蓄积状态。境界未满足时仅供参考。"));
    o.set("damage", num(pv.damage)); o.set("shield", num(pv.shield));
    o.set("heal", num(pv.heal)); o.set("healPotential", num(pv.healPotential));
    o.set("cost", num(pv.cost)); o.set("energy", num(pv.energy)); o.set("manaGain", num(pv.manaGain));
    o.set("qi", num(pv.qi)); o.set("draw", num(pv.draw)); o.set("drawNext", num(pv.drawNext));
    o.set("hpMaxChange", num(pv.hpMaxChange)); o.set("manaRegenDelta", num(pv.manaRegenDelta));
    o.set("storedMana", num(pv.storedMana)); o.set("damageBonusPct", num(pv.damageBonusPct));
    o.set("reductionPct", num(pv.reductionPct)); o.set("guardPct", num(pv.guardPct));
    o.set("text", str(pv.text)); o.set("conditions", str(battlePreviewConditions(player, spell)));
    json::Array statuses;
    for (const auto& effect : pv.statuses) {
        Value item(json::Object{});
        item.set("id", str(effect.id)); item.set("name", str(effect.name)); item.set("target", str(effect.target));
        item.set("amount", num(effect.amount)); item.set("total", num(effect.total));
        item.set("turns", num(effect.turns)); item.set("cap", num(effect.cap)); item.set("perTurn", num(effect.perTurn));
        statuses.push_back(std::move(item));
    }
    o.set("statuses", Value(std::move(statuses)));
    return o;
}
Player artifactProjectedPlayer(const Player& player, const Gear& gear) {
    Player projected = player;
    auto& inventory = projected.arts.gear;
    auto existing = std::find_if(inventory.begin(), inventory.end(), [&](const Gear& item) { return item.id == gear.id; });
    if (existing == inventory.end()) inventory.push_back(gear); else *existing = gear;
    projected.arts.equipped[gear.slot] = gear.id;
    return projected;
}
Value artifactPassiveView(const Player& player, const Gear& gear) {
    Player projected = artifactProjectedPlayer(player, gear);
    const ArtMods before = talentMods(player), after = talentMods(projected);
    const double primary = gear.bonus * (artHas(player.arts, "器", 3) ? 1.2 : 1.0) * (gear.inscribed ? 1.12 : 1.0);
    const double element = gear.bonus * 0.5;
    const double polarity = gear.bonus * (gear.polarity == "天阳" ? 0.1 : 0.2);
    const std::string primaryName = gear.slot == "weapon" ? "攻击倍率" : gear.slot == "armor" ? "气血倍率" : "灵力属性";
    const double primaryValue = gear.slot == "accessory" ? jsRound(primary * 10) : primary * 100;
    std::string text = "装备被动：" + primaryName + "+" + decimal(primaryValue) +
        (gear.slot == "accessory" ? "点" : "个百分点") + "；";
    if (daoIndex(gear.element) >= 0) text += gear.element + "系伤害倍率+" + decimal(element * 100) + "个百分点；";
    text += (gear.polarity == "天阳" ? "攻击倍率+" : "护盾倍率+") + decimal(polarity * 100) + "个百分点。";
    text += "替换当前同槽装备后：攻击倍率" + decimal((after.attack - before.attack) * 100) +
        "个百分点、气血上限" + std::to_string(battleReferenceState(projected).max - battleReferenceState(player).max) +
        "、每回合灵力" + std::to_string(battleEnergy(projected) - battleEnergy(player)) +
        "（已计入属性上限；灵力属性每4点影响自然回复）。";
    Value o(json::Object{});
    o.set("text", str(text)); o.set("primaryStat", str(primaryName)); o.set("primaryValue", num(primaryValue));
    o.set("elementBonusPct", num(daoIndex(gear.element) >= 0 ? element * 100 : 0));
    o.set("polarityStat", str(gear.polarity == "天阳" ? "attack" : "shield")); o.set("polarityBonusPct", num(polarity * 100));
    o.set("inscribed", Value(gear.inscribed));
    Value delta(json::Object{});
    delta.set("attackPct", num((after.attack - before.attack) * 100)); delta.set("hpPct", num((after.hp - before.hp) * 100));
    delta.set("shieldPct", num((after.shield - before.shield) * 100)); delta.set("mana", num(after.mana - before.mana));
    delta.set("hpMax", num(battleReferenceState(projected).max - battleReferenceState(player).max));
    delta.set("manaRegen", num(battleEnergy(projected) - battleEnergy(player)));
    const int ei = daoIndex(gear.element); delta.set("elementPct", num(ei >= 0 ? (after.elements[ei] - before.elements[ei]) * 100 : 0));
    o.set("delta", std::move(delta)); return o;
}
Value artifactView(const Player& player, const ArtifactAbility& ability) {
    if (!ability.valid) return Value();
    const auto& sp = ability.spell;
    Value o(json::Object{});
    o.set("id", str(sp.id)); o.set("gearId", str(ability.gearId));
    o.set("name", str(sp.name)); o.set("shape", str(ability.shape)); o.set("shapeName", str(ability.shapeName));
    o.set("dao", str(sp.dao)); o.set("kind", str(sp.kind));
    o.set("description", str(ability.desc)); o.set("cost", num(battleCost(player, sp)));
    o.set("cooldown", num(sp.cooldown)); o.set("stage", num(sp.stage)); o.set("stageName", str(realmName(sp.stage)));
    const Player projected = artifactProjectedPlayer(player, ability.gear);
    o.set("preview", previewView(projected, sp));
    o.set("passive", artifactPassiveView(player, ability.gear));
    o.set("canEquip", Value(player.stage >= sp.stage));
    o.set("inscribed", Value(ability.gear.inscribed));
    return o;
}

// 统一响应
http::Response jsonOk(const Value& v) {
    http::Response r;
    r.status = 200;
    r.body = v.dump();
    return r;
}

http::Response jsonError(const std::string& msg, int status = 400) {
    Value o(json::Object{});
    o.set("ok", Value(false));
    o.set("error", str(msg));
    http::Response r;
    r.status = status;
    r.body = o.dump();
    return r;
}

// ------------------------------------------------------------
// 传承档案的存读（放存档目录下的 legacy.json）
// ------------------------------------------------------------
std::string legacyPath() { return defaultSaveDir() + "/legacy.json"; }

std::filesystem::path utf8Path(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

legacy::Archive legacyLoad() {
    std::ifstream f(utf8Path(legacyPath()), std::ios::binary);
    if (!f) return legacy::create();
    std::stringstream ss;
    ss << f.rdbuf();
    bool ok = false;
    legacy::Archive a = legacy::fromJson(ss.str(), &ok);
    // 档案损坏时退回空白档案，不要卡死游戏
    return ok ? a : legacy::create();
}

bool legacySave(const legacy::Archive& a) {
    std::error_code ec;
    std::filesystem::create_directories(utf8Path(defaultSaveDir()), ec);
    if (ec) return false;
    const auto path = utf8Path(legacyPath());
    const auto temporary = utf8Path(legacyPath() + ".tmp");
    std::ofstream f(temporary, std::ios::binary);
    if (!f) return false;
    f << legacy::toJson(a);
    f.close();
    if (f.fail()) return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, path, ec);
    return !ec;
#endif
}

// ------------------------------------------------------------
// 状态快照：前端每次动作后都拿它整页重绘
// ------------------------------------------------------------
Value buildState(const Game& g) {
    Value s(json::Object{});
    s.set("dungeons", dungeonView(g));
    s.set("cave", caveView(g));
    s.set("checkpoint", checkpointView(g));

    // ---- 玩家 ----
    {
        const Player& p = g.player;
        Value o(json::Object{});
        o.set("name", str(p.name));
        o.set("gender", str(p.gender));
        o.set("age", num(p.age));
        // 已选天赋：建号后要能回头看自己选了什么
        {
            json::Array arr;
            for (const auto& tid : p.traits) {
                const TalentDef* td = findTalent(tid);
                Value to(json::Object{});
                to.set("id", str(tid));
                to.set("name", str(td ? td->name : tid));
                to.set("tag", str(td ? td->tag : ""));
                to.set("cost", num(td ? td->cost : 0));
                to.set("desc", str(td ? td->desc : ""));
                arr.push_back(std::move(to));
            }
            o.set("talents", Value(std::move(arr)));
        }
        {
            json::Array arr;
            for (const auto& tid : p.traits) arr.push_back(str(tid));
            o.set("traits", Value(std::move(arr)));
        }
        o.set("apt", num(p.apt));
        o.set("luck", num(p.luck));
        o.set("insight", num(p.insight));
        o.set("sword", num(p.sword));
        o.set("escapeSpeed", num(artEscapeSpeed(p)));
        o.set("portrait", str(personPortrait(p.gender, p.sect, true)));
        o.set("alchemy", num(p.alchemy));
        o.set("body", num(p.body));
        // 体魄带来的气血加成（带软上限，界面上要说明为什么不再线性）
        {
            Value b(json::Object{});
            b.set("points", num(p.body));
            b.set("bonus", num(cultivate::bodyHpBonus(p.body)));
            b.set("softCap", num(cultivate::bodySoftCap()));
            b.set("cap", num(cultivate::bodyHpCap()));
            b.set("atSoftCap", Value(p.body >= cultivate::bodySoftCap()));
            o.set("bodyHp", std::move(b));
        }

        o.set("health", num(p.health));
        o.set("lifespan", num(p.lifespan));
        o.set("lifeBonus", num(p.lifeBonus));
        o.set("stage", num(p.stage));
        o.set("highestStage", num(p.highestStage));
        o.set("realmName", str(realmName(p.stage)));
        o.set("xp", num(p.xp));
        o.set("xpNeed", num(realmOf(p.stage).need));
        o.set("stones", num(p.stones));
        o.set("herbs", num(p.herbs));
        o.set("pills", num(p.pills));
        o.set("cores", num(p.cores));
        o.set("renown", num(p.renown));
        o.set("sect", str(p.sect));
        o.set("sectName",
               str(p.sect.empty() ? "散修"
                                  : (findSect(p.sect) ? findSect(p.sect)->name
                                                      : p.sect)));
        // 身份也是玩家属性，放进 player 里；
        // 之前只有顶层 rank，任务页读 S.player.rank 会显示成「—」。
        o.set("rank", str(socRank(p)));
        o.set("location", str(p.location));
        o.set("locationName",
               str(findLoc(p.location) ? findLoc(p.location)->name : p.location));
        // 洞府与配给
        o.set("cave", str(p.cave));
        o.set("caveName",
               str(p.cave.empty()
                       ? "尚未安府"
                       : (findLoc(p.cave) ? findLoc(p.cave)->name : p.cave)));
        // 档位本身在下面的 supply 子对象里（带名称/月耗/说明），
        // 这里不再重复下发裸数字，免得两处不一致。
        o.set("atCave", Value(cultivate::atCave(p)));
        o.set("qiActive", Value(cultivate::qiActive(p)));
        {
            Value c(json::Object{});
            c.set("stoneLevel", num(p.stoneSupply));
            c.set("qiLevel", num(p.qiSupply));
            c.set("stoneCost", num(cultivate::supplyCost(p, "stone", p.stoneSupply)));
            c.set("qiCost", num(cultivate::supplyCost(p, "qi", p.qiSupply)));
            c.set("stoneName", str(cultivate::supplyName("stone", p.stoneSupply)));
            c.set("qiName", str(cultivate::supplyName("qi", p.qiSupply)));
            c.set("stoneDesc", str(cultivate::supplyDesc("stone", p.stoneSupply)));
            c.set("qiDesc", str(cultivate::supplyDesc("qi", p.qiSupply)));
            c.set("maxLevel", num(cultivate::maxSupplyLevel()));
            c.set("maxQiLevel", num(cultivate::unlockedSupplyLevel(p)));
            c.set("moveCost", num(40 + (p.stage / 3) * 30));
            o.set("supply", std::move(c));
        }
        // 修炼速度拆解：让玩家看清速度是怎么来的
        {
            const cultivate::Breakdown b = cultivate::breakdown(p);
            Value r(json::Object{});
            r.set("manual", num(b.manual));
            r.set("apt", num(b.apt));
            r.set("insight", num(b.insight));
            r.set("realm", num(b.realm));
            r.set("legacy", num(b.legacy));
            r.set("stone", num(b.stone));
            r.set("qi", num(b.qi));
            r.set("cave", num(b.cave));
            r.set("talentMul", num(talentCultivationMul(p)));
            r.set("total", num(b.total));
            r.set("base", num(cultivate::baseRate(p)));
            r.set("note", str(b.caveNote));
            r.set("aptMul", num(cultivate::aptMul(p)));
            r.set("insightMul", num(cultivate::insightMul(p)));
            o.set("cultivation", std::move(r));
        }
        s.set("player", std::move(o));
    }

    // ---- 时间 ----
    {
        const CalDate d = calendarDate(g);
        Value o(json::Object{});
        o.set("days", num(g.clock.days));
        o.set("year", num(g.year));
        o.set("month", num(d.month));
        o.set("day", num(d.day));
        o.set("left", num(d.left));
        o.set("label", str(calendarLabel(g)));
        s.set("clock", std::move(o));
    }

    // ---- 兽潮（全图事件）----
    {
        Value t(json::Object{});
        t.set("active", Value(g.tide.active));
        t.set("severity", num(g.tide.severity));
        t.set("monthsLeft", num(g.tide.monthsLeft));
        t.set("wavesFought", num(g.tide.wavesFought));
        t.set("wavesWon", num(g.tide.wavesWon));
        t.set("npcDeaths", num(g.tide.npcDeaths));
        t.set("dangerBonus", num(tide::dangerBonus(g)));
        t.set("status", str(tide::statusText(g)));
        json::Array fa;
        for (const auto& nm : g.tide.fallen) fa.push_back(str(nm));
        t.set("fallen", Value(std::move(fa)));
        s.set("tide", std::move(t));
    }


    // ---- 洞府 ----


    {
        const Arts& a = g.player.arts;
        Value o(json::Object{});
        o.set("focus", str(a.focus));
        o.set("main", str(a.main));
        o.set("points", num(a.points));
        o.set("poison", num(a.poison));
        o.set("heart", num(a.heart));
        o.set("furnace", str(a.furnace));
        o.set("durability", num(a.durability));
        {
            Value e(json::Object{});
            for (const auto& [k, v] : a.exp) e.set(k, num(v));
            o.set("exp", std::move(e));
        }
        {
            json::Array arr;
            for (const auto& n : a.nodes) arr.push_back(str(n));
            o.set("nodes", Value(std::move(arr)));
        }
        {
            json::Array arr;
            for (const auto& x : a.spells) arr.push_back(str(x));
            o.set("spells", Value(std::move(arr)));
        }
        o.set("spellSlots", num(battleSpellSlots(g.player)));
        {
            json::Array known, books;
            for (const auto& id : a.knownSpells) known.push_back(str(id));
            vector<string> ownedBooks = a.bookList;
            for (const auto& id : g.economy.books)
                if (std::find(ownedBooks.begin(), ownedBooks.end(), id) == ownedBooks.end()) ownedBooks.push_back(id);
            for (const auto& id : ownedBooks) books.push_back(str(id));
            o.set("knownSpells", Value(std::move(known))); o.set("bookList", Value(std::move(books)));
        }
        o.set("manaRegen", num(battleEnergy(g.player)));
        {
            json::Array mandatory;
            for (const char* id : {"strike", "defend"}) {
                const SpellDef* sp = battleSpell(id);
                Value item(json::Object{});
                item.set("id", str(id));
                item.set("name", str(sp ? sp->name : id));
                item.set("copies", num(5));
                item.set("mandatory", Value(true));
                item.set("description", str(sp ? battleSpellDescription(*sp) : ""));
                if (sp) item.set("preview", previewView(g.player, *sp));
                mandatory.push_back(std::move(item));
            }
            o.set("mandatorySpells", Value(std::move(mandatory)));
        }
        // 携带神通的完整信息：光给 id，玩家看不出这是什么东西
        {
            json::Array arr;
            for (const auto& sid : a.spells) {
                const SpellDef* sp = battleSpell(sid);
                Value so(json::Object{});
                so.set("id", str(sid));
                so.set("name", str(sp ? sp->name : sid));
                so.set("dao", str(sp ? sp->dao : ""));
                so.set("kind", str(sp ? sp->kind : ""));
                so.set("cost", num(sp ? sp->cost : 0));
                so.set("actualCost", num(sp ? battleCost(g.player, *sp) : 0));
                so.set("cooldown", num(sp ? sp->cooldown : 0));
                so.set("stage", num(sp ? sp->stage : 0));
                so.set("stageName", str(sp ? realmName(sp->stage) : ""));
                so.set("description", str(sp ? battleSpellDescription(*sp) : ""));
                if (sp) so.set("preview", previewView(g.player, *sp));
                arr.push_back(std::move(so));
            }
            o.set("spellList", Value(std::move(arr)));
        }
        {
            Value m(json::Object{});
            for (const auto& [k, v] : a.manuals) m.set(k, num(v));
            o.set("manuals", std::move(m));
        }
        // 已修功法的完整信息：名称、所属大道、层数、效果
        {
            json::Array arr;
            for (const auto& [mid, lv] : a.manuals) {
                ManualInfo mv;
                if (!findManualInfo(mid, mv)) continue;
                Value mo(json::Object{});
                mo.set("id", str(mid));
                mo.set("name", str(mv.name));
                mo.set("dao", str(mv.dao));
                mo.set("level", num(lv));
                mo.set("effect", str(mv.effect));
                mo.set("stage", num(mv.stage));
                mo.set("price", num(mv.price));
                mo.set("isMain", Value(a.main == mid));
                mo.set("isAux",
                       Value(std::find(a.aux.begin(), a.aux.end(), mid) !=
                             a.aux.end()));
                mo.set("nextDays", num(cultivate::learnDays(g.player, lv)));
                mo.set("nextCost", num(8 + (lv + 1) * 4));
                mo.set("maxed", Value(lv >= 5));
                arr.push_back(std::move(mo));
            }
            o.set("manualList", Value(std::move(arr)));
        }
        {
            Value m(json::Object{});
            for (const auto& [k, v] : a.herbs) m.set(k, num(v));
            o.set("herbs", std::move(m));
        }
        {
            Value m(json::Object{});
            for (const auto& [k, v] : a.materials) m.set(k, num(v));
            o.set("materials", std::move(m));
        }
        // 丹药与服用记录：前端「丹药行囊」要用，
        // 之前没下发，Object.entries(undefined) 会让整个坊市页崩掉。
        {
            Value m(json::Object{});
            for (const auto& [k, v] : a.potions) m.set(k, num(v));
            o.set("potions", std::move(m));
        }
        {
            Value m(json::Object{});
            for (const auto& [k, v] : a.used) m.set(k, num(v));
            o.set("used", std::move(m));
        }
        {
            Value m(json::Object{});
            for (const auto& [k, v] : a.combatPrep) m.set(k, num(v));
            o.set("combatPrep", std::move(m));
        }
        o.set("aptPills", num(a.aptPills));
        o.set("breakBuff", str(a.breakBuff));

        // 加成汇总，前端可直接显示
        {
            const ArtMods& m = talentMods(g.player);
            Value mo(json::Object{});
            mo.set("cult", num(m.cult));
            mo.set("attack", num(m.attack));
            mo.set("hp", num(m.hp));
            mo.set("mana", num(m.mana));
            mo.set("sword", num(m.sword));
            mo.set("brewYield", num(m.brewYield));
            mo.set("forgeSpeed", num(m.forgeSpeed));
            mo.set("craftedSale", num(m.craftedSale));
            mo.set("escapeSpeed", num(m.escapeSpeed));
            o.set("mods", std::move(mo));
        }
        s.set("arts", std::move(o));
    }
    {
        json::Array builds;
        for (const auto& build : artBuilds()) {
            Value item(json::Object{});
            item.set("id", str(build.id));
            item.set("name", str(build.name));
            item.set("dao", str(build.dao));
            item.set("desc", str(build.desc));
            auto ids = [](const std::vector<std::string>& list) {
                json::Array arr;
                for (const auto& id : list) arr.push_back(str(id));
                return Value(std::move(arr));
            };
            item.set("manuals", ids(build.manuals));
            item.set("spells", ids(build.spells));
            item.set("nodes", ids(build.nodes));
            builds.push_back(std::move(item));
        }
        s.set("artBuilds", Value(std::move(builds)));
    }

    // ---- 计划与在途工作 ----
    s.set("plan", str(g.plan));
    if (g.job.has_value()) {
        Value o(json::Object{});
        o.set("type", str(g.job->actionType));
        o.set("name", str(g.job->name));
        o.set("remaining", num(g.job->remaining));
        o.set("total", num(g.job->total));
        o.set("from", str(g.job->origin.empty() && g.job->actionType == "travel" ?
                          g.player.location : g.job->origin));
        o.set("to", str(g.job->destination));
        o.set("mode", str(g.job->travelMode.empty() && g.job->actionType == "travel" ?
                          "boat" : g.job->travelMode));
        o.set("elapsed", num(g.job->total - g.job->remaining));
        o.set("cost", num(g.job->cost));
        s.set("job", std::move(o));
    }

    // ---- 待处理抉择 ----
    if (g.pending.has_value()) {
        Value o(json::Object{});
        o.set("type", str(g.pending->type));
        o.set("title", str(g.pending->title));
        o.set("subtitle", str(g.pending->subtitle));
        o.set("text", str(g.pending->text));
        json::Array opts;
        for (const auto& c : g.pending->options) {
            Value co(json::Object{});
            co.set("id", str(c.id));
            co.set("name", str(c.name));
            co.set("desc", str(c.desc));
            co.set("disabled", Value(c.disabled));
            opts.push_back(std::move(co));
        }
        o.set("options", Value(std::move(opts)));
        s.set("pending", std::move(o));
    }

    // ---- 斗法（含每张手牌的即时预览）----
    if (g.player.arts.battle.has_value()) {
        const Battle& b = *g.player.arts.battle;
        Value o(json::Object{});
        o.set("enemy", str(b.enemy));
        o.set("npcId", str(b.npcId));
        o.set("style", str(b.style));
        o.set("intentKind", str(b.intentKind));
        o.set("enemyPortrait", str(b.enemyBeast ? beastPortrait(b.enemyStage) : personPortrait(b.enemyGender, b.enemySect)));
        o.set("enemyStage", num(b.enemyStage));
        o.set("dungeonSite", str(b.dungeonSite));
        o.set("dungeonNode", str(b.dungeonNode));
        o.set("enemyRealmName", str(realmName(b.enemyStage)));
        o.set("playerSpeed", num(b.playerSpeed));
        o.set("enemySpeed", num(b.enemySpeed));
        o.set("playerFirst", Value(b.playerFirst));
        o.set("escapeChance", num(battleEscapeChance(g.player, b)));
        o.set("enemyDao", str(b.enemyDao));
        o.set("enemyBeast", Value(b.enemyBeast));
        o.set("enemySect", str(b.enemySect));
        o.set("enemyMana", num(b.enemyCards.mana));
        o.set("enemyMaxMana", num(b.enemyCards.maxMana));
        const auto intent = battleEnemyPreview(g);
        o.set("intentDescription", str(intent.description));
        o.set("intentDamage", num(intent.damage));
        o.set("intentShield", num(intent.shield));
        o.set("intentHeal", num(intent.heal));
        { json::Array spells; for (const auto& id : intent.spells) { const auto* sp = battleSpell(id); spells.push_back(str(sp ? sp->name : id)); } o.set("intentSpells", Value(std::move(spells))); }
        o.set("hp", num(b.hp));
        o.set("max", num(b.max));
        o.set("enemyHp", num(b.enemyHp));
        o.set("enemyMax", num(b.enemyMax));
        o.set("mana", num(b.mana));
        o.set("maxMana", num(b.maxMana));
        o.set("shield", num(b.shield));
        o.set("enemyShield", num(b.enemyShield));
        o.set("swordQi", num(b.swordQi));
        o.set("turn", num(b.turn));
        o.set("intentName", str(b.intentName));
        o.set("intentRaw", num(b.intentRaw));
        // 生死斗法要在界面上明确提示，否则玩家会莫名其妙死掉
        o.set("lethal", Value(b.lethal));
        o.set("kind", str(b.kind));
        o.set("quest", str(b.quest));

        {
            json::Array lg;
            for (const auto& s2 : b.log) lg.push_back(str(s2));
            o.set("log", Value(std::move(lg)));
        }
        // 手牌：带上真实预览（和出牌共用同一函数，所见即所得）
        {
            json::Array hand;
            const vector<string> spells = handSpells(b);
            for (size_t i = 0; i < b.hand.size(); ++i) {
                const SpellDef* sp = battleSpell(spells[i]);
                if (!sp) continue;
                const Preview pv = battlePreview(g.player, b, spells[i]);
                const int cost = battleCost(g.player, *sp);
                Value c(json::Object{});
                c.set("uid", str(b.hand[i]));
                c.set("id", str(sp->id));
                c.set("name", str(sp->name));
                c.set("dao", str(sp->dao));
                c.set("kind", str(sp->kind));
                c.set("cost", num(cost));
                c.set("stage", num(sp->stage));
                c.set("stageName", str(realmName(sp->stage)));
                c.set("description", str(battleSpellDescription(*sp)));
                c.set("requiresTarget", Value(battleNeedsTarget(*sp)));
                // 能不能出这张牌，以及不能的原因。
                // 之前手牌永远可点，点了才报错，玩家得一张张试。
                {
                    std::string why;
                    auto cd = b.cooldowns.find(sp->id);
                    if (g.player.stage < sp->stage)
                        why = "境界不足，需达到" + realmName(sp->stage) + "。";
                    else if (cd != b.cooldowns.end() && cd->second > 0)
                        why = "冷却中（还剩 " + std::to_string(cd->second) +
                              " 回合）";
                    else if (b.mana < cost)
                        why = "灵力不足（需 " + std::to_string(cost) +
                              "，有 " + std::to_string(b.mana) + "）";
                    else if (b.played >= 24)
                        why = "本回合出牌已达上限";
                    else if (battleNeedsTarget(*sp)) {
                        const bool hasTarget = std::any_of(b.deck.begin(), b.deck.end(), [&](const Card& card) {
                            return card.spell != sp->id &&
                                std::find(b.exhausted.begin(), b.exhausted.end(), card.uid) == b.exhausted.end();
                        });
                        if (!hasTarget) why = "本场已无其它可移除神通";
                    }
                    c.set("playable", Value(why.empty()));
                    c.set("reason", str(why));
                }
                c.set("damage", num(pv.damage));
                c.set("shield", num(pv.shield));
                c.set("heal", num(pv.heal));
                c.set("qi", num(pv.qi));
                c.set("text", str(pv.text));
                hand.push_back(std::move(c));
            }
            o.set("hand", Value(std::move(hand)));
        }
        {
            json::Array artifacts;
            for (const auto& ability : equippedArtifactAbilities(g.player)) {
                Value a = artifactView(g.player, ability);
                const auto& sp = ability.spell;
                const Preview pv = battlePreview(g.player, b, sp.id);
                const int cd = battleCooldown(b, sp.id), cost = battleCost(g.player, sp);
                const string reason = cd > 0 ? "法宝冷却中，还剩" + std::to_string(cd) + "个回合。" :
                    b.mana < cost ? "灵力不足，需" + std::to_string(cost) + "点。" :
                    b.played >= 24 ? "本回合施术已达上限。" : "";
                a.set("playable", Value(reason.empty())); a.set("reason", str(reason)); a.set("cooldownRemaining", num(cd));
                a.set("damage", num(pv.damage)); a.set("shield", num(pv.shield)); a.set("heal", num(pv.heal));
                a.set("energy", num(pv.energy)); a.set("qi", num(pv.qi)); a.set("text", str(pv.text));
                artifacts.push_back(std::move(a));
            }
            o.set("artifacts", Value(std::move(artifacts)));
        }
        {
            Value cd(json::Object{});
            for (const auto& [k, v] : b.cooldowns)
                if (v > 0) cd.set(k, num(v));
            o.set("cooldowns", std::move(cd));
        }
        // 自身状态：卡牌游戏的核心信息，之前一个都没显示
        o.set("poison", num(b.poison));
        o.set("burn", num(b.burn));
        o.set("playerPoison", num(b.playerPoison));
        o.set("playerPoisonTurns", num(b.playerPoisonTurns));
        o.set("poisonTurns", num(b.poisonTurns));
        o.set("burnTurns", num(b.burnTurns));
        o.set("water", num(b.water));
        o.set("soul", num(b.soul));
        o.set("charge", num(b.charge));
        o.set("swordQi", num(b.swordQi));
        o.set("guard", num(b.guard));
        o.set("reduction", num(b.reduction));
        o.set("buff", num(b.buff));
        o.set("manaBonus", num(b.manaBonus));
        o.set("manaPenalty", num(b.manaPenalty));
        o.set("carryMana", num(b.carryMana));
        o.set("carryCap", num(b.carryCap));
        o.set("drawNext", num(b.drawNext));
        o.set("lock", num(b.lock));
        o.set("enemyWeaken", num(b.enemyWeaken));
        o.set("weakened", num(b.enemyWeaken));
        o.set("weakenedPenalty", num(std::min(0.5, b.enemyWeaken * 0.15)));
        o.set("hpCostPct", num(b.hpCostPct));
        o.set("discardPower", num(b.discardPower));
        o.set("swordSequence", num(b.swordSequence));
        o.set("comboDraws", num(b.comboDraws));
        o.set("lastDao", str(b.last));
        o.set("manaCap", num(battleManaCap(g.player)));
        o.set("nextMana", num(battleNextMana(g.player, b)));
        o.set("baseReduction", num(talentMods(g.player).reduction));
        o.set("effectiveReduction", num(battleReduction(g.player, b)));
        o.set("played", num(b.played));
        o.set("shuffles", num(b.shuffles));
        o.set("style", str(b.style));
        o.set("enemyShield", num(b.enemyShield));
        o.set("mana", num(b.mana));
        o.set("maxMana", num(b.maxMana));
        o.set("draw", num(static_cast<int>(b.draw.size())));
        o.set("discard", num(static_cast<int>(b.discard.size())));
        o.set("exhausted", num(static_cast<int>(b.exhausted.size())));
        {
            std::map<std::string, int> active, removed;
            for (const auto& card : b.deck) {
                if (std::find(b.exhausted.begin(), b.exhausted.end(), card.uid) != b.exhausted.end())
                    ++removed[card.spell];
                else if (std::find(b.hand.begin(), b.hand.end(), card.uid) != b.hand.end() ||
                         std::find(b.draw.begin(), b.draw.end(), card.uid) != b.draw.end() ||
                         std::find(b.discard.begin(), b.discard.end(), card.uid) != b.discard.end())
                    ++active[card.spell];
            }
            auto pileInfo = [&](const std::map<std::string, int>& pile) {
                json::Array arr;
                for (const auto& [id, count] : pile) {
                    const SpellDef* sp = battleSpell(id);
                    if (!sp) continue;
                    Value item(json::Object{});
                    item.set("id", str(id));
                    item.set("name", str(sp->name));
                    item.set("dao", str(sp->dao));
                    item.set("copies", num(count));
                    arr.push_back(std::move(item));
                }
                return Value(std::move(arr));
            };
            o.set("removalOptions", pileInfo(active));
            o.set("exhaustedSpells", pileInfo(removed));
        }
        s.set("battle", std::move(o));
    }

    // ---- 世界 ----
    {
        Value o(json::Object{});
        o.set("herbs", num(g.world.herbs));
        o.set("coreStock", num(g.world.coreStock));
        o.set("pillStock", num(g.world.pillStock));
        o.set("market", num(g.world.market));
        o.set("weather", str(g.world.weather));
        o.set("mineOwner", str(g.world.mineOwner));
        o.set("npcCount", num(static_cast<int>(g.npcs.size())));
        o.set("marketCash", num(g.economy.marketCash));
        s.set("world", std::move(o));
    }

    // ---- 人情 ----
    {
        const Bonds& b = g.bonds;
        Value o(json::Object{});
        o.set("master", str(b.master));
        o.set("partner", str(b.partner));
        o.set("bondDays", num(b.bondDays));
        o.set("lessons", num(b.lessons));
        o.set("notoriety", num(b.notoriety));
        o.set("stipendBalance", num(b.payroll.balance));
        {
            json::Array d;
            for (const auto& x : b.disciples) d.push_back(str(x));
            o.set("disciples", Value(std::move(d)));
        }
        {
            Value st(json::Object{});
            for (const auto& [k, v] : b.stories) st.set(k, num(v));
            o.set("stories", std::move(st));
        }
        s.set("bonds", std::move(o));
    }
    {
        json::Array actions;
        for (const char* kind : {"lesson", "teach", "together", "part", "dismiss"}) {
            const auto info = bondActionInfo(g, kind);
            const auto reason = bondActionError(g, kind);
            Value action(json::Object{});
            action.set("id", str(info.id));
            action.set("name", str(info.name));
            action.set("desc", str(info.desc));
            action.set("days", num(info.days));
            action.set("cost", num(info.cost));
            action.set("enabled", Value(reason.empty()));
            action.set("reason", str(reason));
            actions.push_back(std::move(action));
        }
        s.set("bondActions", Value(std::move(actions)));
    }

    // ---- 游历 ----
    {
        const Adventure& a = g.adventure;
        Value o(json::Object{});
        o.set("since", num(a.since));
        o.set("total", num(a.total));
        o.set("events", num(a.events));
        o.set("wins", num(a.wins));
        s.set("adventure", std::move(o));
    }

    // ---- 纪事 ----
    {
        json::Array arr;
        const int n = std::min<int>(60, static_cast<int>(g.logs.size()));
        for (int i = static_cast<int>(g.logs.size()) - n;
             i < static_cast<int>(g.logs.size()); ++i) {
            const LogEntry& e = g.logs[static_cast<size_t>(i)];
            Value o(json::Object{});
            o.set("id", num(e.id));
            o.set("year", num(e.year));
            o.set("age", num(e.age));
            o.set("days", num(e.days.value_or(e.year * 360)));
            o.set("category", str(e.category));
            o.set("text", str(e.text));
            o.set("important", Value(e.important));
            o.set("delta", str(e.delta));
            arr.push_back(std::move(o));
        }
        // 反转，最新在前
        std::reverse(arr.begin(), arr.end());
        s.set("logs", Value(std::move(arr)));
    }

    // ---- 地点（舆图用）----
    {
        json::Array arr;
        for (const auto& l : LOCATIONS()) {
            Value o(json::Object{});
            o.set("id", str(l.id));
            o.set("name", str(l.name));
            o.set("type", str(l.type));
            o.set("x", num(l.x));
            o.set("y", num(l.y));
            o.set("desc", str(l.description));
            o.set("danger", num(l.danger));
            o.set("here", Value(g.player.location == l.id));
            {
                json::Array services;
                for (const auto& item : locationServices(g, l.id)) {
                    Value service(json::Object{}); service.set("id", str(item.id));
                    service.set("name", str(item.name)); service.set("desc", str(item.desc));
                    service.set("days", num(item.days)); service.set("cost", num(item.cost));
                    service.set("enabled", Value(item.enabled)); service.set("reason", str(item.reason));
                    service.set("targetTab", str(item.targetTab));
                    services.push_back(std::move(service));
                }
                o.set("services", Value(std::move(services)));
            }
            const TravelPlan plan = travelPlan(g, l.id);
            Value route(json::Object{});
            route.set("from", str(plan.from));
            route.set("to", str(plan.to));
            route.set("distance", num(plan.distance));
            route.set("escapeSpeed", num(plan.escapeSpeed));
            json::Array modes;
            for (const auto& option : plan.options) {
                Value mode(json::Object{});
                mode.set("id", str(option.id));
                mode.set("name", str(option.name));
                mode.set("days", num(option.days));
                mode.set("speed", num(option.speed));
                mode.set("ambushChance", num(option.ambushChance));
                mode.set("cost", num(option.cost));
                mode.set("enabled", Value(option.enabled));
                mode.set("reason", str(option.reason));
                mode.set("desc", str(option.desc));
                modes.push_back(std::move(mode));
            }
            route.set("options", Value(std::move(modes)));
            o.set("travel", std::move(route));
            arr.push_back(std::move(o));
        }
        s.set("locations", Value(std::move(arr)));
    }

    // ---- 可玩内容清单（供界面列出）----
    {
        json::Array arr;
        for (const auto& st : data::StoryDef_TABLE()) {
            const std::string err = storyError(g, &st);
            Value o(json::Object{});
            o.set("id", str(st.id));
            o.set("name", str(st.name));
            o.set("desc", str(st.desc));
            o.set("days", num(st.days));
            o.set("hidden", Value(st.hidden != 0));
            o.set("available", Value(err.empty()));
            o.set("reason", str(err));
            // 正文与两个选项：让前端能真正把故事读给玩家，
            // 而不是只显示一句简介
            o.set("location", str(st.location));
            o.set("opening", str(st.opening));
            o.set("opt1Name", str(st.opt1Name));
            o.set("opt1Effect", str(st.opt1Effect));
            const std::string firstError = err.empty() ? storyChoiceError(g, st, 0) : err;
            const std::string secondError = err.empty() ? storyChoiceError(g, st, 1) : err;
            o.set("opt1Enabled", Value(firstError.empty())); o.set("opt1Reason", str(firstError));
            o.set("opt2Enabled", Value(secondError.empty())); o.set("opt2Reason", str(secondError));
            o.set("opt2Name", str(st.opt2Name));
            o.set("opt2Effect", str(st.opt2Effect));
            o.set("done", Value(g.bonds.stories.count(st.id) > 0));
            arr.push_back(std::move(o));
        }
        s.set("stories", Value(std::move(arr)));
    }
    {
        json::Array arr;
        for (const auto& t : data::WildTaskDef_TABLE()) {
            Value o(json::Object{});
            o.set("id", str(t.id));
            o.set("name", str(t.name));
            o.set("desc", str(t.desc));
            o.set("days", num(t.days));
            o.set("fee", num(t.fee + (g.player.stage / 3) * 25));
            const std::string reason = wildActionError(g, t.id);
            o.set("enabled", Value(reason.empty()));
            o.set("reason", str(reason));
            arr.push_back(std::move(o));
        }
        s.set("wildTasks", Value(std::move(arr)));
    }
    {
        json::Array arr;
        for (const auto& p : PLANS()) {
            Value o(json::Object{});
            o.set("id", str(p.id));
            o.set("name", str(p.name));
            o.set("icon", str(p.icon));
            o.set("desc", str(p.desc));
            const Player& player = g.player;
            const std::string id = p.id;
            std::string preview;
            if (id == "cultivate") {
                const double mult = (player.sect.empty() ? 1.0 : 1.16) *
                    (player.health < 50 ? 0.6 : 1.0) *
                    (player.sect.empty() && hasTalent(player, "solitary") ? 1.08 : 1.0);
                const int bill = jsRound(cultivationCost(player) / 12.0) +
                    cultivate::supplyCost(player, "stone", player.stoneSupply) +
                    (cultivate::qiActive(player) ? cultivate::supplyCost(player, "qi", player.qiSupply) : 0);
                preview = "30 天：约修为 +" + std::to_string(jsRound(cultivate::rate(player) * mult / 12.0)) +
                    " · 灵石投入 " + std::to_string(bill) + "（不足时收益降低）";
            } else if (id == "work") {
                const double mul = g.world.market * (hasTalent(player, "merchant") ? 1.18 : 1.0);
                preview = "30 天：灵石约 +" + std::to_string(jsRound(25 * mul)) +
                    "～" + std::to_string(jsRound(37 * mul));
            } else if (id == "gather") {
                const LocationDef* here = findLoc(player.location);
                const int bonus = (here && std::string(here->type) == "wild" ? 2 : 0) +
                    (player.sect == "zhushan" ? 2 : 0) + (artHas(player.arts, "丹", 1) ? 1 : 0);
                preview = "30 天：药材约 +" + std::to_string(std::min(g.world.herbs, 2 + bonus)) +
                    "～" + std::to_string(std::min(g.world.herbs, 4 + bonus)) + " · 险地可能受伤";
            } else if (id == "rest") {
                preview = "30 天：气血最多 +" + std::to_string(std::min(100 - player.health,
                    20 + (hasTalent(player, "body") ? 8 : 0))) + " · 积累少量修为";
            } else if (id == "explore") {
                preview = "30 天：灵石约 +8～25 · 积累见闻与修为 · 可能遇事或斗法";
            }
            o.set("preview", str(preview));
            arr.push_back(std::move(o));
        }
        s.set("plans", Value(std::move(arr)));
    }

    // ---- 突破信息 ----
    {
        const BreakthroughInfo b = breakthroughInfo(g);
        Value o(json::Object{});
        o.set("next", str(b.next));
        o.set("chance", num(b.chance));
        o.set("cost", num(b.cost));
        o.set("ready", Value(b.ready));
        o.set("major", Value(b.major));
        s.set("breakthrough", std::move(o));
    }

    // ---- 附近的人（带好感度，供 NPC 面板直接用）----
    {
        json::Array arr;
        for (const auto* n : play::nearbyPeople(g)) {
            Value o(json::Object{});
            o.set("id", str(n->id));
            o.set("name", str(n->name));
            o.set("gender", str(n->gender));
            o.set("age", num(n->age));
            o.set("stage", num(n->stage));
            o.set("realmName", str(realmName(n->stage)));
            o.set("sect", str(n->sect));
            o.set("sectName", n->sect.empty() ? str("")
                            : str(findSect(n->sect) ? findSect(n->sect)->name : ""));
            o.set("personality", str(n->personality));
            o.set("goal", str(n->goal));
            o.set("sword", num(n->sword));
            o.set("portrait", str(n->beast ? beastPortrait(n->stage) : personPortrait(n->gender, n->sect)));
            o.set("dao", str(n->dao));
            {
                const Arts loadout = artNpcLoadout(*n);
                Value lo(json::Object{}); json::Array spells, manuals, nodes;
                for (const auto& id : loadout.spells) { const auto* sp = battleSpell(id); Value v(json::Object{}); v.set("id", str(id)); v.set("name", str(sp ? sp->name : id)); spells.push_back(std::move(v)); }
                for (const auto& [id, level] : loadout.manuals) { ManualInfo m; findManualInfo(id, m); Value v(json::Object{}); v.set("id", str(id)); v.set("name", str(m.found ? m.name : id)); v.set("level", num(level)); v.set("main", Value(id == loadout.main)); manuals.push_back(std::move(v)); }
                for (const auto& id : loadout.nodes) { const auto* node = findNode(id); Value v(json::Object{}); v.set("id", str(id)); v.set("name", str(node ? node->name : id)); nodes.push_back(std::move(v)); }
                lo.set("main", str(loadout.main)); lo.set("spells", Value(std::move(spells))); lo.set("manuals", Value(std::move(manuals))); lo.set("nodes", Value(std::move(nodes)));
                o.set("loadout", std::move(lo));
            }
            {
                json::Array lessons;
                for (const auto& item : npcLessons(g, n->id)) {
                    Value lesson(json::Object{}); lesson.set("kind", str(item.kind)); lesson.set("id", str(item.id)); lesson.set("name", str(item.name)); lesson.set("dao", str(item.dao)); lesson.set("stage", num(item.stage)); lesson.set("stageName", str(realmName(item.stage))); lesson.set("days", num(item.days)); lesson.set("creditCost", num(item.creditCost)); lesson.set("stoneCost", num(item.stoneCost)); lesson.set("affinityNeed", num(item.affinityNeed)); lesson.set("learned", Value(item.learned)); lesson.set("enabled", Value(item.enabled)); lesson.set("reason", str(item.reason)); lessons.push_back(std::move(lesson));
                }
                o.set("learning", Value(std::move(lessons)));
            }
            o.set("apt", num(n->apt));
            o.set("insight", num(n->insight));
            o.set("relation", num(relationOf(g, n->id)));
            // 道友往来：交情与「是否打过招呼」
            {
                const int cr = friendship::creditOf(g, n->id);
                o.set("credit", num(cr));
                o.set("creditLabel", str(friendship::creditLabel(cr)));
                auto bit = g.player.npcBonds.find(n->id);
                o.set("greeted",
                      Value(bit != g.player.npcBonds.end() && bit->second.greeted));
            }
            o.set("relationLabel", str(play::relationLabel(relationOf(g, n->id))));
            o.set("isMaster", Value(g.bonds.master == n->id));
            o.set("isPartner", Value(g.bonds.partner == n->id));
            o.set("isDisciple",
                  Value(std::find(g.bonds.disciples.begin(),
                                  g.bonds.disciples.end(),
                                  n->id) != g.bonds.disciples.end()));
            arr.push_back(std::move(o));
        }
        s.set("nearby", Value(std::move(arr)));
    }

    // ---- 坊市（只在城池有货）----
    {
        json::Array arr;
        for (const auto& m : play::marketList(g)) {
            Value o(json::Object{});
            o.set("kind", str(m.kind));
            o.set("id", str(m.id));
            o.set("name", str(m.name));
            o.set("desc", str(m.desc));
            o.set("price", num(m.price));
            o.set("sellPrice", num(m.sellPrice));
            o.set("owned", num(m.owned));
            arr.push_back(std::move(o));
        }
        for (const auto& m : play::gearList(g)) {
            Value o(json::Object{});
            o.set("kind", str(m.kind));
            o.set("id", str(m.id));
            o.set("name", str(m.name));
            o.set("desc", str(m.desc));
            o.set("price", num(m.price));
            o.set("sellPrice", num(m.sellPrice));
            o.set("owned", num(m.owned));
            arr.push_back(std::move(o));
        }
        s.set("market", Value(std::move(arr)));
        s.set("inCity", Value(ecoIsCity(g.player.location)));
    }

    // ---- 炼丹选项（带成丹率和缺料原因）----
    {
        json::Array arr;
        for (const auto& c : play::craftOptions(g, "brew")) {
            Value o(json::Object{});
            o.set("id", str(c.id));
            o.set("name", str(c.name));
            o.set("desc", str(c.desc));
            o.set("chance", num(c.chance));
            o.set("output", num(c.output));
            o.set("stoneCost", num(c.stoneCost));
            o.set("days", num(c.days));
            o.set("enabled", Value(c.enabled));
            o.set("reason", str(c.reason));
            arr.push_back(std::move(o));
        }
        s.set("brewOptions", Value(std::move(arr)));
    }

    // ---- 拥有的法器 ----
    {
        json::Array arr;
        for (const auto& gr : g.player.arts.gear) {
            Value o(json::Object{});
            o.set("id", str(gr.id));
            o.set("name", str(gr.name));
            o.set("slot", str(gr.slot));
            o.set("grade", num(gr.grade));
            o.set("bonus", num(gr.bonus));
            o.set("price", num(gr.price));
            o.set("element", str(gr.element)); o.set("polarity", str(gr.polarity));
            o.set("crafted", Value(gr.crafted)); o.set("shape", str(artifactShape(gr)));
            o.set("inscribed", Value(gr.inscribed)); o.set("passive", artifactPassiveView(g.player, gr));
            const ArtifactAbility ability = artifactAbility(gr);
            o.set("shapeName", str(ability.shapeName)); o.set("ability", artifactView(g.player, ability));
            auto it = g.player.arts.equipped.find(gr.slot);
            o.set("equipped",
                  Value(it != g.player.arts.equipped.end() && it->second == gr.id));
            arr.push_back(std::move(o));
        }
        s.set("gears", Value(std::move(arr)));
    }

    // ---- 丹毒明细（让玩家看得见）----
    {
        const play::PoisonInfo pi = play::poisonInfo(g);
        Value o(json::Object{});
        o.set("current", num(pi.current));
        o.set("threshold", num(pi.threshold));
        o.set("cultPenalty", num(pi.cultPenalty));
        o.set("detoxPerYear", num(pi.detoxPerYear));
        o.set("daysToClear", num(pi.daysToClear));
        o.set("affecting", Value(pi.affecting));
        o.set("summary", str(pi.summary));
        s.set("poison", std::move(o));
    }

    // ---- 通讯录统计 ----
    {
        int known = 0, here = 0;
        for (const auto& n : g.npcs) {
            if (!n.alive) continue;
            if (n.location == g.player.location) ++here;
            const bool isBond = (g.bonds.master == n.id) ||
                                (g.bonds.partner == n.id) ||
                                std::find(g.bonds.disciples.begin(),
                                          g.bonds.disciples.end(),
                                          n.id) != g.bonds.disciples.end();
            if (relationOf(g, n.id) != 0 || isBond) ++known;
        }
        Value o(json::Object{});
        o.set("known", num(known));
        o.set("here", num(here));
        s.set("rosterStat", std::move(o));
    }

    // ---- 坊市与洞府的额外信息 ----
    s.set("contribution", num(g.economy.contribution));
    // 身份与恶名：宗门页常显，缺了会显示成「—」和 0
    s.set("rank", str(socRank(g.player)));
    s.set("notoriety", num(g.bonds.notoriety));

    s.set("notorietyNote", str(conflict::notorietyNote(g)));
    // ---- 声望：档位 + 实际加成，让玩家看得见它有什么用 ----
    {
        const int rn = g.player.renown;
        Value o(json::Object{});
        o.set("value", num(rn));
        o.set("tier", str(renown::tierOf(rn).name));
        o.set("tierDesc", str(renown::tierOf(rn).desc));
        o.set("describe", str(renown::describe(rn)));
        o.set("toNext", num(renown::toNextTier(rn)));
        o.set("progress", num(renown::tierProgress(rn)));
        o.set("buyDiscount", num(renown::buyDiscount(rn)));
        o.set("sellBonus", num(renown::sellBonus(rn)));
        o.set("sellLimit", num(renown::sellLimit(rn)));
        o.set("marketCashCap", num(renown::marketCashCap(rn)));
        o.set("firstImpression", num(renown::firstImpression(rn)));
        o.set("libraryDiscount", num(renown::libraryDiscount(rn)));
        o.set("stipendBonus", num(renown::stipendBonus(rn)));
        o.set("masterRequirement", num(renown::masterRequirement()));
        {
            json::Array arr;
            for (const auto& x : renown::tiers()) {
                Value to(json::Object{});
                to.set("id", str(x.id));
                to.set("name", str(x.name));
                to.set("desc", str(x.desc));
                to.set("at", num(x.at));
                to.set("reached", Value(rn >= x.at));
                arr.push_back(std::move(to));
            }
            o.set("tiers", Value(std::move(arr)));
        }
        {
            json::Array arr;
            for (const auto& source : renown::sources(g)) {
                Value v(json::Object{});
                v.set("id", str(source.id)); v.set("name", str(source.name)); v.set("gainText", str(source.gainText));
                json::Array conditions; for (const auto& condition : source.conditions) conditions.push_back(str(condition));
                v.set("conditions", Value(std::move(conditions))); v.set("limit", str(source.limit));
                v.set("enabled", Value(source.enabled)); v.set("reason", str(source.reason));
                Value entry(json::Object{});
                entry.set("tab", str(source.entry.tab)); entry.set("sub", str(source.entry.sub));
                entry.set("location", str(source.entry.location)); entry.set("label", str(source.entry.label));
                v.set("entry", std::move(entry)); arr.push_back(std::move(v));
            }
            o.set("sources", Value(std::move(arr)));
        }
        s.set("renownInfo", std::move(o));
    }
    s.set("marketCash", num(g.economy.marketCash));
    s.set("coreBuyPrice", num(shop::corePrice(g, true)));
    s.set("pillBuyPrice", num(shop::pillPrice(g, true)));
    {
        json::Array arr;
        for (const auto* f : shop::buyableFurnaces(g)) {
            Value o(json::Object{});
            o.set("id", str(f->id));
            o.set("name", str(f->name));
            o.set("grade", num(f->grade));
            o.set("capacity", num(f->capacity));
            o.set("price", num(f->price > 0 ? f->price : 100 * f->grade * f->grade));
            o.set("current", Value(g.player.arts.furnace == f->id));
            arr.push_back(std::move(o));
        }
        s.set("furnaces", Value(std::move(arr)));
    }
    {
        json::Array arr;
        for (const auto& def : data::BaseManualDef_TABLE()) {
            ManualInfo info;
            if (!findManualInfo(def.id, info)) continue;
            Value item(json::Object{});
            item.set("id", str(info.id)); item.set("name", str(info.name)); item.set("dao", str(info.dao));
            item.set("stage", num(info.stage)); item.set("stageName", str(realmName(info.stage)));
            item.set("effect", str(info.effect));
            arr.push_back(std::move(item));
        }
        s.set("manualCatalog", Value(std::move(arr)));
    }
    {
        json::Array arr;
        for (const auto& ms : shop::availableManuals(g)) {
            Value o(json::Object{});
            o.set("id", str(ms.id));
            o.set("name", str(ms.name));
            o.set("city", str(ms.city));
            o.set("sect", str(ms.sect));
            o.set("price", num(ms.price));
            o.set("points", num(ms.points));
            o.set("stage", num(ms.stage));
            o.set("how", str(ms.how));
            const Arts& arts = g.player.arts;
            const bool owned = arts.manuals.count(ms.id) > 0 ||
                std::find(arts.bookList.begin(), arts.bookList.end(), ms.id) != arts.bookList.end() ||
                std::find(g.economy.books.begin(), g.economy.books.end(), ms.id) != g.economy.books.end();
            o.set("owned", Value(owned));
            const std::string learnReason = ecoActionError(g, "learn", ms.id);
            const EcoActionInfo learn = ecoActionInfo(g, "learn", ms.id);
            o.set("canLearn", Value(learnReason.empty() && !g.ended && !g.pending && !g.job && !arts.battle));
            o.set("learnReason", str(learnReason));
            o.set("learnDays", num(learn.days));
            o.set("learnCost", num(learn.cost));
            arr.push_back(std::move(o));
        }
        s.set("manualSources", Value(std::move(arr)));
    }
    {
        json::Array arr;
        for (const auto& spell : shop::availableLibrarySpells(g)) {
            Value item(json::Object{});
            item.set("id",str(spell.id));
            item.set("name",str(spell.name));
            item.set("dao",str(spell.dao));
            item.set("desc",str(spell.desc));
            item.set("sect",str(spell.sect));
            item.set("stage",num(spell.stage));
            item.set("stageName",str(realmName(spell.stage)));
            item.set("points",num(spell.points));
            item.set("known",Value(spell.known));
            item.set("enabled",Value(spell.enabled));
            item.set("reason",str(spell.reason));
            arr.push_back(std::move(item));
        }
        s.set("librarySpells",Value(std::move(arr)));
    }

    s.set("ended", Value(g.ended));

    // 结局详情：前端要显示「因何落笔、最终境界、评分」，
    // 之前只下发了一个布尔值，结算界面等于没有。
    if (g.ending.has_value()) {
        const Ending& e = *g.ending;
        Value o(json::Object{});
        o.set("cause", str(e.cause));
        const auto archive=legacyLoad();
        const bool claimed=std::find(archive.claimed.begin(),archive.claimed.end(),g.lifeId)!=archive.claimed.end();
        o.set("canRestore",Value(e.cause!="寿终正寝" && g.checkpoint.enabled && !claimed));
        o.set("year", num(e.year));
        o.set("age", num(e.age));
        o.set("realm", str(e.realm));
        o.set("friends", num(e.friends));
        o.set("score", num(e.score));
        s.set("ending", std::move(o));
    }
    s.set("version", num(g.version));
    return s;
}

// 统一响应：带上完整状态
Value okWithState(Game& g) {
    checkpointTick(g);
    // 月历可能在一次长行动中创建自动快照；请求返回时将其落入独立槽位。
    string& lastAutomatic = cur().lastAutomatic;
    string& writeError = cur().writeError;
    if (g.checkpoint.enabled && g.checkpoint.source == "auto" &&
        !g.checkpoint.snapshot.empty() && lastAutomatic != g.checkpoint.snapshot) {
        Game automatic;
        string error;
        if (loadFromJson(g.checkpoint.snapshot, automatic, &error)) {
            automatic.checkpoint = g.checkpoint;
            if (saveToFile(automatic, savePathFor(4), &error)) {
                lastAutomatic = g.checkpoint.snapshot;
                writeError.clear();
            } else writeError = "自动存档写入失败：" + error;
        } else writeError = "自动存档校验失败：" + error;
    }
    Value o(json::Object{});
    o.set("ok", Value(true));
    o.set("state", buildState(g));
    if (!writeError.empty()) o.set("checkpointWarning", str(writeError));
    return o;
}

// ------------------------------------------------------------
// 推进时间（前端点「推进行动」）
// ------------------------------------------------------------
// 记录推进过程中发生的事，返回给前端做「这段时间发生了什么」的提示
struct AdvanceReport {
    int daysPassed = 0;
    std::string stopReason;      // event / battle / ended / done
    std::vector<std::string> highlights;
};

// 推进。autoStop 为 true 时会连续跨月，直到发生事件、进入斗法、或到达目标天数。
// 这是为了解决「悟道炼丹过月反复点击」的坏体验：
// 玩家点一次，就该一直走到有事发生为止。
AdvanceReport advanceEx(Game& g, int days, bool autoStop) {
    AdvanceReport rep;
    const int startDays = g.clock.days;
    const size_t startLogs = g.logs.size();

    int guard = 0;
    while (g.clock.days - startDays < days && guard++ < 200) {
        if (g.ended) { rep.stopReason = "ended"; break; }
        if (g.pending.has_value()) { rep.stopReason = "event"; break; }
        if (g.player.arts.battle.has_value()) { rep.stopReason = "battle"; break; }

        if (!g.job.has_value()) {
            Job j;
            j.actionType = "plan";
            j.plan = g.plan;
            j.name = findPlan(g.plan) ? findPlan(g.plan)->name : "计划";
            // 一次安排不超过剩余目标天数，跨月由循环自然接续
            const int remain = days - (g.clock.days - startDays);
            const int chunk = std::max(1, std::min(remain, calendarDate(g).left));
            j.total = chunk;
            j.remaining = chunk;
            j.started = g.clock.days;
            const double yearRatio = chunk / 360.0;
            const int cost = (g.plan == "cultivate")
                                 ? calendarTake(g, "cultivationCost", cultivationCost(g.player) * yearRatio)
                                 : 0;
            j.cost = cost;
            const int reserve = g.plan == "cultivate" && g.clock.carry["cultivationCost"] > 1e-9 ? 1 : 0;
            j.paid = g.player.stones >= cost + reserve;
            if (j.paid) g.player.stones -= cost;
            g.job = j;
        }

        const CalRunResult r = calendarRun(g, days - (g.clock.days - startDays));
        if (!r.ok) { rep.stopReason = "error"; break; }
        if (r.restored) { rep.stopReason = "restored"; break; }
        if (r.ended) { rep.stopReason = "ended"; break; }
        if (r.pending) { rep.stopReason = "event"; break; }
        if (g.player.arts.battle.has_value()) { rep.stopReason = "battle"; break; }
        if (!autoStop) break;   // 单步模式：跑完当前这段就返回
    }

    rep.daysPassed = std::max(0, g.clock.days - startDays);
    if (rep.stopReason.empty()) rep.stopReason = "done";

    // 收集这段时间里值得给玩家看的事
    for (size_t i = startLogs; i < g.logs.size(); ++i) {
        const LogEntry& e = g.logs[i];
        if (!e.important) continue;
        rep.highlights.push_back(e.text);
    }
    return rep;
}

void advance(Game& g, int days) {
    if (!g.job.has_value()) {

        Job j;
        j.actionType = "plan";
        j.plan = g.plan;
        j.name = findPlan(g.plan) ? findPlan(g.plan)->name : "计划";
        j.total = days;
        j.remaining = days;
        j.started = g.clock.days;
        const int cost = (g.plan == "cultivate")
                             ? calendarTake(g, "cultivationCost", cultivationCost(g.player) * days / 360.0)
                             : 0;
        j.cost = cost;
        const int reserve = g.plan == "cultivate" && g.clock.carry["cultivationCost"] > 1e-9 ? 1 : 0;
        j.paid = g.player.stones >= cost + reserve;
        if (j.paid) g.player.stones -= cost;
        g.job = j;
    }
    int guard = 0;
    while (g.job.has_value() && guard++ < 400) {
        const CalRunResult r = calendarRun(g);
        if (!r.ok || r.restored || r.ended || r.pending || g.player.arts.battle.has_value())
            break;
    }
}

} // namespace

// ------------------------------------------------------------
// 路由
// ------------------------------------------------------------
http::Response handle(const http::Request& req) {
    Game& g = game();
    const std::string& path = req.path;
    Value in(json::Object{});
    if (!req.body.empty()) {
        std::string err;
        in = Value::parse(req.body, &err);
        if (!err.empty() || !in.isObject())
            return jsonError("请求正文必须是有效的 JSON 对象。");
    }

    const bool readOnly = readOnlyRoute(path);
    if (req.method != "POST" && !(req.method == "GET" && readOnly))
        return jsonError("此操作需要 POST 请求。", 405);
    // 数值先验范围，避免浮点转整数溢出或非法参数悄悄执行默认操作。
    for (const char* key : {"days", "slot", "count", "choice", "level", "roll", "batch"}) {
        const Value& x = in[key];
        if (!x.isNull() && (!x.isNumber() || !std::isfinite(x.asNumber()) ||
            std::floor(x.asNumber()) != x.asNumber() ||
            x.asNumber() < std::numeric_limits<int>::min() ||
            x.asNumber() > std::numeric_limits<int>::max()))
            return jsonError(std::string("参数 ") + key + " 必须是有效整数。");
    }
    if (!in["seed"].isNull() && (!in["seed"].isNumber() ||
        !std::isfinite(in["seed"].asNumber()) ||
        std::floor(in["seed"].asNumber()) != in["seed"].asNumber() ||
        in["seed"].asNumber() < 0 || in["seed"].asNumber() > 4294967295.0))
        return jsonError("种子必须是 0 到 4294967295 之间的整数。");
    if (in["allocation"].isObject()) {
        for (const auto& [key, value] : in["allocation"].asObject())
            if (!value.isNumber() || !std::isfinite(value.asNumber()) ||
                std::floor(value.asNumber()) != value.asNumber() ||
                value.asNumber() < 0 || value.asNumber() > MAX_POINTS)
                return jsonError("属性分配必须是有效的非负整数。");
    }
    const bool replaceGame = path == "/api/new" || path == "/api/import" || path == "/api/load";
    if (!readOnly && !replaceGame) {
        if (!started()) return jsonError("请先创建角色或读取存档。");
        const bool persist = path == "/api/save" || path == "/api/legacy/settle" || path == "/api/checkpoint/restore";
        if (!persist) {
            if (g.dungeons.run && path.rfind("/api/dungeon/",0) != 0 && path != "/api/battle/turn")
                return jsonError("请先结束当前秘境探索。");
            if (g.ended) return jsonError("这一世已经结束，请查看结局或开始新的一世。");
            if (g.player.arts.battle.has_value() && path != "/api/battle/turn")
                return jsonError("请先完成当前斗法。");
            if (g.pending.has_value() && path != "/api/choose" && path != "/api/battle/turn")
                return jsonError("请先处理当前抉择。");
            if (g.job.has_value() && path != "/api/advance" && path != "/api/cancel-job" &&
                path != "/api/plan" && path != "/api/choose" && path != "/api/battle/turn")
                return jsonError("已有进行中的安排，请继续推进或先中止安排。");
        }
    }
    if (path == "/api/export" && !started())
        return jsonError("当前没有可导出的角色存档。");

    // ---- 新游戏 ----
    if (path == "/api/new") {
        CreateOptions opts;
        opts.name = strOf(in, "name", "沈知微");
        opts.gender = strOf(in, "gender", "女");
        opts.seed = seedOf(in);

        // 天赋：支持多选（traits 数组）；兼容旧的单个 talent 字段
        if (in["traits"].isArray()) {
            for (const auto& x : in["traits"].asArray())
                if (x.isString()) opts.traits.push_back(x.asString());
            // 明确给了数组（哪怕是空的）就按它来，不再补默认天赋
            opts.traitsExplicit = true;
        } else {
            opts.traits.push_back(strOf(in, "talent", "sword"));
        }

        // 属性点分配
        if (in["allocation"].isObject())
            for (const auto& [k, v] : in["allocation"].asObject())
                if (v.isNumber()) opts.allocation[k] = v.asInt();

        // 建号方案非法时给出具体原因，而不是直接把角色建坏
        const int budget = legacy::talentBudget(legacyLoad());
        const play::BuildCheck c = play::checkBuild(opts.traits, opts.allocation, budget);
        if (!c.ok) return jsonError(c.error.empty() ? "建号方案不合法。" : c.error);
        opts.budget = budget;

        g = createGame(opts);
        checkpointEnable(g);
        started() = true;
        return jsonOk(okWithState(g));
    }

    // ---- 查询状态 ----
    if (path == "/api/state") {
        // 还没开过局：明确告诉前端「没有存档」，
        // 不要把一个空壳默认值当成存档发过去 —— 前端会照着渲染然后崩掉。
        if (!started()) {
            Value o(json::Object{});
            o.set("ok", Value(true));
            o.set("hasGame", Value(false));
            return jsonOk(o);
        }
        Value o = okWithState(g);
        o.set("hasGame", Value(true));
        return jsonOk(o);
    }

    // ---- 存档 ----
    if (path == "/api/checkpoint/restore") {
        if (!g.ended || !g.ending || g.ending->cause=="寿终正寝")
            return jsonError("只有意外死亡且尚未接受结局时，才能选择回到最近存档。");
        const auto archive=legacyLoad();
        if (std::find(archive.claimed.begin(),archive.claimed.end(),g.lifeId)!=archive.claimed.end())
            return jsonError("已接受本世结局并结算传承，不能再恢复这一世。");
        const string reason=g.ending->cause;
        if (!checkpointRestore(g,reason)) return jsonError("最近存档不存在或损坏，无法恢复。");
        return jsonOk(okWithState(g));
    }
    if (path == "/api/save") {
        std::string err;
        const int slot = intOf(in, "slot", 1);
        if (slot < 1 || slot > 3) return jsonError("存档位需为 1 到 3。");
        const auto previous = g.checkpoint;
        checkpointManual(g,slot);
        if (!saveToFile(g, savePathFor(slot), &err)) {
            g.checkpoint = previous;
            return jsonError("保存失败：" + err, 500);
        }
        return jsonOk(okWithState(g));
    }
    // ---- 导出存档为 JSON 文本 ----
    // 对应 JS app.mjs 的 exportSave：{format, version, game, legacy}
    // 玩家可以把这一世存成文件带走、备份、或改完再导回来。
    if (path == "/api/export") {
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("format", str("ningzhou-dream"));
        o.set("version", num(VERSION));
        // game 用字符串塞进去，避免前端重新序列化时改变数值
        o.set("game", str(saveToJson(g)));
        o.set("legacy", str(legacy::toJson(legacyLoad())));
        o.set("name", str(g.player.name));
        o.set("year", num(g.year));
        o.set("realm", str(realmOf(g.player.stage).name));
        return jsonOk(o);
    }

    // ---- 从 JSON 文本导入存档 ----
    if (path == "/api/import") {
        // 接受两种输入：整包 {game:"...", legacy:"..."}，或直接给 game 的 JSON
        std::string gameText = strOf(in, "game");
        if (gameText.empty() && in["save"].isString())
            gameText = in["save"].asString();
        if (gameText.empty() && in["text"].isString())
            gameText = in["text"].asString();
        if (gameText.empty())
            return jsonError("没有读到存档内容。");

        Game loaded;
        std::string err;
        if (!loadFromJson(gameText, loaded, &err))
            return jsonError("存档解析失败：" + err);
        // 传承档案可选：带上了就一并导入
        const std::string legText = strOf(in, "legacy");
        if (!legText.empty()) {
            bool okLegacy = false;
            legacy::Archive a = legacy::fromJson(legText, &okLegacy);
            if (!okLegacy) return jsonError("传承档案格式或数值无效。");
            if (!legacySave(a)) return jsonError("传承档案写入失败，当前角色未被替换。", 500);
        }
        g = std::move(loaded);
        checkpointEnable(g);
        started() = true;
        return jsonOk(okWithState(g));
    }

    if (path == "/api/load") {

        const int slot = intOf(in, "slot", 1);
        if (slot < 1 || slot > 4) return jsonError("读取存档位需为 1 到 4（4 为自动存档）。");
        Game loaded;
        std::string err;
        if (!loadFromFile(savePathFor(slot), loaded, &err))
            return jsonError("读取失败：" + err, 404);
        g = loaded;
        checkpointEnable(g);
        started() = true;
        return jsonOk(okWithState(g));
    }
    if (path == "/api/slots") {
        Value o(json::Object{});
        json::Array arr;
        for (const auto& s : listSaveSlots()) {
            Value so(json::Object{});
            so.set("slot", num(s.slot));
            so.set("automatic", Value(s.automatic));
            so.set("exists", Value(s.exists));
            so.set("name", str(s.name));
            so.set("realm", str(s.realm));
            so.set("year", num(s.year));
            so.set("age", num(s.age));
            arr.push_back(std::move(so));
        }
        o.set("ok", Value(true));
        o.set("slots", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 推进 ----
    if (path == "/api/advance") {
        const int days = intOf(in, "days", calendarDate(g).left);
        if (days < 1 || days > 3600) return jsonError("每次推进天数需为 1 到 3600 天。");
        // autoStop 默认开启：一直走到有事发生为止，不用玩家反复点
        const bool autoStop = in["autoStop"].isBool() ? in["autoStop"].asBool() : true;
        const AdvanceReport rep = advanceEx(g, days, autoStop);
        Value o = okWithState(g);
        Value ro(json::Object{});
        ro.set("daysPassed", num(rep.daysPassed));
        ro.set("stopReason", str(rep.stopReason));
        json::Array hl;
        for (const auto& h : rep.highlights) hl.push_back(str(h));
        ro.set("highlights", Value(std::move(hl)));
        o.set("report", std::move(ro));
        return jsonOk(o);
    }
    if (path == "/api/plan") {
        const std::string plan = strOf(in, "plan", g.plan);
        if (!findPlan(plan)) return jsonError("没有这种修行计划。");
        g.plan = plan;
        return jsonOk(okWithState(g));
    }

    // ---- 突破 ----
    if (path == "/api/breakthrough") {
        const BreakthroughInfo info = breakthroughInfo(g);
        if (!info.ready) return jsonError("修为尚未圆满。");
        if (g.player.stones < info.cost) return jsonError("灵石不足。");
        // 重伤不许强冲（对应 JS ordinaryActionError）
        if (g.player.health < 35)
            return jsonError("伤势过重，请先休养至 35 气血。");
        g.player.stones -= info.cost;
        // 大境界要消耗凝元丹与灵核各一枚。
        // 之前不扣，但成功率公式仍然吃 +18% / +10% 的加成，
        // 等于每次突破白拿 28% —— 核心成长曲线会被破坏。
        if (info.major) {
            if (g.player.pills > 0) g.player.pills--;
            if (g.player.cores > 0) g.player.cores--;
            g.player.arts.breakBuff.clear();
        }
        Job j;
        j.actionType = "breakthrough";
        j.name = "闭关叩关";
        j.total = info.major ? 90 : 30;
        j.remaining = j.total;
        j.started = g.clock.days;
        j.cost = info.cost;
        j.paid = true;
        j.breakthroughPrepared = true;
        j.breakthroughMajor = info.major;
        j.breakthroughChance = info.chance;
        j.breakthroughFailureLoss = info.failureLoss;
        g.job = j;
        advance(g, j.total);
        return jsonOk(okWithState(g));
    }

    // ---- 抉择 ----
    if (path == "/api/choose") {
        if (!g.pending.has_value()) return jsonError("当前没有待处理的抉择。");
        const std::string id = strOf(in, "id");
        // 选项必须真的存在于这次抉择里，且未被禁用。
        // 之前直接往下走，随便传个 id 也能「选成功」，
        // 等于绕过了事件的正确分支。
        {
            bool found = false;
            for (const auto& o : g.pending->options) {
                if (o.id != id) continue;
                found = true;
                if (o.disabled) return jsonError("这个选项当前不可选。");
                break;
            }
            if (!found) return jsonError("没有这个选项。");
        }
        if (g.pending->type == "adventure") {
            adventureChoose(g, id);
        } else {
            Player& p = g.player;
            const Pending ev = *g.pending;
            const RewardReceipt receipt(g);
            if (ev.type == "sectInvitation") {
                if (id == "join" && p.sect.empty()) {
                    logEvent(g, "sect",
                             "你接受金虹剑派的邀请，记下山门所在。前往山门通过入门考核后，方可正式拜师。",
                             true);
                } else if (id == "study") {
                    p.insight = std::min(INSIGHT_MAX, p.insight + 8);
                    logEvent(g, "self", "你向五宗使者请教，记下各自的修炼门径。");
                } else if (id == "free") {
                    p.stones += 20;
                    logEvent(g, "self", "你替使者捎带一封回信，获赠路资。");
                }
            } else if (ev.type == "valley") {
                Npc* npc = nullptr;
                if (!ev.npcId.empty())
                    for (auto& n : g.npcs)
                        if (n.id == ev.npcId && n.alive) npc = &n;
                if (id == "help") {
                    const int found = std::min(3, g.world.herbs);
                    p.herbs += found;
                    g.world.herbs -= found;
                    if (npc) addRelation(g, npc->id, 20);
                    if (randomOf(g) > 0.48 + p.luck * 0.003) {
                        applyDamage(g, 16);
                        p.health = std::max(1, p.health);
                    }
                    logEvent(g, "self", "你合力破阵，按灵药存量与同行道友结算收获。", true);
                } else if (id == "take") {
                    const int found = std::min(5, g.world.herbs);
                    p.herbs += found;
                    g.world.herbs -= found;
                    if (npc) addRelation(g, npc->id, -15);
                    logEvent(g, "self",
                             "你采走 " + std::to_string(found) + " 份灵药，避开了禁制。");
                } else if (id == "leave") {
                    p.insight = std::min(INSIGHT_MAX, p.insight + 5);
                    logEvent(g, "self", "你记下方位与阵纹，留待日后再访。");
                }
            }
            logEvent(g, "self", "「" + ev.title + "」实际结算：" + receipt.changes(g) + "。");
            g.pending.reset();
        }
        return jsonOk(okWithState(g));
    }

    // ---- 斗法 ----
    if (path == "/api/dungeon/enter" || path == "/api/dungeon/choose" ||
        path == "/api/dungeon/claim" || path == "/api/dungeon/leave" || path == "/api/dungeon/intel") {
        string error;
        const int restores=g.checkpoint.restores;
        if (path == "/api/dungeon/enter") error = dungeonEnter(g,strOf(in,"id"));
        else if (path == "/api/dungeon/choose") error = dungeonChoose(g,strOf(in,"id"));
        else if (path == "/api/dungeon/claim") error = dungeonClaim(g,strOf(in,"id"));
        else if (path == "/api/dungeon/leave") error = dungeonLeave(g);
        else error = dungeonIntel(g,strOf(in,"kind"),strOf(in,"id"));
        if (!error.empty()) return jsonError(error);
        if (g.checkpoint.restores==restores && !g.player.arts.battle && g.job && g.job->actionType == "socialElapsed" && !g.ended)
            advanceEx(g,g.job->remaining,true);
        Value response = okWithState(g); response.set("targetTab",str("dungeon")); return jsonOk(response);
    }
    if (path == "/api/battle/turn") {
        if (!g.player.arts.battle.has_value())
            return jsonError("当前没有斗法。");
        const std::string id = strOf(in, "id");
        const int restores = g.checkpoint.restores;
        const TurnResult r = battleTurn(g, id, strOf(in, "target"));
        if (!r.ok) return jsonError(r.error);
        // 奖励结算已收口在 core 的 battleFinish 里，
        // 所以这里不需要再做什么 —— 见 battle.cpp 的说明。
        if (g.checkpoint.restores == restores && !g.player.arts.battle && g.job && g.job->actionType == "socialElapsed" && !g.ended) advanceEx(g, g.job->remaining, true);
        return jsonOk(okWithState(g));
    }

    // ---- 设主修 / 辅修功法 ----
    // 主修决定修炼与战斗的主要加成；辅修效果减半。
    // 之前只有 CLI 能设主修，web 完全没有入口。
    if (path == "/api/arts/main" || path == "/api/arts/aux") {
        Arts& a = g.player.arts;
        const std::string mid = strOf(in, "id");
        if (a.manuals.find(mid) == a.manuals.end() || a.manuals.at(mid) <= 0)
            return jsonError("尚未修习这部功法。");
        if (path == "/api/arts/main") {
            if (a.main == mid) return jsonError("已经是主修了。");
            // 从辅修里摘掉，避免同时是主修又辅修
            a.aux.erase(std::remove(a.aux.begin(), a.aux.end(), mid),
                        a.aux.end());
            a.main = mid;
            ManualInfo mv;
            logEvent(g, "self",
                     "改以《" + (findManualInfo(mid, mv) ? mv.name : mid) +
                         "》为主修功法。",
                     true);
        } else {
            if (a.main == mid) return jsonError("主修功法不能同时作辅修。");
            const auto found = std::find(a.aux.begin(), a.aux.end(), mid);
            if (found != a.aux.end()) a.aux.erase(found);
            else {
                if (a.aux.size() >= 4) return jsonError("辅修最多四部。");
                a.aux.push_back(mid);
            }
            ManualInfo mv;
            logEvent(g, "self",
                     "以《" + (findManualInfo(mid, mv) ? mv.name : mid) +
                         "》为辅修功法。",
                     false);
        }
        return jsonOk(okWithState(g));
    }

    // ---- 携带 / 卸下神通 ----
    // 与 JS arts-ui.mjs 的「携带神通 / 卸下神通」对应。
    // 之前 C++ 版完全没有这个入口（artInstant 未实现），
    // 玩家只能一直用开局那四个初始神通，换了功法也带不上新神通。
    if (path == "/api/arts/spell") {
        const std::string sid = strOf(in, "id");
        // 传 spell 字段也接受，方便前端两种写法
        const std::string target = sid.empty() ? strOf(in, "spell") : sid;
        const ActionError r = artInstant(g, "spell", target);
        if (!r.ok) return jsonError(r.error);
        const SpellDef* sp = battleSpell(target);
        const bool nowCarried =
            std::find(g.player.arts.spells.begin(), g.player.arts.spells.end(),
                      target) != g.player.arts.spells.end();
        Value o = okWithState(g);
        o.set("text",
              str((nowCarried ? "已携带神通「" : "已卸下神通「") +
                  (sp ? sp->name : target) + "」。"));
        o.set("carried", Value(nowCarried));
        return jsonOk(o);
    }

    // ---- 法器：装备 / 卸下 / 铭刻 / 分解 ----
    if (path == "/api/arts/gear") {
        const std::string action = strOf(in, "action", "equip");
        const std::string gid = strOf(in, "id");
        const ActionError r = artInstant(g, action, gid);
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(g));
    }

    // ---- 洞府与配给 ----
    // 灵石配给到处都算；灵气配给只在洞府闭关时生效。
    // 两者都按月扣灵石，境界越高越贵。
    if (path == "/api/supply") {
        const auto r = shop::caveSupplySet(g,strOf(in,"kind"),intOf(in,"level",0));
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(g));
    }

    // 迁府：把洞府安在当下所在地（灵气配给只在这里生效）
    if (path == "/api/cave/move") {
        const auto r = shop::caveMove(g);
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(g));
    }
    if (path == "/api/cave/upgrade") {
        const auto r = shop::caveUpgrade(g);
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(g));
    }

    if (path == "/api/arts/node") {
        const std::string nid = strOf(in, "node");
        const ActionError result = artInstant(g, "node", nid);
        if (!result.ok) return jsonError(result.error);
        return jsonOk(okWithState(g));
    }
    if (path == "/api/arts/focus") {
        const std::string dao = strOf(in, "dao");
        if (daoIndex(dao) < 0) return jsonError("没有这个大道。");
        g.player.arts.focus = dao;
        return jsonOk(okWithState(g));
    }
    if (path == "/api/arts/learn") {
        const std::string id = strOf(in, "id");
        const std::string err = ecoActionError(g, "learn", id);
        if (!err.empty()) return jsonError(err);
        const EcoActionInfo info = ecoActionInfo(g, "learn", id);
        if (!info.valid) return jsonError("无此功法。");
        ecoBegin(g, "learn", id);
        advance(g, info.days);
        return jsonOk(okWithState(g));
    }

    // ---- 全部神通（含是否已携带、是否够境界）----
    // 前端「携带神通」面板要能列出 40 张神通并逐张切换，
    // 光给已携带的 spellList 是没法换神通的。
    if (path == "/api/arts/spells") {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        auto addSpell = [&](const SpellDef& sp) {
            Value so(json::Object{});
            so.set("id", str(sp.id));
            so.set("name", str(sp.name));
            so.set("dao", str(sp.dao));
            so.set("kind", str(sp.kind));
            so.set("cost", num(sp.cost));
            so.set("actualCost", num(battleCost(g.player, sp)));
            so.set("cooldown", num(sp.cooldown));
            so.set("stage", num(sp.stage));
            so.set("stageName", str(realmName(sp.stage)));
            so.set("description", str(battleSpellDescription(sp)));
            so.set("preview", previewView(g.player, sp));
            so.set("requiresTarget", Value(battleNeedsTarget(sp)));
            const bool mandatory = sp.id == "strike" || sp.id == "defend";
            const bool carried = mandatory || std::find(g.player.arts.spells.begin(),
                                   g.player.arts.spells.end(),
                                   sp.id) != g.player.arts.spells.end();
            so.set("carried", Value(carried));
            so.set("mandatory", Value(mandatory));
            const bool known = mandatory || std::find(g.player.arts.knownSpells.begin(), g.player.arts.knownSpells.end(), sp.id) != g.player.arts.knownSpells.end();
            so.set("known", Value(known));
            so.set("learned", Value(known)); so.set("equipped", Value(carried));
            so.set("learnable", Value(!known && g.player.stage >= sp.stage));
            so.set("canEquip", Value(!mandatory && !carried && known && g.player.stage >= sp.stage &&
                g.player.arts.spells.size() < static_cast<size_t>(battleSpellSlots(g.player))));
            so.set("canUnequip", Value(!mandatory && carried));
            so.set("unlocked", Value(g.player.stage >= sp.stage));
            std::string reason;
            if (mandatory) reason = "普通招式固定携带，不占额外神通名额。";
            else if (!carried && g.player.stage < sp.stage) reason = "需达到" + realmName(sp.stage) + "。";
            else if (!carried && !known) reason = "尚未领悟，请向熟悉此术的道友请教。";
            else if (!carried && g.player.arts.spells.size() >= static_cast<size_t>(battleSpellSlots(g.player)))
                reason = "额外神通已满，请先卸下一个。";
            so.set("enabled", Value(reason.empty()));
            so.set("reason", str(reason));
            arr.push_back(std::move(so));
        };
        bool includesDefend = false;
        for (const auto& sp : allSpells()) {
            addSpell(sp);
            includesDefend = includesDefend || sp.id == "defend";
        }
        if (!includesDefend) addSpell(*battleSpell("defend"));
        o.set("spells", Value(std::move(arr)));
        o.set("max", num(battleSpellSlots(g.player)));
        o.set("manaRegen", num(battleEnergy(g.player)));
        o.set("carriedCount", num(static_cast<int>(g.player.arts.spells.size())));
        return jsonOk(o);
    }

    // ---- 十二大道的全部节点定义（含名称与效果说明）----
    // 前端要能把「金道入门：金系伤害 +10%」这类信息显示出来，
    // 所以这里把 60 个节点的定义整份发过去。
    if (path == "/api/arts/nodes") {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& n : artNodes()) {
            Value no(json::Object{});
            const std::string nid =
                std::to_string(n.dao) + "-" + std::to_string(n.level);
            no.set("id", str(nid));
            no.set("dao", str(ART_DAOS[n.dao]));
            no.set("daoIndex", num(n.dao));
            no.set("level", num(n.level));
            no.set("cost", num(n.cost));
            no.set("name", str(n.name));
            no.set("desc", str(n.desc));
            no.set("unlocked",
                   Value(std::find(g.player.arts.nodes.begin(),
                                   g.player.arts.nodes.end(),
                                   nid) != g.player.arts.nodes.end()));
            arr.push_back(std::move(no));
        }
        o.set("nodes", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 参悟大道（30 天）----
    if (path == "/api/arts/meditate") {
        const std::string dao = strOf(in, "dao");
        const std::string err = artAnnualError(g, "meditate", dao);
        if (!err.empty()) return jsonError(err);
        // 参悟是一段 30 天的工作，走正常的在途工作流程
        const EcoActionInfo info = ecoActionInfo(g, "meditate", dao);
        if (!info.valid) return jsonError("无法参悟此道。");
        ecoBegin(g, "meditate", dao);
        advance(g, info.days);
        return jsonOk(okWithState(g));
    }

    // ---- 师徒与道侣日常 ----
    if (path == "/api/bond") {
        const std::string kind = strOf(in, "kind");
        const std::string err = bondActionError(g, kind);
        if (!err.empty()) return jsonError(err);
        const BondActionInfo info = bondActionInfo(g, kind);
        if (!info.valid) return jsonError("没有这项师门往来。");
        societyBondBegin(g, kind);
        if (g.job.has_value()) advance(g, info.days);
        return jsonOk(okWithState(g));
    }

    // ---- 宗门与知交 ----
    if (path == "/api/stipend") {
        const StipendResult r = socCollectStipend(g);
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(g));
    }
    if (path == "/api/story") {
        const std::string id = strOf(in, "id");
        const int choice = intOf(in, "choice", 0);
        if (choice < 0 || choice > 1) return jsonError("请选择这段故事提供的选项。");
        for (const auto& st : data::StoryDef_TABLE()) {
            if (st.id != id) continue;
            const std::string err = storyError(g, &st);
            if (!err.empty()) return jsonError(err);
            const std::string choiceError = storyChoiceError(g, st, choice);
            if (!choiceError.empty()) return jsonError(choiceError);
            storyComplete(g, st, choice);
            return jsonOk(okWithState(g));
        }
        return jsonError("没有这段机缘。");
    }
    if (path == "/api/wild") {
        const std::string id = strOf(in, "id");
        const std::string err = wildActionError(g, id);
        if (!err.empty()) return jsonError(err);
        societyWildBegin(g, id);
        advance(g, g.job->total);
        return jsonOk(okWithState(g));
    }
    if (path == "/api/expedition") {
        const std::string err = adventureActionError(g, "backhill");
        if (!err.empty()) return jsonError(err);
        adventureBegin(g, "backhill");
        advance(g, 7);
        return jsonOk(okWithState(g));
    }

    // ---- 移动 ----
    if (path == "/api/travel") {
        const auto modeField = in.asObject().find("mode");
        const auto beginField = in.asObject().find("begin");
        if (modeField != in.asObject().end() && !modeField->second.isString())
            return jsonError("赶路方式 mode 必须是字符串。");
        if (beginField != in.asObject().end() && !beginField->second.isBool())
            return jsonError("赶路参数 begin 必须是布尔值。");
        const std::string dest = strOf(in, "to");
        const std::string mode = strOf(in, "mode", "boat");
        const string error = travelBegin(g, dest, mode);
        if (!error.empty()) return jsonError(error);
        AdvanceReport rep;
        if (in["begin"].asBool(false)) rep.stopReason = "started";
        else rep = advanceEx(g, g.job->remaining, true);
        const bool arrived = rep.stopReason != "restored" && !g.ended && g.player.location == dest &&
                             (!g.job || g.job->actionType != "travel");
        Value out = okWithState(g);
        out.set("arrived", Value(arrived));
        out.set("text", str(rep.stopReason == "restored" ? "赶路途中死亡，已恢复最近存档；原行程已中止。" :
                         arrived ? "已抵达" + string(findLoc(dest)->name) + "。" :
                         g.ended ? "赶路途中此世已结束，未抵达目的地。" :
                         g.pending ? "赶路暂停，请先处理抉择。" :
                         g.player.arts.battleActive() ? "赶路暂停，请先完成斗法。" :
                         "已启程，请继续推进这段行程。"));
        Value report(json::Object{});
        report.set("daysPassed", num(rep.daysPassed));
        report.set("stopReason", str(rep.stopReason));
        json::Array highlights;
        for (const auto& line : rep.highlights) highlights.push_back(str(line));
        report.set("highlights", Value(std::move(highlights)));
        out.set("report", std::move(report));
        return jsonOk(out);
    }

    // ---- 营生 ----
    if (path == "/api/task") {
        const std::string id = strOf(in, "id", "errand");
        const std::string err = ecoActionError(g, "task", id);
        if (!err.empty()) return jsonError(err);
        ecoBegin(g, "task", id);
        advance(g, ecoActionInfo(g, "task", id).days);
        return jsonOk(okWithState(g));
    }

    // ---- 坊市买卖 ----
    if (path == "/api/buy") {
        const std::string kind = strOf(in, "kind");
        const std::string id = strOf(in, "id");
        const int count = intOf(in, "count", 1);
        if (count < 1 || count > 1000) return jsonError("购买数量需为 1 到 1000。");
        const std::string err = play::marketBuy(g, kind, id, count);
        if (!err.empty()) return jsonError(err);
        return jsonOk(okWithState(g));
    }

    if (path == "/api/location-service") {
        if (!in["begin"].isNull() && !in["begin"].isBool()) return jsonError("begin 必须为布尔值。");
        std::string tab;
        const auto error = locationServiceBegin(g, strOf(in, "id"), tab);
        if (!error.empty()) return jsonError(error);
        if (g.job && !in["begin"].asBool()) advanceEx(g, g.job->remaining, true);
        Value o = okWithState(g); o.set("targetTab", str(tab)); return jsonOk(o);
    }
    if (path == "/api/npc-learn") {
        if (!in["begin"].isNull() && !in["begin"].isBool()) return jsonError("begin 必须为布尔值。");
        const auto error = npcLearnBegin(g, strOf(in, "npc"), strOf(in, "kind"), strOf(in, "id"));
        if (!error.empty()) return jsonError(error);
        if (g.job && !in["begin"].asBool()) advanceEx(g, g.job->remaining, true);
        return jsonOk(okWithState(g));
    }
    // ---- NPC 往来 ----
    if (path == "/api/social") {
        const std::string npcId = strOf(in, "npc");
        const std::string act = strOf(in, "action");
        int days = 0;
        for (const auto& action : play::socialActions(g, npcId))
            if (action.id == act) days = action.days;
        const play::SocialResult r = play::socialDo(g, npcId, act);
        if (!r.ok) return jsonError(r.error);
        if (days > 0 && !g.ended) {
            Job job;
            job.actionType = "socialElapsed";
            job.name = "道友往来";
            job.total = days;
            job.remaining = days;
            job.started = g.clock.days;
            g.job = job;
            if (!g.player.arts.battle) advance(g, days);
        }
        Value o = okWithState(g);
        o.set("text", str(r.text));
        o.set("relationDelta", num(r.relationDelta));
        return jsonOk(o);
    }

    // ---- 某个人的可用往来（前端点开某人时拉取）----
    if (path == "/api/social-actions") {
        const std::string npcId = strOf(in, "npc");
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& a : play::socialActions(g, npcId)) {
            Value ao(json::Object{});
            ao.set("id", str(a.id));
            ao.set("name", str(a.name));
            ao.set("desc", str(a.desc));
            ao.set("days", num(a.days));
            ao.set("cost", num(a.cost));
            ao.set("enabled", Value(a.enabled));
            ao.set("reason", str(a.reason));
            arr.push_back(std::move(ao));
        }
        o.set("actions", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 炼丹：使用同一套配料、耗时和在途工作规则 ----
    if (path == "/api/craft") {
        const std::string kind = strOf(in, "kind", "brew");
        const std::string id = strOf(in, "id");
        const int batch = intOf(in, "batch", 1);
        if (kind != "brew") return jsonError("炼器请在工坊选择材料与器型。");
        const std::string err = ecoActionError(g, kind, id, batch);
        if (!err.empty()) return jsonError(err);
        const EcoActionInfo info = ecoActionInfo(g, kind, id, batch);
        if (!info.valid) return jsonError("无法炼制此丹。");
        ecoBegin(g, kind, id, batch);
        advance(g, info.days);
        Value o = okWithState(g);
        o.set("text", str("已安排「" + info.name + "」，共 " +
            std::to_string(batch) + " 炉，需 " + std::to_string(info.days) + " 天。"));
        return jsonOk(o);
    }

    // ---- 装备法器 ----
    if (path == "/api/equip") {
        const std::string gearId = strOf(in, "id");
        const ActionError result = artInstant(g, "equip", gearId);
        if (!result.ok) return jsonError(result.error);
        return jsonOk(okWithState(g));
    }

    // ---- 服用丹药 ----
    if (path == "/api/use-pill") {
        const std::string pid = strOf(in, "id");
        Player& p = g.player;
        Arts& a = p.arts;
        if (a.potions.count(pid) == 0 || a.potions[pid] <= 0)
            return jsonError("没有这味丹药。");
        const data::RecipeDef* r = findRecipe(pid);
        if (!r) return jsonError("没有这张丹方。");
        if (r->kind == "xp" && hasTalent(p, "oddMeridian"))
            return jsonError("经脉异变无法吸收修为丹药；可通过修炼增长修为。");

        // ---- 门槛（对应 JS arts.mjs 的 eat 分支）----
        // 1) 耐药上限：丹道三层翻倍
        const int limit = r->tolerance * (artHas(a, "丹", 3) ? 2 : 1);
        const auto usedEntry = a.used.find(pid);
        const int used = usedEntry == a.used.end() ? 0 : usedEntry->second;
        if (r->tolerance > 0 && used >= limit)
            return jsonError("已达到此丹耐药上限（" +
                             std::to_string(used) + "/" +
                             std::to_string(limit) + "）。");
        // 2) 丹毒过高时不能乱吃，化毒丹除外
        if (a.poison >= 80 && r->kind != "detox")
            return jsonError("丹毒过高（" + std::to_string(a.poison) +
                             "），请先化毒。");
        // 3) 资质丹本世最多提升 10 点
        if (r->kind == "apt" && a.aptPills >= 10)
            return jsonError("本世资质丹药提升已达 10 点，不能反复洗炼。");

        const PillEffect ef = artPillEffect(p, *r);
        if (r->kind == "apt" && ef.value <= 0)
            return jsonError("资质已达上限，此丹无法继续提升。");

        // ---- 效果结算（原来只扣丹加毒，效果全都没生效）----
        const string kind = r->kind;
        if (kind == "breakthrough") {
            // 大境界准备：给一次突破增益
            const int stage = r->sourceEffect.find("结婴") != string::npos ? 8
                              : r->sourceEffect.find("化神") != string::npos ? 11
                              : r->name == "筑基丹" ? 2
                                                    : 5;
            if (p.stage != stage)
                return jsonError("该丹只用于对应的大境界准备。");
            a.breakBuff = "stage";
        } else if (kind == "xp") {
            const int need = realmOf(p.stage).need;
            p.xp = std::min(need, p.xp + ef.value);
        } else if (kind == "heal") {
            p.health = std::min(100, p.health + ef.value);
        } else if (kind == "detox") {
            a.poison = std::max(0, a.poison - ef.value);
        } else if (kind == "points") {
            a.points += ef.value;
        } else if (kind == "dao") {
            if (daoIndex(r->dao) < 0) return jsonError("此道尚未开放。");
            artGain(p, r->dao, ef.value);
        } else if (kind == "life") {
            p.lifeBonus = std::min(250, p.lifeBonus + ef.value);
            const int base[5] = {100, 200, 500, 1000, 2000};
            p.lifespan = std::min(3000, base[std::min(4, p.stage / 3)] +
                                            p.lifeBonus);
        } else if (kind == "apt") {
            const int inc = std::min({ef.value, 10 - a.aptPills, APT_MAX - p.apt});
            if (inc > 0) {
                p.apt += inc;
                a.aptPills += inc;
            }
        } else if (kind == "insight") {
            p.insight = std::min(INSIGHT_MAX, p.insight + ef.value);
        } else if (kind == "body") {
            a.hpBonus = std::min(200, a.hpBonus + ef.value);
        } else if (kind == "heart" || kind == "mind" || kind == "speed") {
            if (kind == "heart") a.heart = std::min(500, a.heart + ef.value);
            else if (kind == "mind") a.mind = std::min(500, a.mind + ef.value);
            else a.speed = std::min(500, a.speed + ef.value);
        } else if (kind == "repel") {
            a.repel = 2;
        } else if (kind == "lure") {
            // 引兽丹：给同阶炼器材料
            const int grade = ordinaryMaterialGradeLimit(p);
            for (const auto& mt : data::MaterialDef_TABLE()) {
                if (mt.grade != grade) continue;
                a.materials[mt.id] += 2;
                break;
            }
        } else {
            // 其余归入战斗准备（斗法起手生效）
            const CombatPill prep = artCombatPill(*r);
            a.combatPrep[prep.key] = prep.value;
            if (prep.key == "attack") a.combatBuff = 0.1;
        }

        a.potions[pid] -= 1;
        a.used[pid] += 1;
        // 丹毒：丹药本身 + 越阶服用的额外惩罚
        const bool overTier = (r->grade > artTier(p));
        a.poison = std::min(100, a.poison + r->poison + (overTier ? 2 : 0));

        logEvent(g, "self",
                 "服下" + std::string(r->name) + "：" + ef.text, true, "",
                 "丹毒 +" + std::to_string(r->poison + (overTier ? 2 : 0)));
        return jsonOk(okWithState(g));
    }

    // ---- 传承档案 ----
    // 存在 build/legacy.json，跨存档共享（对应 JS 的 localStorage）
    // ---- 拜入宗门（自选五宗）----
    // 之前唯一入宗途径是第 3 年的「山门来信」，而且只能进金虹剑派。
    // 26 部拓展功法全靠宗门藏经阁，等于大半养成线被卡死。
    if (path == "/api/join") {
        const std::string sid = strOf(in, "sect");
        const string error = sectTrialBegin(g, sid);
        if (!error.empty()) return jsonError(error);
        return jsonOk(okWithState(g));
    }

    // ---- 可拜入的宗门清单 ----
    if (path == "/api/sects") {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& s : SECTS()) {
            Value so(json::Object{});
            so.set("id", str(s.id));
            so.set("name", str(s.name));
            so.set("element", str(s.element));
            so.set("motto", str(s.motto));
            so.set("description", str(s.description));
            auto it = g.world.sects.find(s.id);
            so.set("members", num(it == g.world.sects.end() ? 0 : it->second.members));
            so.set("influence", num(it == g.world.sects.end() ? 0 : it->second.influence));
            so.set("treasury", num(it == g.world.sects.end() ? 0 : it->second.treasury));
            const bool femaleOnly = (std::string(s.id) == "xinghe");
            so.set("femaleOnly", Value(femaleOnly));
            const string why = sectTrialError(g, s.id);
            if (const auto* def = findSectTrial(s.id)) {
                Value trial(json::Object{});
                trial.set("name", str(def->name));
                trial.set("desc", str(def->desc));
                trial.set("style", str(def->style));
                trial.set("cost", num(def->fee));
                trial.set("days", num(def->days));
                trial.set("minStage", num(def->minStage));
                trial.set("stageName", str(realmName(def->minStage)));
                so.set("trial", std::move(trial));
            }
            so.set("enabled", Value(why.empty()));
            so.set("reason", str(why));
            so.set("current", Value(g.player.sect == s.id));
            arr.push_back(std::move(so));
        }
        o.set("sects", Value(std::move(arr)));
        o.set("masterRequirement", num(renown::masterRequirement()));
        o.set("mentorAffinityNeed", num(40));
        o.set("sectTaskUsed", num(sectTasksTakenThisMonth(g))); o.set("sectTaskLimit", num(3));
        json::Array mentors;
        for (const auto* npc : sectMentorCandidates(g)) {
            Value item(json::Object{});
            item.set("id", str(npc->id)); item.set("name", str(npc->name)); item.set("stage", num(npc->stage));
            item.set("realmName", str(realmName(npc->stage))); item.set("location", str(npc->location));
            const auto* location = findLoc(npc->location);
            item.set("locationName", str(location ? location->name : npc->location));
            const auto relation = g.player.relations.find(npc->id);
            item.set("relation", num(relation == g.player.relations.end() ? 0 : relation->second));
            item.set("isMaster", Value(g.bonds.master == npc->id));
            const string reason = sectMasterError(g, npc->id);
            item.set("enabled", Value(reason.empty())); item.set("reason", str(reason));
            item.set("portrait", str(personPortrait(npc->gender, npc->sect)));
            item.set("cost", num(20)); item.set("days", num(5));
            mentors.push_back(std::move(item));
        }
        o.set("mentors", Value(std::move(mentors)));
        return jsonOk(o);
    }

    // ---- 成就（14 项）----
    // artAchievements() 早就写好了，但从来没有调用点，
    // 所以 achievementTexts 一直是空的，14 个成就完全不可得。
    if (path == "/api/achievements") {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& a : data::AchievementDef_TABLE()) {
            const int prog = artProgress(g.player, a.key);
            const bool got = prog >= a.target;
            Value ao(json::Object{});
            ao.set("id", str(a.id));
            ao.set("name", str(a.name));
            ao.set("key", str(a.key));   // AchievementDef 没有 desc，用 key 代替展示
            ao.set("key", str(a.key));
            ao.set("target", num(a.target));
            ao.set("progress", num(prog));
            ao.set("points", num(a.points));
            ao.set("done", Value(got));
            arr.push_back(std::move(ao));
        }
        o.set("achievements", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 拜入宗门（自选五宗）占位结束 ----
    if (path == "/api/legacy") {
        const legacy::Archive a = legacyLoad();
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("earned", num(a.earned));
        o.set("runs", num(a.runs));
        o.set("budget", num(legacy::talentBudget(a)));
        o.set("basePoints", num(BASE_POINTS));
        o.set("maxPoints", num(MAX_POINTS));
        {
            json::Array arr;
            for (const auto& m : legacy::milestonesOf(g, a)) {
                Value mo(json::Object{});
                mo.set("key", str(m.key));
                mo.set("label", str(m.label));
                mo.set("points", num(m.points));
                mo.set("reached", Value(m.reached));
                mo.set("claimed", Value(m.alreadyHad));
                arr.push_back(std::move(mo));
            }
            o.set("milestones", Value(std::move(arr)));
        }
        if (g.ending.has_value()) {
            Value e(json::Object{});
            e.set("cause", str(g.ending->cause));
            e.set("age", num(g.ending->age));
            e.set("realm", str(g.ending->realm));
            e.set("friends", num(g.ending->friends));
            e.set("score", num(g.ending->score));
            const bool claimed = std::find(a.claimed.begin(),a.claimed.end(),g.lifeId)!=a.claimed.end();
            e.set("canRestore",Value(g.ending->cause!="寿终正寝" && g.checkpoint.enabled && !claimed));
            o.set("ending", std::move(e));
        }
        return jsonOk(o);
    }

    // 结算这一世，换取传承点
    if (path == "/api/legacy/settle") {
        legacy::Archive a = legacyLoad();
        const legacy::SettleResult r = legacy::settle(g, a);
        if (!r.eligible) return jsonError(r.note);
        if (!legacySave(a)) return jsonError("传承档案保存失败，请检查存档目录是否可写。", 500);
        g.checkpoint.enabled=false;
        g.checkpoint.snapshot.clear();
        Value o = okWithState(g);
        o.set("points", num(r.points));
        o.set("earned", num(a.earned));
        o.set("budget", num(legacy::talentBudget(a)));
        {
            json::Array arr;
            for (const auto& s : r.reasons) arr.push_back(str(s));
            o.set("reasons", Value(std::move(arr)));
        }
        return jsonOk(o);
    }

    // 建号抽天赋：抽 12 项候选，roll 是重抽次数
    if (path == "/api/legacy/draft") {
        const uint32_t seed = seedOf(in);
        const int roll = intOf(in, "roll", 0);
        if (roll < 0 || roll > 2) return jsonError("天赋最多重抽两次。");
        json::Array arr;
        for (const auto& id : legacy::draft(seed, roll)) {
            Value o(json::Object{});
            o.set("id", str(id));
            const TalentDef* t = findTalent(id);
            o.set("name", str(t ? t->name : id));
            o.set("tag", str(t ? t->tag : ""));
            o.set("cost", num(t ? t->cost : 0));
            o.set("desc", str(t ? t->desc : ""));
            arr.push_back(std::move(o));
        }
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("talents", Value(std::move(arr)));
        o.set("budget", num(legacy::talentBudget(legacyLoad())));
        return jsonOk(o);
    }

    // ---- 野外冲突：拦路索财 / 野外截杀 ----
    // ---- 野外冲突：拦路索财 / 野外截杀 ----
    if (path == "/api/conflict") {
        const std::string npcId = strOf(in, "npc");
        const std::string intent = strOf(in, "intent", "rob");
        const conflict::ConflictResult r =
            conflict::doConflict(g, npcId, intent);
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        o.set("response", str(r.responseText));
        o.set("notoriety", num(r.notoriety));
        return jsonOk(o);
    }

    // ---- 对方可能的反应（给界面做预告）----
    if (path == "/api/conflict/preview") {
        const std::string npcId = strOf(in, "npc");
        const std::string intent = strOf(in, "intent", "rob");
        const std::string err = conflict::conflictError(g, npcId, intent);
        Value o(json::Object{});
        o.set("ok", Value(err.empty()));
        if (!err.empty()) {
            o.set("error", str(err));
            return jsonOk(o);
        }
        for (const auto& n : g.npcs) {
            if (n.id != npcId) continue;
            o.set("response", str(conflict::response(g, n, intent)));
            o.set("name", str(n.name));
            o.set("stones", num(n.stones));
            o.set("herbs", num(n.herbs));
            break;
        }
        return jsonOk(o);
    }

    // ---- 通用营生行动（对应 JS 的 economy 年度行动）----
    // commission / hunt / mine / arena / quest / forge 的规则层早就写好了，
    // 但一直没有接口，属于「函数在、入口不在」。这里统一接上。
    if (path == "/api/eco") {
        const std::string type = strOf(in, "type");
        const std::string id = strOf(in, "id");
        const int batch = intOf(in, "batch", 1);
        const std::string err = ecoActionError(g, type, id, batch);
        if (!err.empty()) return jsonError(err);
        const EcoActionInfo info = ecoActionInfo(g, type, id, batch);
        if (!info.valid) return jsonError("无法进行此项行动。");
        ecoBegin(g, type, id, batch);
        advance(g, info.days);
        Value o = okWithState(g);
        o.set("text", str("已安排「" + info.name + "」，用时 " +
                          std::to_string(info.days) + " 天。"));
        return jsonOk(o);
    }

    if (path == "/api/forge-preview") {
        const string main = strOf(in, "main"), aux = strOf(in, "aux"), core = strOf(in, "core"), shape = strOf(in, "shape", "sword");
        const ForgeInfo forge = artForgeInfo(g.player, main, aux, core, shape);
        const string id = main + "|" + aux + "|" + core + "|" + shape;
        const string reason = forge.valid ? ecoActionError(g, "forge", id) : "请选择有效的主料、辅料、灵核与器型。";
        const EcoActionInfo info = forge.valid ? ecoActionInfo(g, "forge", id) : EcoActionInfo{};
        Value result(json::Object{}), o(json::Object{});
        o.set("valid", Value(forge.valid)); o.set("grade", num(forge.grade)); o.set("cost", num(forge.cost));
        o.set("days", num(info.days)); o.set("bonus", num(forge.bonus)); o.set("chance", num(forge.chance));
        o.set("shapeId", str(forge.shapeId)); o.set("shapeName", str(forge.shapeName)); o.set("shapeSlot", str(forge.shapeSlot));
        o.set("mainName", str(forge.mainName)); o.set("mainElement", str(forge.mainElement)); o.set("mainPolarity", str(forge.mainPolarity));
        o.set("enabled", Value(forge.valid && reason.empty())); o.set("reason", str(reason));
        json::Array materials, blockers;
        const int stonesRequired = forge.valid ? forge.cost : 0;
        const int stonesOwned = std::max(0, g.player.stones);
        const int stonesMissing = std::max(0, stonesRequired - stonesOwned);
        o.set("stonesRequired", num(stonesRequired)); o.set("stonesOwned", num(stonesOwned));
        o.set("stonesMissing", num(stonesMissing));
        if (forge.valid) {
            for (const auto& material : artForgeMaterials(g.player, main, aux, core)) {
                Value item(json::Object{}); json::Array roles;
                item.set("id", str(material.id)); item.set("name", str(material.name));
                item.set("required", num(material.required)); item.set("owned", num(material.owned));
                item.set("missing", num(material.missing));
                for (const auto& role : material.roles) roles.push_back(str(role));
                item.set("roles", Value(std::move(roles))); materials.push_back(std::move(item));
                if (material.missing > 0)
                    blockers.push_back(str(material.name + "不足：需 " + std::to_string(material.required) +
                                           " 份，持有 " + std::to_string(material.owned) + " 份，缺 " +
                                           std::to_string(material.missing) + " 份。"));
            }
            if (stonesMissing > 0)
                blockers.push_back(str("灵石不足：需 " + std::to_string(stonesRequired) + " 枚，持有 " +
                                       std::to_string(stonesOwned) + " 枚，缺 " +
                                       std::to_string(stonesMissing) + " 枚。"));
        }
        // 保留原行动校验优先级；资源缺口单独完整列出，事件等门槛不能被足料覆盖。
        if (!reason.empty() && (blockers.empty() ||
            (reason.find("材料不足") == string::npos && reason.find("灵石不足") == string::npos)))
            blockers.insert(blockers.begin(), str(reason));
        o.set("materials", Value(std::move(materials))); o.set("blockers", Value(std::move(blockers)));
        Gear projected; projected.id = "preview"; projected.crafted = forge.valid; projected.shape = forge.shapeId;
        projected.name = forge.mainName + forge.shapeName; projected.slot = forge.shapeSlot; projected.grade = forge.grade;
        projected.element = forge.mainElement; projected.polarity = forge.mainPolarity; projected.bonus = forge.bonus;
        o.set("ability", artifactView(g.player, artifactAbility(projected)));
        if (forge.valid) {
            o.set("passive", artifactPassiveView(g.player, projected));
            projected.inscribed = true;
            o.set("inscribedAbility", artifactView(g.player, artifactAbility(projected)));
            o.set("inscribedPassive", artifactPassiveView(g.player, projected));
            o.set("inscribeCost", num(30 * forge.grade));
            o.set("inscribeRequirement", str("器道入门；成器后花费30×品阶灵石铭刻。"));
        }
        result.set("ok", Value(true)); result.set("info", std::move(o)); return jsonOk(result);
    }

    // ---- 炼器（要选主料/辅料/灵核/器型）----
    if (path == "/api/forge") {
        const std::string mainId = strOf(in, "main");
        const std::string auxId = strOf(in, "aux");
        const std::string coreId = strOf(in, "core");
        const std::string shapeId = strOf(in, "shape", "sword");
        const std::string id =
            mainId + "|" + auxId + "|" + coreId + "|" + shapeId;
        const std::string err = ecoActionError(g, "forge", id);
        if (!err.empty()) return jsonError(err);
        const EcoActionInfo info = ecoActionInfo(g, "forge", id);
        if (!info.valid) return jsonError("炼器配料无效。");
        ecoBegin(g, "forge", id);
        advance(g, info.days);
        return jsonOk(okWithState(g));
    }

    // ---- 炼器可选项（材料 / 器型）----
    if (path == "/api/forge-options") {
        Value o(json::Object{});
        o.set("ok", Value(true));
        {
            json::Array arr;
            for (const auto& mt : data::MaterialDef_TABLE()) {
                Value mo(json::Object{});
                mo.set("id", str(mt.id));
                mo.set("name", str(mt.name));
                mo.set("grade", num(mt.grade));
                mo.set("element", str(mt.element));
                mo.set("polarity", str(mt.polarity));
                auto it = g.player.arts.materials.find(mt.id);
                mo.set("owned", num(it == g.player.arts.materials.end() ? 0 : it->second));
                mo.set("price", num(mt.price));
                mo.set("buyable", Value(ordinaryMaterialError(g.player,mt.grade).empty()));
                mo.set("source", str(mt.grade <= 2 ? "普通坊市、野外采矿" : "天机阁定向材料秘境，通关保底十份"));
                arr.push_back(std::move(mo));
            }
            o.set("materials", Value(std::move(arr)));
        }
        {
            json::Array arr;
            for (const auto& sh : data::ShapeDef_TABLE()) {
                Value so(json::Object{});
                so.set("id", str(sh.id));
                so.set("name", str(sh.name));
                so.set("slot", str(sh.slot));
                arr.push_back(std::move(so));
            }
            o.set("shapes", Value(std::move(arr)));
        }
        return jsonOk(o);
    }

    // ---- 机缘（12 项限时机缘）----
    if (path == "/api/quest") {
        const std::string qid = strOf(in, "id");
        const data::QuestDef* q = nullptr;
        for (const auto& x : data::QuestDef_TABLE())
            if (x.id == qid) q = &x;
        if (!q) return jsonError("没有这项机缘。");
        const std::string err = artQuestError(g, q);
        if (!err.empty()) return jsonError(err);
        artAnnual(g, "quest", qid);
        return jsonOk(okWithState(g));
    }

    // ---- 当前可参与的行动清单（供界面直接渲染）----
    if (path == "/api/eco-options") {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        // 任务、委托、猎妖、寻矿、演武、机缘
        // quest 单独在下面按 12 项机缘列，不要在这里重复
        const char* types[] = {"task", "commission", "hunt", "mine", "arena"};
        for (const char* ty : types) {
            const std::string type(ty);
            if (type == "task") {
                // 城中差事及与 sectTaskDefs 共用定义的宗门差事。
                for (const char* tid : {"errand", "escort", "inspect", "sect", "sect-patrol", "sect-records", "sect-array"}) {
                    const std::string err = ecoActionError(g, "task", tid);
                    const EcoActionInfo info = ecoActionInfo(g, "task", tid);
                    Value eo(json::Object{});
                    eo.set("type", str("task"));
                    eo.set("id", str(tid));
                    eo.set("name", str(info.name));
                    eo.set("desc", str(std::string(tid) == "errand" ? "替商铺递送书信、整理账簿"
                                    : std::string(tid) == "escort" ? "护送往来商旅，有小概率受伤"
                                    : std::string(tid) == "inspect" ? "巡检灵阵，考验阵道见识"
                                                                    : "山门差事，积累宗门贡献"));
                    eo.set("days", num(info.days));
                    eo.set("pay", num(info.fee));
                    eo.set("enabled", Value(err.empty()));
                    eo.set("reason", str(err));
                    if (const auto* task = findSectTask(tid)) {
                        eo.set("desc", str(task->desc)); eo.set("sect", Value(true));
                        eo.set("contribution", num(task->contribution));
                        const auto* sect = findSect(g.player.sect); eo.set("dao", str(sect ? sect->element : ""));
                        Player rewardPreview = g.player;
                        if (sect && task->daoExp > 0) artGain(rewardPreview, sect->element, task->daoExp);
                        const auto expBefore = sect ? g.player.arts.exp.find(sect->element) : g.player.arts.exp.end();
                        const double originalExp = expBefore == g.player.arts.exp.end() ? 0 : expBefore->second;
                        eo.set("daoExp", num(sect ? jsRound(rewardPreview.arts.exp[sect->element] - originalExp) : 0));
                        eo.set("daoPoints", num(rewardPreview.arts.points - g.player.arts.points));
                        eo.set("monthlyUsed", num(sectTasksTakenThisMonth(g))); eo.set("monthlyLimit", num(3));
                    }
                    arr.push_back(std::move(eo));
                }
            } else if (type == "commission") {
                for (const char* mode : {"brew", "forge"}) {
                    const std::string err = ecoActionError(g, "commission", mode);
                    const EcoActionInfo info = ecoActionInfo(g, "commission", mode);
                    Value eo(json::Object{});
                    eo.set("type", str("commission"));
                    eo.set("id", str(mode));
                    eo.set("name", str(info.name.empty()
                        ? (std::string("商会委托·") + (std::string(mode) == "brew" ? "炼丹" : "炼器"))
                        : info.name));
                    eo.set("desc", str(std::string(mode) == "brew"
                        ? "代工炼丹，交付后领工费"
                        : "代工炼器，交付后领工费"));
                    eo.set("days", num(info.days));
                    eo.set("pay", num(info.fee));
                    eo.set("cost", num(info.cost));
                    eo.set("chance", num(info.chance));
                    eo.set("enabled", Value(err.empty()));
                    eo.set("reason", str(err));
                    arr.push_back(std::move(eo));
                }
            } else {
                const std::string err = ecoActionError(g, type, "");
                const EcoActionInfo info = ecoActionInfo(g, type, "");
                if (!info.valid && err.empty()) continue;
                Value eo(json::Object{});
                eo.set("type", str(type));
                eo.set("id", str(""));
                eo.set("name", str(info.name));
                eo.set("desc", str(type == "hunt" ? "与妖兽斗法，胜后得赏金与材料"
                                : type == "mine" ? "开采炼器矿材"
                                : type == "arena" ? "演武切磋，增进剑道与斗法心得"
                                                  : "限时机缘"));
                eo.set("days", num(info.days));
                eo.set("pay", num(info.fee));
                eo.set("cost", num(info.cost));
                eo.set("enabled", Value(err.empty()));
                eo.set("reason", str(err));
                arr.push_back(std::move(eo));
            }
        }
        // 机缘单独列（12 项各有条件）
        for (const auto& q : data::QuestDef_TABLE()) {
            const std::string err = artQuestError(g, &q);
            const bool done = g.player.arts.quests.count(q.id) > 0;
            Value eo(json::Object{});
            eo.set("type", str("quest"));
            eo.set("id", str(q.id));
            eo.set("name", str(q.name));
            eo.set("desc", str(q.desc));
            eo.set("days", num(0));   // 机缘本身不占固定天数
            eo.set("reward", str(q.reward));
            eo.set("done", Value(done));
            eo.set("enabled", Value(!done && err.empty()));
            eo.set("reason", str(done ? "已完成" : err));
            arr.push_back(std::move(eo));
        }
        o.set("options", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 坊市出售 ----
    if (path == "/api/sell") {
        const shop::SellResult r =
            shop::sell(g, strOf(in, "kind"), strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 灵核 / 凝元丹 交易 ----
    if (path == "/api/trade-stock") {
        const shop::SellResult r = shop::tradeStock(
            g, strOf(in, "kind"), in["buy"].isBool() ? in["buy"].asBool() : true,
            intOf(in, "count", 1));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 书坊买功法 ----
    if (path == "/api/book-buy") {
        const shop::SellResult r = shop::bookBuy(g, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 藏经阁兑换 ----
    if (path == "/api/library") {
        const shop::SellResult r = shop::libraryExchange(g, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 法器：卸下 / 分解 / 铭刻 ----
    if (path == "/api/library/spell") {
        const auto result = shop::librarySpellExchange(g,strOf(in,"id"));
        if (!result.ok) return jsonError(result.error);
        Value o=okWithState(g);
        o.set("text",str(result.text));
        return jsonOk(o);
    }

    if (path == "/api/gear/unequip") {
        const shop::SellResult r = shop::unequip(g, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(g));
    }
    if (path == "/api/gear/salvage") {
        const shop::SellResult r = shop::salvage(g, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }
    if (path == "/api/gear/inscribe") {
        const shop::SellResult r = shop::inscribe(g, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 丹炉：修炉 / 换炉 ----
    if (path == "/api/furnace/repair") {
        const shop::SellResult r = shop::repairFurnace(g);
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }
    if (path == "/api/furnace/buy") {
        const shop::SellResult r = shop::buyFurnace(g, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 中止当前安排 ----
    if (path == "/api/cancel-job") {
        if (g.job && g.job->actionType == "socialElapsed")
            return jsonError("此次往来已结算，请完成剩余的约定天数。");
        const shop::SellResult r = shop::cancelJob(g);
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 道友往来（好感度 / 交情 / 送礼 / 讨要）----
    // 这几项都不消耗天数：轻交互即时完成，重交互（切磋/请教/游历/论道）才耗时。
    if (path == "/api/greet") {
        const friendship::GreetResult r = friendship::greet(g, strOf(in, "npc"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        o.set("firstTime", Value(r.firstTime));
        o.set("affinityDelta", num(r.affinityDelta));
        o.set("creditDelta", num(r.creditDelta));
        return jsonOk(o);
    }

    if (path == "/api/gift") {
        const friendship::GiftResult r = friendship::giftItem(
            g, strOf(in, "npc"), strOf(in, "kind"), strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        o.set("affinityDelta", num(r.affinityDelta));
        o.set("creditDelta", num(r.creditDelta));
        return jsonOk(o);
    }

    if (path == "/api/npc-bag") {
        const std::string npcId = strOf(in, "npc");
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("credit", num(friendship::creditOf(g, npcId)));
        o.set("creditLabel",
              str(friendship::creditLabel(friendship::creditOf(g, npcId))));
        json::Array arr;
        for (const auto& b : friendship::npcBag(g, npcId)) {
            Value bo(json::Object{});
            bo.set("kind", str(b.kind));
            bo.set("id", str(b.id));
            bo.set("name", str(b.name));
            bo.set("desc", str(b.desc));
            bo.set("count", num(b.count));
            bo.set("value", num(b.value));
            bo.set("creditCost", num(b.creditCost));
            bo.set("affordable", Value(b.affordable));
            bo.set("reason", str(b.reason));
            arr.push_back(std::move(bo));
        }
        o.set("items", Value(std::move(arr)));
        return jsonOk(o);
    }

    if (path == "/api/npc-take") {
        const friendship::TradeResult r = friendship::takeFromNpc(
            g, strOf(in, "npc"), strOf(in, "kind"), strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        o.set("cost", num(r.cost));
        return jsonOk(o);
    }

    // ---- 兽潮 ----
    if (path == "/api/tide") {
        Value o = okWithState(g);
        Value t(json::Object{});
        t.set("active", Value(g.tide.active));
        t.set("severity", num(g.tide.severity));
        t.set("monthsLeft", num(g.tide.monthsLeft));
        t.set("wavesFought", num(g.tide.wavesFought));
        t.set("wavesWon", num(g.tide.wavesWon));
        t.set("npcDeaths", num(g.tide.npcDeaths));
        t.set("dangerBonus", num(tide::dangerBonus(g)));
        t.set("status", str(tide::statusText(g)));
        json::Array fa;
        for (const auto& nm : g.tide.fallen) fa.push_back(str(nm));
        t.set("fallen", Value(std::move(fa)));
        o.set("tide", std::move(t));
        return jsonOk(o);
    }

    if (path == "/api/tide/fight") {
        const tide::FightResult r = tide::fight(g);
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        o.set("lethal", Value(r.lethal));
        return jsonOk(o);
    }

    if (path == "/api/tide/hunker") {
        const std::string txt = tide::hunkerDown(g);
        Value o = okWithState(g);
        o.set("text", str(txt));
        return jsonOk(o);
    }

    // ---- 通讯录 ----
    if (path == "/api/roster") {
        const bool strangers = in["strangers"].isBool() ? in["strangers"].asBool() : false;
        const std::string sortBy = strOf(in, "sort", "default");
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& e : play::roster(g, strangers, sortBy)) {
            Value ro(json::Object{});
            ro.set("id", str(e.id));
            ro.set("name", str(e.name));
            ro.set("gender", str(e.gender));
            ro.set("age", num(e.age));
            ro.set("stage", num(e.stage));
            ro.set("realmName", str(e.realmName));
            ro.set("sectName", str(e.sectName));
            ro.set("personality", str(e.personality));
            ro.set("goal", str(e.goal));
            ro.set("locationName", str(e.locationName));
            ro.set("relation", num(e.relation));
            ro.set("relationLabel", str(e.relationLabel));
            ro.set("here", Value(e.here));
            ro.set("isMaster", Value(e.isMaster));
            ro.set("isPartner", Value(e.isPartner));
            ro.set("isDisciple", Value(e.isDisciple));
            ro.set("sword", num(e.sword));
            ro.set("insight", num(e.insight));
            ro.set("apt", num(e.apt));
            ro.set("canMessage", Value(e.canMessage));
            ro.set("canInvite", Value(e.canInvite));
            arr.push_back(std::move(ro));
        }
        o.set("roster", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 传讯问候 ----
    if (path == "/api/message") {
        const play::MessageResult r = play::sendMessage(g, strOf(in, "npc"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        o.set("relationDelta", num(r.relationDelta));
        return jsonOk(o);
    }

    // ---- 邀请对方前来 ----
    if (path == "/api/invite") {
        const play::MessageResult r = play::inviteToMe(g, strOf(in, "npc"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(g);
        o.set("text", str(r.text));
        o.set("relationDelta", num(r.relationDelta));
        return jsonOk(o);
    }

    // ---- 建号数据：全部天赋 + 五项属性 ----
    if (path == "/api/build-options") {
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("budget", num(legacy::talentBudget(legacyLoad())));
        o.set("maxPoints", num(MAX_POINTS));
        json::Array ts;
        for (const auto& t : play::allTalents()) {
            Value to(json::Object{});
            to.set("id", str(t.id));
            to.set("name", str(t.name));
            to.set("category", str(t.category));
            to.set("cost", num(t.cost));
            to.set("effect", str(t.effect));
            ts.push_back(std::move(to));
        }
        o.set("talents", Value(std::move(ts)));
        json::Array as;
        for (const auto& a : play::allocOptions({})) {
            Value ao(json::Object{});
            ao.set("id", str(a.id));
            ao.set("name", str(a.name));
            ao.set("desc", str(a.desc));
            ao.set("max", num(a.max));
            as.push_back(std::move(ao));
        }
        o.set("stats", Value(std::move(as)));
        // 先天气运：按地图种子掷出的初始资质/悟性。
        // 种子没给就现摇一个 —— 玩家可以点「换个种子」重掷。
        {
            const uint32_t seedIn = seedOf(in, 0);
            const unsigned seed =
                seedIn > 0 ? static_cast<unsigned>(seedIn)
                           : (static_cast<unsigned>(g.seed) ^
                              (static_cast<unsigned>(g.eventSeq + 1) * 2654435761u));
            const InnateRoll inn = rollInnate(seed);
            Value io(json::Object{});
            io.set("apt", num(inn.apt));
            io.set("insight", num(inn.insight));
            io.set("seed", num(static_cast<long long>(seed)));
            // 上下限一起下发：界面上要告诉玩家「还能涨到哪」
            io.set("aptMin", num(APT_MIN));
            io.set("aptMax", num(APT_MAX));
            io.set("aptRollMin", num(APT_ROLL_MIN));
            io.set("aptRollMax", num(APT_ROLL_MAX));
            io.set("insightMin", num(INSIGHT_MIN));
            io.set("insightMax", num(INSIGHT_MAX));
            io.set("insightRollMin", num(INSIGHT_ROLL_MIN));
            io.set("insightRollMax", num(INSIGHT_ROLL_MAX));
            o.set("innate", std::move(io));
        }
        return jsonOk(o);
    }

    // ---- 校验建号方案 ----
    if (path == "/api/build-check") {
        std::vector<std::string> traits;
        if (in["traits"].isArray())
            for (const auto& x : in["traits"].asArray())
                if (x.isString()) traits.push_back(x.asString());
        std::map<std::string, int> alloc;
        if (in["allocation"].isObject())
            for (const auto& [k, v] : in["allocation"].asObject())
                if (v.isNumber()) alloc[k] = v.asInt();
        const int budget = legacy::talentBudget(legacyLoad());

        const play::BuildCheck c = play::checkBuild(traits, alloc, budget);
        Value o(json::Object{});
        o.set("ok", Value(true));       // 接口本身成功
        o.set("valid", Value(c.ok));    // 方案是否合法
        o.set("error", str(c.error));
        o.set("budget", num(c.budget));
        o.set("spent", num(c.spent));
        o.set("allocSpent", num(c.allocSpent));
        o.set("remain", num(c.remain));
        o.set("traitCount", num(c.traitCount));
        return jsonOk(o);
    }

    return jsonError("未知接口：" + path, 404);
}

} // namespace nz::api
