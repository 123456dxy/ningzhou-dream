// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/forge_codec.h"
#include "core/persistence/json_helpers.h"
#include "core/artifact.h"
#include "core/arts.h"

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
        for (const auto& Item : npc.gear) gearArray.push_back(mfGearToJson(Item));
        npcObject.set("gear", Value(std::move(gearArray)));
    }
    npcObject.set("equipped", strMapToJson(npc.equipped));
    { json::Array arr; for (const auto& id : npc.pillsUsed) arr.push_back(str(id));
      npcObject.set("pillsUsed", Value(std::move(arr))); }
    {
        const auto& state = npc.pillState;
        Value pills(json::Object{});
        pills.set("used", mapToJson(state.used));
        pills.set("daoExp", mapToJson(state.daoExp));
        pills.set("nodes", stringVectorToJson(state.nodes));
        pills.set("poison", num(state.poison));
        pills.set("aptPills", num(state.aptPills));
        pills.set("hpBonus", num(state.hpBonus));
        pills.set("baseHealthBonus", num(state.BaseHealthBonus));
        pills.set("heart", num(state.heart));
        pills.set("mind", num(state.mind));
        pills.set("speed", num(state.speed));
        pills.set("points", num(state.points));
        pills.set("repel", num(state.repel));
        pills.set("breakBuff", str(state.breakBuff));
        pills.set("breakthroughPillStage", num(state.BreakthroughPillStage));
        pills.set("combatPrep", mapToJson(state.combatPrep));
        pills.set("combatPillDoseCount", num(state.CombatPillDoseCount));
        pills.set("combatBuff", num(state.combatBuff));
        npcObject.set("pillState", std::move(pills));
    }
    // 功法与流派
    npcObject.set("dao", str(npc.dao));
    npcObject.set("combatProfileId", str(npc.combatProfileId));
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
    for (const auto& Input : npcObject["gear"].asArray()) {
        Gear Item = mfGearFromJson(Input);
        if (Input["slot"].isNull()) Item.slot = "weapon";
        if (Input["crafted"].isNull()) Item.crafted = false;
        npc.gear.push_back(std::move(Item));
    }
    npc.equipped = jsonToStrMap(npcObject["equipped"]);
    npc.pillsUsed = jsonToStringVec(npcObject["pillsUsed"]);
    npc.combatProfileId = npcObject["combatProfileId"].asString();

    const auto& pills = npcObject["pillState"];
    auto& state = npc.pillState;
    state.used = jsonToMap<int>(pills["used"]);
    state.daoExp = jsonToMap<int>(pills["daoExp"]);
    state.nodes = jsonToStringVec(pills["nodes"]);
    state.poison = pills["poison"].asInt();
    state.aptPills = pills["aptPills"].asInt();
    state.hpBonus = pills["hpBonus"].asInt();
    state.BaseHealthBonus = pills["baseHealthBonus"].asInt();
    state.heart = pills["heart"].asInt();
    state.mind = pills["mind"].asInt();
    state.speed = pills["speed"].asInt();
    state.points = pills["points"].asInt();
    state.repel = pills["repel"].asInt();
    state.breakBuff = pills["breakBuff"].asString();
    state.BreakthroughPillStage = pills["breakthroughPillStage"].asInt(-1);
    state.combatPrep = jsonToMap<double>(pills["combatPrep"]);
    state.CombatPillDoseCount = pills["combatPillDoseCount"].asInt(
        static_cast<int>(std::min<std::size_t>(3, state.combatPrep.size())));
    state.combatBuff = pills["combatBuff"].asNumber();
    if (pills.isNull()) {
        // 老服药记录已结算过属性；只迁移次数和资质额度，绝不重放收益。
        for (const auto& id : npc.pillsUsed) {
            const auto* recipe = findRecipe(id);
            if (!recipe || state.used.count(id)) continue;
            state.used[id] = 1;
            if (recipe->kind == "apt") state.aptPills += recipe->value;
        }
    }
    npc.dao = npcObject["dao"].asString();
    npc.beast = npcObject["beast"].asBool();
    npc.spells = jsonToStringVec(npcObject["spells"]);
    return npc;
    }

} // namespace nz::persistence
