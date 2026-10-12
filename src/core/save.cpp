// 宁州一梦 · C++ 版 — 存档读写实现
#include "core/save.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#endif

#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/travel.h"
#include "core/economy.h"
#include "core/npc_learning.h"
#include "core/location_services.h"
#include "core/adventure.h"
#include "core/dungeon.h"
#include "core/sect_trial.h"
#include "core/json.h"
#include "core/society.h"
#include "core/sim.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace nz {

using json::Value;

// ------------------------------------------------------------
// 小工具
// ------------------------------------------------------------
static Value num(int v) { return Value(static_cast<double>(v)); }
static Value num(double v) { return Value(v); }
static Value str(const std::string& s) { return Value(s); }

// map<string, 整数/浮点> 的互转
template <typename T>
static Value mapToJson(const std::map<std::string, T>& m) {
    Value obj(json::Object{});
    for (const auto& [k, v] : m) obj.set(k, Value(static_cast<double>(v)));
    return obj;
}
template <typename T>
static std::map<std::string, T> jsonToMap(const Value& v) {
    std::map<std::string, T> out;
    if (!v.isObject()) return out;
    for (const auto& [k, val] : v.asObject())
        if constexpr (std::is_integral_v<T>) out[k] = val.asInt();
        else out[k] = static_cast<T>(val.asNumber());
    return out;
}

// map<string,string> 专用（法器装备槽）
static Value strMapToJson(const std::map<std::string, std::string>& m) {
    Value obj(json::Object{});
    for (const auto& [k, v] : m) obj.set(k, str(v));
    return obj;
}
static std::map<std::string, std::string> jsonToStrMap(const Value& v) {
    std::map<std::string, std::string> out;
    if (!v.isObject()) return out;
    for (const auto& [k, val] : v.asObject()) out[k] = val.asString();
    return out;
}

// vector<pair<string,int>> 的互转（成就用）
static Value pairsToJson(const std::vector<std::pair<std::string, int>>& v) {
    json::Array arr;
    for (const auto& [name, year] : v) {
        Value o(json::Object{});
        o.set("name", str(name));
        o.set("year", num(year));
        arr.push_back(std::move(o));
    }
    return Value(std::move(arr));
}

// ------------------------------------------------------------
// 序列化
// ------------------------------------------------------------
static Value personToJson(const Person& p) {
    Value o(json::Object{});
    o.set("name", str(p.name));
    o.set("gender", str(p.gender));
    o.set("age", num(p.age));
    o.set("lifespan", num(p.lifespan));
    o.set("stage", num(p.stage));
    o.set("xp", num(p.xp));
    o.set("apt", num(p.apt));
    o.set("luck", num(p.luck));
    o.set("insight", num(p.insight));
    o.set("sword", num(p.sword));
    o.set("alchemy", num(p.alchemy));
    o.set("health", num(p.health));
    o.set("stones", num(p.stones));
    o.set("herbs", num(p.herbs));
    o.set("pills", num(p.pills));
    o.set("cores", num(p.cores));
    o.set("body", num(p.body));
    o.set("lifeBonus", num(p.lifeBonus));
    o.set("location", str(p.location));
    o.set("relations", mapToJson(p.relations));
    return o;
}

static void personFromJson(const Value& o, Person& p) {
    p.name = o["name"].asString();
    p.gender = o["gender"].asString("女");
    p.age = o["age"].asInt();
    p.lifespan = o["lifespan"].asInt(100);
    p.stage = o["stage"].asInt();
    p.xp = o["xp"].asInt();
    p.apt = o["apt"].asInt(55);
    p.luck = o["luck"].asInt(45);
    p.insight = o["insight"].asInt(10);
    p.sword = o["sword"].asInt(2);
    p.alchemy = o["alchemy"].asInt(5);
    p.health = o["health"].asInt(100);
    p.stones = o["stones"].asInt(100);
    p.herbs = o["herbs"].asInt(3);
    p.pills = o["pills"].asInt();
    p.cores = o["cores"].asInt();
    p.body = o["body"].asInt();
    p.lifeBonus = o["lifeBonus"].asInt();
    p.location = o["location"].asString("wuling");
    p.relations = jsonToMap<int>(o["relations"]);
}

// 斗法的序列化定义在文件后半，但 artsToJson / artsFromJson 要用到，
// 所以先声明。
static Value battleToJson(const Battle& b);
static void battleFromJson(const Value& o, Battle& b);

static Value artsToJson(const Arts& a) {
    Value o(json::Object{});
    o.set("focus", str(a.focus));
    {
        auto known = a.knownSpells;
        for (const auto& id : a.spells) if (std::find(known.begin(), known.end(), id) == known.end()) known.push_back(id);
        for (const char* id : {"skim", "refresh", "purge"}) if (std::find(known.begin(), known.end(), id) == known.end()) known.push_back(id);
        json::Array sp; for (const auto& id : known) sp.push_back(str(id)); o.set("knownSpells", Value(std::move(sp)));
    }
    o.set("exp", mapToJson(a.exp));
    o.set("points", num(a.points));
    {
        json::Array nodes;
        for (const auto& n : a.nodes) nodes.push_back(str(n));
        o.set("nodes", Value(std::move(nodes)));
    }
    o.set("manuals", mapToJson(a.manuals));
    o.set("main", str(a.main));
    {
        json::Array aux;
        for (const auto& n : a.aux) aux.push_back(str(n));
        o.set("aux", Value(std::move(aux)));
    }
    {
        json::Array sp;
        for (const auto& n : a.spells) sp.push_back(str(n));
        o.set("spells", Value(std::move(sp)));
    }
    {
        json::Array bk;
        for (const auto& n : a.bookList) bk.push_back(str(n));
        o.set("books", Value(std::move(bk)));
    }
    o.set("herbs", mapToJson(a.herbs));
    o.set("potions", mapToJson(a.potions));
    o.set("used", mapToJson(a.used));
    o.set("poison", num(a.poison));
    o.set("furnace", str(a.furnace));
    o.set("durability", num(a.durability));
    o.set("materials", mapToJson(a.materials));
    {
        json::Array gear;
        for (const auto& g : a.gear) {
            Value go(json::Object{});
            go.set("id", str(g.id));
            go.set("name", str(g.name));
            go.set("slot", str(g.slot));
            go.set("element", str(g.element));
            go.set("polarity", str(g.polarity));
            go.set("grade", num(g.grade));
            go.set("bonus", num(g.bonus));
            go.set("inscribed", Value(g.inscribed));
            go.set("price", num(g.price));
            go.set("craftCost", num(g.craftCost));
            go.set("crafted", Value(g.crafted));
            go.set("shape", str(g.crafted && g.shape.empty() ? artifactShape(g) : g.shape));
            json::Array dims;
            for (int d : g.dimensions) dims.push_back(num(d));
            go.set("dimensions", Value(std::move(dims)));
            gear.push_back(std::move(go));
        }
        o.set("gear", Value(std::move(gear)));
    }
    o.set("equipped", strMapToJson(a.equipped));
    o.set("heart", num(a.heart));
    o.set("mind", num(a.mind));
    o.set("speed", num(a.speed));
    o.set("hpBonus", num(a.hpBonus));
    o.set("aptPills", num(a.aptPills));
    o.set("breakBuff", str(a.breakBuff));
    o.set("combatBuff", num(a.combatBuff));
    o.set("combatPrep", mapToJson(a.combatPrep));
    o.set("repel", num(a.repel));
    {
        Value pr(json::Object{});
        pr.set("brew", num(a.progress.brew));
        pr.set("forge", num(a.progress.forge));
        pr.set("wins", num(a.progress.wins));
        pr.set("quests", num(a.progress.quests));
        pr.set("wealth", num(a.progress.wealth));
        o.set("progress", std::move(pr));
    }
    o.set("quests", mapToJson(a.quests));
    o.set("visits", mapToJson(a.visits));
    // 在途斗法也要进存档。
    // battleToJson / battleFromJson 早就写好了，却一直没人调用 ——
    // 结果是战斗中途存档再读档，整场斗法凭空消失，
    // 玩家可以靠存/读档白嫖逃跑（甚至躲开生死斗）。
    if (a.battle.has_value()) o.set("battle", battleToJson(*a.battle));
    return o;
}

static std::vector<std::string> jsonToStringVec(const Value& v) {
    std::vector<std::string> out;
    if (!v.isArray()) return out;
    for (const auto& x : v.asArray()) out.push_back(x.asString());
    return out;
}

static void artsFromJson(const Value& o, Arts& a) {
    a.focus = o["focus"].asString("气");
    a.exp = jsonToMap<int>(o["exp"]);
    a.points = o["points"].asInt();
    a.nodes = jsonToStringVec(o["nodes"]);
    a.manuals = jsonToMap<int>(o["manuals"]);
    a.main = o["main"].asString("tuna");
    a.aux = jsonToStringVec(o["aux"]);
    a.spells = jsonToStringVec(o["spells"]);
    a.knownSpells = jsonToStringVec(o["knownSpells"]);
    if (o["knownSpells"].isNull()) {
        a.knownSpells = a.spells;
        for (const auto& sp : allSpells()) if (sp.stage == 0 && std::find(a.knownSpells.begin(), a.knownSpells.end(), sp.id) == a.knownSpells.end()) a.knownSpells.push_back(sp.id);
    }
    a.bookList = jsonToStringVec(o["books"]);
    a.herbs = jsonToMap<int>(o["herbs"]);
    a.potions = jsonToMap<int>(o["potions"]);
    a.used = jsonToMap<int>(o["used"]);
    a.poison = o["poison"].asInt();
    a.furnace = o["furnace"].asString("furnace-1");
    a.durability = o["durability"].asInt(40);
    a.materials = jsonToMap<int>(o["materials"]);
    a.gear.clear();
    for (const auto& go : o["gear"].asArray()) {
        Gear g;
        g.id = go["id"].asString();
        g.name = go["name"].asString();
        g.slot = go["slot"].asString();
        g.element = go["element"].asString();
        g.polarity = go["polarity"].asString("天阳");
        g.grade = go["grade"].asInt(1);
        g.bonus = go["bonus"].asNumber();
        g.inscribed = go["inscribed"].asBool();
        g.price = go["price"].asInt();
        g.craftCost = go["craftCost"].asInt();
        g.crafted = go["crafted"].asBool(g.id.rfind("forge-", 0) == 0 ||
            (g.id.rfind("gear-", 0) == 0 && g.craftCost > 0));
        g.shape = go["shape"].asString();
        if (g.crafted && g.shape.empty()) g.shape = artifactShape(g);
        for (const auto& d : go["dimensions"].asArray())
            g.dimensions.push_back(d.asInt());
        a.gear.push_back(g);
    }
    a.equipped = jsonToStrMap(o["equipped"]);
    a.heart = o["heart"].asInt();
    a.mind = o["mind"].asInt();
    a.speed = o["speed"].asInt();
    a.hpBonus = o["hpBonus"].asInt();
    a.aptPills = o["aptPills"].asInt();
    a.breakBuff = o["breakBuff"].asString();
    a.combatBuff = o["combatBuff"].asNumber();
    a.combatPrep = jsonToMap<double>(o["combatPrep"]);
    a.repel = o["repel"].asInt();
    const Value& pr = o["progress"];
    a.progress.brew = pr["brew"].asInt();
    a.progress.forge = pr["forge"].asInt();
    a.progress.wins = pr["wins"].asInt();
    a.progress.quests = pr["quests"].asInt();
    a.progress.wealth = pr["wealth"].asInt();
    a.quests = jsonToMap<int>(o["quests"]);
    a.visits = jsonToMap<int>(o["visits"]);
    // 在途斗法（对应 battleToJson）。旧存档没有这个字段，缺了就是没在打。
    if (o["battle"].isObject()) {
        Battle b;
        battleFromJson(o["battle"], b);
        a.battle = b;
    } else {
        a.battle.reset();
    }
}

