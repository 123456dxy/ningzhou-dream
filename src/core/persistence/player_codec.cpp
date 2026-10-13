// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"
#include "core/talents.h"
#include <stdexcept>

namespace nz::persistence {

Value playerToJson(const Player& player) {
    Value playerObject = personToJson(player);
    playerObject.set("talent", str(player.talent));
    playerObject.set("portraitId", str(player.portraitId));
    {
        const Build& build = player.build;
        Value buildObject(json::Object{});
        buildObject.set("budget", num(build.budget)); buildObject.set("spent", num(build.spent));
        buildObject.set("allocation", mapToJson(build.allocation));
        json::Array traits; for (const auto& trait : build.traits) traits.push_back(str(trait));
        buildObject.set("traits", Value(std::move(traits)));
        buildObject.set("apt", num(build.apt)); buildObject.set("luck", num(build.luck));
        buildObject.set("insight", num(build.insight)); buildObject.set("sword", num(build.sword));
        buildObject.set("alchemy", num(build.alchemy)); buildObject.set("body", num(build.body));
        buildObject.set("lifeBonus", num(build.lifeBonus)); buildObject.set("stones", num(build.stones));
        buildObject.set("herbs", num(build.herbs));
        playerObject.set("build", std::move(buildObject));
    }
    {
        json::Array traitsArray;
        for (const auto& trait : player.traits) traitsArray.push_back(str(trait));
        playerObject.set("traits", Value(std::move(traitsArray)));
    }
    playerObject.set("sect", str(player.sect));
    playerObject.set("renown", num(player.renown));
    // 道友往来：交情 + 打招呼记录（好感度在 relations 里，见 personToJson）
    {
        Value bondsObject(json::Object{});
        for (const auto& [id, bond] : player.npcBonds) {
            Value bondObject(json::Object{});
            bondObject.set("credit", num(bond.credit));
            bondObject.set("spent", num(bond.spent));
            bondObject.set("greeted", Value(bond.greeted));
            bondObject.set("greetYear", num(bond.greetYear));
            bondObject.set("talkMonth", num(bond.talkMonth));
            bondObject.set("giftMonth", num(bond.giftMonth));
            bondObject.set("giftsThisMonth", num(bond.giftsThisMonth));
            bondObject.set("discussMonth", num(bond.discussMonth));
            bondObject.set("discussCount", num(bond.discussCount));
            bondObject.set("learnMonth", num(bond.learnMonth));
            bondObject.set("learnedThisMonth", num(bond.learnedThisMonth));
            bondObject.set("travelMonth", num(bond.travelMonth));
            bondObject.set("sparMonth", num(bond.sparMonth));
            bondsObject.set(id, std::move(bondObject));
        }
        playerObject.set("npcBonds", std::move(bondsObject));
    }
    // 洞府与配给（只有玩家有，NPC 没有）
    playerObject.set("cave", str(player.cave));
    playerObject.set("caveTier", num(player.caveTier));
    playerObject.set("stoneSupply", num(player.stoneSupply));
    playerObject.set("qiSupply", num(player.qiSupply));
    playerObject.set("supplyPaidMonth", num(player.supplyPaidMonth));

    playerObject.set("highestStage", num(player.highestStage));
    playerObject.set("achievements", pairsToJson(player.achievements));
    playerObject.set("arts", artsToJson(player.arts));
    return playerObject;
}


void playerFromJson(const Value& playerObject, Player& player, uint32_t seed) {
    personFromJson(playerObject, player);
    player.portraitId = playerObject["portraitId"].asString();
    // 洞府与配给
    // 道友往来（旧档没有这个字段，缺了就是空）
    player.npcBonds.clear();
    if (playerObject["npcBonds"].isObject()) {
        for (const auto& [id, bondObject] : playerObject["npcBonds"].asObject()) {
            NpcBond bond;
            bond.credit = bondObject["credit"].asInt();
            bond.spent = bondObject["spent"].asInt();
            bond.greeted = bondObject["greeted"].asBool();
            bond.greetYear = bondObject["greetYear"].isNumber() ? bondObject["greetYear"].asInt() : -1;
            bond.talkMonth = bondObject["talkMonth"].asInt(-1);
            bond.giftMonth = bondObject["giftMonth"].asInt(-1);
            bond.giftsThisMonth = bondObject["giftsThisMonth"].asInt(0);
            bond.discussMonth = bondObject["discussMonth"].asInt(-1);
            bond.discussCount = bondObject["discussCount"].asInt(0);
            bond.learnMonth = bondObject["learnMonth"].asInt(-1);
            bond.learnedThisMonth = bondObject["learnedThisMonth"].asInt(0);
            bond.travelMonth = bondObject["travelMonth"].asInt(-1);
            bond.sparMonth = bondObject["sparMonth"].asInt(-1);
            player.npcBonds[id] = bond;
        }
    }
    player.cave = playerObject["cave"].isString() ? playerObject["cave"].asString() : "";

    player.stoneSupply = playerObject["stoneSupply"].isNumber() ? playerObject["stoneSupply"].asInt() : 0;
    player.qiSupply = playerObject["qiSupply"].isNumber() ? playerObject["qiSupply"].asInt() : 0;
    player.caveTier = playerObject["caveTier"].isNumber() ? playerObject["caveTier"].asInt() :
        clampInt(player.qiSupply - 1, 0, 2);
    player.supplyPaidMonth =
        playerObject["supplyPaidMonth"].isNumber() ? playerObject["supplyPaidMonth"].asInt() : -1;

    player.talent = playerObject["talent"].asString("sword");
    player.traits = jsonToStringVec(playerObject["traits"]);
    if (playerObject["build"].isObject()) {
        const Value& buildObject = playerObject["build"];
        Build& build = player.build;
        build.budget = buildObject["budget"].asInt(BASE_POINTS); build.spent = buildObject["spent"].asInt();
        build.allocation = jsonToMap<int>(buildObject["allocation"]);
        build.traits = jsonToStringVec(buildObject["traits"]);
        build.apt = buildObject["apt"].asInt(55); build.luck = buildObject["luck"].asInt(45);
        build.insight = buildObject["insight"].asInt(10); build.sword = buildObject["sword"].asInt(2);
        build.alchemy = buildObject["alchemy"].asInt(5); build.body = buildObject["body"].asInt();
        build.lifeBonus = buildObject["lifeBonus"].asInt(); build.stones = buildObject["stones"].asInt(100);
        build.herbs = buildObject["herbs"].asInt(3);
    } else {
        // 旧版漏存建号结果，以本世天赋补出可展示的预算与起始属性。
        try { player.build = buildCharacter(player.traits, {}, MAX_POINTS, seed); }
        catch (const std::invalid_argument&) { player.build.traits = player.traits; }
    }
    player.sect = playerObject["sect"].asString();
    player.renown = playerObject["renown"].asInt();
    player.highestStage = playerObject["highestStage"].asInt();
    if (playerObject["achievements"].isArray()) {
        for (const auto& achievementObject : playerObject["achievements"].asArray())
            player.achievements.emplace_back(achievementObject["name"].asString(),
                                               achievementObject["year"].asInt());
    }
    artsFromJson(playerObject["arts"], player.arts);
    if (!playerObject["build"].isObject() && player.lifeBonus == 0 &&
        player.stage >= 0 && player.stage < static_cast<int>(REALMS().size())) {
        // 旧 Player 重复定义 lifeBonus，基类序列化永远写零。
        player.lifeBonus = player.lifespan - realmOf(player.stage).life;
    }
}

} // namespace nz::persistence
