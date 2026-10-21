#include "harness.h"

#include <algorithm>
#include <set>

#include "core/api.h"
#include "data/reference_tables.h"
#include "core/battle.h"
#include "core/checkpoint.h"
#include "core/dungeon.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
using namespace nz;
namespace {
struct ApiGameRestore {
    Game original = api::game();
    ~ApiGameRestore() { api::game() = std::move(original); }
};
http::Response rewardRequest(const string& route, const json::Value& body = json::Object{}) {
    return api::handle({"POST", "/api/" + route, body.dump(), ""});
}
json::Value rewardInput(const string& key, const string& value) {
    json::Value body(json::Object{}); body.set(key, value); return body;
}
json::Value rewardDungeons() {
    return json::Value::parse(rewardRequest("state").body)["state"]["dungeons"];
}
Game rewardGame(int stage = 0) {
    CreateOptions options; options.seed = 120933; options.name = "永久奖励接口回归";
    Game g = createGame(options);
    g.pending.reset(); g.job.reset(); g.npcs.clear();
    g.player.stage = g.player.highestStage = stage;
    g.player.health = 100; g.player.stones = 100000; g.player.lifespan = realmOf(stage).life;
    g.dungeons = DungeonWorld{}; ensureDungeons(g);
    return g;
}
void rewardReject(const string& route, const json::Value& body, const string& label) {
    const string before = saveToJson(api::game());
    check(rewardRequest(route, body).status >= 400, label + "被拒绝");
    check(saveToJson(api::game()) == before, label + "不改变资源、节点、时间和领取记录");
}
bool rewardEnter() {
    auto& g = api::game();
    const auto site = std::find_if(g.dungeons.sites.begin(), g.dungeons.sites.end(), [](const auto& entry) { return entry.type == "wild"; });
    if (site == g.dungeons.sites.end()) return false;
    const string id = site->id; g.player.location = site->location;
    return rewardRequest("dungeon/enter", rewardInput("id", id)).status == 200;
}
bool rewardBoss() {
    auto& g = api::game();
    for (int guard = 0; guard < 50 && g.dungeons.run; ++guard) {
        if (g.dungeons.run->cleared && g.dungeons.run->phase == "reward") return true;
        if (g.player.arts.battle) {
            auto& battle = *g.player.arts.battle;
            battle.hp = battle.max; battle.enemyHp = 1; battle.enemyShield = 0; battle.mana = 5;
            battle.hand.clear(); battle.draw.clear(); battle.discard.clear(); battle.exhausted.clear();
            bool placed = false;
            for (const auto& card : battle.deck) {
                if (!placed && card.spell == "strike") { battle.hand.push_back(card.uid); placed = true; }
                else battle.draw.push_back(card.uid);
            }
            if (!placed || rewardRequest("battle/turn", rewardInput("id", "strike")).status != 200 || g.player.arts.battle) return false;
        } else if (g.dungeons.run->phase == "reward") {
            if (rewardRequest("dungeon/claim", rewardInput("id", "skip")).status != 200) return false;
        } else {
            const auto node = std::find_if(g.dungeons.run->nodes.begin(), g.dungeons.run->nodes.end(), [&](const auto& item) { return dungeonNodeAvailable(*g.dungeons.run, item.id); });
            if (node == g.dungeons.run->nodes.end()) return false;
            const string id = node->id;
            if (rewardRequest("dungeon/choose", rewardInput("id", id)).status != 200) return false;
        }
    }
    return false;
}
json::Value rewardOfKind(const json::Value& list, const string& kind) {
    for (const auto& item : list.asArray()) if (item["kind"].asString() == kind) return item;
    return {};
}
// 导入仍走完整 checkpoint 校验；使用手动来源，避免测试覆盖试玩自动槽。
void rewardManualCheckpoint(Game& g) {
    checkpointEnable(g); g.checkpoint.source = "manual"; g.checkpoint.slot = 1;
}
json::Value oldCompletedRewardSave(const Game& g) {
    auto saved = json::Value::parse(saveToJson(g)); saved.set("combatRulesVersion", 4);
    const auto& material = data::MaterialDef_TABLE().front();
    auto target = [&](json::Value& entry) {
        entry.set("targetKind", "material"); entry.set("targetId", material.id);
        entry.set("targetName", material.name); entry.set("targetGrade", material.grade);
    };
    auto& world = saved.asObject()["dungeons"];
    for (auto& site : world.asObject()["sites"].asArray()) {
        site.asObject().erase("bossRewards"); target(site);
    }
    auto& run = world.asObject()["run"];
    run.asObject().erase("bossRewards"); run.asObject().erase("bossRewardClaimed"); run.asObject().erase("bossRewardTaken");
    target(run); run.set("phase", "complete"); run.set("targetGranted", true); run.set("rewards", json::Array{});
    return saved;
}
}

