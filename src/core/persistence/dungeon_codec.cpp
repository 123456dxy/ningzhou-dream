// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"

namespace nz::persistence {

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
Value dungeonWorldToJson(const DungeonWorld& world) {
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
void dungeonWorldFromJson(const Value& value, DungeonWorld& world) {
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

} // namespace nz::persistence