static Value enemyCombatToJson(const EnemyCombat& e) {
    Value o(json::Object{});
    o.set("stage", num(e.stage));
    o.set("speed", num(e.speed));
    o.set("body", num(e.body));
    o.set("mind", num(e.mind));
    o.set("mana", num(e.mana));
    o.set("maxMana", num(e.maxMana));
    o.set("shuffles", num(e.shuffles));
    o.set("played", num(e.played));
    o.set("swordQi", num(e.swordQi));
    o.set("water", num(e.water));
    o.set("soul", num(e.soul));
    o.set("charge", num(e.charge));
    o.set("lock", num(e.lock));
    o.set("carryMana", num(e.carryMana));
    o.set("manaBonus", num(e.manaBonus));
    o.set("manaPenalty", num(e.manaPenalty));
    o.set("drawNext", num(e.drawNext));
    o.set("carryCap", num(e.carryCap));
    o.set("hpCostPct", num(e.hpCostPct));
    o.set("poison", num(e.poison));
    o.set("poisonTurns", num(e.poisonTurns));
    o.set("burn", num(e.burn));
    o.set("burnTurns", num(e.burnTurns));
    o.set("swordSequence", num(e.swordSequence));
    o.set("comboDraws", num(e.comboDraws));
    o.set("buff", num(e.buff));
    o.set("reduction", num(e.reduction));
    o.set("guard", num(e.guard));
    o.set("discardPower", num(e.discardPower));
    o.set("sect", str(e.sect));
    o.set("dao", str(e.dao));
    o.set("main", str(e.main));
    o.set("last", str(e.last));
    o.set("enabled", Value(e.enabled));
    o.set("attacked", Value(e.attacked));
    { json::Array a; for (const auto& v : e.aux) a.push_back(str(v)); o.set("aux", Value(std::move(a))); }
    { json::Array a; for (const auto& v : e.nodes) a.push_back(str(v)); o.set("nodes", Value(std::move(a))); }
    { json::Array a; for (const auto& v : e.hand) a.push_back(str(v)); o.set("hand", Value(std::move(a))); }
    { json::Array a; for (const auto& v : e.draw) a.push_back(str(v)); o.set("draw", Value(std::move(a))); }
    { json::Array a; for (const auto& v : e.discard) a.push_back(str(v)); o.set("discard", Value(std::move(a))); }
    { json::Array a; for (const auto& v : e.exhausted) a.push_back(str(v)); o.set("exhausted", Value(std::move(a))); }
    { json::Array a; for (const auto& v : e.planned) a.push_back(str(v)); o.set("planned", Value(std::move(a))); }
    o.set("manuals", mapToJson(e.manuals));
    o.set("cooldowns", mapToJson(e.cooldowns));
    { json::Array a; for (const auto& c : e.deck) { Value v(json::Object{}); v.set("uid", str(c.uid)); v.set("spell", str(c.spell)); a.push_back(std::move(v)); } o.set("deck", Value(std::move(a))); }
    return o;
}

static void enemyCombatFromJson(const Value& o, EnemyCombat& e) {
    e.stage = o["stage"].asInt(0);
    e.speed = o["speed"].asInt(0);
    e.body = o["body"].asInt(0);
    e.mind = o["mind"].asInt(0);
    e.mana = o["mana"].asInt(0);
    e.maxMana = o["maxMana"].asInt(3);
    e.shuffles = o["shuffles"].asInt(0);
    e.played = o["played"].asInt(0);
    e.swordQi = o["swordQi"].asInt(0);
    e.water = o["water"].asInt(0);
    e.soul = o["soul"].asInt(0);
    e.charge = o["charge"].asInt(0);
    e.lock = o["lock"].asInt(0);
    e.carryMana = o["carryMana"].asInt(0);
    e.manaBonus = o["manaBonus"].asInt(0);
    e.manaPenalty = o["manaPenalty"].asInt(0);
    e.drawNext = o["drawNext"].asInt(0);
    e.carryCap = o["carryCap"].asInt(0);
    e.hpCostPct = o["hpCostPct"].asInt(0);
    e.poison = o["poison"].asInt(0);
    e.poisonTurns = o["poisonTurns"].asInt(0);
    e.burn = o["burn"].asInt(0);
    e.burnTurns = o["burnTurns"].asInt(0);
    e.swordSequence = o["swordSequence"].asInt(0);
    e.comboDraws = o["comboDraws"].asInt(0);
    e.buff = o["buff"].asNumber();
    e.reduction = o["reduction"].asNumber();
    e.guard = o["guard"].asNumber();
    e.discardPower = o["discardPower"].asNumber();
    e.sect = o["sect"].asString();
    e.dao = o["dao"].asString();
    e.main = o["main"].asString();
    e.last = o["last"].asString();
    e.enabled = o["enabled"].asBool();
    e.attacked = o["attacked"].asBool();
    e.aux = jsonToStringVec(o["aux"]);
    e.nodes = jsonToStringVec(o["nodes"]);
    e.hand = jsonToStringVec(o["hand"]);
    e.draw = jsonToStringVec(o["draw"]);
    e.discard = jsonToStringVec(o["discard"]);
    e.exhausted = jsonToStringVec(o["exhausted"]);
    e.planned = jsonToStringVec(o["planned"]);
    e.manuals = jsonToMap<int>(o["manuals"]);
    e.cooldowns = jsonToMap<int>(o["cooldowns"]);
    for (const auto& c : o["deck"].asArray()) e.deck.push_back({c["uid"].asString(), c["spell"].asString()});
}

static Value battleToJson(const Battle& b) {
    Value o(json::Object{});
    o.set("enemy", str(b.enemy));
    o.set("enemyCards", enemyCombatToJson(b.enemyCards));
    o.set("playerFirst", Value(b.playerFirst));
    o.set("playerSpeed", num(b.playerSpeed));
    o.set("enemySpeed", num(b.enemySpeed));
    o.set("enemyStage", num(b.enemyStage));
    o.set("enemySect", str(b.enemySect));
    o.set("enemyGender", str(b.enemyGender));
    o.set("npcId", str(b.npcId));
    o.set("quest", str(b.quest));
    o.set("kind", str(b.kind));
    o.set("huntFee", num(b.huntFee));
    o.set("wildTask", str(b.wildTask));
    o.set("dungeonSite", str(b.dungeonSite));
    o.set("dungeonNode", str(b.dungeonNode));
    o.set("lethal", Value(b.lethal));
    o.set("turn", num(b.turn));
    o.set("hp", num(b.hp));
    o.set("max", num(b.max));
    o.set("enemyHp", num(b.enemyHp));
    o.set("enemyMax", num(b.enemyMax));
    o.set("enemyPower", num(b.enemyPower));
    o.set("enemySword", num(b.enemySword));
    o.set("enemySwordCult", Value(b.enemySwordCult));
    o.set("mana", num(b.mana));
    o.set("maxMana", num(b.maxMana));
    o.set("shield", num(b.shield));
    o.set("enemyShield", num(b.enemyShield));
    o.set("burn", num(b.burn));
    o.set("swordQi", num(b.swordQi));
    o.set("buff", num(b.buff));
    o.set("reduction", num(b.reduction));
    o.set("speed", num(b.speed));
    o.set("guard", num(b.guard));
    o.set("attacked", Value(b.attacked));
    o.set("played", num(b.played));
    o.set("style", str(b.style));
    o.set("retreat", Value(b.retreat));
    o.set("won", Value(b.won));
    {
        json::Array deck;
        for (const auto& c : b.deck) {
            Value co(json::Object{});
            co.set("uid", str(c.uid));
            co.set("spell", str(c.spell));
            deck.push_back(std::move(co));
        }
        o.set("deck", Value(std::move(deck)));
    }
    auto strArr = [](const std::vector<std::string>& v) {
        json::Array arr;
        for (const auto& s : v) arr.push_back(str(s));
        return Value(std::move(arr));
    };
    o.set("hand", strArr(b.hand));
    o.set("draw", strArr(b.draw));
    o.set("discard", strArr(b.discard));
    o.set("exhausted", strArr(b.exhausted));
    o.set("shuffles", num(b.shuffles));
    o.set("intentKind", str(b.intentKind));
    o.set("intentName", str(b.intentName));
    o.set("intentScale", num(b.intentScale));
    o.set("intentRaw", num(b.intentRaw));
    o.set("intentSpell", str(b.intentSpell));
    o.set("enemyDao", str(b.enemyDao));
    { json::Array arr; for (const auto& s : b.enemySpells) arr.push_back(str(s));
      o.set("enemySpells", Value(std::move(arr))); }
    o.set("enemyBeast", Value(b.enemyBeast));
    o.set("enemyCd", mapToJson(b.enemyCd));
    o.set("enemyWeaken", num(b.enemyWeaken));
    o.set("cooldowns", mapToJson(b.cooldowns));
    o.set("poison", num(b.poison));
    o.set("poisonTurns", num(b.poisonTurns));
    o.set("playerPoison", num(b.playerPoison));
    o.set("playerPoisonTurns", num(b.playerPoisonTurns));
    o.set("burnTurns", num(b.burnTurns));
    o.set("water", num(b.water));
    o.set("soul", num(b.soul));
    o.set("charge", num(b.charge));
    o.set("lock", num(b.lock));
    o.set("carryMana", num(b.carryMana));
    o.set("manaBonus", num(b.manaBonus));
    o.set("manaPenalty", num(b.manaPenalty));
    o.set("drawNext", num(b.drawNext));
    o.set("pendingDraw", num(b.pendingDraw));
    o.set("carryCap", num(b.carryCap));
    o.set("hpCostPct", num(b.hpCostPct));
    o.set("discardPower", num(b.discardPower));
    o.set("swordSequence", num(b.swordSequence));
    o.set("comboDraws", num(b.comboDraws));
    o.set("last", str(b.last));
    {
        json::Array lg;
        for (const auto& s : b.log) lg.push_back(str(s));
        o.set("log", Value(std::move(lg)));
    }
    o.set("advKind", str(b.advKind));
    o.set("advSeq", num(b.advSeq));
    o.set("advChoice", str(b.advChoice));
    o.set("advFee", num(b.advFee));
    o.set("advGrade", num(b.advGrade));
    o.set("advBook", str(b.advBook));
    o.set("advNpcId", str(b.advNpcId));
    o.set("advStyle", str(b.advStyle));
    return o;
}

static void battleFromJson(const Value& o, Battle& b) {
    b.enemy = o["enemy"].asString();
    enemyCombatFromJson(o["enemyCards"], b.enemyCards);
    b.playerFirst = o["playerFirst"].asBool(true);
    b.playerSpeed = o["playerSpeed"].asInt();
    b.enemySpeed = o["enemySpeed"].asInt();
    b.enemyStage = o["enemyStage"].asInt();
    b.enemySect = o["enemySect"].asString();
    b.enemyGender = o["enemyGender"].asString();
    b.npcId = o["npcId"].asString();
    b.quest = o["quest"].asString();
    b.kind = o["kind"].asString("spar");
    b.huntFee = o["huntFee"].asInt();
    b.wildTask = o["wildTask"].asString();
    b.dungeonSite = o["dungeonSite"].asString();
    b.dungeonNode = o["dungeonNode"].asString();
    b.lethal = o["lethal"].asBool();
    b.turn = o["turn"].asInt();
    b.hp = o["hp"].asInt(1);
    b.max = o["max"].asInt(1);
    b.enemyHp = o["enemyHp"].asInt(1);
    b.enemyMax = o["enemyMax"].asInt(1);
    b.enemyPower = o["enemyPower"].asInt(14);
    b.enemySword = o["enemySword"].asInt();
    b.enemySwordCult = o["enemySwordCult"].asBool();
    b.mana = o["mana"].asInt();
    b.maxMana = o["maxMana"].asInt(3);
    b.shield = o["shield"].asInt();
    b.enemyShield = o["enemyShield"].asInt();
    b.burn = o["burn"].asInt();
    b.swordQi = o["swordQi"].asInt();
    b.buff = o["buff"].asNumber();
    b.reduction = o["reduction"].asNumber();
    b.speed = o["speed"].asInt();
    b.guard = o["guard"].asNumber();
    b.attacked = o["attacked"].asBool();
    b.played = o["played"].asInt();
    b.style = o["style"].asString("剑");
    b.retreat = o["retreat"].asBool();
    b.won = o["won"].asBool();
    b.deck.clear();
    for (const auto& co : o["deck"].asArray())
        b.deck.push_back({co["uid"].asString(), co["spell"].asString()});
    b.hand = jsonToStringVec(o["hand"]);
    b.draw = jsonToStringVec(o["draw"]);
    b.discard = jsonToStringVec(o["discard"]);
    b.exhausted = jsonToStringVec(o["exhausted"]);
    b.shuffles = o["shuffles"].asInt();
    b.intentKind = o["intentKind"].asString();
    b.intentName = o["intentName"].asString();
    b.intentScale = o["intentScale"].asNumber();
    b.intentRaw = o["intentRaw"].asInt();
    b.intentSpell = o["intentSpell"].asString();
    b.enemyDao = o["enemyDao"].asString();
    b.enemySpells = jsonToStringVec(o["enemySpells"]);
    b.enemyBeast = o["enemyBeast"].asBool();
    b.enemyCd = jsonToMap<int>(o["enemyCd"]);
    b.enemyWeaken = o["enemyWeaken"].asInt();
    b.cooldowns = jsonToMap<int>(o["cooldowns"]);
    b.poison = o["poison"].asInt();
    b.poisonTurns = o["poisonTurns"].asInt();
    b.playerPoison = o["playerPoison"].asInt();
    b.playerPoisonTurns = o["playerPoisonTurns"].asInt();
    b.burnTurns = o["burnTurns"].asInt();
    b.water = o["water"].asInt();
    b.soul = o["soul"].asInt();
    b.charge = o["charge"].asInt();
    b.lock = o["lock"].asInt();
    b.carryMana = o["carryMana"].asInt();
    b.manaBonus = o["manaBonus"].asInt();
    b.manaPenalty = o["manaPenalty"].asInt();
    b.drawNext = o["drawNext"].asInt();
    b.pendingDraw = o["pendingDraw"].asInt();
    b.carryCap = o["carryCap"].asInt();
    b.hpCostPct = o["hpCostPct"].asInt();
    b.discardPower = o["discardPower"].asNumber();
    b.swordSequence = o["swordSequence"].asInt();
    b.comboDraws = o["comboDraws"].asInt();
    b.last = o["last"].asString();
    b.log = jsonToStringVec(o["log"]);
    b.advKind = o["advKind"].asString();
    b.advSeq = o["advSeq"].asInt();
    b.advChoice = o["advChoice"].asString();
    b.advFee = o["advFee"].asInt();
    b.advGrade = o["advGrade"].asInt(1);
    b.advBook = o["advBook"].asString();
    b.advNpcId = o["advNpcId"].asString();
    b.advStyle = o["advStyle"].asString();
}

