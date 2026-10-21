#include "harness.h"

#include "core/arts.h"
#include "core/battle.h"
#include "core/dungeon.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"

#include <filesystem>
#include <fstream>
#include <chrono>

namespace nztest {
using namespace nz;
namespace {
Game persistenceFixture() {
    CreateOptions options;
    options.name = "存档模块回归";
    options.seed = 20261012;
    Game game = createGame(options);
    game.pending.reset();
    game.clock.days = 365;
    game.year = 1;
    game.player.age = game.clock.ageBase + 1;
    game.clock.carry = {{"cultivate", 0.25}};
    game.rng = 3456789012u;
    game.player.arts.focus = "剑";
    game.player.arts.exp["剑"] = 145;
    if (!game.npcs.empty()) game.npcs.front().dao = "剑";
    return game;
}

// Same world-free loadout shape as test_shadow_combat; all state is synthetic.
Player frozenPlayer(const string& name) {
    Player player;
    player.name = name;
    player.stage = player.highestStage = 6;
    player.lifespan = realmOf(6).life;
    player.arts.main = "tuna";
    player.arts.manuals["tuna"] = 3;
    player.arts.spells = {"gold", "wood", "skim", "fire"};
    player.arts.knownSpells = player.arts.spells;
    return player;
}

void rejectionPreserves(const json::Value& document, Game& destination, const string& label,
                        bool detached = false) {
    const string before = saveToJson(destination);
    string error;
    const bool accepted = detached
        ? loadDetachedFromJson(document.dump(), destination, &error)
        : loadFromJson(document.dump(), destination, &error);
    check(!accepted && !error.empty(), label + "被拒绝并给出原因");
    check(saveToJson(destination) == before, label + "拒绝后原状态不变");
}
}

void runPersistenceRefactorTests() {
    std::printf("\n-- 存档模块兼容与原子性 --\n");
    Game original = persistenceFixture(), restored;
    string error;
    const string text = saveToJson(original);
    check(loadFromJson(text, restored, &error), "模型模块普通存档往返：" + error);
    checkEq(restored.rng, 3456789012u, "无符号 RNG 不溢出或重新抽取");
    checkEq(restored.seed, 20261012, "原始建号种子保留");
    checkEq(restored.clock.days, 365, "日历天数保留");
    checkNear(restored.clock.carry["cultivate"], 0.25, "小数时间余量保留");
    checkStr(restored.player.arts.focus, "剑", "旧中文道途键原样恢复");
    checkEq(restored.player.arts.exp["剑"], 145, "旧中文经验键原样恢复");
    check(!restored.npcs.empty() && restored.npcs.front().dao == "剑", "NPC 中文道途值兼容");
    check(saveToJson(restored) == text, "普通存档所有字段序列化稳定");

    EnemySpec enemy;
    enemy.name = "持久化试炼"; enemy.stage = 0; enemy.style = "剑";
    battleStart(original, enemy);
    const string fighting = saveToJson(original);
    check(loadFromJson(fighting, restored, &error) && restored.player.arts.battle.has_value(),
          "在途 PvE 斗法恢复：" + error);
    check(saveToJson(restored) == fighting, "在途斗法牌组及计划完整保留");

    Game dungeon = persistenceFixture();
    dungeon.pending.reset(); dungeon.job.reset(); dungeon.npcs.clear();
    dungeon.player.stones = 100000;
    dungeon.dungeons = DungeonWorld{};
    ensureDungeons(dungeon);
    string siteId;
    for (const auto& site : dungeon.dungeons.sites) if (site.type == "wild") {
        siteId = site.id; dungeon.player.location = site.location; break;
    }
    check(!siteId.empty() && dungeonEnter(dungeon, siteId).empty(), "合成秘境进入真实探索流程");
    if (dungeon.dungeons.run) {
        string nextNode;
        for (const auto& node : dungeon.dungeons.run->nodes)
            if (dungeonNodeAvailable(*dungeon.dungeons.run, node.id)) { nextNode = node.id; break; }
        check(!nextNode.empty() && dungeonChoose(dungeon, nextNode).empty(), "合成秘境推进至在途节点");
        const string exploring = saveToJson(dungeon);
        check(loadFromJson(exploring, restored, &error) && restored.dungeons.run.has_value(),
              "秘境路线与临时背包恢复：" + error);
        check(saveToJson(restored) == exploring, "秘境与关联斗法往返无损");
    }

    Game frozen;
    frozen.player = frozenPlayer("冻结构筑"); frozen.clock.ageBase = frozen.player.age;
    const string frozenText = saveToJson(frozen);
    check(loadDetachedFromJson(frozenText, restored, &error), "冻结构筑独立重载：" + error);
    check(restored.npcs.empty() && restored.dungeons.sites.empty() && !restored.dungeons.initialized,
          "独立重载不创建 NPC 或秘境");
    check(saveToJson(restored) == frozenText, "独立快照不改写世界或构筑");

    Game shadow;
    const string sourceText = saveToJson(frozen);
    battleStartShadow(shadow, frozen.player, frozenPlayer("对手快照"), 71023);
    const string shadowText = saveToJson(shadow);
    check(loadDetachedFromJson(shadowText, restored, &error), "真人影录在途斗法恢复：" + error);
    check(saveToJson(restored) == shadowText, "影录敌方计划和不可变构筑往返无损");
    check(saveToJson(frozen) == sourceText, "影录和独立读档不改动源角色存档");
    rejectionPreserves(json::Value::parse(shadowText), original, "普通导入天机切磋");

    auto legacy = json::Value::parse(text);
    legacy.asObject().erase("combatRulesVersion");
    auto& legacyPlayer = legacy.asObject()["player"];
    legacyPlayer.asObject().erase("build");
    legacyPlayer.asObject()["arts"].asObject().erase("knownSpells");
    check(loadFromJson(legacy.dump(), restored, &error), "旧存档缺版本、构筑及领悟列表可迁移：" + error);
    checkEq(restored.rng, 3456789012u, "旧存档迁移保持 RNG");
    checkEq(restored.clock.days, 365, "旧存档迁移保持日历");
    check(restored.player.build.traits == restored.player.traits && !restored.player.arts.knownSpells.empty(),
          "旧存档补齐展示构筑与领悟列表");

    auto invalid = json::Value::parse(text);
    invalid.asObject()["player"].set("health", -1);
    rejectionPreserves(invalid, original, "负生命状态");
    invalid = json::Value::parse(text);
    invalid.asObject()["clock"].set("days", 366.5);
    rejectionPreserves(invalid, original, "非整数日历");
    invalid = json::Value::parse(text);
    invalid.asObject()["player"].asObject()["arts"].set("gear", "损坏数组");
    rejectionPreserves(invalid, original, "错误装备字段类型");
    invalid = json::Value::parse(text);
    invalid.asObject()["player"].set("location", "unknown-location");
    rejectionPreserves(invalid, original, "未知地点标识");
    invalid = json::Value::parse(shadowText);
    invalid.asObject()["player"].asObject()["arts"].asObject()["battle"]
        .asObject()["shadowPlayer"].asObject()["arts"].set("battle", json::Value(json::Object{}));
    rejectionPreserves(invalid, original, "递归影录", true);

    // Account paths and automatic slot are exercised only in a temporary fixture.
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("ningzhou-persistence-refactor-tests-" + std::to_string(unique));
    std::filesystem::create_directories(directory / "account-a");
    std::filesystem::create_directories(directory / "account-b");
    const auto utf8Path = [](const std::filesystem::path& path) {
        const auto utf8 = path.u8string();
        return string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    };
    const string previousDirectory = defaultSaveDir();
    setSaveDirOverride(utf8Path(directory / "account-a"));
    const string firstPath = savePathFor(4);
    check(saveToFile(persistenceFixture(), firstPath, &error), "账号 A 自动存档写入：" + error);
    const auto temporaryPath = std::filesystem::u8path(firstPath + ".tmp");
    std::filesystem::create_directory(temporaryPath);
    Game replacement = persistenceFixture(); replacement.player.stones += 99;
    check(!saveToFile(replacement, firstPath, &error) && !error.empty(), "临时文件不可写时拒绝替换旧档");
    Game stillOriginal;
    check(loadFromFile(firstPath, stillOriginal, &error), "失败写入后旧档仍可读取");
    checkEq(stillOriginal.player.stones, 100, "失败写入保持旧档资源");
    std::filesystem::remove(temporaryPath);
    setSaveDirOverride(utf8Path(directory / "account-b"));
    check(savePathFor(4) != firstPath, "账号覆盖目录隔离自动存档路径");
    check(!listSaveSlots()[3].exists && listSaveSlots()[3].automatic, "账号 B 自动档不读取账号 A 文件");
    setSaveDirOverride(utf8Path(directory / "account-a"));
    check(listSaveSlots()[3].exists && listSaveSlots()[3].automatic, "账号 A 自动档摘要可读取");
    setSaveDirOverride(previousDirectory);
    std::error_code ignored;
    std::filesystem::remove(std::filesystem::u8path(firstPath), ignored);
    std::filesystem::remove(directory / "account-a", ignored);
    std::filesystem::remove(directory / "account-b", ignored);
    std::filesystem::remove(directory, ignored);
}
}

#ifdef NZ_PERSISTENCE_REFACTOR_TEST_MAIN
int main() { nztest::runPersistenceRefactorTests(); return nztest::failures() ? 1 : 0; }
#endif
