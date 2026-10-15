#include "harness.h"
#include <algorithm>
#include <deque>
#include <set>
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/checkpoint.h"
#include "core/dungeon.h"
#include "core/exploration/dungeon_events.h"
#include "core/exploration/dungeon_reward_rules.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
using namespace nz;
namespace {
Game dungeonGame(int stage = 0, uint32_t seed = 120933) {
    CreateOptions options; options.seed = seed;
    Game g = createGame(options);
    g.pending.reset(); g.job.reset(); g.npcs.clear();
    g.player.stage = g.player.highestStage = stage; g.player.stones = 100000;
    g.player.health = 100; g.player.lifespan = realmOf(stage).life;
    g.dungeons = DungeonWorld{}; ensureDungeons(g);
    return g;
}
const DungeonSite* siteType(const Game& g, const string& type) {
    for (const auto& site : g.dungeons.sites) if (site.type == type) return &site;
    return nullptr;
}
vector<string> availableNodes(const Game& g) {
    vector<string> result;
    if (!g.dungeons.run) return result;
    for (const auto& node : g.dungeons.run->nodes) if (dungeonNodeAvailable(*g.dungeons.run, node.id)) result.push_back(node.id);
    return result;
}
bool startWild(Game& g) {
    const auto* site = siteType(g, "wild");
    if (!site) return false;
    const string id = site->id; g.player.location = site->location;
    return dungeonEnter(g, id).empty();
}
void winCurrentBattle(Game& g, int health = 80) {
    if (!g.player.arts.battle) return;
    auto& battle = *g.player.arts.battle;
    battle.hp = std::max(1, jsRound(battle.max * health / 100.0));
    battle.enemyHp = 0;
    battleFinish(g, true);
}
void completeRoute(Game& g, bool claimBoss = true) {
    int guard = 0;
    while (g.dungeons.run && g.dungeons.run->phase != "complete" && ++guard < 50) {
        if (g.player.arts.battle) winCurrentBattle(g, 75);
        else if (g.dungeons.run->phase == "reward") {
            if (g.dungeons.run->cleared && !claimBoss) break;
            dungeonClaim(g, "skip");
        }
        else {
            const auto available = availableNodes(g);
            if (available.empty()) break;
            dungeonChoose(g, available.front());
        }
    }
}
// 用途：按真实连线查找可达目标房间；流程：新版随机列路线交互回归。
// 使用方式：传当前route状态与目标type；返回：包含目标的最短节点ID路线，找不到时为空。
vector<string> mfRouteToRoomType(const DungeonRun& Run, const string& Type) {
    std::deque<vector<string>> Paths;
    for (const auto& Node : Run.nodes)
        if (dungeonNodeAvailable(Run, Node.id)) Paths.push_back({Node.id});
    std::set<string> Seen;
    while (!Paths.empty()) {
        auto Path = std::move(Paths.front()); Paths.pop_front();
        const string NodeId = Path.back();
        if (!Seen.insert(NodeId).second) continue;
        const auto Node = std::find_if(Run.nodes.begin(), Run.nodes.end(), [&](const auto& Entry) { return Entry.id == NodeId; });
        if (Node == Run.nodes.end()) continue;
        if (Node->type == Type) return Path;
        for (const auto& NextId : Node->next) {
            auto NextPath = Path; NextPath.push_back(NextId); Paths.push_back(std::move(NextPath));
        }
    }
    return {};
}
// 用途：逐层实际行走到目标房间；流程：事件、游商、营地与首领奖励回归。
// 使用方式：仅在隔离夹具调用，途中以已有胜利夹具结束斗法、跳过奖励；返回：是否真实抵达目标。
// 说明：该辅助只验证路线与兑现，不使用自动判胜结果说明任何自然战斗胜率。
bool mfVisitRoomType(Game& State, const string& Type) {
    if (!State.dungeons.run || State.dungeons.run->phase != "route") return false;
    const auto Path = mfRouteToRoomType(*State.dungeons.run, Type);
    if (Path.empty()) return false;
    for (const auto& NodeId : Path) {
        if (!dungeonChoose(State, NodeId).empty() || !State.dungeons.run) return false;
        if (NodeId == Path.back()) return true;
        if (State.player.arts.battle) winCurrentBattle(State, 70);
        if (!State.dungeons.run) return false;
        if (State.dungeons.run->phase == "reward" && !dungeonClaim(State, "skip").empty()) return false;
        if (State.dungeons.run->phase != "route") return false;
    }
    return false;
}
int stashTarget(const DungeonRun& run) {
    const auto& inventory = run.targetKind == "herb" ? run.herbs : run.materials;
    const auto found = inventory.find(run.targetKind == "herb" ? run.targetName : run.targetId);
    return found == inventory.end() ? 0 : found->second;
}
int bagTarget(const Player& player, const string& kind, const string& id, const string& name) {
    const auto& inventory = kind == "herb" ? player.arts.herbs : player.arts.materials;
    const auto found = inventory.find(kind == "herb" ? name : id);
    return found == inventory.end() ? 0 : found->second;
}
json::Value legacyDungeonSave(const Game& g, bool completed = false) {
    auto root = json::Value::parse(saveToJson(g));
    root.set("combatRulesVersion", 4);
    const auto& material = data::MaterialDef_TABLE().front();
    auto oldTarget = [&](json::Value& value) {
        value.set("targetKind", "material"); value.set("targetId", material.id);
        value.set("targetName", material.name); value.set("targetGrade", material.grade);
    };
    auto& world = root.asObject()["dungeons"];
    for (auto& site : world.asObject()["sites"].asArray()) {
        site.asObject().erase("bossRewards");
        if (site["type"].asString() != "intel") oldTarget(site);
    }
    auto& run = world.asObject()["run"];
    if (!run.isNull()) {
        run.asObject().erase("bossRewards"); run.asObject().erase("bossRewardClaimed"); run.asObject().erase("bossRewardTaken");
        if (run["type"].asString() != "intel") oldTarget(run);
        if (completed) {
            run.set("phase", "complete"); run.set("targetGranted", true);
            run.set("rewards", json::Array{});
        }
    }
    return root;
}
}