static Value stringVectorToJson(const vector<string>& values) {
    json::Array array;
    for (const auto& value : values) array.push_back(str(value));
    return Value(std::move(array));
}
static Value dungeonRewardToJson(const DungeonReward& reward) {
    Value part(json::Object{});
    part.set("id", str(reward.id)); part.set("kind", str(reward.kind));
    part.set("name", str(reward.name)); part.set("desc", str(reward.desc));
    part.set("spell", str(reward.spell)); part.set("relic", str(reward.relic));
    part.set("manual", str(reward.manual)); part.set("amount", num(reward.amount)); part.set("grade", num(reward.grade));
    return part;
}
static Value dungeonRewardsToJson(const vector<DungeonReward>& rewards) {
    json::Array result;
    for (const auto& reward : rewards) result.push_back(dungeonRewardToJson(reward));
    return Value(std::move(result));
}
static DungeonReward dungeonRewardFromJson(const Value& part) {
    DungeonReward reward;
    reward.id = part["id"].asString(); reward.kind = part["kind"].asString();
    reward.name = part["name"].asString(); reward.desc = part["desc"].asString();
    reward.spell = part["spell"].asString(); reward.relic = part["relic"].asString();
    reward.manual = part["manual"].asString(); reward.amount = part["amount"].asInt(); reward.grade = part["grade"].asInt(1);
    return reward;
}
static vector<DungeonReward> dungeonRewardsFromJson(const Value& value) {
    vector<DungeonReward> rewards;
    for (const auto& part : value.asArray()) rewards.push_back(dungeonRewardFromJson(part));
    return rewards;
}
static Value dungeonWorldToJson(const DungeonWorld& world) {
    Value value(json::Object{});
    value.set("initialized", Value(world.initialized));
    value.set("lastSpawnYear", num(world.lastSpawnYear)); value.set("seq", num(world.seq));
    value.set("intelMonth", num(world.intelMonth)); value.set("intelPurchases", num(world.intelPurchases));
    json::Array sites;
    for (const auto& site : world.sites) {
        Value item(json::Object{});
        item.set("id", str(site.id)); item.set("name", str(site.name)); item.set("type", str(site.type)); item.set("location", str(site.location));
        item.set("openedYear", num(site.openedYear)); item.set("expiresYear", num(site.expiresYear)); item.set("stage", num(site.stage));
        item.set("revealed", Value(site.revealed)); item.set("cleared", Value(site.cleared));
        item.set("targetKind", str(site.targetKind)); item.set("targetId", str(site.targetId)); item.set("targetName", str(site.targetName)); item.set("targetGrade", num(site.targetGrade));
        item.set("bossRewards", dungeonRewardsToJson(site.bossRewards));
        sites.push_back(std::move(item));
    }
    value.set("sites", Value(std::move(sites)));
    if (world.run) {
        const auto& run = *world.run;
        Value item(json::Object{});
        item.set("siteId", str(run.siteId)); item.set("siteName", str(run.siteName)); item.set("type", str(run.type)); item.set("location", str(run.location));
        item.set("phase", str(run.phase)); item.set("current", str(run.current)); item.set("stage", num(run.stage)); item.set("health", num(run.health));
        item.set("row", num(run.row)); item.set("seq", num(run.seq)); item.set("elapsedDays", num(run.elapsedDays));
        item.set("relics", stringVectorToJson(run.relics));
        item.set("herbs", mapToJson(run.herbs)); item.set("materials", mapToJson(run.materials)); item.set("potions", mapToJson(run.potions));
        item.set("stones", num(run.stones)); item.set("cores", num(run.cores)); item.set("insight", num(run.insight)); item.set("cleared", Value(run.cleared));
        item.set("targetKind", str(run.targetKind)); item.set("targetId", str(run.targetId)); item.set("targetName", str(run.targetName));
        item.set("targetGrade", num(run.targetGrade)); item.set("targetGranted", Value(run.targetGranted));
        item.set("bossRewards", dungeonRewardsToJson(run.bossRewards));
        item.set("bossRewardClaimed", Value(run.bossRewardClaimed));
        item.set("bossRewardTaken", run.bossRewardTaken ? dungeonRewardToJson(*run.bossRewardTaken) : Value());
        json::Array nodes, cards;
        for (const auto& node : run.nodes) {
            Value part(json::Object{});
            part.set("id", str(node.id)); part.set("type", str(node.type)); part.set("name", str(node.name)); part.set("text", str(node.text));
            part.set("row", num(node.row)); part.set("lane", num(node.lane)); part.set("next", stringVectorToJson(node.next)); part.set("visited", Value(node.visited));
            nodes.push_back(std::move(part));
        }
        for (const auto& card : run.deck) {
            Value part(json::Object{}); part.set("uid", str(card.uid)); part.set("spell", str(card.spell)); cards.push_back(std::move(part));
        }
        item.set("nodes", Value(std::move(nodes))); item.set("deck", Value(std::move(cards))); item.set("rewards", dungeonRewardsToJson(run.rewards));
        value.set("run", std::move(item));
    }
    return value;
}
static void dungeonWorldFromJson(const Value& value, DungeonWorld& world) {
    world.initialized = value["initialized"].asBool(); world.lastSpawnYear = value["lastSpawnYear"].asInt(-1); world.seq = value["seq"].asInt();
    world.intelMonth = value["intelMonth"].asInt(-1); world.intelPurchases = value["intelPurchases"].asInt();
    for (const auto& item : value["sites"].asArray()) {
        DungeonSite site;
        site.id = item["id"].asString(); site.name = item["name"].asString(); site.type = item["type"].asString(); site.location = item["location"].asString();
        site.openedYear = item["openedYear"].asInt(); site.expiresYear = item["expiresYear"].asInt(); site.stage = item["stage"].asInt();
        site.revealed = item["revealed"].asBool(true); site.cleared = item["cleared"].asBool();
        site.targetKind = item["targetKind"].asString(); site.targetId = item["targetId"].asString(); site.targetName = item["targetName"].asString(); site.targetGrade = item["targetGrade"].asInt(1);
        site.bossRewards = dungeonRewardsFromJson(item["bossRewards"]);
        world.sites.push_back(std::move(site));
    }
    if (!value["run"].isObject()) return;
    const auto& item = value["run"];
    DungeonRun run;
    run.siteId = item["siteId"].asString(); run.siteName = item["siteName"].asString(); run.type = item["type"].asString(); run.location = item["location"].asString();
    run.phase = item["phase"].asString("route"); run.current = item["current"].asString(); run.stage = item["stage"].asInt(); run.health = item["health"].asInt(100);
    run.row = item["row"].asInt(-1); run.seq = item["seq"].asInt(); run.elapsedDays = item["elapsedDays"].asInt(); run.relics = jsonToStringVec(item["relics"]);
    run.herbs = jsonToMap<int>(item["herbs"]); run.materials = jsonToMap<int>(item["materials"]); run.potions = jsonToMap<int>(item["potions"]);
    run.stones = item["stones"].asInt(); run.cores = item["cores"].asInt(); run.insight = item["insight"].asInt(); run.cleared = item["cleared"].asBool();
    run.targetKind = item["targetKind"].asString(); run.targetId = item["targetId"].asString(); run.targetName = item["targetName"].asString();
    run.targetGrade = item["targetGrade"].asInt(1); run.targetGranted = item["targetGranted"].asBool();
    run.bossRewards = dungeonRewardsFromJson(item["bossRewards"]);
    run.bossRewardClaimed = item["bossRewardClaimed"].asBool();
    if (item["bossRewardTaken"].isObject()) run.bossRewardTaken = dungeonRewardFromJson(item["bossRewardTaken"]);
    for (const auto& part : item["nodes"].asArray()) {
        DungeonNode node;
        node.id = part["id"].asString(); node.type = part["type"].asString(); node.name = part["name"].asString(); node.text = part["text"].asString();
        node.row = part["row"].asInt(); node.lane = part["lane"].asInt(); node.next = jsonToStringVec(part["next"]); node.visited = part["visited"].asBool();
        run.nodes.push_back(std::move(node));
    }
    for (const auto& part : item["deck"].asArray()) run.deck.push_back({part["uid"].asString(), part["spell"].asString()});
    run.rewards = dungeonRewardsFromJson(item["rewards"]);
    world.run = std::move(run);
}

