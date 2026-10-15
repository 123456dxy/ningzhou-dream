// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/foundation/state_limits.h"
#include "core/combat/elemental_sword.h"
#include "core/combat/fire_growth.h"
#include <cmath>
#include <set>

namespace nz::persistence {

bool saveInteger(const Value& value, double low, double high) {
    return value.isNumber() && std::isfinite(value.asNumber()) &&
           value.asNumber() == std::floor(value.asNumber()) &&
           value.asNumber() >= low && value.asNumber() <= high;
}

bool validSaveNumbers(const Value& value, const std::string& field) {
    static const std::set<std::string> floating = {
        "market", "bonus", "combatBuff", "buff", "reduction", "guard",
        "intentScale", "chance", "breakthroughChance", "breakthroughFailureLoss", "discardPower"
    };
    if (value.isNumber()) {
        if (field == "rng" || field == "seed") return saveInteger(value, 0, 4294967295.0);
        const double npc = value.asNumber();
        return std::isfinite(npc) && std::fabs(npc) <= StateLimits::MaxPersistedInteger &&
               (floating.count(field) || npc == std::floor(npc));
    }
    if (value.isObject()) {
        for (const auto& [key, child] : value.asObject()) {
            if (field == "carry" || field == "combatPrep") {
                if (!child.isNumber() || !std::isfinite(child.asNumber()) ||
                    child.asNumber() < 0 || child.asNumber() > 1000) return false;
            } else if (!validSaveNumbers(child, key)) return false;
        }
    } else if (value.isArray()) {
        for (const auto& child : value.asArray()) if (!validSaveNumbers(child, field)) return false;
    }
    return true;
}

namespace {
bool saveObjectFieldTypes(const Value& object,
                          std::initializer_list<std::pair<const char*, json::Type>> fields) {
    for (const auto& [name, type] : fields) {
        const auto it = object.asObject().find(name);
        if (it != object.asObject().end() && it->second.type() != type &&
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
    if (run.asObject().count("layoutVersion") && !saveInteger(run["layoutVersion"], 1, 2)) return false;
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

} // namespace

bool saveShapesValid(const Value& root) {
    const auto MarketCycle = root["economy"].asObject().find("marketCycleIndex");
    if (MarketCycle != root["economy"].asObject().end() &&
        !saveInteger(MarketCycle->second, -1, StateLimits::MaxPersistedInteger)) return false;
    if (!tideShapeValid(root["tide"])) return false;
    if (!mfGardenShapeValid(root["garden"])) return false;
    if (!root["player"]["arts"]["greenBottleGrade"].isNull() &&
        !saveInteger(root["player"]["arts"]["greenBottleGrade"], 0, 6)) return false;
    using json::Type;
    if (!saveObjectFieldTypes(root, {{"rng", Type::Number}, {"seed", Type::Number},
            {"year", Type::Number}, {"version", Type::Number}, {"ended", Type::Bool},
            {"clock", Type::Object}, {"flags", Type::Object}, {"npcs", Type::Array},
            {"logs", Type::Array}, {"job", Type::Object}, {"pending", Type::Object},
            {"ending", Type::Object}, {"dungeons", Type::Object}, {"checkpoint", Type::Object}, {"garden", Type::Object}})) return false;
    if (!root["dungeons"].isNull() && !dungeonShapesValid(root["dungeons"], root["combatRulesVersion"].asInt(1) >= 5)) return false;
    if (!root["checkpoint"].isNull() && !saveObjectFieldTypes(root["checkpoint"], {{"enabled", Type::Bool}, {"days", Type::Number},
            {"lastAutoYear", Type::Number}, {"restores", Type::Number}, {"slot", Type::Number}, {"source", Type::String}, {"snapshot", Type::String}, {"lastReason", Type::String}})) return false;
    const Value& playerObject = root["player"];
    for (const char* field : {"age", "lifespan", "stage", "xp", "apt", "health", "stones", "herbs"})
        if (!playerObject[field].isNumber()) return false;
    if (!playerObject["location"].isString()) return false;
    if (!saveObjectFieldTypes(playerObject, {{"portraitId", Type::String}, {"arts", Type::Object}, {"traits", Type::Array},
            {"relations", Type::Object}, {"build", Type::Object}, {"npcBonds", Type::Object},
            {"caveTier", Type::Number}, {"stoneSupply", Type::Number}, {"qiSupply", Type::Number}})) return false;
    if (!saveObjectFieldTypes(playerObject["arts"], {{"battle", Type::Object}, {"exp", Type::Object},
            {"herbs", Type::Object}, {"potions", Type::Object}, {"materials", Type::Object},
            {"manuals", Type::Object}, {"gear", Type::Array}, {"equipped", Type::Object},
            {"knownSpells", Type::Array}, {"spells", Type::Array}, {"greenBottleGrade", Type::Number}})) return false;
    for (const char* key : {"knownSpells", "spells"})
        for (const auto& id : playerObject["arts"][key].asArray()) if (!id.isString()) return false;
    for (const auto& gear : playerObject["arts"]["gear"].asArray())
        if (!gear.isObject() || !saveObjectFieldTypes(gear, {{"crafted", Type::Bool}, {"shape", Type::String}})) return false;
    for (const auto& [id, bond] : playerObject["npcBonds"].asObject()) {
        if (!bond.isObject() || !saveObjectFieldTypes(bond, {{"credit", Type::Number}, {"spent", Type::Number},
            {"greeted", Type::Bool}, {"greetYear", Type::Number}, {"talkMonth", Type::Number},
            {"giftMonth", Type::Number}, {"giftsThisMonth", Type::Number}, {"discussMonth", Type::Number},
            {"discussCount", Type::Number}, {"travelMonth", Type::Number}, {"sparMonth", Type::Number},
            {"learnMonth", Type::Number}, {"learnedThisMonth", Type::Number}})) return false;
    }
    for (const auto& npc : root["npcs"].asArray())
        if (!npc.isObject() || !saveObjectFieldTypes(npc, {{"combatProfileId",Type::String}})) return false;
    const Value& battle = playerObject["arts"]["battle"];
    if (root["job"].isObject() && !saveObjectFieldTypes(root["job"], {
            {"origin", Type::String}, {"travelMode", Type::String}, {"craftOutput", Type::Number},
            {"travelChecks", Type::Number}, {"travelAmbushes", Type::Number}})) return false;
    if (battle.isObject() && !saveObjectFieldTypes(battle, {
            {"tideEventId", Type::String}, {"tideBattlefield", Type::Number}, {"tideDaysCharged", Type::Number},
            {"deck", Type::Array}, {"hand", Type::Array}, {"draw", Type::Array}, {"beastActions", Type::Array},
            {"discard", Type::Array}, {"exhausted", Type::Array},
            {"cooldowns", Type::Object}, {"enemyCd", Type::Object}, {"enemyCards", Type::Object},
            {"playerFirst", Type::Bool}, {"enemyStage", Type::Number}, {"elementalSwordManaReturns", Type::Number}, {"elementalSwordSequence", Type::Number},
            {"shadowPlayer", Type::Object}, {"shadowOutcome", Type::String},
            {"playerSpeed", Type::Number}, {"enemySpeed", Type::Number}, {"dungeonSite", Type::String}, {"dungeonNode", Type::String}})) return false;
    if (battle["beastActions"].asArray().size() > 4) return false;
    for (const auto& Action : battle["beastActions"].asArray()) {
        if (!Action.isObject() || !Action["id"].isString() ||
            !BeastCombat::mfParseActionKind(Action["id"].asString())) return false;
        static const std::set<string> Fields{"id", "rawDamage", "shieldGain", "healGain", "poisonDamage",
            "burnDamage", "weakenLayers", "chargeGain", "piercePercent"};
        for (const auto& [Key, Item] : Action.asObject())
            if (!Fields.count(Key) || (Key != "id" && !saveInteger(Item, 0, 1000000))) return false;
        for (const char* Key : {"rawDamage", "shieldGain", "healGain", "poisonDamage", "burnDamage", "weakenLayers", "chargeGain", "piercePercent"})
            if (!saveInteger(Action[Key], 0, 1000000)) return false;
    }
    const auto& shadow = battle["shadowPlayer"];
    if (shadow.isObject()) {
        // Reject recursive snapshots before any recursive deserialization.
        if (!shadow["arts"].isObject() || !shadow["arts"]["battle"].isNull() ||
            !shadow["name"].isString() || !shadow["traits"].isArray() ||
            !saveObjectFieldTypes(shadow, {{"talent", Type::String}, {"sect", Type::String}})) return false;
        for (const auto& trait : shadow["traits"].asArray()) if (!trait.isString()) return false;
        for (const char* key : {"stage", "age", "lifespan", "health", "body", "apt", "insight", "sword"})
            if (!shadow[key].isNumber()) return false;
        if (!saveObjectFieldTypes(shadow["arts"], {{"manuals", Type::Object}, {"equipped", Type::Object},
                {"gear", Type::Array}, {"spells", Type::Array}, {"nodes", Type::Array}, {"aux", Type::Array}})) return false;
    } else if (!battle["shadowOutcome"].isNull() && !battle["shadowOutcome"].asString().empty()) return false;
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
    for (const char* key : {"stage", "speed", "body", "mind", "mana", "maxMana", "shuffles", "played", "turns",
            "swordQi", "water", "soul", "charge", "lock", "carryMana", "manaBonus", "manaPenalty", "drawNext",
            "carryCap", "hpCostPct", "poison", "poisonTurns", "poisonDamage", "burn", "burnTurns", "swordSequence", "comboDraws", "elementalSwordManaReturns",
            "buff", "reduction", "guard", "discardPower", "elementalSwordSequence"})
        if (!enemy[key].isNull() && !enemy[key].isNumber()) return false;
    for (const auto* CombatState : {&battle, &enemy})
        if (!(*CombatState)["elementalSwordManaReturns"].isNull() &&
            !saveInteger((*CombatState)["elementalSwordManaReturns"], 0, ElementalSword::ManaReturnLimitPerTurn)) return false;
    for (const auto* CombatState : {&battle, &enemy}) {
        const auto Growth = CombatState->asObject().find("fireGrowthRounds");
        if (Growth != CombatState->asObject().end() &&
            !saveInteger(Growth->second, 0, FireGrowth::MaximumGrowthRounds)) return false;
        const auto Sequence = CombatState->asObject().find("elementalSwordSequence");
        if (Sequence != CombatState->asObject().end() &&
            !saveInteger(Sequence->second, 0, ElementalSword::SwordChainCycleLength)) return false;
    }
    for (const char* key : {"manuals", "cooldowns"})
        for (const auto& [id, value] : enemy[key].asObject()) if (!value.isNumber()) return false;
    for (const auto& npc : root["npcs"].asArray()) {
        if (!npc.isObject() || !npc["id"].isString() || !npc["name"].isString() ||
            !npc["stage"].isNumber() || !npc["location"].isString()) return false;
        if (!saveObjectFieldTypes(npc, {{"pillState", Type::Object}})) return false;
        const auto& pills = npc["pillState"];
        if (!pills.isNull()) {
            if (!saveObjectFieldTypes(pills, {{"used", Type::Object}, {"daoExp", Type::Object},
                    {"nodes", Type::Array}, {"poison", Type::Number}, {"aptPills", Type::Number},
                    {"hpBonus", Type::Number}, {"heart", Type::Number}, {"mind", Type::Number},
                    {"speed", Type::Number}, {"points", Type::Number}, {"repel", Type::Number},
                    {"breakBuff", Type::String}, {"combatPrep", Type::Object}, {"combatBuff", Type::Number}})) return false;
            for (const char* key : {"used", "daoExp"})
                for (const auto& [id, value] : pills[key].asObject())
                    if (!saveInteger(value, 0, 10000)) return false;
            for (const auto& node : pills["nodes"].asArray()) if (!node.isString()) return false;
        }
        for (const auto& gear : npc["gear"].asArray())
            if (!gear.isObject() || !saveObjectFieldTypes(gear, {{"crafted", Type::Bool}, {"shape", Type::String}})) return false;
    }
    return true;
}

} // namespace nz::persistence
