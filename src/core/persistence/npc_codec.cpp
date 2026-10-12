// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"
#include "core/artifact.h"

namespace nz::persistence {

Value npcToJson(const Npc& npc) {
    Value npcObject = personToJson(npc);
    npcObject.set("id", str(npc.id));
    npcObject.set("bornYear", num(npc.bornYear));
    npcObject.set("sect", str(npc.sect));
    npcObject.set("personality", str(npc.personality));
    npcObject.set("goal", str(npc.goal));
    npcObject.set("alive", Value(npc.alive));
    npcObject.set("isNewborn", Value(npc.isNewborn));
    npcObject.set("deathYear", num(npc.deathYear));
    json::Array historyArray;
    for (const auto& event : npc.history) {
        Value historyObject(json::Object{});
        historyObject.set("year", num(event.year));
        historyObject.set("text", str(event.text));
        historyArray.push_back(std::move(historyObject));
    }
    npcObject.set("history", Value(std::move(historyArray)));
    // 背包与家当
    npcObject.set("bag", mapToJson(npc.bag));
    {
        json::Array gearArray;
        for (const auto& gear : npc.gear) {
            Value gearObject(json::Object{});
            gearObject.set("id", str(gear.id));
            gearObject.set("name", str(gear.name));
            gearObject.set("slot", str(gear.slot));
            gearObject.set("element", str(gear.element));
            gearObject.set("grade", num(gear.grade));
            gearObject.set("bonus", num(gear.bonus));
            gearObject.set("inscribed", Value(gear.inscribed));
            gearObject.set("price", num(gear.price));
            gearObject.set("polarity", str(gear.polarity));
            gearObject.set("craftCost", num(gear.craftCost));
            gearObject.set("crafted", Value(gear.crafted));
            gearObject.set("shape", str(gear.crafted && gear.shape.empty() ? artifactShape(gear) : gear.shape));
            json::Array dims; for (int d : gear.dimensions) dims.push_back(num(d));
            gearObject.set("dimensions", Value(std::move(dims)));
            gearArray.push_back(std::move(gearObject));
        }
        npcObject.set("gear", Value(std::move(gearArray)));
    }
    npcObject.set("equipped", strMapToJson(npc.equipped));
    { json::Array arr; for (const auto& id : npc.pillsUsed) arr.push_back(str(id));
      npcObject.set("pillsUsed", Value(std::move(arr))); }
    // 功法与流派
    npcObject.set("dao", str(npc.dao));
    npcObject.set("beast", Value(npc.beast));
    {
        json::Array sp;
        for (const auto& s : npc.spells) sp.push_back(str(s));
        npcObject.set("spells", Value(std::move(sp)));
    }
    return npcObject;
}

Npc npcFromJson(const Value& npcObject) {
    Npc npc;
    personFromJson(npcObject, npc);
    npc.id = npcObject["id"].asString();
    npc.bornYear = npcObject["bornYear"].asInt(-1);
    npc.sect = npcObject["sect"].asString();
    npc.personality = npcObject["personality"].asString();
    npc.goal = npcObject["goal"].asString("修炼");
    npc.alive = npcObject["alive"].asBool(true);
    npc.isNewborn = npcObject["isNewborn"].asBool();
    npc.deathYear = npcObject["deathYear"].asInt(-1);
    for (const auto& historyObject : npcObject["history"].asArray())
        npc.history.push_back({historyObject["year"].asInt(), historyObject["text"].asString()});
    // 背包与家当（旧档没有这些字段，缺了就是空，之后由 ensureAllNpcData 补）
    npc.bag = jsonToMap<int>(npcObject["bag"]);
    for (const auto& gearObject : npcObject["gear"].asArray()) {
        Gear gear;
        gear.id = gearObject["id"].asString();
        gear.name = gearObject["name"].asString();
        gear.slot = gearObject["slot"].asString("weapon");
        gear.element = gearObject["element"].asString();
        gear.grade = gearObject["grade"].asInt(1);
        gear.bonus = gearObject["bonus"].asNumber();
        gear.inscribed = gearObject["inscribed"].asBool();
        gear.price = gearObject["price"].asInt();
        gear.polarity = gearObject["polarity"].asString("天阳");
        gear.craftCost = gearObject["craftCost"].asInt();
        gear.crafted = gearObject["crafted"].asBool(false);
        gear.shape = gearObject["shape"].asString();
        if (gear.crafted && gear.shape.empty()) gear.shape = artifactShape(gear);
        for (const auto& d : gearObject["dimensions"].asArray()) gear.dimensions.push_back(d.asInt());
        npc.gear.push_back(gear);
    }
    npc.equipped = jsonToStrMap(npcObject["equipped"]);
    npc.pillsUsed = jsonToStringVec(npcObject["pillsUsed"]);
    npc.dao = npcObject["dao"].asString();
    npc.beast = npcObject["beast"].asBool();
    npc.spells = jsonToStringVec(npcObject["spells"]);
    return npc;
    }

} // namespace nz::persistence