std::string saveToJson(const Game& g) {
    Value root(json::Object{});
    root.set("format", str(SAVE_FORMAT));
    root.set("version", num(g.version));
    root.set("combatRulesVersion", num(5));
    root.set("rng", num(static_cast<double>(g.rng)));
    root.set("seed", num(static_cast<double>(g.seed)));
    root.set("year", num(g.year));
    root.set("eventSeq", num(g.eventSeq));
    root.set("npcSeq", num(g.npcSeq));
    root.set("ended", Value(g.ended));
    root.set("lifeId", str(g.lifeId));
    root.set("plan", str(g.plan));
    root.set("flags", mapToJson(g.flags));
    root.set("dungeons", dungeonWorldToJson(g.dungeons));
    {
        Value checkpoint(json::Object{});
        checkpoint.set("enabled", Value(g.checkpoint.enabled)); checkpoint.set("days", num(g.checkpoint.days));
        checkpoint.set("lastAutoYear", num(g.checkpoint.lastAutoYear)); checkpoint.set("restores", num(g.checkpoint.restores)); checkpoint.set("slot", num(g.checkpoint.slot));
        checkpoint.set("source", str(g.checkpoint.source)); checkpoint.set("snapshot", str(g.checkpoint.snapshot)); checkpoint.set("lastReason", str(g.checkpoint.lastReason));
        root.set("checkpoint", std::move(checkpoint));
    }
    if (g.ending.has_value()) {
        const Ending& e = *g.ending;
        Value o(json::Object{});
        o.set("cause", str(e.cause));
        o.set("year", num(e.year));
        o.set("age", num(e.age));
        o.set("realm", str(e.realm));
        o.set("friends", num(e.friends));
        o.set("score", num(e.score));
        o.set("legacyRewardPoints", num(e.legacyRewardPoints));
        o.set("legacyEligible", Value(e.legacyEligible));
        json::Array reasons;
        for (const auto& s : e.legacyReasons) reasons.push_back(str(s));
        o.set("legacyReasons", Value(std::move(reasons)));
        root.set("ending", std::move(o));
    }

    // 玩家
    {
        Value p = personToJson(g.player);
        p.set("talent", str(g.player.talent));
        {
            const Build& b = g.player.build;
            Value o(json::Object{});
            o.set("budget", num(b.budget)); o.set("spent", num(b.spent));
            o.set("allocation", mapToJson(b.allocation));
            json::Array traits; for (const auto& t : b.traits) traits.push_back(str(t));
            o.set("traits", Value(std::move(traits)));
            o.set("apt", num(b.apt)); o.set("luck", num(b.luck));
            o.set("insight", num(b.insight)); o.set("sword", num(b.sword));
            o.set("alchemy", num(b.alchemy)); o.set("body", num(b.body));
            o.set("lifeBonus", num(b.lifeBonus)); o.set("stones", num(b.stones));
            o.set("herbs", num(b.herbs));
            p.set("build", std::move(o));
        }
        {
            json::Array tr;
            for (const auto& t : g.player.traits) tr.push_back(str(t));
            p.set("traits", Value(std::move(tr)));
        }
        p.set("sect", str(g.player.sect));
        p.set("renown", num(g.player.renown));
        // 兽潮（全图事件）
        {
            Value t(json::Object{});
            t.set("active", Value(g.tide.active));
            t.set("startYear", num(g.tide.startYear));
            t.set("monthsLeft", num(g.tide.monthsLeft));
            t.set("severity", num(g.tide.severity));
            t.set("wavesFought", num(g.tide.wavesFought));
            t.set("wavesWon", num(g.tide.wavesWon));
            t.set("npcDeaths", num(g.tide.npcDeaths));
            t.set("warned", Value(g.tide.warned));
            json::Array fa;
            for (const auto& nm : g.tide.fallen) fa.push_back(str(nm));
            t.set("fallen", Value(std::move(fa)));
            root.set("tide", std::move(t));
        }
        // 道友往来：交情 + 打招呼记录（好感度在 relations 里，见 personToJson）
        {
            Value nb(json::Object{});
            for (const auto& [id, b] : g.player.npcBonds) {
                Value bo(json::Object{});
                bo.set("credit", num(b.credit));
                bo.set("spent", num(b.spent));
                bo.set("greeted", Value(b.greeted));
                bo.set("greetYear", num(b.greetYear));
                bo.set("talkMonth", num(b.talkMonth));
                bo.set("giftMonth", num(b.giftMonth));
                bo.set("giftsThisMonth", num(b.giftsThisMonth));
                bo.set("discussMonth", num(b.discussMonth));
                bo.set("discussCount", num(b.discussCount));
                bo.set("learnMonth", num(b.learnMonth));
                bo.set("learnedThisMonth", num(b.learnedThisMonth));
                bo.set("travelMonth", num(b.travelMonth));
                bo.set("sparMonth", num(b.sparMonth));
                nb.set(id, std::move(bo));
            }
            p.set("npcBonds", std::move(nb));
        }
        // 洞府与配给（只有玩家有，NPC 没有）
        p.set("cave", str(g.player.cave));
        p.set("caveTier", num(g.player.caveTier));
        p.set("stoneSupply", num(g.player.stoneSupply));
        p.set("qiSupply", num(g.player.qiSupply));
        p.set("supplyPaidMonth", num(g.player.supplyPaidMonth));

        p.set("highestStage", num(g.player.highestStage));
        p.set("achievements", pairsToJson(g.player.achievements));
        p.set("arts", artsToJson(g.player.arts));
        root.set("player", std::move(p));
    }

    // 世界
    {
        Value w(json::Object{});
        w.set("prosperity", num(g.world.prosperity));
        w.set("market", num(g.world.market));
        w.set("mineOwner", str(g.world.mineOwner));
        w.set("herbs", num(g.world.herbs));
        w.set("coreStock", num(g.world.coreStock));
        w.set("pillStock", num(g.world.pillStock));
        w.set("weather", str(g.world.weather));
        Value sects(json::Object{});
        for (const auto& [id, s] : g.world.sects) {
            Value so(json::Object{});
            so.set("treasury", num(s.treasury));
            so.set("influence", num(s.influence));
            so.set("members", num(s.members));
            sects.set(id, std::move(so));
        }
        w.set("sects", std::move(sects));
        root.set("world", std::move(w));
    }

    // 月历
    {
        Value c(json::Object{});
        c.set("days", num(g.clock.days));
        c.set("ageBase", num(g.clock.ageBase));
        c.set("carry", mapToJson(g.clock.carry));
        root.set("clock", std::move(c));
    }

    // 营生
    {
        Value e(json::Object{});
        json::Array books;
        for (const auto& b : g.economy.books) books.push_back(str(b));
        e.set("books", Value(std::move(books)));
        e.set("contribution", num(g.economy.contribution));
        e.set("taskCounts", mapToJson(g.economy.taskCounts));
        e.set("huntCounts", num(g.economy.huntCounts));
        e.set("commissionBrew", num(g.economy.commissionBrew));
        e.set("commissionForge", num(g.economy.commissionForge));
        e.set("cityTasks", num(g.economy.cityTasks));
        e.set("month", num(g.economy.month));
        e.set("orders", mapToJson(g.economy.orders));
        e.set("sold", mapToJson(g.economy.sold));
        e.set("marketCash", num(g.economy.marketCash));
        e.set("seq", num(g.economy.seq));
        root.set("economy", std::move(e));
    }

    // 人情
    {
        Value b(json::Object{});
        Value pr(json::Object{});
        pr.set("sect", str(g.bonds.payroll.sect));
        pr.set("lastMonth", num(g.bonds.payroll.lastMonth));
        pr.set("balance", num(g.bonds.payroll.balance));
        pr.set("credited", num(g.bonds.payroll.credited));
        b.set("payroll", std::move(pr));
        b.set("master", str(g.bonds.master));
        {
            json::Array d;
            for (const auto& x : g.bonds.disciples) d.push_back(str(x));
            b.set("disciples", Value(std::move(d)));
        }
        b.set("partner", str(g.bonds.partner));
        b.set("stipend", num(g.bonds.stipend));
        b.set("lessons", num(g.bonds.lessons));
        b.set("mentorMonth", num(g.bonds.mentorMonth));
        b.set("discipleMonth", num(g.bonds.discipleMonth));
        b.set("partnerMonth", num(g.bonds.partnerMonth));
        b.set("bondDays", num(g.bonds.bondDays));
        b.set("notoriety", num(g.bonds.notoriety));
        b.set("wildWins", num(g.bonds.wildWins));
        b.set("wildMonth", num(g.bonds.wildMonth));
        b.set("wildCount", num(g.bonds.wildCount));
        b.set("aptStory", num(g.bonds.aptStory));
        b.set("stories", mapToJson(g.bonds.stories));
        { json::Array arr; for (const auto& s : g.bonds.achievementTexts) arr.push_back(str(s));
          b.set("achievementTexts", Value(std::move(arr))); }
        { json::Array arr; for (const auto& [id, text] : g.bonds.origins) {
              Value o(json::Object{}); o.set("id", str(id)); o.set("text", str(text));
              arr.push_back(std::move(o)); }
          b.set("origins", Value(std::move(arr))); }
        root.set("bonds", std::move(b));
    }

    // 游历
    {
        Value a(json::Object{});
        a.set("since", num(g.adventure.since));
        a.set("total", num(g.adventure.total));
        a.set("seq", num(g.adventure.seq));
        a.set("events", num(g.adventure.events));
        a.set("wins", num(g.adventure.wins));
        a.set("lastKind", str(g.adventure.lastKind));
        a.set("backhillMonth", num(g.adventure.backhillMonth));
        root.set("adventure", std::move(a));
    }

    // 在途工作
    if (g.job.has_value()) {
        const Job& j = *g.job;
        Value o(json::Object{});
        o.set("actionType", str(j.actionType));
        o.set("name", str(j.name));
        o.set("plan", str(j.plan));
        o.set("total", num(j.total));
        o.set("remaining", num(j.remaining));
        o.set("started", num(j.started));
        o.set("cost", num(j.cost));
        o.set("paid", Value(j.paid));
        o.set("gift", num(j.gift));
        o.set("chance", num(j.chance));
        o.set("fee", num(j.fee));
        o.set("batch", num(j.batch));
        o.set("craftOutput", num(j.craftOutput));
        o.set("travelChecks", num(j.travelChecks));
        o.set("travelAmbushes", num(j.travelAmbushes));
        o.set("destination", str(j.destination));
        o.set("origin", str(j.origin.empty() && j.actionType == "travel" ?
                             g.player.location : j.origin));
        o.set("travelMode", str(j.travelMode.empty() && j.actionType == "travel" ?
                                 "boat" : j.travelMode));
        o.set("sect", str(j.sect));
        o.set("npcId", str(j.npcId));
        o.set("kind", str(j.kind));
        o.set("breakthroughPrepared", Value(j.breakthroughPrepared));
        o.set("breakthroughMajor", Value(j.breakthroughMajor));
        o.set("breakthroughChance", num(j.breakthroughChance));
        o.set("breakthroughFailureLoss", num(j.breakthroughFailureLoss));
        root.set("job", std::move(o));
    }

    // 待处理抉择
    if (g.pending.has_value()) {
        const Pending& p = *g.pending;
        Value o(json::Object{});
        o.set("type", str(p.type));
        o.set("title", str(p.title));
        o.set("subtitle", str(p.subtitle));
        o.set("text", str(p.text));
        o.set("npcId", str(p.npcId));
        json::Array opts;
        for (const auto& c : p.options) {
            Value co(json::Object{});
            co.set("id", str(c.id));
            co.set("name", str(c.name));
            co.set("desc", str(c.desc));
            co.set("disabled", Value(c.disabled));
            opts.push_back(std::move(co));
        }
        o.set("options", Value(std::move(opts)));
        if (p.adventure.has_value()) {
            const AdvContext& d = *p.adventure;
            Value ao(json::Object{});
            ao.set("kind", str(d.kind));
            ao.set("seq", num(d.seq));
            ao.set("stage", num(d.stage));
            ao.set("fee", num(d.fee));
            ao.set("grade", num(d.grade));
            ao.set("book", str(d.book));
            ao.set("npcId", str(d.npcId));
            ao.set("style", str(d.style));
            o.set("adventure", std::move(ao));
        }
        root.set("pending", std::move(o));
    }

    // NPC
    {
        json::Array arr;
        for (const auto& n : g.npcs) {
            Value o = personToJson(n);
            o.set("id", str(n.id));
            o.set("bornYear", num(n.bornYear));
            o.set("sect", str(n.sect));
            o.set("personality", str(n.personality));
            o.set("goal", str(n.goal));
            o.set("alive", Value(n.alive));
            o.set("isNewborn", Value(n.isNewborn));
            o.set("deathYear", num(n.deathYear));
            json::Array hist;
            for (const auto& e : n.history) {
                Value ho(json::Object{});
                ho.set("year", num(e.year));
                ho.set("text", str(e.text));
                hist.push_back(std::move(ho));
            }
            o.set("history", Value(std::move(hist)));
            // 背包与家当
            o.set("bag", mapToJson(n.bag));
            {
                json::Array gs;
                for (const auto& gr : n.gear) {
                    Value go(json::Object{});
                    go.set("id", str(gr.id));
                    go.set("name", str(gr.name));
                    go.set("slot", str(gr.slot));
                    go.set("element", str(gr.element));
                    go.set("grade", num(gr.grade));
                    go.set("bonus", num(gr.bonus));
                    go.set("inscribed", Value(gr.inscribed));
                    go.set("price", num(gr.price));
                    go.set("polarity", str(gr.polarity));
                    go.set("craftCost", num(gr.craftCost));
                    go.set("crafted", Value(gr.crafted));
                    go.set("shape", str(gr.crafted && gr.shape.empty() ? artifactShape(gr) : gr.shape));
                    json::Array dims; for (int d : gr.dimensions) dims.push_back(num(d));
                    go.set("dimensions", Value(std::move(dims)));
                    gs.push_back(std::move(go));
                }
                o.set("gear", Value(std::move(gs)));
            }
            o.set("equipped", strMapToJson(n.equipped));
            { json::Array arr; for (const auto& id : n.pillsUsed) arr.push_back(str(id));
              o.set("pillsUsed", Value(std::move(arr))); }
            // 功法与流派
            o.set("dao", str(n.dao));
            o.set("beast", Value(n.beast));
            {
                json::Array sp;
                for (const auto& s : n.spells) sp.push_back(str(s));
                o.set("spells", Value(std::move(sp)));
            }
            arr.push_back(std::move(o));
        }
        root.set("npcs", Value(std::move(arr)));
    }

    // 纪事
    {
        json::Array arr;
        for (const auto& e : g.logs) {
            Value o(json::Object{});
            o.set("id", num(e.id));
            o.set("year", num(e.year));
            o.set("age", num(e.age));
            o.set("category", str(e.category));
            o.set("text", str(e.text));
            o.set("important", Value(e.important));
            o.set("npcId", str(e.npcId.value_or("")));
            o.set("delta", str(e.delta));
            o.set("days", num(e.days.value_or(0)));
            arr.push_back(std::move(o));
        }
        root.set("logs", Value(std::move(arr)));
    }

    return root.dump(1);
}

