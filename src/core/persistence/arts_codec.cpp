// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/forge_codec.h"
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
        for (const auto& Item : arts.gear) gearArray.push_back(mfGearToJson(Item));
        artsObject.set("gear", Value(std::move(gearArray)));
    }
    artsObject.set("equipped", strMapToJson(arts.equipped));
    artsObject.set("heart", num(arts.heart));
    artsObject.set("mind", num(arts.mind));
    artsObject.set("speed", num(arts.speed));
    artsObject.set("hpBonus", num(arts.hpBonus));
    artsObject.set("baseHealthBonus", num(arts.BaseHealthBonus));
    artsObject.set("foundationHealthBonus", num(arts.FoundationHealthBonus));
    artsObject.set("lifeboundSpell", str(arts.LifeboundSpell));
    artsObject.set("lifeboundKind", str(arts.LifeboundKind));
    artsObject.set("aptPills", num(arts.aptPills));
    artsObject.set("breakBuff", str(arts.breakBuff));
    artsObject.set("breakthroughPillStage", num(arts.BreakthroughPillStage));
    artsObject.set("combatBuff", num(arts.combatBuff));
    artsObject.set("combatPrep", mapToJson(arts.combatPrep));
    artsObject.set("combatPillDoseCount", num(arts.CombatPillDoseCount));
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
    for (const auto& Input : artsObject["gear"].asArray()) arts.gear.push_back(mfGearFromJson(Input));
    arts.equipped = jsonToStrMap(artsObject["equipped"]);
    arts.heart = artsObject["heart"].asInt();
    arts.mind = artsObject["mind"].asInt();
    arts.speed = artsObject["speed"].asInt();
    arts.hpBonus = artsObject["hpBonus"].asInt();
    arts.BaseHealthBonus = artsObject["baseHealthBonus"].asInt();
    arts.FoundationHealthBonus = artsObject["foundationHealthBonus"].asInt();
    arts.LifeboundSpell = artsObject["lifeboundSpell"].asString();
    arts.LifeboundKind = artsObject["lifeboundKind"].asString();
    arts.aptPills = artsObject["aptPills"].asInt();
    arts.breakBuff = artsObject["breakBuff"].asString();
    arts.BreakthroughPillStage = artsObject["breakthroughPillStage"].asInt(-1);
    arts.combatBuff = artsObject["combatBuff"].asNumber();
    arts.combatPrep = jsonToMap<double>(artsObject["combatPrep"]);
    arts.CombatPillDoseCount = artsObject["combatPillDoseCount"].asInt(
        static_cast<int>(std::min<std::size_t>(3, arts.combatPrep.size())));
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
