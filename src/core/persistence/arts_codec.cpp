// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"
#include "core/arts.h"
#include "core/artifact.h"
#include <algorithm>

namespace nz::persistence {

Value artsToJson(const Arts& arts) {
    Value artsObject(json::Object{});
    artsObject.set("focus", str(arts.focus));
    {
        auto known = arts.knownSpells;
        for (const auto& id : arts.spells) if (std::find(known.begin(), known.end(), id) == known.end()) known.push_back(id);
        for (const char* id : {"skim", "refresh", "purge"}) if (std::find(known.begin(), known.end(), id) == known.end()) known.push_back(id);
        json::Array sp; for (const auto& id : known) sp.push_back(str(id)); artsObject.set("knownSpells", Value(std::move(sp)));
    }
    artsObject.set("exp", mapToJson(arts.exp));
    artsObject.set("points", num(arts.points));
    {
        json::Array nodes;
        for (const auto& n : arts.nodes) nodes.push_back(str(n));
        artsObject.set("nodes", Value(std::move(nodes)));
    }
    artsObject.set("manuals", mapToJson(arts.manuals));
    artsObject.set("main", str(arts.main));
    {
        json::Array aux;
        for (const auto& n : arts.aux) aux.push_back(str(n));
        artsObject.set("aux", Value(std::move(aux)));
    }
    {
        json::Array sp;
        for (const auto& n : arts.spells) sp.push_back(str(n));
        artsObject.set("spells", Value(std::move(sp)));
    }
    {
        json::Array bk;
        for (const auto& n : arts.bookList) bk.push_back(str(n));
        artsObject.set("books", Value(std::move(bk)));
    }
    artsObject.set("herbs", mapToJson(arts.herbs));
    artsObject.set("potions", mapToJson(arts.potions));
    artsObject.set("used", mapToJson(arts.used));
    artsObject.set("poison", num(arts.poison));
    artsObject.set("greenBottleGrade", num(arts.GreenBottleGrade));
    artsObject.set("furnace", str(arts.furnace));
    artsObject.set("durability", num(arts.durability));
    artsObject.set("materials", mapToJson(arts.materials));
    {
        json::Array gearArray;
        for (const auto& gear : arts.gear) {
            Value gearObject(json::Object{});
            gearObject.set("id", str(gear.id));
            gearObject.set("name", str(gear.name));
            gearObject.set("slot", str(gear.slot));
            gearObject.set("element", str(gear.element));
            gearObject.set("polarity", str(gear.polarity));
            gearObject.set("grade", num(gear.grade));
            gearObject.set("bonus", num(gear.bonus));
            gearObject.set("inscribed", Value(gear.inscribed));
            gearObject.set("price", num(gear.price));
            gearObject.set("craftCost", num(gear.craftCost));
            gearObject.set("crafted", Value(gear.crafted));
            gearObject.set("shape", str(gear.crafted && gear.shape.empty() ? artifactShape(gear) : gear.shape));
            json::Array dims;
            for (int dimension : gear.dimensions) dims.push_back(num(dimension));
            gearObject.set("dimensions", Value(std::move(dims)));
            gearArray.push_back(std::move(gearObject));
        }
        artsObject.set("gear", Value(std::move(gearArray)));
    }
    artsObject.set("equipped", strMapToJson(arts.equipped));
    artsObject.set("heart", num(arts.heart));
    artsObject.set("mind", num(arts.mind));
    artsObject.set("speed", num(arts.speed));
    artsObject.set("hpBonus", num(arts.hpBonus));
    artsObject.set("aptPills", num(arts.aptPills));
    artsObject.set("breakBuff", str(arts.breakBuff));
    artsObject.set("combatBuff", num(arts.combatBuff));
    artsObject.set("combatPrep", mapToJson(arts.combatPrep));
    artsObject.set("repel", num(arts.repel));
    {
        Value progressObject(json::Object{});
        progressObject.set("brew", num(arts.progress.brew));
        progressObject.set("forge", num(arts.progress.forge));
        progressObject.set("wins", num(arts.progress.wins));
        progressObject.set("quests", num(arts.progress.quests));
        progressObject.set("wealth", num(arts.progress.wealth));
        artsObject.set("progress", std::move(progressObject));
    }
    artsObject.set("quests", mapToJson(arts.quests));
    artsObject.set("visits", mapToJson(arts.visits));
    // 在途斗法随构筑保存，恢复时保留牌组、资源与敌方计划。
    if (arts.battle.has_value()) artsObject.set("battle", battleToJson(*arts.battle));
    return artsObject;
}

void artsFromJson(const Value& artsObject, Arts& arts) {
    arts.GreenBottleGrade = artsObject["greenBottleGrade"].asInt(0);
    arts.focus = artsObject["focus"].asString("气");
    arts.exp = jsonToMap<int>(artsObject["exp"]);
    arts.points = artsObject["points"].asInt();
    arts.nodes = jsonToStringVec(artsObject["nodes"]);
    arts.manuals = jsonToMap<int>(artsObject["manuals"]);
    arts.main = artsObject["main"].asString("tuna");
    arts.aux = jsonToStringVec(artsObject["aux"]);
    arts.spells = jsonToStringVec(artsObject["spells"]);
    arts.knownSpells = jsonToStringVec(artsObject["knownSpells"]);
    if (artsObject["knownSpells"].isNull()) {
        arts.knownSpells = arts.spells;
        for (const auto& sp : allSpells()) if (sp.stage == 0 && std::find(arts.knownSpells.begin(), arts.knownSpells.end(), sp.id) == arts.knownSpells.end()) arts.knownSpells.push_back(sp.id);
    }
    arts.bookList = jsonToStringVec(artsObject["books"]);
    arts.herbs = jsonToMap<int>(artsObject["herbs"]);
    arts.potions = jsonToMap<int>(artsObject["potions"]);
    arts.used = jsonToMap<int>(artsObject["used"]);
    arts.poison = artsObject["poison"].asInt();
    arts.furnace = artsObject["furnace"].asString("furnace-1");
    arts.durability = artsObject["durability"].asInt(40);
    arts.materials = jsonToMap<int>(artsObject["materials"]);
    arts.gear.clear();
    for (const auto& gearObject : artsObject["gear"].asArray()) {
        Gear gear;
        gear.id = gearObject["id"].asString();
        gear.name = gearObject["name"].asString();
        gear.slot = gearObject["slot"].asString();
        gear.element = gearObject["element"].asString();
        gear.polarity = gearObject["polarity"].asString("天阳");
        gear.grade = gearObject["grade"].asInt(1);
        gear.bonus = gearObject["bonus"].asNumber();
        gear.inscribed = gearObject["inscribed"].asBool();
        gear.price = gearObject["price"].asInt();
        gear.craftCost = gearObject["craftCost"].asInt();
        gear.crafted = gearObject["crafted"].asBool(gear.id.rfind("forge-", 0) == 0 ||
            (gear.id.rfind("gear-", 0) == 0 && gear.craftCost > 0));
        gear.shape = gearObject["shape"].asString();
        if (gear.crafted && gear.shape.empty()) gear.shape = artifactShape(gear);
        for (const auto& dimension : gearObject["dimensions"].asArray())
            gear.dimensions.push_back(dimension.asInt());
        arts.gear.push_back(gear);
    }
    arts.equipped = jsonToStrMap(artsObject["equipped"]);
    arts.heart = artsObject["heart"].asInt();
    arts.mind = artsObject["mind"].asInt();
    arts.speed = artsObject["speed"].asInt();
    arts.hpBonus = artsObject["hpBonus"].asInt();
    arts.aptPills = artsObject["aptPills"].asInt();
    arts.breakBuff = artsObject["breakBuff"].asString();
    arts.combatBuff = artsObject["combatBuff"].asNumber();
    arts.combatPrep = jsonToMap<double>(artsObject["combatPrep"]);
    arts.repel = artsObject["repel"].asInt();
    const Value& progressObject = artsObject["progress"];
    arts.progress.brew = progressObject["brew"].asInt();
    arts.progress.forge = progressObject["forge"].asInt();
    arts.progress.wins = progressObject["wins"].asInt();
    arts.progress.quests = progressObject["quests"].asInt();
    arts.progress.wealth = progressObject["wealth"].asInt();
    arts.quests = jsonToMap<int>(artsObject["quests"]);
    arts.visits = jsonToMap<int>(artsObject["visits"]);
    // 在途斗法（对应 battleToJson）。旧存档没有这个字段，缺了就是没在打。
    if (artsObject["battle"].isObject()) {
        Battle battle;
        battleFromJson(artsObject["battle"], battle);
        arts.battle = battle;
    } else {
        arts.battle.reset();
    }
}

} // namespace nz::persistence