// ------------------------------------------------------------
// 反序列化
// ------------------------------------------------------------
namespace {

bool saveInteger(const Value& v, double low, double high) {
    return v.isNumber() && std::isfinite(v.asNumber()) &&
           v.asNumber() == std::floor(v.asNumber()) &&
           v.asNumber() >= low && v.asNumber() <= high;
}

// 存档来自文件或导入，不能把字段静默变为默认值后继续操作坏状态。
bool validSaveNumbers(const Value& v, const std::string& field = "") {
    static const std::set<std::string> floating = {
        "market", "bonus", "combatBuff", "buff", "reduction", "guard",
        "intentScale", "chance", "breakthroughChance", "breakthroughFailureLoss", "discardPower"
    };
    if (v.isNumber()) {
        if (field == "rng" || field == "seed") return saveInteger(v, 0, 4294967295.0);
        const double n = v.asNumber();
        return std::isfinite(n) && std::fabs(n) <= 1000000000.0 &&
               (floating.count(field) || n == std::floor(n));
    }
    if (v.isObject()) {
        for (const auto& [key, child] : v.asObject()) {
            if (field == "carry" || field == "combatPrep") {
                if (!child.isNumber() || !std::isfinite(child.asNumber()) ||
                    child.asNumber() < 0 || child.asNumber() > 1000) return false;
            } else if (!validSaveNumbers(child, key)) return false;
        }
    } else if (v.isArray()) {
        for (const auto& child : v.asArray()) if (!validSaveNumbers(child, field)) return false;
    }
    return true;
}

bool validPerson(const Person& p) {
    return !p.name.empty() && p.stage >= 0 && p.stage < static_cast<int>(REALMS().size()) &&
           p.age >= 0 && p.age <= 100000 && p.lifespan > 0 && p.lifespan <= 100000 &&
           p.health >= 0 && p.health <= 100 && p.xp >= 0 && p.stones >= 0 &&
           p.herbs >= 0 && p.pills >= 0 && p.cores >= 0 && p.body >= 0 &&
           p.apt > 0 && p.insight >= 0 && p.sword >= 0 && p.alchemy >= 0 && findLoc(p.location);
}

bool nonnegativeMap(const map<string, int>& values) {
    for (const auto& [key, value] : values) if (value < 0) return false;
    return true;
}

bool saveObjectFieldTypes(const Value& o,
                          std::initializer_list<std::pair<const char*, json::Type>> fields) {
    for (const auto& [name, type] : fields) {
        const auto it = o.asObject().find(name);
        if (it != o.asObject().end() && it->second.type() != type &&
            !(it->second.isNull() && (std::string(name) == "battle" ||
                                      std::string(name) == "job" || std::string(name) == "pending" ||
                                      std::string(name) == "ending" || std::string(name) == "run"))) return false;
    }
    return true;
}

bool dungeonRewardShapeValid(const Value& reward, bool modern) {
    if (!reward.isObject()) return false;
    for (const char* name : {"id", "kind", "name", "desc", "spell", "relic"}) if (!reward[name].isString()) return false;
    if (modern ? !reward["manual"].isString() : (!reward["manual"].isNull() && !reward["manual"].isString())) return false;
    return saveInteger(reward["amount"], 0, 100000000) && saveInteger(reward["grade"], 1, 6);
}
bool dungeonRewardArrayValid(const Value& value, bool modern) {
    if (!value.isArray() || value.asArray().size() > 64) return false;
    for (const auto& part : value.asArray()) if (!dungeonRewardShapeValid(part, modern)) return false;
    return true;
}
bool dungeonShapesValid(const Value& world, bool modern) {
    using json::Type;
    if (!world.isObject()) return false;
    if (!saveObjectFieldTypes(world, {{"initialized", Type::Bool}, {"lastSpawnYear", Type::Number}, {"seq", Type::Number},
            {"intelMonth", Type::Number}, {"intelPurchases", Type::Number}, {"sites", Type::Array}, {"run", Type::Object}})) return false;
    for (const auto& site : world["sites"].asArray()) {
        if (!site.isObject()) return false;
        for (const char* name : {"id", "name", "type", "location", "targetKind", "targetId", "targetName"}) if (!site[name].isString()) return false;
        for (const char* name : {"openedYear", "expiresYear", "stage", "targetGrade"}) if (!site[name].isNumber()) return false;
        for (const char* name : {"revealed", "cleared"}) if (!site[name].isBool()) return false;
        if ((modern || !site["bossRewards"].isNull()) && !dungeonRewardArrayValid(site["bossRewards"], modern)) return false;
    }
    const auto& run = world["run"];
    if (!run.isObject()) return true;
    for (const char* name : {"siteId", "siteName", "type", "location", "phase", "current", "targetKind", "targetId", "targetName"}) if (!run[name].isString()) return false;
    for (const char* name : {"stage", "health", "row", "seq", "elapsedDays", "stones", "cores", "insight", "targetGrade"}) if (!run[name].isNumber()) return false;
    for (const char* name : {"cleared", "targetGranted"}) if (!run[name].isBool()) return false;
    if ((modern || !run["bossRewardClaimed"].isNull()) && !run["bossRewardClaimed"].isBool()) return false;
    if ((modern || !run["bossRewards"].isNull()) && !dungeonRewardArrayValid(run["bossRewards"], modern)) return false;
    if (!run["bossRewardTaken"].isNull() && !dungeonRewardShapeValid(run["bossRewardTaken"], modern)) return false;
    for (const char* name : {"nodes", "deck", "relics", "rewards"}) if (!run[name].isArray()) return false;
    for (const char* name : {"herbs", "materials", "potions"}) {
        if (!run[name].isObject()) return false;
        for (const auto& [id, count] : run[name].asObject()) if (!saveInteger(count, 0, 100000)) return false;
    }
    for (const auto& id : run["relics"].asArray()) if (!id.isString()) return false;
    for (const auto& card : run["deck"].asArray()) if (!card.isObject() || !card["uid"].isString() || !card["spell"].isString()) return false;
    for (const auto& node : run["nodes"].asArray()) {
        if (!node.isObject()) return false;
        for (const char* name : {"id", "type", "name", "text"}) if (!node[name].isString()) return false;
        if (!node["row"].isNumber() || !node["lane"].isNumber() || !node["visited"].isBool() || !node["next"].isArray()) return false;
        for (const auto& id : node["next"].asArray()) if (!id.isString()) return false;
    }
    if (!dungeonRewardArrayValid(run["rewards"], modern)) return false;
    return true;
}

bool saveShapesValid(const Value& root) {
    using json::Type;
    if (!saveObjectFieldTypes(root, {{"rng", Type::Number}, {"seed", Type::Number},
            {"year", Type::Number}, {"version", Type::Number}, {"ended", Type::Bool},
            {"clock", Type::Object}, {"flags", Type::Object}, {"npcs", Type::Array},
            {"logs", Type::Array}, {"job", Type::Object}, {"pending", Type::Object},
            {"ending", Type::Object}, {"dungeons", Type::Object}, {"checkpoint", Type::Object}})) return false;
    if (!root["dungeons"].isNull() && !dungeonShapesValid(root["dungeons"], root["combatRulesVersion"].asInt(1) >= 5)) return false;
    if (!root["checkpoint"].isNull() && !saveObjectFieldTypes(root["checkpoint"], {{"enabled", Type::Bool}, {"days", Type::Number},
            {"lastAutoYear", Type::Number}, {"restores", Type::Number}, {"slot", Type::Number}, {"source", Type::String}, {"snapshot", Type::String}, {"lastReason", Type::String}})) return false;
    const Value& p = root["player"];
    for (const char* field : {"age", "lifespan", "stage", "xp", "apt", "health", "stones", "herbs"})
        if (!p[field].isNumber()) return false;
    if (!p["location"].isString()) return false;
    if (!saveObjectFieldTypes(p, {{"arts", Type::Object}, {"traits", Type::Array},
            {"relations", Type::Object}, {"build", Type::Object}, {"npcBonds", Type::Object},
            {"caveTier", Type::Number}, {"stoneSupply", Type::Number}, {"qiSupply", Type::Number}})) return false;
    if (!saveObjectFieldTypes(p["arts"], {{"battle", Type::Object}, {"exp", Type::Object},
            {"herbs", Type::Object}, {"potions", Type::Object}, {"materials", Type::Object},
            {"manuals", Type::Object}, {"gear", Type::Array}, {"equipped", Type::Object},
            {"knownSpells", Type::Array}, {"spells", Type::Array}})) return false;
    for (const char* key : {"knownSpells", "spells"})
        for (const auto& id : p["arts"][key].asArray()) if (!id.isString()) return false;
    for (const auto& gear : p["arts"]["gear"].asArray())
        if (!gear.isObject() || !saveObjectFieldTypes(gear, {{"crafted", Type::Bool}, {"shape", Type::String}})) return false;
    for (const auto& [id, bond] : p["npcBonds"].asObject()) {
        if (!bond.isObject() || !saveObjectFieldTypes(bond, {{"credit", Type::Number}, {"spent", Type::Number},
            {"greeted", Type::Bool}, {"greetYear", Type::Number}, {"talkMonth", Type::Number},
            {"giftMonth", Type::Number}, {"giftsThisMonth", Type::Number}, {"discussMonth", Type::Number},
            {"discussCount", Type::Number}, {"travelMonth", Type::Number}, {"sparMonth", Type::Number},
            {"learnMonth", Type::Number}, {"learnedThisMonth", Type::Number}})) return false;
    }
    const Value& battle = p["arts"]["battle"];
    if (root["job"].isObject() && !saveObjectFieldTypes(root["job"], {
            {"origin", Type::String}, {"travelMode", Type::String}, {"craftOutput", Type::Number},
            {"travelChecks", Type::Number}, {"travelAmbushes", Type::Number}})) return false;
    if (battle.isObject() && !saveObjectFieldTypes(battle, {
            {"deck", Type::Array}, {"hand", Type::Array}, {"draw", Type::Array},
            {"discard", Type::Array}, {"exhausted", Type::Array},
            {"cooldowns", Type::Object}, {"enemyCd", Type::Object}, {"enemyCards", Type::Object},
            {"playerFirst", Type::Bool}, {"enemyStage", Type::Number},
            {"playerSpeed", Type::Number}, {"enemySpeed", Type::Number}, {"dungeonSite", Type::String}, {"dungeonNode", Type::String}})) return false;
    const auto& enemy = battle["enemyCards"];
    if (enemy.isObject() && !saveObjectFieldTypes(enemy, {{"enabled", Type::Bool},
            {"attacked", Type::Bool}, {"sect", Type::String}, {"dao", Type::String}, {"main", Type::String},
            {"last", Type::String}, {"aux", Type::Array}, {"nodes", Type::Array},
            {"deck", Type::Array}, {"hand", Type::Array}, {"draw", Type::Array},
            {"discard", Type::Array}, {"exhausted", Type::Array}, {"planned", Type::Array},
            {"cooldowns", Type::Object}, {"manuals", Type::Object}})) return false;
    for (const char* key : {"hand", "draw", "discard", "exhausted", "planned", "aux", "nodes"})
        for (const auto& id : enemy[key].asArray()) if (!id.isString()) return false;
    for (const auto& card : enemy["deck"].asArray())
        if (!card.isObject() || !card["uid"].isString() || !card["spell"].isString()) return false;
    for (const char* key : {"stage", "speed", "body", "mind", "mana", "maxMana", "shuffles", "played",
            "swordQi", "water", "soul", "charge", "lock", "carryMana", "manaBonus", "manaPenalty", "drawNext",
            "carryCap", "hpCostPct", "poison", "poisonTurns", "burn", "burnTurns", "swordSequence", "comboDraws",
            "buff", "reduction", "guard", "discardPower"})
        if (!enemy[key].isNull() && !enemy[key].isNumber()) return false;
    for (const char* key : {"manuals", "cooldowns"})
        for (const auto& [id, value] : enemy[key].asObject()) if (!value.isNumber()) return false;
    for (const auto& n : root["npcs"].asArray()) {
        if (!n.isObject() || !n["id"].isString() || !n["name"].isString() ||
            !n["stage"].isNumber() || !n["location"].isString()) return false;
        for (const auto& gear : n["gear"].asArray())
            if (!gear.isObject() || !saveObjectFieldTypes(gear, {{"crafted", Type::Bool}, {"shape", Type::String}})) return false;
    }
    return true;
}

std::string saveStateError(const Game& g) {
    if (!validPerson(g.player)) return "角色属性、境界或地点无效";
    if (!g.ended && (g.player.health == 0 || g.player.age >= g.player.lifespan))
        return "死亡角色尚未结算";
    if (!findPlan(g.plan) || (!g.player.sect.empty() && !findSect(g.player.sect)) ||
        (!g.player.cave.empty() && !findLoc(g.player.cave))) return "角色计划、宗门或洞府无效";
    if (g.player.caveTier < 0 || g.player.caveTier > 2 ||
        g.player.stoneSupply < 0 || g.player.stoneSupply > 3 ||
        g.player.qiSupply < 0 || g.player.qiSupply > g.player.caveTier + 1) return "洞府或配给档位无效";
    if (g.clock.days < 0 || g.year != g.clock.days / 360 ||
        g.player.age != g.clock.ageBase + g.year) return "日历、年份与年龄不一致";
    if (g.world.herbs < 0 || g.world.coreStock < 0 || g.world.pillStock < 0 ||
        !findSect(g.world.mineOwner) || g.world.market <= 0 || g.world.market > 10)
        return "世界库存、灵脉归属或行情无效";
    const Arts& a = g.player.arts;
    auto gearError = [](const vector<Gear>& gears, const map<string, string>& equipped) -> string {
        std::set<string> ids;
        for (const auto& gear : gears) {
            if (gear.id.empty() || !ids.insert(gear.id).second || gear.grade < 1 || gear.grade > 6 ||
                gear.bonus < 0 || gear.bonus > 10 ||
                (gear.slot != "weapon" && gear.slot != "armor" && gear.slot != "accessory") ||
                (!gear.shape.empty() && artifactShape(gear).empty())) return "法宝标识、器型或品阶无效";
        }
        for (const auto& [slot, id] : equipped) {
            if (id.empty()) continue;
            const auto gear = std::find_if(gears.begin(), gears.end(), [&](const Gear& x) { return x.id == id && x.slot == slot; });
            if (gear == gears.end()) return "已装备法宝引用无效";
        }
        return "";
    };
    if (const auto error = gearError(a.gear, a.equipped); !error.empty()) return error;
    if (!nonnegativeMap(a.herbs) || !nonnegativeMap(a.potions) ||
        !nonnegativeMap(a.materials) || !nonnegativeMap(a.manuals) || a.points < 0 ||
        a.poison < 0 || a.combatBuff < 0) return "物品、功法或道法数量无效";
    std::set<string> known;
    for (const auto& id : a.knownSpells)
        if (!battleSpell(id) || !known.insert(id).second) return "已领悟神通无效或重复";
    for (const auto& [id, bond] : g.player.npcBonds) {
        if (id.empty() || bond.credit < 0 || bond.spent < 0 || bond.giftsThisMonth < 0 ||
            bond.discussCount < 0 || bond.discussCount > 2 || bond.learnedThisMonth < 0 ||
            bond.learnedThisMonth > 3 || bond.greetYear < -1 || bond.greetYear > g.year)
            return "同道交往记录无效";
        for (const int month : {bond.talkMonth, bond.giftMonth, bond.discussMonth,
                               bond.travelMonth, bond.sparMonth, bond.learnMonth})
            if (month < -1 || month > g.clock.days / 30) return "同道交往月份无效";
    }
    std::set<string> ids;
    for (const auto& n : g.npcs) {
        if (n.id.empty() || !ids.insert(n.id).second || !validPerson(n) ||
            (!n.sect.empty() && !findSect(n.sect)) || !nonnegativeMap(n.bag)) return "同道数据无效";
        if (const auto error = gearError(n.gear, n.equipped); !error.empty()) return error;
    }
    if (g.npcs.size() > 10000 || g.logs.size() > 10000) return "人口或纪事数据过大";
    if (g.job.has_value()) {
        const Job& j = *g.job;
        static const std::set<string> jobs = {
            "plan", "travel", "join", "breakthrough", "social", "socialElapsed",
            "task", "commission", "hunt", "mine", "arena", "quest", "brew", "forge",
            "learn", "meditate", "expedition", "bond", "wild"
            , "npcLearn", "locationService", "story"
        };
        if (!jobs.count(j.actionType) || j.total <= 0 || j.total > 360000 ||
            j.remaining <= 0 || j.remaining > j.total || j.started < 0 ||
            j.started > g.clock.days || j.cost < 0 || j.fee < 0 || j.gift < 0 || j.gift > 1000 ||
            j.batch <= 0 || j.batch > 1000 ||
            j.chance < 0 || j.chance > 1 || j.breakthroughChance < 0 ||
            j.breakthroughChance > 1 || j.breakthroughFailureLoss < 0 ||
            j.breakthroughFailureLoss > 1) return "在途安排参数无效";
        if (j.craftOutput < 1 || j.craftOutput > 4 || j.travelChecks < 0 ||
            j.travelChecks > j.total - j.remaining || j.travelAmbushes < 0 || j.travelAmbushes > 1)
            return "炼制产出或赶路记录无效";
        if (j.actionType == "brew" &&
            ((j.gift != 1 && j.gift != 3 && j.gift != 5 && j.gift != 10) ||
             j.batch != j.gift)) return "炼丹炉数无效或与批次数不一致";
        if (j.actionType == "social" && j.kind == "gift" && j.gift > g.player.herbs)
            return "赠礼安排的药材不足";
        if (j.actionType == "social" && (!j.paid || (j.kind != "talk" && j.kind != "gift") ||
            (j.kind == "gift" && j.gift <= 0))) return "社交安排无效";
        if (j.actionType == "npcLearn") {
            bool matched = false;
            for (const auto& lesson : npcLessons(g, j.npcId))
                if (lesson.kind == j.kind && lesson.id == j.plan && j.paid &&
                    j.cost == lesson.stoneCost && j.gift == lesson.creditCost && j.total == lesson.days)
                    matched = true;
            if (!matched) return "授业内容、费用或用时无效";
        }
        if (j.actionType == "story") {
            bool matched = false;
            for (const auto& story : data::StoryDef_TABLE()) {
                if (story.id != j.plan) continue;
                const int choice = j.kind == "0" ? 0 : j.kind == "1" ? 1 : -1;
                matched = choice >= 0 && j.paid && j.cost == 0 && j.total == story.days &&
                    g.clock.days - j.started == j.total - j.remaining &&
                    j.origin == g.player.location && storyError(g, &story).empty() &&
                    storyChoiceError(g, story, choice).empty();
            }
            if (!matched) return "在途因缘的内容、选项或用时无效";
        }
        if (j.actionType == "locationService") {
            bool matched = false;
            for (const auto& service : locationServices(g, j.origin))
                if (service.id == j.kind && service.targetTab.empty() && j.paid &&
                    j.cost == service.cost && j.total == service.days) matched = true;
            if (!matched || j.origin != g.player.location) return "当地事务内容或出发地点无效";
        }
        if ((j.actionType == "plan" && !findPlan(j.plan)) ||
            (j.actionType == "travel" && !findLoc(j.destination)) ||
            (j.actionType == "join" && !findSect(j.sect))) return "在途安排的目标无效";
        if (j.actionType == "travel" &&
            (!findLoc(j.origin) || j.origin != g.player.location ||
             !travelModeKnown(j.travelMode) ||
             (j.travelMode == "flight" && g.player.stage < 3) ||
             (j.travelMode == "walk" && (j.origin == "sea" || j.destination == "sea"))))
            return "赶路出发地或方式无效";
    }
    if (a.battle.has_value()) {
        const Battle& b = *a.battle;
        if (b.max <= 0 || b.enemyMax <= 0 || b.hp <= 0 || b.hp > b.max ||
            b.enemyHp <= 0 || b.enemyHp > b.enemyMax || b.mana < 0 || b.maxMana < 0 ||
            b.mana > 100000 || b.maxMana > 100000 ||
            b.manaBonus < 0 || b.manaBonus > 100000 || b.manaPenalty < 0 || b.manaPenalty > 100000 ||
            b.carryMana < 0 || b.carryMana > 100000 || b.carryCap < 0 || b.carryCap > 100000 ||
            b.drawNext < 0 || b.drawNext > 256 || b.pendingDraw < 0 || b.pendingDraw > 256 ||
            b.enemyPower < 0 || b.turn < 0 || b.buff < 0 || b.reduction < 0 || b.guard < 0 ||
            b.deck.size() > 256 || b.hand.size() > 8 || b.played < 0 || b.played > 24 ||
            b.discardPower < 0 || b.discardPower > 0.3 + 1e-9 ||
            b.swordSequence < 0 || b.swordSequence > 24 || b.comboDraws < 0 || b.comboDraws > 2)
            return "斗法参数无效";
        if (b.enemyStage < 0 || b.enemyStage > 12 || b.playerSpeed < 0 || b.enemySpeed < 0)
            return "斗法境界或遁速无效";
        if (b.poison < 0 || b.poison > BATTLE_POISON_CAP || b.poisonTurns < 0 || b.poisonTurns > 3 ||
            b.burn < 0 || b.burn > BATTLE_BURN_CAP || b.burnTurns < 0 || b.burnTurns > 3 ||
            b.playerPoison < 0 || b.playerPoison > BATTLE_BURN_CAP || b.playerPoisonTurns < 0 || b.playerPoisonTurns > 3)
            return "斗法持续伤害状态无效";
        for (const auto& [id, cooldown] : b.cooldowns)
            if ((!battleSpell(id) && !equippedArtifactAbility(g.player, id)) || cooldown < 0 || cooldown > 24)
                return "神通或法宝冷却无效";
        std::set<string> cards;
        for (const auto& c : b.deck) {
            if (c.uid.empty() || !battleSpell(c.spell) || !cards.insert(c.uid).second) return "斗法牌组无效";
        }
        std::set<string> placed;
        for (const auto* pile : {&b.hand, &b.draw, &b.discard, &b.exhausted}) {
            for (const auto& id : *pile) {
                if (!cards.count(id) || !placed.insert(id).second) return "斗法牌堆重复或引用未知牌";
            }
        }
        const EnemyCombat& e = b.enemyCards;
        std::set<string> enemyCards, enemyPlaced, planned;
        if (e.deck.size() > 256 || e.hand.size() > 8 || e.planned.size() > 8) return "对手牌组过大";
        for (const auto& c : e.deck) {
            const auto* spell = battleSpell(c.spell);
            if (c.uid.empty() || !spell || !enemyCards.insert(c.uid).second ||
                (e.enabled && spell->stage > e.stage)) return "对手牌组无效";
        }
        for (const auto* pile : {&e.hand, &e.draw, &e.discard, &e.exhausted})
            for (const auto& id : *pile)
                if (!enemyCards.count(id) || !enemyPlaced.insert(id).second) return "对手牌堆引用无效或重复";
        if (enemyPlaced.size() != enemyCards.size()) return "对手牌组存在未归入牌堆的神通";
        for (const auto& id : e.planned)
            if (!planned.insert(id).second ||
                (std::find(e.hand.begin(), e.hand.end(), id) == e.hand.end() &&
                 std::find(e.draw.begin(), e.draw.end(), id) == e.draw.end())) return "对手行动计划无效";
        if (e.enabled) {
            if (b.enemyBeast || e.deck.empty() || e.stage != b.enemyStage || e.speed != b.enemySpeed ||
                e.stage < 0 || e.stage > 12 || e.mana < 0 || e.mana > 30 || e.maxMana <= 0 || e.maxMana > 30 ||
                e.carryMana < 0 || e.carryCap < 0 || e.carryCap > 30 || e.carryMana > e.carryCap ||
                e.played < 0 || e.played > 24 || e.drawNext < 0 || e.drawNext > 256 ||
                e.swordSequence < 0 || e.swordSequence > 24 || e.comboDraws < 0 || e.comboDraws > 2 ||
                e.swordQi < 0 || e.swordQi > 8 || e.water < 0 || e.water > 6 || e.soul < 0 || e.soul > 6 ||
                e.poison < 0 || e.poison > BATTLE_POISON_CAP || e.charge < 0 || e.charge > 4 || e.lock < 0 || e.lock > 2 ||
                e.poisonTurns < 0 || e.poisonTurns > 3 || e.burn < 0 || e.burn > BATTLE_BURN_CAP || e.burnTurns < 0 || e.burnTurns > 3 ||
                e.buff < 0 || e.buff > 0.3 + 1e-9 || e.reduction < 0 || e.reduction > 0.2 + 1e-9 ||
                e.guard < 0 || e.guard > 0.1 + 1e-9 || e.discardPower < 0 || e.discardPower > 0.3 + 1e-9)
                return "对手斗法参数无效";
            for (const int value : {e.speed, e.body, e.mind, e.shuffles, e.manaBonus, e.manaPenalty, e.hpCostPct})
                if (value < 0 || value > 100000) return "对手斗法属性无效";
            for (const auto& [id, cooldown] : e.cooldowns)
                if (!battleSpell(id) || cooldown < 0 || cooldown > 24) return "对手神通冷却无效";
            for (const auto& [id, level] : e.manuals) {
                ManualInfo info;
                if (!findManualInfo(id, info) || level < 1 || level > 5) return "对手功法无效";
            }
            if (!e.manuals.count(e.main)) return "对手主修功法未领悟";
            std::set<string> aux, nodes;
            for (const auto& id : e.aux)
                if (!e.manuals.count(id) || !aux.insert(id).second) return "对手辅修功法无效";
            for (const auto& id : e.nodes)
                if (!findNode(id) || !nodes.insert(id).second) return "对手悟道节点无效";
        }
    }
    if (g.pending.has_value()) {
        const Pending& p = *g.pending;
        if (p.type != "sectInvitation" && p.type != "valley" && p.type != "adventure")
            return "抉择类型无效";
        if (p.options.empty() || (p.type == "adventure" && !p.adventure.has_value()))
            return "抉择内容不完整";
        std::set<string> choices;
        for (const auto& o : p.options)
            if (o.id.empty() || !choices.insert(o.id).second) return "抉择选项无效";
    }
    if ((g.pending.has_value() && a.battleActive()) ||
        (g.ended && (g.job.has_value() || g.pending.has_value() || a.battleActive())))
        return "存档包含互相冲突的行动状态";
    if (const string error = dungeonStateError(g); !error.empty()) return error;
    if (const string error = sectTrialStateError(g); !error.empty()) return error;
    if (a.battle && ((!a.battle->dungeonSite.empty() || !a.battle->dungeonNode.empty()) && !g.dungeons.run))
        return "秘境斗法缺少在途探索状态";
    const auto& checkpoint = g.checkpoint;
    if (checkpoint.days < 0 || checkpoint.days > g.clock.days || checkpoint.lastAutoYear < 0 ||
        checkpoint.lastAutoYear % 10 != 0 || checkpoint.lastAutoYear > g.year || checkpoint.restores < 0 ||
        checkpoint.slot < 1 || checkpoint.slot > 4 || (checkpoint.source != "auto" && checkpoint.source != "manual") ||
        checkpoint.snapshot.size() > 8u * 1024u * 1024u) return "死亡回档记录无效";
    if (checkpoint.enabled != !checkpoint.snapshot.empty()) return "启用死亡回档必须包含完整且非嵌套的存档快照";
    if (checkpoint.enabled) {
        string parseError;
        const Value snapshot = Value::parse(checkpoint.snapshot, &parseError);
        // 先拒绝任何内层快照，再做一次普通读档验证；不会递归遍历嵌套快照。
        if (!parseError.empty() || !snapshot.isObject() || snapshot["lifeId"].asString() != g.lifeId ||
            !snapshot["checkpoint"]["snapshot"].asString().empty() || snapshot["checkpoint"]["enabled"].asBool())
            return "死亡回档快照嵌套、格式错误或属于其他人生";
        Game restored;
        if (!loadFromJson(checkpoint.snapshot, restored, &parseError) || restored.lifeId != g.lifeId || restored.ended ||
            restored.clock.days != checkpoint.days || restored.clock.days > g.clock.days)
            return "死亡回档快照内容无效或晚于当前进度";
    }
    return "";
}

} // namespace