void runRewardApiTests() {
    std::printf("\n-- 永久秘境奖励 Web 接口集成 --\n");
    {
        ApiGameRestore restore;
        api::game() = rewardGame(6); api::game().player.location = "tianji";
        const string original = saveToJson(api::game()); const uint32_t rng = api::game().rng;
        const auto first = rewardDungeons();
        check(!first["sites"].asArray().empty() && first["sites"][size_t(0)]["bossRewards"].asArray().size() == 3, "地图接口下发冻结神通、抄本、灵石三类永久目标");
        bool ordinary = true, permanent = true, highIntel = !first["intel"].asArray().empty();
        for (const auto& site : first["sites"].asArray()) {
            ordinary = ordinary && site["targetKind"].asString().empty() && site["targetId"].asString().empty() && site["targetName"].asString().empty();
            for (const auto& reward : site["bossRewards"].asArray()) permanent = permanent && reward["permanent"].asBool() && !reward["name"].asString().empty() && !reward["desc"].asString().empty();
        }
        for (const auto& item : first["intel"].asArray()) highIntel = highIntel && item["grade"].asInt() >= 3;
        check(ordinary && permanent, "普通秘境地图展示永久目标，材料主目标为空且奖励有可读文本");
        check(highIntel, "天机阁接口目录仅下发三阶及以上材料位置");
        for (int i = 0; i < 4; ++i) check(rewardDungeons()["sites"].dump() == first["sites"].dump(), "重复查看地图保持同一永久目标和位置");
        check(saveToJson(api::game()) == original && api::game().rng == rng, "地图和情报只读接口不改存档或世界随机数");
        auto low = rewardInput("id", "iron"); low.set("kind", "material");
        rewardReject("dungeon/intel", low, "直接购买一阶材料情报");
    }
    for (const string kind : {"learnSpell", "manual", "stones"}) {
        ApiGameRestore restore; api::game() = rewardGame();
        const auto carried = api::game().player.arts.spells;
        check(rewardEnter() && rewardBoss(), "通过入场、路线和真实出牌接口抵达永久首领奖励");
        if (!api::game().dungeons.run || !api::game().dungeons.run->cleared) continue;
        const auto dungeon = rewardDungeons(), run = dungeon["run"];
        const string frozen = run["bossRewards"].dump();
        std::set<string> kinds;
        for (const auto& reward : run["rewards"].asArray()) kinds.insert(reward["kind"].asString());
        check(run["phase"].asString() == "reward" && !run["bossRewardClaimed"].asBool() && kinds == std::set<string>{"learnSpell", "manual", "stones"}, "终点接口明确提供一次永久三选一");
        rewardReject("dungeon/leave", {}, "首领已胜但永久奖励未选时离开");
        const auto selected = rewardOfKind(run["rewards"], kind);
        check(selected.isObject() && selected["permanent"].asBool(), "首领奖励项带永久标志" + kind);
        if (!selected.isObject()) continue;
        const auto known = api::game().player.arts.knownSpells;
        const int books = api::game().economy.books.size(), stored = api::game().dungeons.run->stones;
        const auto claimed = rewardRequest("dungeon/claim", rewardInput("id", selected["id"].asString()));
        checkEq(claimed.status, 200, "永久选择接口成功领取" + kind);
        const auto actual = json::Value::parse(claimed.body)["state"]["dungeons"]["run"];
        const auto taken = actual["bossRewardTaken"];
        check(actual["phase"].asString() == "complete" && actual["bossRewardClaimed"].asBool() && actual["bossRewards"].dump() == frozen, "领取后完成标志与原始冻结选择一同下发");
        check(taken["kind"].asString() == kind && taken["name"].asString() == selected["name"].asString() && !taken["desc"].asString().empty(), "完成接口显示实际领取项和文本" + kind);
        if (kind == "learnSpell") {
            checkEq(api::game().player.arts.knownSpells.size(), known.size() + 1, "神通接口奖励实际增加永久领悟");
            check(std::find(api::game().player.arts.knownSpells.begin(), api::game().player.arts.knownSpells.end(), selected["spell"].asString()) != api::game().player.arts.knownSpells.end(), "实际已领悟ID对应界面所选神通");
        } else if (kind == "manual") {
            checkEq(api::game().economy.books.size(), books + 1, "抄本接口奖励实际增加永久功法库存");
            check(std::find(api::game().player.arts.bookList.begin(), api::game().player.arts.bookList.end(), selected["manual"].asString()) != api::game().player.arts.bookList.end(), "玩家功法页可看到实际所选抄本");
        } else checkEq(api::game().dungeons.run->stones, stored + selected["amount"].asInt(), "灵石接口奖励真实写入探索行囊");
        check(api::game().player.arts.spells == carried, "永久奖励接口不自动替换外部携带神通");
        rewardReject("dungeon/claim", rewardInput("id", selected["id"].asString()), "重复领取永久通关奖励");
        const int before = api::game().player.stones, loot = api::game().dungeons.run->stones;
        checkEq(rewardRequest("dungeon/leave").status, 200, "已领主奖励后离开接口成功结算");
        check(!api::game().dungeons.run && api::game().player.stones == before + loot, "离开接口仅结算一次探索行囊");
        if (kind == "learnSpell") check(std::find(api::game().player.arts.knownSpells.begin(), api::game().player.arts.knownSpells.end(), selected["spell"].asString()) != api::game().player.arts.knownSpells.end(), "离开后永久神通仍可用");
        rewardReject("dungeon/leave", {}, "已经退出后再次结算");
    }
    {
        ApiGameRestore restore; api::game() = rewardGame();
        const auto frozen = rewardDungeons()["sites"][size_t(0)]["bossRewards"];
        const auto spell = rewardOfKind(frozen, "learnSpell");
        if (spell.isObject()) {
            api::game().player.arts.knownSpells.push_back(spell["spell"].asString());
            check(rewardEnter() && rewardBoss(), "重复知识补偿测试通过真实路线抵达终点");
            if (api::game().dungeons.run && api::game().dungeons.run->cleared) {
                const int stones = api::game().dungeons.run->stones, known = api::game().player.arts.knownSpells.size();
                const auto result = rewardRequest("dungeon/claim", rewardInput("id", spell["id"].asString()));
                const auto taken = json::Value::parse(result.body)["state"]["dungeons"]["run"]["bossRewardTaken"];
                check(result.status == 200 && taken["kind"].asString() == "stones" && taken["amount"].asInt() == 250 && taken["name"].asString().find("250") != string::npos, "重复神通的完成接口显示实际二百五十灵石补偿");
                checkEq(api::game().dungeons.run->stones, stones + 250, "知识折算接口实际增加同额灵石");
                checkEq(api::game().player.arts.knownSpells.size(), known, "补偿不会重复添加已学神通");
            }
        }
    }
    {
        ApiGameRestore restore; api::game() = rewardGame(3);
        check(rewardEnter() && rewardBoss(), "旧版本通关导入测试建立完整首领路线");
        if (!api::game().dungeons.run || !api::game().dungeons.run->cleared) return;
        api::game().dungeons.run->materials["iron"] += 10;
        rewardManualCheckpoint(api::game());
        const auto known = api::game().player.arts.knownSpells;
        const int stones = api::game().player.stones, stash = api::game().dungeons.run->stones, material = api::game().dungeons.run->materials["iron"];
        const uint32_t rng = api::game().rng;
        const auto legacy = oldCompletedRewardSave(api::game());
        checkEq(rewardRequest("import", rewardInput("game", legacy.dump())).status, 200, "导入接口支持旧v4已通关但未退出的副本");
        if (!api::game().dungeons.run) return;
        auto view = rewardDungeons()["run"];
        const string frozen = view["bossRewards"].dump();
        const auto MigratedStones = rewardOfKind(view["bossRewards"], "stones");
        checkEq(MigratedStones["amount"].asInt(), 750, "缺少冻结主奖的旧筑基副本按当前三倍档补建七百五十灵石选项");
        check(view["phase"].asString() == "reward" && !view["bossRewardClaimed"].asBool() && view["targetKind"].asString().empty(), "旧v4导入仅迁移到新永久待选状态");
        check(api::game().player.arts.knownSpells == known && api::game().player.stones == stones && api::game().dungeons.run->stones == stash && api::game().rng == rng, "迁移不发知识或灵石，也不消耗世界随机数");
        checkEq(api::game().dungeons.run->materials["iron"], material, "旧通关材料库存保留且不再额外发十份");
        const auto exported = json::Value::parse(rewardRequest("export").body);
        checkEq(rewardRequest("import", rewardInput("game", exported["game"].asString())).status, 200, "迁移后可导出存档再通过接口导入");
        check(rewardDungeons()["run"]["bossRewards"].dump() == frozen && saveToJson(api::game()) == exported["game"].asString(), "新版本再导入完整保持冻结奖励与原存档");
        checkEq(rewardRequest("dungeon/claim", rewardInput("id", "boss-stones")).status, 200, "迁移后的永久待选项可实际领取");
        const int claimedStash = api::game().dungeons.run->stones;
        checkEq(claimedStash, stash + MigratedStones["amount"].asInt(), "旧副本补领奖励只兑现一次迁移后已冻结的实际金额");
        const auto completed = json::Value::parse(rewardRequest("export").body);
        checkEq(rewardRequest("import", rewardInput("game", completed["game"].asString())).status, 200, "已领取的升级收益可完整导出再导入");
        view = rewardDungeons()["run"];
        check(view["bossRewardClaimed"].asBool() && view["phase"].asString() == "complete" && view["bossRewards"].dump() == frozen && view["bossRewardTaken"]["kind"].asString() == "stones", "再次导入保留领取锁定、冻结目录和实际收益");
        rewardReject("dungeon/claim", rewardInput("id", "boss-stones"), "升级旧档重存后再次领取");
        checkEq(api::game().dungeons.run->stones, claimedStash, "重复导入和重复领奖不增加升级收益");
    }
}
} // namespace nztest