void runDungeonTests() {
    std::printf("\n-- 材料秘境、爬塔路线与年表 --\n");
    {
        Game g = dungeonGame();
        const uint32_t rng = g.rng;
        const string snapshot = saveToJson(g);
        for (int i = 0; i < 5; ++i) { ensureDungeons(g); dungeonIntelOptions(g); dungeonStateError(g); }
        checkEq(g.rng, rng, "秘境初始化和目录查询不消耗世界随机数");
        check(saveToJson(g) == snapshot, "反复查询秘境不重绘年表或改变存档");
        checkEq(g.dungeons.sites.size(), 1, "前期初始出现一个可探索野外秘境");
        check(g.dungeons.sites.front().targetKind.empty() && !g.dungeons.sites.front().bossRewards.empty(), "普通秘境目录冻结永久传承与灵石奖励，材料不是主目标");
        Game same = dungeonGame();
        check(g.dungeons.sites.front().location == same.dungeons.sites.front().location && saveToJson(g) == saveToJson(same),
              "相同种子对应相同秘境位置与冻结永久奖励");
        g.clock.days = 2 * 360; dungeonYearTick(g);
        checkEq(g.dungeons.seq, 1, "初始秘境出现后不足三年不再生成");
        g.clock.days = 3 * 360; dungeonYearTick(g);
        checkEq(g.dungeons.seq, 2, "前三大境界每三年刷新野外秘境");
        checkEq(g.dungeons.lastSpawnYear, 4, "随机秘境年表记录实际刷新年份");
        g.player.stage = 9; g.clock.days = 52 * 360; dungeonYearTick(g);
        checkEq(g.dungeons.seq, 2, "进入元婴后距上次刷新不足五十年不新增");
        g.clock.days = 53 * 360; dungeonYearTick(g);
        checkEq(g.dungeons.seq, 3, "元婴及以后每五十年刷新秘境");
        checkEq(g.dungeons.sites.back().stage, 9, "新增秘境难度随当前境界");
    }
    {
        Game g = dungeonGame(6);
        g.clock.days = 9 * 360; dungeonYearTick(g);
        const auto* sea = siteType(g, "sea");
        check(sea && sea->openedYear == 10 && sea->expiresYear == 12, "无尽海每十年开启两年");
        check(!siteType(g, "tianmo"), "未到固定年份时天魔眼关闭");
        if (sea) {
            const string id = sea->id; g.player.location = "sea";
            g.clock.days = 11 * 360;
            const auto before = saveToJson(g);
            check(!dungeonEnter(g, id).empty(), "无尽海窗口结束后拒绝入场");
            check(saveToJson(g) == before, "关闭窗口的入场失败不改变状态");
        }
        g.clock.days = 49 * 360; dungeonYearTick(g);
        const auto* tianmo = siteType(g, "tianmo");
        check(tianmo && tianmo->openedYear == 50 && tianmo->expiresYear == 55, "天魔眼每五十年开启五年");
        if (tianmo) {
            const string id = tianmo->id; g.player.location = "tianmo"; g.player.stage = 3;
            check(!dungeonEnter(g, id).empty(), "低境界不能闯入金丹门槛的天魔眼");
        }
    }
    for (const string type : {"sea", "tianmo"}) {
        Game g = dungeonGame(6);
        g.clock.days = (type == "sea" ? 9 : 49) * 360; g.year = g.clock.days / 360;
        g.player.age = g.clock.ageBase + g.year;
        dungeonYearTick(g);
        const auto* site = siteType(g, type);
        check(site && site->targetKind.empty() && !site->bossRewards.empty(), "周期" + type + "秘境以冻结永久奖励作为主目标");
        if (!site) continue;
        const auto cash = std::find_if(site->bossRewards.begin(), site->bossRewards.end(), [](const auto& reward) { return reward.kind == "stones"; });
        const int expected = mfDungeonRewardRulesForStage(6, type).MainStones;
        check(cash != site->bossRewards.end() && cash->amount == expected, "海域和天魔眼提供比同境野外更多的灵石宝藏");
        const int EntryStage = site->stage;
        const string id = site->id; g.player.location = site->location;
        check(dungeonEnter(g, id).empty(), "进入周期秘境检验真实首领奖励");
        completeRoute(g, false);
        check(g.dungeons.run && g.dungeons.run->cleared && !g.dungeons.run->targetGranted, "周期秘境真实通关不再固定发材料十份");
        if (!g.dungeons.run) continue;
        const auto HerbPool = mfDungeonHerbRewardPool(EntryStage);
        const auto MaterialPool = mfDungeonMaterialRewardPool(EntryStage);
        const int EntryGrade = mfDungeonRewardRulesForStage(EntryStage, type).MaterialGrade;
        bool FitsEntryPools = true, SawHighGradeMaterial = false;
        for (const auto& [name, count] : g.dungeons.run->herbs) {
            const auto* herb = findHerbByName(name);
            FitsEntryPools = FitsEntryPools && herb && count > 0 && std::any_of(HerbPool.begin(), HerbPool.end(),
                [&](const auto* Allowed) { return herb && Allowed->id == herb->id; });
            SawHighGradeMaterial = SawHighGradeMaterial || (herb && herb->grade == EntryGrade && herb->grade > 2 && count > 0);
        }
        for (const auto& [id, count] : g.dungeons.run->materials) {
            const auto item = std::find_if(data::MaterialDef_TABLE().begin(), data::MaterialDef_TABLE().end(), [&](const auto& def) { return def.id == id; });
            FitsEntryPools = FitsEntryPools && item != data::MaterialDef_TABLE().end() && count > 0 &&
                std::any_of(MaterialPool.begin(), MaterialPool.end(), [&](const auto* Allowed) { return Allowed->id == id; });
            SawHighGradeMaterial = SawHighGradeMaterial || (item != data::MaterialDef_TABLE().end() && item->grade == EntryGrade && item->grade > 2 && count > 0);
        }
        check(FitsEntryPools, "高境周期秘境沿途材料属于冻结入口的真实适阶池");
        check(SawHighGradeMaterial, "高境周期秘境真实获得入口对应的三阶材料，不再固定一二阶");
        const int stored = g.dungeons.run->stones;
        check(dungeonClaim(g, "skip").empty(), "周期秘境首领选择可领取永久灵石兜底");
        checkEq(g.dungeons.run->stones, stored + expected, "周期秘境主奖励真实进入探索行囊");
        check(dungeonStateError(g).empty(), "周期秘境完成状态与实取奖励匹配");
    }
    {
        Game g = dungeonGame(6);
        check(startWild(g), "抵达实际入口后进入秘境");
        if (!g.dungeons.run) return;
        const string siteId = g.dungeons.run->siteId;
        const auto originalSpells = g.player.arts.spells, originalKnown = g.player.arts.knownSpells;
        auto& run = *g.dungeons.run;
        int strike = 0, defend = 0;
        for (const auto& card : run.deck) { strike += card.spell == "strike"; defend += card.spell == "defend"; }
        checkEq(strike, 5, "秘境固定携带五张普通攻击");
        checkEq(defend, 5, "秘境固定携带五张普通防御");
        checkEq(run.nodes.size(), (11 - 1) * 3 + 1, "金丹秘境拥有十一层三列分支路线");
        check(dungeonStateError(g).empty(), "合法初始临时牌组和路线通过状态校验");
        const auto initial = saveToJson(g);
        check(!dungeonChoose(g, "floor-3-0").empty(), "不能跳过连线直接领取高层奖励");
        check(saveToJson(g) == initial, "非法跳层不消耗时间或更改路线");
        const auto options = availableNodes(g);
        checkEq(options.size(), 3, "初始可选择三条入场路线");
        check(dungeonChoose(g, options.front()).empty(), "沿合法路线进入真实斗法");
        check(g.player.arts.battle && g.dungeons.run->phase == "battle", "秘境节点启动卡牌战斗而非立即结算");
        if (g.player.arts.battle) {
            check(g.player.arts.battle->dungeonSite == siteId && g.player.arts.battle->dungeonNode == options.front(), "战斗在先手之前绑定秘境节点上下文");
            checkEq(g.player.arts.battle->deck.size(), g.dungeons.run->deck.size(), "战斗使用完整的秘境临时牌组");
            check(dungeonStateError(g).empty(), "在途秘境斗法可安全保存");
            const auto fighting = saveToJson(g);
            check(!dungeonLeave(g).empty(), "斗法中直接离开被拒绝，须使用战斗撤离");
            check(saveToJson(g) == fighting, "直接离开失败不预发奖励");
            winCurrentBattle(g, 53);
        }
        check(g.dungeons.run && g.dungeons.run->phase == "reward", "真实斗法胜利后进入三选一奖励");
        if (!g.dungeons.run) return;
        checkNear(g.dungeons.run->health, 53, "战后气血百分比延续到后续楼层", 1.1);
        checkEq(g.dungeons.run->rewards.size(), 3, "普通战斗提供三张合法神通供选择");
        const auto reward = g.dungeons.run->rewards.front();
        const int size = static_cast<int>(g.dungeons.run->deck.size());
        check(dungeonClaim(g, reward.id).empty(), "领取一张神通加入临时牌组");
        checkEq(g.dungeons.run->deck.size(), size + 1, "一次选择只增加一张牌");
        check(g.player.arts.spells == originalSpells && g.player.arts.knownSpells == originalKnown, "秘境获牌不改变外部携带槽和已领悟列表");
        const auto claimed = saveToJson(g);
        check(!dungeonClaim(g, reward.id).empty(), "不能重复领取同一战斗奖励");
        check(saveToJson(g) == claimed, "重复领奖请求完全不改变状态");
        Game loaded; string error;
        const auto saved = saveToJson(g);
        check(loadFromJson(saved, loaded, &error), "秘境路线、临时牌组和战后奖励可读档：" + error);
        if (loaded.dungeons.run) {
            check(saveToJson(loaded) == saved, "秘境状态读写保持稳定");
            check(availableNodes(g) == availableNodes(loaded), "读档不重新生成线路与可走节点");
            check(dungeonStateError(loaded).empty(), "读档后的秘境仍通过状态校验");
        }
        const int stored = g.dungeons.run->stones, days = g.dungeons.run->elapsedDays;
        const int beforeStones = g.player.stones;
        check(dungeonLeave(g).empty(), "主动撤离秘境可带回已有奖励");
        check(std::none_of(g.dungeons.sites.begin(), g.dungeons.sites.end(), [&](const auto& site) { return site.id == siteId; }), "未通关主动退出后地图入口立即消失");
        checkEq(g.player.stones, beforeStones + stored, "主动撤离真实转移探索灵石");
        check(g.job && g.job->actionType == "socialElapsed" && g.job->remaining == days, "探索耗时冻结为真实日历工作，不能免费修炼");
        g.job.reset();
        const auto exited = saveToJson(g);
        check(!dungeonEnter(g, siteId).empty(), "中途退出后不能重入同一秘境刷奖励");
        check(saveToJson(g) == exited, "重复入场拒绝不产生新路线或扣费");
        check(g.player.arts.spells == originalSpells && g.player.arts.knownSpells == originalKnown, "离开秘境后临时神通消失且原构筑完整");
    }
    {
        Game g = dungeonGame(6);
        g.player.location = "tianji";
        const auto options = dungeonIntelOptions(g);
        check(std::none_of(options.begin(), options.end(), [](const auto& item) { return item.grade <= 2; }), "天机阁只售高阶材料情报，不售一二阶材料位置");
        const auto choice = std::find_if(options.begin(), options.end(), [](const auto& item) { return item.kind == "material" && item.grade == 3 && item.enabled; });
        check(choice != options.end(), "天机阁目录包含与金丹匹配的定向高阶矿材");
        if (choice == options.end()) return;
        const auto target = *choice;
        const auto invalid = saveToJson(g);
        check(!dungeonIntel(g, "material", "invented-material").empty(), "缺失或伪造目标不能购买情报");
        check(saveToJson(g) == invalid, "错误材料情报不扣费用");
        check(!dungeonIntel(g, "material", "iron").empty() && saveToJson(g) == invalid, "直接提交一阶材料ID也不能绕过情报目录购买");
        const int stones = g.player.stones;
        const int original = bagTarget(g.player, target.kind, target.id, target.name);
        check(dungeonIntel(g, target.kind, target.id).empty(), "付费购买指定目标材料副本的位置");
        checkEq(g.player.stones, stones - target.cost, "购买只收取一次真实情报费用");
        checkEq(bagTarget(g.player, target.kind, target.id, target.name), original, "购买位置不直接赠送目标材料");
        const auto* site = siteType(g, "intel");
        check(site && site->targetId == target.id && site->targetGrade == 3, "定向副本冻结指定材料和品阶");
        const auto paid = saveToJson(g);
        check(!dungeonIntel(g, target.kind, target.id).empty(), "每月最多购买一份材料位置情报");
        check(saveToJson(g) == paid, "重复购买不重复扣费用");
        if (!site) return;
        const string id = site->id, location = site->location;
        check(!dungeonEnter(g, id).empty(), "买到情报之后仍须赶往地图真实入口");
        g.player.location = location;
        check(dungeonEnter(g, id).empty(), "定向副本到达后可以进入");
        check(mfVisitRoomType(g, "boss"), "定向秘境沿真实合法路线抵达首领");
        check(g.player.arts.battle.has_value(), "终点材料验证停在实际首领战尚未结算时");
        if (!g.dungeons.run || !g.player.arts.battle) return;
        const int BeforeBossTargetCount = stashTarget(*g.dungeons.run);
        check(!g.dungeons.run->targetGranted, "首领战前未发放保底目标材料");
        winCurrentBattle(g, 75);
        check(g.dungeons.run && g.dungeons.run->targetGranted, "首领胜利实际发放终点保底");
        if (!g.dungeons.run) return;
        checkEq(stashTarget(*g.dungeons.run) - BeforeBossTargetCount, 10, "首领相对沿途既有库存恰好追加十份目标材料");
        completeRoute(g);
        check(g.dungeons.run && g.dungeons.run->phase == "complete", "沿路线击败终点首领后真实通关");
        if (!g.dungeons.run) return;
        check(g.dungeons.run->targetGranted && g.dungeons.run->cleared, "Boss结算登记一次终点固定掉落");
        const int targetCount = stashTarget(*g.dungeons.run);
        check(targetCount >= 10, "定向目标最终库存包含十份保底及可能沿途掉落");
        check(g.dungeons.run->bossRewardClaimed, "定向副本保底材料之外仍实际选择永久通关奖励");
        checkEq(bagTarget(g.player, target.kind, target.id, target.name), original, "通关前后材料先放探索行囊，未提前写入普通行囊");
        check(dungeonStateError(g).empty(), "完整通关路线与终点掉落通过校验");
        Battle replay; replay.dungeonSite = id; replay.dungeonNode = g.dungeons.run->current;
        const auto complete = saveToJson(g);
        dungeonBattleComplete(g, replay, true);
        check(saveToJson(g) == complete, "重复结算Boss不会重复获得十份目标材料");
        check(dungeonLeave(g).empty(), "通关后撤离并带出材料");
        checkEq(bagTarget(g.player, target.kind, target.id, target.name), original + targetCount, "通关材料数量完整落入常规炼器行囊");
    }
    for (const string kind : {"learnSpell", "manual", "stones"}) {
        Game g = dungeonGame();
        const auto beforeKnown = g.player.arts.knownSpells;
        const auto carried = g.player.arts.spells;
        const int stones = g.player.stones;
        check(startWild(g), "普通秘境永久奖励测试进入真实爬塔");
        if (!g.dungeons.run) return;
        const auto frozen = g.dungeons.run->bossRewards;
        completeRoute(g, false);
        check(g.dungeons.run && g.dungeons.run->cleared && g.dungeons.run->phase == "reward" && !g.dungeons.run->bossRewardClaimed, "击败首领后进入永久通关奖励选择");
        if (!g.dungeons.run) return;
        check(g.dungeons.run->targetKind.empty() && !g.dungeons.run->targetGranted, "普通通关不发材料十份主奖励");
        check(g.dungeons.run->bossRewards.size() == frozen.size(), "一路临时卡牌奖励不改变入场前永久目标");
        check(dungeonStateError(g).empty(), "待领取永久首领奖励可安全保存");
        const auto pending = saveToJson(g);
        check(!dungeonLeave(g).empty() && saveToJson(g) == pending, "未选择永久通关奖励不能直接离开而漏领奖");
        Game loaded; string error;
        check(loadFromJson(pending, loaded, &error), "首领永久奖励选择可保存读回：" + error);
        if (loaded.dungeons.run) check(saveToJson(loaded) == pending, "读档不重新挑选永久神通或抄本");
        const auto selected = std::find_if(g.dungeons.run->rewards.begin(), g.dungeons.run->rewards.end(), [&](const auto& reward) { return reward.kind == kind; });
        check(selected != g.dungeons.run->rewards.end(), "初期三选一包含" + kind);
        if (selected == g.dungeons.run->rewards.end()) continue;
        const auto reward = *selected;
        const auto deck = g.dungeons.run->deck;
        const int stash = g.dungeons.run->stones;
        check(dungeonClaim(g, reward.id).empty(), "领取实际永久通关奖励" + kind);
        check(g.dungeons.run->phase == "complete" && g.dungeons.run->bossRewardClaimed, "永久奖励领取后锁定完成状态");
        check(g.dungeons.run->bossRewardTaken && g.dungeons.run->bossRewardTaken->kind == kind && g.dungeons.run->bossRewardTaken->id == reward.id, "完成界面保存实际领取的永久奖励");
        check(g.player.arts.spells == carried, "永久神通领悟不自动挤占外部携带槽");
        check(g.dungeons.run->deck.size() == deck.size(), "永久奖励不混入临时神通牌组");
        if (kind == "learnSpell") {
            check(std::find(g.player.arts.knownSpells.begin(), g.player.arts.knownSpells.end(), reward.spell) != g.player.arts.knownSpells.end(), "神通奖励永久加入已领悟列表");
            checkEq(g.player.arts.knownSpells.size(), beforeKnown.size() + 1, "一次永久奖励只领悟一门神通");
        } else if (kind == "manual") {
            check(std::find(g.player.arts.bookList.begin(), g.player.arts.bookList.end(), reward.manual) != g.player.arts.bookList.end(), "抄本奖励永久进入手中功法列表");
            check(!g.player.arts.manuals.count(reward.manual), "获得抄本仍需要实际修习，不直接叠功法层数");
        } else {
            checkEq(reward.amount, 250, "低阶九层秘境主灵石奖励至少二百五十");
            checkEq(g.dungeons.run->stones, stash + reward.amount, "灵石三选一真实记入探索行囊");
        }
        const auto claimed = saveToJson(g);
        check(!dungeonClaim(g, reward.id).empty() && saveToJson(g) == claimed, "永久通关奖励不能重复领取");
        check(dungeonStateError(g).empty(), "领取永久奖励后的完整状态通过校验");
        check(loadFromJson(claimed, loaded, &error) && saveToJson(loaded) == claimed, "永久实取记录与知识或灵石可稳定往返存档：" + error);
        const int actualStash = g.dungeons.run->stones;
        check(dungeonLeave(g).empty(), "永久奖励已领后可离开秘境");
        checkEq(g.player.stones, stones + actualStash, "主奖励灵石与沿途所得完整带出");
        if (kind == "learnSpell") check(std::find(g.player.arts.knownSpells.begin(), g.player.arts.knownSpells.end(), reward.spell) != g.player.arts.knownSpells.end(), "临时牌组消失后永久神通仍保留");
    }
    {
        Game g = dungeonGame();
        const auto frozen = g.dungeons.sites.front().bossRewards;
        const auto spell = std::find_if(frozen.begin(), frozen.end(), [](const auto& reward) { return reward.kind == "learnSpell"; });
        check(spell != frozen.end(), "初始秘境冻结未领悟神通主奖励");
        if (spell != frozen.end()) {
            g.player.arts.knownSpells.push_back(spell->spell);
            startWild(g); completeRoute(g, false);
            const int stored = g.dungeons.run->stones;
            check(dungeonClaim(g, spell->id).empty(), "入场前已经学会冻结神通时允许稳定灵石折算");
            checkEq(g.dungeons.run->stones, stored + 250, "已有神通固定折算主灵石奖励而不重掷知识");
            check(g.dungeons.run->bossRewardTaken && g.dungeons.run->bossRewardTaken->kind == "stones" && g.dungeons.run->bossRewardTaken->amount == 250, "重复神通完成界面显示实际折算灵石");
        }
        Game book = dungeonGame();
        const auto manual = std::find_if(book.dungeons.sites.front().bossRewards.begin(), book.dungeons.sites.front().bossRewards.end(), [](const auto& reward) { return reward.kind == "manual"; });
        check(manual != book.dungeons.sites.front().bossRewards.end(), "初始秘境拥有永久功法抄本选项");
        if (manual != book.dungeons.sites.front().bossRewards.end()) {
            const auto reward = *manual;
            book.economy.books.push_back(reward.manual);
            startWild(book); completeRoute(book, false);
            const int stored = book.dungeons.run->stones, copies = book.economy.books.size();
            check(dungeonClaim(book, reward.id).empty(), "手中已有冻结抄本时允许固定折算灵石");
            checkEq(book.economy.books.size(), copies, "重复抄本不额外增加功法库存");
            checkEq(book.dungeons.run->stones, stored + 250, "重复抄本折算同档主灵石奖励");
            check(book.dungeons.run->bossRewardTaken && book.dungeons.run->bossRewardTaken->kind == "stones", "重复抄本也保存实取灵石记录");
        }
        Game all = dungeonGame();
        for (const auto& spellDef : allSpells()) if (std::find(all.player.arts.knownSpells.begin(), all.player.arts.knownSpells.end(), spellDef.id) == all.player.arts.knownSpells.end()) all.player.arts.knownSpells.push_back(spellDef.id);
        for (const auto& manual : data::BaseManualDef_TABLE()) all.economy.books.push_back(manual.id);
        all.dungeons = DungeonWorld{}; ensureDungeons(all);
        check(all.dungeons.sites.front().bossRewards.size() == 1 && all.dungeons.sites.front().bossRewards.front().kind == "stones", "没有可用新知识时只保留稳定灵石兜底");
        startWild(all); completeRoute(all, false);
        const int cash = all.dungeons.run->stones;
        check(dungeonClaim(all, "skip").empty(), "通关跳过知识选项会领取最大灵石兜底");
        checkEq(all.dungeons.run->stones, cash + 250, "跳过通关选择仍获得足够主奖励");
    }
    {
        Game g = dungeonGame(3);
        check(startWild(g), "斗法逃离结算测试进入秘境");
        if (!g.dungeons.run) return;
        const string escapedSite = g.dungeons.run->siteId;
        g.dungeons.run->stones = 51;
        g.dungeons.run->herbs["银月花"] = 5;
        const int stones = g.player.stones, herbs = g.player.herbs;
        dungeonChoose(g, availableNodes(g).front());
        if (g.player.arts.battle) { g.player.arts.battle->hp = jsRound(g.player.arts.battle->max * 0.25); battleFinish(g, false, true); }
        check(!g.dungeons.run && !g.ended, "秘境斗法逃离终止爬塔并保留这一世");
        check(std::none_of(g.dungeons.sites.begin(), g.dungeons.sites.end(), [&](const auto& site) { return site.id == escapedSite; }), "斗法逃离也移除未通关秘境入口");
        checkEq(g.player.stones, stones + 25, "斗法逃离仅保留一半已获得灵石，向下取整");
        checkEq(g.player.herbs, herbs + 2, "斗法逃离仅带回一半已得药材");
        check(g.player.health >= 1 && g.job.has_value(), "残血逃离且需要结算真实探索天数");
    }
    {
        Game g = dungeonGame(6);
        g.clock.days = 49 * 360; g.year = 49;
        g.player.age = g.clock.ageBase + g.year;
        dungeonYearTick(g);
        const auto* special = siteType(g, "tianmo");
        check(special != nullptr, "天魔眼当期开启可检查中途退出规则");
        if (!special) return;
        const string id = special->id;
        g.player.location = special->location;
        check(dungeonEnter(g, id).empty(), "进入当期天魔眼秘境");
        check(dungeonLeave(g).empty(), "未通关即可主动退出天魔眼");
        auto missing = [&](const Game& game) { return std::none_of(game.dungeons.sites.begin(), game.dungeons.sites.end(), [&](const auto& site) { return site.id == id; }); };
        check(missing(g), "退出后天魔眼本期入口立即消失");
        ensureDungeons(g); dungeonYearTick(g);
        check(missing(g), "查询及同年刷新不能复活已经退出的特殊秘境");
        Game loaded; string error;
        check(loadFromJson(saveToJson(g), loaded, &error), "退出特殊秘境状态可读档：" + error);
        check(missing(loaded), "读档不能重新生成本期已消失的天魔眼");
        g.clock.days += 360; ++g.year; ++g.player.age;
        dungeonYearTick(g);
        check(missing(g), "下年仍在相同开放窗口时不会重建退出入口");
        g.clock.days = 99 * 360; g.year = 99; g.player.age = g.clock.ageBase + g.year;
        dungeonYearTick(g);
        const auto* later = siteType(g, "tianmo");
        check(later && later->id != id, "天魔眼下一期开启仍正常生成新入口");
    }
    {
        Game g = dungeonGame(); startWild(g);
        if (!g.dungeons.run) return;
        const string id = g.dungeons.run->siteId;
        const int stones = g.player.stones;
        g.dungeons.run->stones = 100; g.dungeons.run->elapsedDays = 17;
        dungeonAbandon(g);
        check(!g.dungeons.run && !g.job, "外部身亡中断秘境不会创建耗时工作");
        checkEq(g.player.stones, stones, "外部中断完全丢弃临时物资");
        check(std::none_of(g.dungeons.sites.begin(), g.dungeons.sites.end(), [&](const auto& site) { return site.id == id; }), "外部中断同样移除未通关入口");
    }
    {
        Game g = dungeonGame(); startWild(g);
        if (!g.dungeons.run) return;
        g.dungeons.run->relics = {"spirit-bead", "jade-slip", "stone-ward", "spring-vial"};
        dungeonChoose(g, availableNodes(g).front());
        check(g.player.arts.battle.has_value(), "秘境遗物测试启动真实战斗");
        if (g.player.arts.battle) {
            checkEq(g.player.arts.battle->maxMana, battleEnergy(g.player) + 1, "聚灵珠实际增加每回合自然回复灵力");
            checkEq(g.player.arts.battle->hand.size(), 6, "洞观玉简实际增加起手抽牌");
            check(g.player.arts.battle->shield >= jsRound(g.player.arts.battle->max * 0.08), "磐石符印实际产生起手护盾");
            battleTurn(g, "end");
            if (g.player.arts.battle) checkEq(g.player.arts.battle->hand.size(), 6, "洞观玉简后续回合持续增加抽牌");
            winCurrentBattle(g, 50);
            checkNear(g.dungeons.run->health, 56, "回春露瓶实际回复战后探索气血", 1.1);
        }
    }
    {
        Game g = dungeonGame(3); startWild(g);
        if (!g.dungeons.run) return;
        check(mfVisitRoomType(g, "event"), "根据真实随机路线逐层抵达奇遇，不假设固定列");
        if (!g.dungeons.run) return;
        check(g.dungeons.run->phase == "reward" && !g.dungeons.run->rewards.empty() &&
            std::all_of(g.dungeons.run->rewards.begin(), g.dungeons.run->rewards.end(),
                [](const auto& Reward) { return Reward.kind == "eventChoice"; }),
            "新版奇遇生成实际主题对应的冻结事件选项");
        const auto EventChoice = std::find_if(g.dungeons.run->rewards.begin(), g.dungeons.run->rewards.end(),
            [&](const auto& Reward) { return mfQuoteDungeonEvent(g, Reward.id).Enabled; });
        check(EventChoice != g.dungeons.run->rewards.end(), "真实奇遇存在符合当前资源的可选取舍");
        if (EventChoice != g.dungeons.run->rewards.end()) {
            const string EventRewardId = EventChoice->id;
            const auto Quote = mfQuoteDungeonEvent(g, EventRewardId);
            const auto KnownSpells = g.player.arts.knownSpells;
            const int Health = g.dungeons.run->health, Stones = g.dungeons.run->stones;
            const auto DeckCount = g.dungeons.run->deck.size(), RelicCount = g.dungeons.run->relics.size();
            check(dungeonClaim(g, EventRewardId).empty(), "通过正式领奖入口实际兑现奇遇取舍");
            check(g.dungeons.run->phase == "route" && g.dungeons.run->rewards.empty(), "奇遇兑现后清空其他选项并回到路线");
            if (Quote.HealthCost > 0) checkEq(g.dungeons.run->health, Health - Quote.HealthCost, "奇遇实际付血与报价一致");
            if (Quote.StoneCost > 0) checkEq(g.dungeons.run->stones, Stones - Quote.StoneCost, "奇遇实际费用与报价一致");
            check(g.dungeons.run->health != Health || g.dungeons.run->stones != Stones ||
                g.dungeons.run->deck.size() != DeckCount || g.dungeons.run->relics.size() != RelicCount,
                "选中的奇遇实际改变临时物资、牌组、遗物或气血");
            check(g.player.arts.knownSpells == KnownSpells, "奇遇临时构筑收益不写入永久神通");
            const string AfterEvent = saveToJson(g);
            check(!dungeonClaim(g, EventRewardId).empty() && saveToJson(g) == AfterEvent, "奇遇选择不能重复领取或重复扣资源");
        }
        check(dungeonStateError(g).empty(), "新版奇遇实际交互后状态保持一致");

        // 各房间使用独立初始run，避免先前选择的合法分支让其他类型变得不可达。
        Game Merchant = dungeonGame(3); check(startWild(Merchant), "游商交互建立独立合法路线");
        check(mfVisitRoomType(Merchant, "merchant"), "根据真实连线逐层抵达游商");
        if (!Merchant.dungeons.run) return;
        const auto shop = std::find_if(Merchant.dungeons.run->rewards.begin(), Merchant.dungeons.run->rewards.end(), [](const auto& reward) { return reward.kind == "buyRelic"; });
        check(shop != Merchant.dungeons.run->rewards.end(), "游商拥有可付费购买的真实秘境遗物");
        if (shop != Merchant.dungeons.run->rewards.end()) {
            const string rewardId = shop->id, relicId = shop->relic;
            const int cost = shop->amount, originalStones = Merchant.dungeons.run->stones;
            Merchant.dungeons.run->stones = 0;
            const auto noCash = saveToJson(Merchant);
            check(!dungeonClaim(Merchant, rewardId).empty(), "探索灵石不足不能挪用玩家普通行囊购买秘境遗物");
            check(saveToJson(Merchant) == noCash, "游商购买失败不改变遗物和探索资金");
            Merchant.dungeons.run->stones = std::max(originalStones, cost);
            const int before = Merchant.dungeons.run->stones;
            check(dungeonClaim(Merchant, rewardId).empty(), "可用本次探索获得的灵石购买遗物");
            checkEq(Merchant.dungeons.run->stones, before - cost, "游商消耗实际探索资金");
            check(std::find(Merchant.dungeons.run->relics.begin(), Merchant.dungeons.run->relics.end(), relicId) != Merchant.dungeons.run->relics.end(), "购买的遗物成为本次探索战斗被动");
            const string Purchased = saveToJson(Merchant);
            check(!dungeonClaim(Merchant, rewardId).empty() && saveToJson(Merchant) == Purchased, "游商购买不能重复提交再次扣费");
        }
        check(dungeonStateError(Merchant).empty(), "游商实际购买后状态保持一致");

        Game Camp = dungeonGame(3); check(startWild(Camp), "营地交互建立独立合法路线");
        check(mfVisitRoomType(Camp, "camp"), "根据真实连线逐层抵达营地");
        if (!Camp.dungeons.run) return;
        const auto remove = std::find_if(Camp.dungeons.run->rewards.begin(), Camp.dungeons.run->rewards.end(), [](const auto& reward) { return reward.kind == "remove"; });
        check(remove != Camp.dungeons.run->rewards.end(), "营地提供针对额外神通的实际删牌选项");
        check(std::none_of(Camp.dungeons.run->rewards.begin(), Camp.dungeons.run->rewards.end(), [&](const auto& reward) {
            if (reward.kind != "remove") return false;
            const auto card = std::find_if(Camp.dungeons.run->deck.begin(), Camp.dungeons.run->deck.end(), [&](const auto& item) { return item.uid == reward.spell; });
            return card != Camp.dungeons.run->deck.end() && (card->spell == "strike" || card->spell == "defend");
        }), "营地不能删除必带普通攻击或普通防御");
        if (remove != Camp.dungeons.run->rewards.end()) {
            const string id = remove->id, CardUid = remove->spell; const int count = Camp.dungeons.run->deck.size();
            check(dungeonClaim(Camp, id).empty(), "营地确认删除一张额外神通");
            checkEq(Camp.dungeons.run->deck.size(), count - 1, "临时牌组实际移除所选的一张牌");
            check(std::none_of(Camp.dungeons.run->deck.begin(), Camp.dungeons.run->deck.end(),
                [&](const auto& Card) { return Card.uid == CardUid; }), "营地删除的确为所选UID实例");
        }
        check(dungeonStateError(Camp).empty(), "营地实际删牌后状态保持一致");
    }
    {
        Game g = dungeonGame(9);
        check(startWild(g), "高境界进入十二层秘境");
        if (!g.dungeons.run) return;
        checkEq(g.dungeons.run->nodes.size(), (12 - 1) * 3 + 1, "元婴秘境增加到十二层");
        const auto original = g;
        g.dungeons.run->nodes[0].type = "chest";
        check(!dungeonStateError(g).empty(), "读档不能把斗法节点篡改成免费宝箱");
        g = original; g.dungeons.run->deck.front().spell = "unknown-card";
        check(!dungeonStateError(g).empty(), "非法临时神通牌拒绝导入");
        g = original; g.dungeons.run->relics.push_back("unknown-relic");
        check(!dungeonStateError(g).empty(), "非法秘境遗物拒绝导入");
        g = original; g.dungeons.run->targetGranted = true;
        check(!dungeonStateError(g).empty(), "提前伪造终点材料发放状态被拒绝");
        g = original; ++g.dungeons.run->bossRewards.back().amount;
        check(!dungeonStateError(g).empty(), "途中不能篡改入场前冻结的永久奖励数量");
        g = original;
        for (auto& site : g.dungeons.sites) if (site.id == g.dungeons.run->siteId) ++site.bossRewards.back().amount;
        ++g.dungeons.run->bossRewards.back().amount;
        check(!dungeonStateError(g).empty(), "同时伪造入口与探索灵石奖励仍拒绝导入");
        g = original; g.dungeons.run->health = -1;
        check(!dungeonStateError(g).empty(), "负数探索气血拒绝导入");
        Game god = dungeonGame(12); startWild(god);
        check(god.dungeons.run && god.dungeons.run->nodes.size() == 37, "化神秘境达到十三层上限");
        const auto options = dungeonIntelOptions(god);
        check(std::any_of(options.begin(), options.end(), [](const auto& item) { return item.kind == "herb" && item.grade == 6 && item.stage == 12; }), "化神开放六阶灵药的定向秘境情报");
        check(std::any_of(options.begin(), options.end(), [](const auto& item) { return item.kind == "material" && item.grade == 5 && item.stage == 12; }), "化神开放五阶炼器材料定向情报");
    }
    for (const string phase : {"route", "battle", "complete"}) {
        Game g = dungeonGame(3);
        check(startWild(g), "旧材料主奖励秘境迁移测试进入真实路线");
        if (!g.dungeons.run) return;
        if (phase == "battle") dungeonChoose(g, availableNodes(g).front());
        else if (phase == "complete") {
            completeRoute(g, false);
            if (!g.dungeons.run) return;
            g.dungeons.run->materials[data::MaterialDef_TABLE().front().id] += 10;
        }
        const auto legacy = legacyDungeonSave(g, phase == "complete");
        Game loaded; string error;
        check(loadFromJson(legacy.dump(), loaded, &error), "规则四旧" + phase + "副本可迁移永久通关目标：" + error);
        if (!loaded.dungeons.run) continue;
        check(loaded.dungeons.run->targetKind.empty() && !loaded.dungeons.run->targetGranted && !loaded.dungeons.run->bossRewards.empty(), "旧普通副本清理材料主目标并补冻结永久选择");
        checkEq(loaded.rng, g.rng, "旧副本奖励迁移不消耗世界随机数");
        check(dungeonStateError(loaded).empty(), "迁移后的路线与奖励状态有效");
        if (phase == "battle") {
            check(loaded.player.arts.battle && loaded.dungeons.run->phase == "battle", "旧在途斗法继续保留真实战斗上下文");
            winCurrentBattle(loaded);
            check(loaded.dungeons.run && loaded.dungeons.run->phase == "reward" && !loaded.dungeons.run->cleared, "迁移后旧普通节点战斗仍给临时卡奖励");
        } else if (phase == "complete") {
            check(loaded.dungeons.run->cleared && loaded.dungeons.run->phase == "reward" && !loaded.dungeons.run->bossRewardClaimed, "旧已通关未退出副本获得新增永久奖励选择");
            checkEq(loaded.dungeons.run->materials[data::MaterialDef_TABLE().front().id], g.dungeons.run->materials[data::MaterialDef_TABLE().front().id], "旧档已得到的材料库存完整保留");
            check(dungeonClaim(loaded, "skip").empty(), "旧已通关副本可实际领取新增主灵石奖励");
        }
        const string stable = saveToJson(loaded);
        Game again;
        check(loadFromJson(stable, again, &error) && saveToJson(again) == stable, "迁移后再次读档不重绘目标或重复开领：" + error);
    }
    {
        Game g = dungeonGame(6);
        g.player.cave = "wuling"; g.player.qiSupply = 3; g.player.caveTier = 2;
        auto legacy = json::Value::parse(saveToJson(g));
        legacy.set("combatRulesVersion", 3);
        legacy.asObject().erase("dungeons"); legacy.asObject().erase("checkpoint");
        legacy.asObject()["player"].asObject().erase("caveTier");
        Game loaded; string error;
        check(loadFromJson(legacy.dump(), loaded, &error), "旧版本没有秘境和洞府字段时可迁移：" + error);
        checkEq(loaded.player.caveTier, 2, "旧三档灵气配给保留最高洞府档位");
        check(loaded.dungeons.initialized && !loaded.dungeons.sites.empty(), "旧存档迁移确定性初始化秘境年表");
        check(!loaded.checkpoint.enabled, "旧createGame存档迁移不擅自启用死亡回档");
        g.player.qiSupply = 2; g.player.caveTier = 0;
        check(!loadFromJson(saveToJson(g), loaded, &error), "新档不能在基础洞府绕过门槛设置二档灵气配给");
        g.player.qiSupply = 0; g.player.stoneSupply = 3;
        check(loadFromJson(saveToJson(g), loaded, &error), "灵石投入三档仍可在基础洞府使用");
    }
    {
        Game g = dungeonGame(6); g.player.location = "tianji";
        Job job; job.actionType = "locationService"; job.kind = "auction"; job.name = "天机珍品会";
        job.origin = "tianji"; job.total = 3; job.remaining = 2; job.started = 0; job.cost = 112;
        g.clock.days = 1; g.job = job;
        auto legacy = json::Value::parse(saveToJson(g)); legacy.set("combatRulesVersion", 3);
        Game loaded; string error;
        check(loadFromJson(legacy.dump(), loaded, &error), "取消的旧版天机珍品会在途档可读回：" + error);
        check(!loaded.job, "已取消的高阶材料直购工作不继续发放材料");
        checkEq(loaded.player.stones, g.player.stones + 112, "旧天机材料购买费用完整退回");
        g.player.location = "dongshi"; g.job->origin = "dongshi"; g.job->total = 2; g.job->remaining = 1; g.job->cost = 68;
        legacy = json::Value::parse(saveToJson(g)); legacy.set("combatRulesVersion", 3);
        check(loadFromJson(legacy.dump(), loaded, &error), "旧版东石谷在途拍卖可按低阶交易迁移：" + error);
        check(loaded.job && loaded.job->cost == 24, "东石谷迁移冻结新的低阶材料价格");
        checkEq(loaded.player.stones, g.player.stones + 44, "旧拍卖价格差额退回且不重复扣费");
        const int stones = loaded.player.stones;
        Game again;
        check(loadFromJson(saveToJson(loaded), again, &error), "迁移后的在途拍卖仍可再次保存读取");
        checkEq(again.player.stones, stones, "新版规则标记防止重复获得迁移退款");
    }
    {
        Game g = dungeonGame(); checkpointEnable(g);
        check(g.checkpoint.enabled && !g.checkpoint.snapshot.empty(), "启用死亡回档持有一份完整恢复快照");
        const auto snapshot = json::Value::parse(g.checkpoint.snapshot);
        check(snapshot["checkpoint"]["snapshot"].asString().empty(), "恢复快照本身清除旧快照，避免无限嵌套");
        Game loaded; string error;
        const auto saved = saveToJson(g);
        check(loadFromJson(saved, loaded, &error), "死亡回档信息可安全往返：" + error);
        check(saveToJson(loaded) == saved, "恢复点和游戏状态读写稳定");
        g.player.stones -= 80;
        check(checkpointRestore(g, "秘境失利测试"), "恢复点可真实还原探索前的状态");
        checkEq(g.player.stones, loaded.player.stones, "死亡回档恢复准确的灵石进度");
        checkEq(g.checkpoint.restores, 1, "恢复后保留恢复次数记录");
        auto bad = json::Value::parse(saved);
        bad.asObject()["checkpoint"].set("snapshot", saved);
        check(!loadFromJson(bad.dump(), loaded, &error), "含内层快照的嵌套恢复点被拒绝导入");
        bad = json::Value::parse(saved);
        auto otherLife = json::Value::parse(g.checkpoint.snapshot); otherLife.set("lifeId", "another-life");
        bad.asObject()["checkpoint"].set("snapshot", otherLife.dump());
        check(!loadFromJson(bad.dump(), loaded, &error), "其他人生的死亡恢复点被拒绝导入");
        bad = json::Value::parse(saved); bad.asObject()["checkpoint"].set("days", 1);
        check(!loadFromJson(bad.dump(), loaded, &error), "未来的恢复点日期被拒绝");
        bad = json::Value::parse(saved); bad.asObject()["checkpoint"].set("snapshot", "");
        check(!loadFromJson(bad.dump(), loaded, &error), "已启用但缺快照的恢复状态被拒绝");
        bad = json::Value::parse(saved); bad.asObject()["checkpoint"].set("lastAutoYear", 7);
        check(!loadFromJson(bad.dump(), loaded, &error), "非十年整数的自动存档周期被拒绝");
        bad = json::Value::parse(saved); bad.asObject()["checkpoint"].set("slot", 5);
        check(!loadFromJson(bad.dump(), loaded, &error), "不存在的第五档恢复点被拒绝");
    }
}
} // namespace nztest