bool loadFromJson(const std::string& text, Game& out, std::string* err) {
    if (err) err->clear();
    if (text.size() > 16u * 1024u * 1024u) { if (err) *err = "存档数据超过大小限制"; return false; }
    std::string perr;
    Value root = Value::parse(text, &perr);
    if (!perr.empty() || !root.isObject()) {
        if (err) *err = perr.empty() ? "存档不是合法的 JSON 对象" : perr;
        return false;
    }
    if (root["format"].asString() != SAVE_FORMAT) {
        if (err) *err = "存档格式不匹配（期望 " + std::string(SAVE_FORMAT) + "）";
        return false;
    }
    if (!root["player"].isObject() || !root["world"].isObject() ||
        !root["player"]["name"].isString() || !root["player"]["stage"].isNumber() ||
        !validSaveNumbers(root) || !saveShapesValid(root) ||
        (root["version"].isNumber() && !saveInteger(root["version"], 1, VERSION)) ||
        (!root["combatRulesVersion"].isNull() && !saveInteger(root["combatRulesVersion"], 1, 5)) ||
        (root["year"].isNumber() && !saveInteger(root["year"], 0, 100000)) ||
        (root["clock"]["days"].isNumber() && !saveInteger(root["clock"]["days"], 0, 36000000))) {
        if (err) *err = "存档缺少角色或世界数据，或字段数值无效";
        return false;
    }

    Game g;
    g.version = root["version"].asInt(VERSION);
    g.rng = static_cast<uint32_t>(root["rng"].asNumber(g.rng));
    g.seed = static_cast<uint32_t>(root["seed"].asNumber(g.seed));
    g.year = root["year"].asInt();
    g.eventSeq = root["eventSeq"].asInt();
    g.npcSeq = root["npcSeq"].asInt();
    g.ended = root["ended"].asBool();
    g.lifeId = root["lifeId"].asString();
    g.plan = root["plan"].asString("cultivate");
    g.flags = jsonToMap<int>(root["flags"]);
    dungeonWorldFromJson(root["dungeons"], g.dungeons);
    if (root["checkpoint"].isObject()) {
        const auto& checkpoint = root["checkpoint"];
        g.checkpoint.enabled = checkpoint["enabled"].asBool(); g.checkpoint.days = checkpoint["days"].asInt();
        g.checkpoint.lastAutoYear = checkpoint["lastAutoYear"].asInt(); g.checkpoint.restores = checkpoint["restores"].asInt(); g.checkpoint.slot = checkpoint["slot"].asInt(4);
        g.checkpoint.source = checkpoint["source"].asString("auto"); g.checkpoint.snapshot = checkpoint["snapshot"].asString(); g.checkpoint.lastReason = checkpoint["lastReason"].asString();
    }
    if (root["ending"].isObject()) {
        const Value& o = root["ending"];
        Ending e;
        e.cause = o["cause"].asString();
        e.year = o["year"].asInt(); e.age = o["age"].asInt();
        e.realm = o["realm"].asString(); e.friends = o["friends"].asInt();
        e.score = o["score"].asInt();
        e.legacyRewardPoints = o["legacyRewardPoints"].asInt();
        e.legacyEligible = o["legacyEligible"].asBool();
        e.legacyReasons = jsonToStringVec(o["legacyReasons"]);
        g.ending = std::move(e);
    }

    {
        const Value& p = root["player"];
        personFromJson(p, g.player);
    // 兽潮（旧档没有这个字段，缺了就是「没有兽潮」）
    if (root["tide"].isObject()) {
        const Value& t = root["tide"];
        g.tide.active = t["active"].asBool();
        g.tide.startYear = t["startYear"].asInt(-1);
        g.tide.monthsLeft = t["monthsLeft"].asInt();
        g.tide.severity = t["severity"].asInt(1);
        g.tide.wavesFought = t["wavesFought"].asInt();
        g.tide.wavesWon = t["wavesWon"].asInt();
        g.tide.npcDeaths = t["npcDeaths"].asInt();
        g.tide.warned = t["warned"].asBool();
        g.tide.fallen.clear();
        for (const auto& nm : t["fallen"].asArray())
            g.tide.fallen.push_back(nm.asString());
    }
    // 洞府与配给
    // 道友往来（旧档没有这个字段，缺了就是空）
    g.player.npcBonds.clear();
    if (p["npcBonds"].isObject()) {
        for (const auto& [id, bo] : p["npcBonds"].asObject()) {
            NpcBond b;
            b.credit = bo["credit"].asInt();
            b.spent = bo["spent"].asInt();
            b.greeted = bo["greeted"].asBool();
            b.greetYear = bo["greetYear"].isNumber() ? bo["greetYear"].asInt() : -1;
            b.talkMonth = bo["talkMonth"].asInt(-1);
            b.giftMonth = bo["giftMonth"].asInt(-1);
            b.giftsThisMonth = bo["giftsThisMonth"].asInt(0);
            b.discussMonth = bo["discussMonth"].asInt(-1);
            b.discussCount = bo["discussCount"].asInt(0);
            b.learnMonth = bo["learnMonth"].asInt(-1);
            b.learnedThisMonth = bo["learnedThisMonth"].asInt(0);
            b.travelMonth = bo["travelMonth"].asInt(-1);
            b.sparMonth = bo["sparMonth"].asInt(-1);
            g.player.npcBonds[id] = b;
        }
    }
    g.player.cave = p["cave"].isString() ? p["cave"].asString() : "";

    g.player.stoneSupply = p["stoneSupply"].isNumber() ? p["stoneSupply"].asInt() : 0;
    g.player.qiSupply = p["qiSupply"].isNumber() ? p["qiSupply"].asInt() : 0;
    g.player.caveTier = p["caveTier"].isNumber() ? p["caveTier"].asInt() :
        clampInt(g.player.qiSupply - 1, 0, 2);
    g.player.supplyPaidMonth =
        p["supplyPaidMonth"].isNumber() ? p["supplyPaidMonth"].asInt() : -1;

        g.player.talent = p["talent"].asString("sword");
        g.player.traits = jsonToStringVec(p["traits"]);
        if (p["build"].isObject()) {
            const Value& o = p["build"];
            Build& b = g.player.build;
            b.budget = o["budget"].asInt(BASE_POINTS); b.spent = o["spent"].asInt();
            b.allocation = jsonToMap<int>(o["allocation"]);
            b.traits = jsonToStringVec(o["traits"]);
            b.apt = o["apt"].asInt(55); b.luck = o["luck"].asInt(45);
            b.insight = o["insight"].asInt(10); b.sword = o["sword"].asInt(2);
            b.alchemy = o["alchemy"].asInt(5); b.body = o["body"].asInt();
            b.lifeBonus = o["lifeBonus"].asInt(); b.stones = o["stones"].asInt(100);
            b.herbs = o["herbs"].asInt(3);
        } else {
            // 旧版漏存建号结果，以本世天赋补出可展示的预算与起始属性。
            try { g.player.build = buildCharacter(g.player.traits, {}, MAX_POINTS, g.seed); }
            catch (const std::invalid_argument&) { g.player.build.traits = g.player.traits; }
        }
        g.player.sect = p["sect"].asString();
        g.player.renown = p["renown"].asInt();
        g.player.highestStage = p["highestStage"].asInt();
        if (p["achievements"].isArray()) {
            for (const auto& o : p["achievements"].asArray())
                g.player.achievements.emplace_back(o["name"].asString(),
                                                   o["year"].asInt());
        }
        artsFromJson(p["arts"], g.player.arts);
        if (!p["build"].isObject() && g.player.lifeBonus == 0 &&
            g.player.stage >= 0 && g.player.stage < static_cast<int>(REALMS().size())) {
            // 旧 Player 重复定义 lifeBonus，基类序列化永远写零。
            g.player.lifeBonus = g.player.lifespan - realmOf(g.player.stage).life;
        }
    }

    {
        const Value& w = root["world"];
        g.world.prosperity = w["prosperity"].asInt(72);
        g.world.market = w["market"].asNumber(1.0);
        g.world.mineOwner = w["mineOwner"].asString("jinhong");
        g.world.herbs = w["herbs"].asInt(3200);
        g.world.coreStock = w["coreStock"].asInt(120);
        g.world.pillStock = w["pillStock"].asInt(90);
        g.world.weather = w["weather"].asString("春和景明");
        for (const auto& [id, so] : w["sects"].asObject()) {
            SectState s;
            s.treasury = so["treasury"].asInt(3000);
            s.influence = so["influence"].asInt(50);
            s.members = so["members"].asInt();
            g.world.sects[id] = s;
        }
    }

    {
        const Value& c = root["clock"];
        g.clock.days = c["days"].asInt(g.year * 360);
        g.clock.ageBase = c["ageBase"].asInt(g.player.age - g.clock.days / 360);
        g.clock.carry = jsonToMap<double>(c["carry"]);
    }

    {
        const Value& e = root["economy"];
        g.economy.books = jsonToStringVec(e["books"]);
        g.economy.contribution = e["contribution"].asInt();
        g.economy.taskCounts = jsonToMap<int>(e["taskCounts"]);
        g.economy.huntCounts = e["huntCounts"].asInt();
        g.economy.commissionBrew = e["commissionBrew"].asInt();
        g.economy.commissionForge = e["commissionForge"].asInt();
        g.economy.cityTasks = e["cityTasks"].asInt();
        g.economy.month = e["month"].asInt(-1);
        g.economy.orders = jsonToMap<int>(e["orders"]);
        g.economy.sold = jsonToMap<int>(e["sold"]);
        g.economy.marketCash = e["marketCash"].asInt(400);
        g.economy.seq = e["seq"].asInt();
    }

    {
        const Value& b = root["bonds"];
        const Value& pr = b["payroll"];
        g.bonds.payroll.sect = pr["sect"].asString();
        g.bonds.payroll.lastMonth = pr["lastMonth"].asInt();
        g.bonds.payroll.balance = pr["balance"].asInt();
        g.bonds.payroll.credited = pr["credited"].asInt();
        g.bonds.master = b["master"].asString();
        g.bonds.disciples = jsonToStringVec(b["disciples"]);
        g.bonds.partner = b["partner"].asString();
        g.bonds.stipend = b["stipend"].asInt(-1);
        g.bonds.lessons = b["lessons"].asInt();
        g.bonds.mentorMonth = b["mentorMonth"].asInt(-1);
        g.bonds.discipleMonth = b["discipleMonth"].asInt(-1);
        g.bonds.partnerMonth = b["partnerMonth"].asInt(-1);
        g.bonds.bondDays = b["bondDays"].asInt();
        g.bonds.notoriety = b["notoriety"].asInt();
        g.bonds.wildWins = b["wildWins"].asInt();
        g.bonds.wildMonth = b["wildMonth"].asInt(-1);
        g.bonds.wildCount = b["wildCount"].asInt();
        g.bonds.aptStory = b["aptStory"].asInt();
        g.bonds.stories = jsonToMap<int>(b["stories"]);
        g.bonds.achievementTexts = jsonToStringVec(b["achievementTexts"]);
        for (const auto& o : b["origins"].asArray())
            g.bonds.origins.emplace_back(o["id"].asString(), o["text"].asString());
    }

    {
        const Value& a = root["adventure"];
        g.adventure.since = a["since"].asInt();
        g.adventure.total = a["total"].asInt();
        g.adventure.seq = a["seq"].asInt();
        g.adventure.events = a["events"].asInt();
        g.adventure.wins = a["wins"].asInt();
        g.adventure.lastKind = a["lastKind"].asString();
        g.adventure.backhillMonth = a["backhillMonth"].asInt(-1);
    }

    if (root["job"].isObject()) {
        const Value& o = root["job"];
        Job j;
        j.actionType = o["actionType"].asString();
        j.name = o["name"].asString();
        j.plan = o["plan"].asString();
        j.total = o["total"].asInt();
        j.remaining = o["remaining"].asInt();
        j.started = o["started"].asInt();
        j.cost = o["cost"].asInt();
        j.paid = o["paid"].asBool(true);
        j.gift = o["gift"].asInt();
        j.chance = o["chance"].asNumber(1.0);
        j.fee = o["fee"].asInt();
        j.batch = o["batch"].asInt(1);
        j.craftOutput = o["craftOutput"].asInt(1);
        j.travelChecks = o["travelChecks"].asInt();
        j.travelAmbushes = o["travelAmbushes"].asInt();
        j.destination = o["destination"].asString();
        j.origin = o["origin"].asString();
        j.travelMode = o["travelMode"].asString();
        if (j.actionType == "travel") {
            if (o["origin"].isNull()) j.origin = g.player.location;
            if (o["travelMode"].isNull()) j.travelMode = "boat";
        }
        j.sect = o["sect"].asString();
        j.npcId = o["npcId"].asString();
        j.kind = o["kind"].asString();
        j.breakthroughPrepared = o["breakthroughPrepared"].asBool();
        j.breakthroughMajor = o["breakthroughMajor"].asBool();
        j.breakthroughChance = o["breakthroughChance"].asNumber();
        j.breakthroughFailureLoss = o["breakthroughFailureLoss"].asNumber();
        if (j.actionType == "brew" &&
            (!root["player"]["build"].isObject() || o["batch"].isNull())) {
            // 旧版实际炉数借存 gift，batch 始终是默认 1；旧单炉档 gift 可能缺失。
            if (j.gift == 0) j.gift = 1;
            if (j.gift == 1 || j.gift == 3 || j.gift == 5 || j.gift == 10) j.batch = j.gift;
        }
        g.job = j;
    }

    if (root["pending"].isObject()) {
        const Value& o = root["pending"];
        Pending p;
        p.type = o["type"].asString();
        p.title = o["title"].asString();
        p.subtitle = o["subtitle"].asString();
        p.text = o["text"].asString();
        p.npcId = o["npcId"].asString();
        for (const auto& co : o["options"].asArray()) {
            ChoiceOption c;
            c.id = co["id"].asString();
            c.name = co["name"].asString();
            c.desc = co["desc"].asString();
            c.disabled = co["disabled"].asBool();
            p.options.push_back(c);
        }
        if (o["adventure"].isObject()) {
            const Value& ao = o["adventure"];
            AdvContext d;
            d.kind = ao["kind"].asString();
            d.seq = ao["seq"].asInt();
            d.stage = ao["stage"].asInt();
            d.fee = ao["fee"].asInt();
            d.grade = ao["grade"].asInt(1);
            d.book = ao["book"].asString();
            d.npcId = ao["npcId"].asString();
            d.style = ao["style"].asString();
            p.adventure = d;
        }
        g.pending = p;
    }

    for (const auto& o : root["npcs"].asArray()) {
        Npc n;
        personFromJson(o, n);
        n.id = o["id"].asString();
        n.bornYear = o["bornYear"].asInt(-1);
        n.sect = o["sect"].asString();
        n.personality = o["personality"].asString();
        n.goal = o["goal"].asString("修炼");
        n.alive = o["alive"].asBool(true);
        n.isNewborn = o["isNewborn"].asBool();
        n.deathYear = o["deathYear"].asInt(-1);
        for (const auto& ho : o["history"].asArray())
            n.history.push_back({ho["year"].asInt(), ho["text"].asString()});
        // 背包与家当（旧档没有这些字段，缺了就是空，之后由 ensureAllNpcData 补）
        n.bag = jsonToMap<int>(o["bag"]);
        for (const auto& go : o["gear"].asArray()) {
            Gear gr;
            gr.id = go["id"].asString();
            gr.name = go["name"].asString();
            gr.slot = go["slot"].asString("weapon");
            gr.element = go["element"].asString();
            gr.grade = go["grade"].asInt(1);
            gr.bonus = go["bonus"].asNumber();
            gr.inscribed = go["inscribed"].asBool();
            gr.price = go["price"].asInt();
            gr.polarity = go["polarity"].asString("天阳");
            gr.craftCost = go["craftCost"].asInt();
            gr.crafted = go["crafted"].asBool(false);
            gr.shape = go["shape"].asString();
            if (gr.crafted && gr.shape.empty()) gr.shape = artifactShape(gr);
            for (const auto& d : go["dimensions"].asArray()) gr.dimensions.push_back(d.asInt());
            n.gear.push_back(gr);
        }
        n.equipped = jsonToStrMap(o["equipped"]);
        n.pillsUsed = jsonToStringVec(o["pillsUsed"]);
        n.dao = o["dao"].asString();
        n.beast = o["beast"].asBool();
        n.spells = jsonToStringVec(o["spells"]);
        g.npcs.push_back(n);
    }

    for (const auto& o : root["logs"].asArray()) {
        LogEntry e;
        e.id = o["id"].asInt();
        e.year = o["year"].asInt();
        e.age = o["age"].asInt();
        e.category = o["category"].asString();
        e.text = o["text"].asString();
        e.important = o["important"].asBool();
        const std::string npcId = o["npcId"].asString();
        if (!npcId.empty()) e.npcId = npcId;
        e.delta = o["delta"].asString();
        if (o["days"].isNumber()) e.days = o["days"].asInt();
        g.logs.push_back(e);
    }

    // 旧版本两个聚气神通共用 gather，实际执行总会选到持续聚气。
    // 带版本标记的新档保留基础聚气；旧档只迁移已有牌，不重置斗法。
    if (root["combatRulesVersion"].asInt(1) < 2) {
        auto migrateSpell = [](string& id) { if (id == "gather") id = "qiFlow"; };
        for (auto& id : g.player.arts.spells) migrateSpell(id);
        for (auto& id : g.player.arts.knownSpells) migrateSpell(id);
        if (g.player.arts.battle) {
            Battle& b = *g.player.arts.battle;
            for (auto& card : b.deck) migrateSpell(card.spell);
            for (auto& id : b.enemySpells) migrateSpell(id);
            migrateSpell(b.intentSpell);
            for (auto* cooldowns : {&b.cooldowns, &b.enemyCd}) {
                const auto old = cooldowns->find("gather");
                if (old != cooldowns->end()) {
                    (*cooldowns)["qiFlow"] = old->second;
                    cooldowns->erase(old);
                }
            }
            b.maxMana = battleEnergy(g.player);
        }
    }
    if (root["combatRulesVersion"].asInt(1) < 3) {
        auto& a = g.player.arts;
        const int legacySwordExp = static_cast<int>(std::min(10000LL, std::max(0LL, static_cast<long long>(g.player.sword) - 2) * 2));
        if (legacySwordExp > a.exp["剑"]) artGain(g.player, "剑", legacySwordExp - a.exp["剑"]);
        for (const auto& sp : allSpells()) if (sp.stage <= g.player.stage && std::find(a.knownSpells.begin(), a.knownSpells.end(), sp.id) == a.knownSpells.end()) a.knownSpells.push_back(sp.id);
        if (a.battle) {
            auto& b = *a.battle;
            const auto& savedBattle = root["player"]["arts"]["battle"];
            if (savedBattle["enemyStage"].isNull()) {
                b.enemyStage = b.enemyCards.enabled ? b.enemyCards.stage : g.player.stage;
                for (const auto& n : g.npcs) if (n.id == b.npcId) { b.enemyStage = n.stage; b.enemySect = n.sect; b.enemyGender = n.gender; break; }
            }
            if (savedBattle["playerSpeed"].isNull()) b.playerSpeed = artEscapeSpeed(g.player);
            if (savedBattle["enemySpeed"].isNull()) b.enemySpeed = b.enemyCards.enabled ? b.enemyCards.speed : 20 + b.enemyStage * 8;
            for (const char* spell : {"strike", "defend"}) {
                int copies = 0; for (const auto& c : b.deck) if (c.spell == spell) ++copies;
                for (; copies < battleBasicCopies(); ++copies) {
                    string uid = "migration-" + string(spell) + "-" + std::to_string(copies);
                    while (std::any_of(b.deck.begin(), b.deck.end(), [&](const Card& c) { return c.uid == uid; })) uid += "-";
                    b.deck.push_back({uid, spell}); b.draw.push_back(uid);
                }
            }
        }
        std::set<string> migratedKnown;
        a.knownSpells.erase(std::remove_if(a.knownSpells.begin(), a.knownSpells.end(),
            [&](const string& id) { return !migratedKnown.insert(id).second; }), a.knownSpells.end());
    }
    if (root["combatRulesVersion"].asInt(1) < 4 && g.job && g.job->actionType == "locationService" && g.job->kind == "auction") {
        if (g.job->origin == "tianji") {
            const int refund = std::max(0, g.job->cost);
            g.player.stones = static_cast<int>(std::min(1000000000LL, static_cast<long long>(g.player.stones) + refund));
            g.job.reset();
            logEvent(g, "self", "旧版天机珍品会已改为定向材料秘境情报，取消在途购买并退回" + std::to_string(refund) + "灵石。", true);
        } else if (g.job->origin == "dongshi") {
            const int refund = std::max(0, g.job->cost - 24);
            g.player.stones = static_cast<int>(std::min(1000000000LL, static_cast<long long>(g.player.stones) + refund));
            g.job->cost = 24;
            if (refund > 0) logEvent(g, "self", "伴月楼在途竞购调整为低阶材料交易，退回旧报价差额" + std::to_string(refund) + "灵石。", true);
        }
    }
    if (root["combatRulesVersion"].asInt(1) < 5) migrateDungeonRewards(g);
    const std::string stateError = saveStateError(g);
    if (!stateError.empty()) {
        if (err) *err = "存档数据无效：" + stateError;
        return false;
    }
    // 历史版本漏存了结局；恢复摘要时不能再次发放奖励或重复落笔纪事。
    if (g.ended && !g.ending.has_value()) {
        Ending e;
        e.cause = "此世已结束"; e.year = g.year; e.age = g.player.age;
        e.realm = realmName(g.player.stage);
        for (const auto& [id, value] : g.player.relations) {
            if (value >= 45) for (const auto& n : g.npcs) if (n.id == id) { ++e.friends; break; }
        }
        e.score = g.player.highestStage * 120 + g.player.renown * 4 +
                  g.player.insight + g.player.stones / 4;
        g.ending = std::move(e);
    }

    // 读档后补齐可能缺失的初始化状态
    ensureArts(g);
    ensureEconomy(g);
    ensureSociety(g);
    ensureAdventure(g);
    ensureDungeons(g);

    // 老版在途斗法没有保存任务来源，将能够确定的上下文迁移回来。
    if (g.player.arts.battle.has_value()) {
        Battle& b = *g.player.arts.battle;
        const Value& saved = root["player"]["arts"]["battle"];
        if (b.kind == "hunt" && saved["huntFee"].isNull()) {
            if (b.enemy == "任务妖兽") b.huntFee = 32 + (g.player.stage / 3) * 30;
            else if (b.enemy == "兽潮先锋" || b.enemy == "妖将" || b.enemy == "妖王麾下")
                b.kind = "tide";
        }
        if (b.kind == "wild" && b.advKind.empty() && saved["wildTask"].isNull()) {
            b.wildTask = "legacy";
            for (const auto& task : data::WildTaskDef_TABLE())
                if (task.name == b.enemy) { b.wildTask = task.id; break; }
        }
    }

    out = std::move(g);
    return true;
}

// ------------------------------------------------------------
// 文件读写
// ------------------------------------------------------------
bool saveToFile(const Game& g, const std::string& path, std::string* err) {
    if (err) err->clear();
    const auto target = std::filesystem::u8path(path);
    std::error_code ec;
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) { if (err) *err = "无法创建存档目录：" + ec.message(); return false; }
    }

    const std::string text = saveToJson(g);
    // 先写临时文件再改名，避免中途失败损坏原档
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(std::filesystem::u8path(tmp), std::ios::binary);
        if (!f) {
            if (err) *err = "无法写入 " + tmp;
            return false;
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();
        if (f.fail()) {
            if (err) *err = "写入过程中出错";
            return false;
        }
    }
#ifdef _WIN32
    const bool replaced = MoveFileExW(std::filesystem::u8path(tmp).c_str(), target.c_str(),
                                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(std::filesystem::u8path(tmp), target, ec);
    const bool replaced = !ec;
#endif
    // 替换失败保留原档；先删除旧档会使「安全临时写入」失去意义。
    if (!replaced) {
        if (err) *err = "无法把临时存档改名为 " + path;
        return false;
    }
    return true;
}

bool loadFromFile(const std::string& path, Game& out, std::string* err) {
    if (err) err->clear();
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) {
        if (err) *err = "找不到存档文件 " + path;
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    if (f.bad()) { if (err) *err = "读取存档过程中出错"; return false; }
    return loadFromJson(ss.str(), out, err);
}

// ------------------------------------------------------------
// 存档位
// ------------------------------------------------------------
namespace {
std::string& saveDirOverrideRef() { static std::string v; return v; }
}
void setSaveDirOverride(const std::string& dir) { saveDirOverrideRef() = dir; }

std::string defaultSaveDir() {
    // 多人服务端：当前玩家的存档目录
    if (!saveDirOverrideRef().empty()) return saveDirOverrideRef();
    // 与可执行文件同级的 saves/；拿不到路径时退回当前目录
#ifdef _WIN32
    wchar_t buf[32768];
    DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n > 0 && n < 32768) {
        const auto utf8 = (std::filesystem::path(std::wstring(buf, n)).parent_path() / L"saves").u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
    return "saves";
#else
    return "saves";
#endif
}

std::string savePathFor(int slot) {
    return defaultSaveDir() + "/slot" + std::to_string(slot) + ".json";
}

std::vector<SaveSlotInfo> listSaveSlots() {
    std::vector<SaveSlotInfo> out;
    for (int slot = 1; slot <= 4; ++slot) {
        SaveSlotInfo info;
        info.slot = slot;
        info.automatic = slot == 4;
        info.path = savePathFor(slot);
        std::ifstream f(std::filesystem::u8path(info.path), std::ios::binary | std::ios::ate);
        if (!f) {
            out.push_back(info);
            continue;
        }
        info.bytes = static_cast<long long>(f.tellg());
        f.close();
        info.exists = true;

        // 读出头部的角色摘要（读整份档简单可靠）
        Game g;
        std::string err;
        if (loadFromFile(info.path, g, &err)) {
            info.name = g.player.name;
            info.realm = realmName(g.player.stage);
            info.year = g.year;
            info.age = g.player.age;
        }
        out.push_back(info);
    }
    return out;
}

} // namespace nz
