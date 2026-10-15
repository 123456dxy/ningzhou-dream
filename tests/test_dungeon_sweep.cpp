#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/dungeon.h"
#include "core/foundation/state_limits.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <functional>

namespace nztest {
namespace {
using namespace nz;

// 用途：先在入口境界生成真实野外秘境，再提升测试角色；流程：扫荡边界夹具。
// 使用：传入口/玩家小境界；返回：无玩家IO或检查点的独立Game，当前位置为入口。
Game mfSweepFixture(int SiteStage = 0, int PlayerStage = 3) {
    CreateOptions Options; Options.seed = 519104; Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear(); State.checkpoint = AutoCheckpoint{};
    State.player.arts.battle.reset(); State.player.traits.clear(); State.player.talent.clear();
    State.player.stage = State.player.highestStage = SiteStage;
    State.player.lifespan = realmOf(SiteStage).life; State.player.health = 100;
    State.player.stones = 10000; State.dungeons = DungeonWorld{}; ensureDungeons(State);
    State.player.location = State.dungeons.sites.front().location;
    State.player.stage = State.player.highestStage = PlayerStage;
    State.player.lifespan = realmOf(PlayerStage).life;
    return State;
}

// 用途：检验报价与命令同步拒绝且完整状态不变；流程：非法/过期/忙碌定向回归。
// 使用：传可修改的临时Game与入口；返回：无，断言计入harness。
void mfCheckSweepRejected(Game& State, const string& SiteId, const string& Label) {
    const string Before = saveToJson(State);
    const auto Quote = mfQuoteDungeonSweep(State, SiteId);
    check(!Quote.Enabled && !Quote.Reason.empty(), Label + "的报价拒绝且说明原因");
    check(!mfSweepDungeon(State, SiteId).empty(), Label + "的实际扫荡拒绝");
    checkStr(saveToJson(State), Before, Label + "失败不改变完整状态");
}
}

// 用途：覆盖越大境界扫荡、冻结终奖、目标材料、重复/过期、纯查询及存读档。
// 流程：独立秘境回归；返回：无，harness汇总，不访问真实服务和玩家存档。
void mfRunDungeonSweepTests() {
    using namespace nz;
    std::printf("\n--- 越境秘境一键扫荡 ---\n");
    for (const auto [SiteStage, PlayerStage] : {std::pair{0, 3}, {2, 3}, {3, 6}, {5, 6}, {6, 9}, {8, 9}, {9, 12}, {11, 12}}) {
        Game State = mfSweepFixture(SiteStage, PlayerStage);
        const auto Quote = mfQuoteDungeonSweep(State, State.dungeons.sites.front().id);
        check(Quote.Enabled && Quote.Reason.empty(), "至少高一大境界可扫荡：" + std::to_string(SiteStage) + "→" + std::to_string(PlayerStage));
        checkEq(Quote.RequiredStage, (SiteStage / 3 + 1) * 3, "扫荡门槛以入口大境界计算");
        checkEq(Quote.Days, 1, "扫荡报价固定耗时一天");
    }
    for (const auto [SiteStage, PlayerStage] : {std::pair{0, 2}, {3, 5}, {6, 8}, {9, 11}, {12, 12}}) {
        Game State = mfSweepFixture(SiteStage, PlayerStage);
        mfCheckSweepRejected(State, State.dungeons.sites.front().id, "同大境界或最高境界入口");
    }
    {
        Game State = mfSweepFixture(); const auto Site = State.dungeons.sites.front();
        const string Before = saveToJson(State); const auto OriginalRng = State.rng;
        for (int Repeat = 0; Repeat < 4; ++Repeat) mfQuoteDungeonSweep(State, Site.id);
        checkStr(saveToJson(State), Before, "反复报价不生成路线或修改存档");
        checkEq(State.rng, OriginalRng, "报价不消费共享RNG");
        const auto Known = State.player.arts.knownSpells; const auto Spells = State.player.arts.spells;
        const auto Materials = State.player.arts.materials; const auto Herbs = State.player.arts.herbs;
        const int Wins = State.player.arts.progress.wins, Stones = State.player.stones, Days = State.clock.days;
        check(mfSweepDungeon(State, Site.id).empty(), "高境界真实扫荡进入最终奖励选择");
        if (!State.dungeons.run) return;
        const auto& Run = *State.dungeons.run;
        check(Run.cleared && Run.phase == "reward" && !Run.bossRewardClaimed && !Run.bossRewardTaken,
            "扫荡已通关但不自动替用户领取奖励");
        checkEq(Run.rewards.size(), Site.bossRewards.size(), "保留入口冻结奖励数量，不强造三选一");
        for (std::size_t Index = 0; Index < Run.rewards.size(); ++Index)
            checkStr(Run.rewards[Index].id, Site.bossRewards[Index].id, "最终选项保持冻结ID和顺序");
        checkEq(Run.elapsedDays, 1, "扫荡只累计一天，不按途经层数累计");
        check(Run.relics.empty() && Run.herbs.empty() && Run.materials.empty() && Run.potions.empty() &&
            Run.stones == 0 && Run.cores == 0 && Run.insight == 0, "扫荡没有沿途或战斗额外掉落");
        check(!State.player.arts.battle && State.player.arts.progress.wins == Wins, "扫荡不制造战斗或击杀统计");
        check(State.player.arts.spells == Spells && State.player.arts.knownSpells == Known &&
            State.player.arts.materials == Materials && State.player.arts.herbs == Herbs, "选择终奖前不改外部构筑或行囊");
        checkEq(State.player.stones, Stones, "扫荡未选择终奖前不预发灵石");
        checkEq(State.clock.days, Days, "扫荡选择终奖期间世界日历仍暂停");
        checkEq(State.rng, OriginalRng, "合法扫荡不虚构战斗随机数");
        check(dungeonStateError(State).empty(), "逐层连线的扫荡通关状态合法");
        Game Restored; string Error; const string PendingReward = saveToJson(State);
        check(loadDetachedFromJson(PendingReward, Restored, &Error), "未领奖扫荡可以存读档：" + Error);
        if (Restored.dungeons.run) checkStr(saveToJson(Restored), PendingReward, "未领奖扫荡读档不重绘路线或奖励");
        mfCheckSweepRejected(State, Site.id, "已经进入的扫荡");
        check(!dungeonLeave(State).empty(), "扫荡后仍必须先选最终奖励");
        check(dungeonClaim(State, "boss-stones").empty(), "原奖励入口可手选冻结灵石宝藏");
        check(!dungeonClaim(State, "boss-stones").empty(), "最终奖励只能选择一次");
        const int RewardStones = State.dungeons.run->stones;
        check(dungeonLeave(State).empty(), "领取最终奖励后沿用正常离境");
        checkEq(State.player.stones, Stones + RewardStones, "离境仅兑现所选冻结宝藏");
        check(State.job && State.job->actionType == "socialElapsed" && State.job->total == 1 && State.job->remaining == 1,
            "离境只安排一天真实socialElapsed工作");
        checkEq(State.clock.days, Days, "离境安排工作而不暗中额外推进世界");
        const auto Result = calendarRun(State, 1);
        check(Result.ok && Result.days == 1 && State.clock.days == Days + 1, "正常日历真实结算扫荡的一天耗时");
        State.pending.reset(); State.job.reset();
        mfCheckSweepRejected(State, Site.id, "已经领取离境的旧入口");
    }
    {
        const std::vector<std::pair<string, std::function<void(Game&)>>> Guards = {
            {"不在入口所在地", [](Game& State) { State.player.location = "wuling"; }},
            {"未定位入口", [](Game& State) { State.dungeons.sites.front().revealed = false; }},
            {"已通关入口", [](Game& State) { State.dungeons.sites.front().cleared = true; }},
            {"已进入标记", [](Game& State) { State.flags["dungeon-enter:" + State.dungeons.sites.front().id] = 1; }},
            {"入口已过期", [](Game& State) { State.clock.days = (State.dungeons.sites.front().expiresYear - 1) * 360; }},
            {"窗口尚未开放", [](Game& State) { ++State.dungeons.sites.front().openedYear; }},
            {"忙碌安排", [](Game& State) { State.job = Job{}; }},
            {"待处理事件", [](Game& State) { State.pending = Pending{}; }},
            {"斗法中", [](Game& State) { State.player.arts.battle = Battle{}; }},
            {"伤势耗尽", [](Game& State) { State.player.health = 0; }},
            {"人生已结束", [](Game& State) { State.ended = true; }},
            {"纪事上限", [](Game& State) { State.eventSeq = StateLimits::MaxPersistedInteger - 1; }},
            {"负纪事编号", [](Game& State) { State.eventSeq = -1; }},
            {"冻结奖励非法", [](Game& State) { ++State.dungeons.sites.front().bossRewards.back().amount; }}
        };
        for (const auto& [Label, Change] : Guards) {
            Game State = mfSweepFixture(); const string SiteId = State.dungeons.sites.front().id;
            Change(State); mfCheckSweepRejected(State, SiteId, Label);
        }
        Game State = mfSweepFixture(); mfCheckSweepRejected(State, "invented-site", "未知入口ID");
        mfCheckSweepRejected(State, "", "空入口ID");
        const string SiteId = State.dungeons.sites.front().id;
        check(dungeonEnter(State, SiteId).empty(), "真实进入后构造在途边界");
        mfCheckSweepRejected(State, SiteId, "已经开始逐层探索");
    }
    {
        Game State = mfSweepFixture(6, 6); State.player.location = "tianji";
        const auto Options = dungeonIntelOptions(State);
        const auto Found = std::find_if(Options.begin(), Options.end(), [](const auto& Entry) {
            return Entry.enabled && Entry.kind == "material" && Entry.grade == 3;
        });
        check(Found != Options.end(), "真实情报目录提供三阶目标材料");
        if (Found != Options.end()) {
            check(dungeonIntel(State, Found->kind, Found->id).empty(), "真实购买并冻结定向秘境");
            const auto Site = State.dungeons.sites.back();
            State.player.stage = State.player.highestStage = 9; State.player.lifespan = realmOf(9).life;
            State.player.location = Site.location;
            const int Owned = State.player.arts.materials.count(Site.targetId) ? State.player.arts.materials.at(Site.targetId) : 0;
            check(mfSweepDungeon(State, Site.id).empty(), "高一大境界扫荡真实定向秘境");
            if (State.dungeons.run) {
                check(State.dungeons.run->targetGranted && State.dungeons.run->materials.at(Site.targetId) == 10,
                    "扫荡保底原目标材料十份，先存探索行囊");
                check(dungeonStateError(State).empty(), "扫荡定向目标材料满足原持久校验");
                check(dungeonClaim(State, "boss-stones").empty() && dungeonLeave(State).empty(), "定向扫荡正常手选与离境");
                checkEq(State.player.arts.materials.at(Site.targetId), Owned + 10, "离境恰兑现一次原目标十份");
            }
        }
    }
    {
        Game State = mfSweepFixture(9, 12);
        const string SiteId = State.dungeons.sites.front().id;
        State.player.arts.spells.clear(); State.player.arts.knownSpells.clear();
        const int Slots = battleSpellSlots(State.player);
        for (const auto& Spell : allSpells()) {
            if (Spell.id == "strike" || Spell.id == "defend" || Spell.stage > 12) continue;
            if (State.player.arts.spells.size() >= static_cast<std::size_t>(Slots)) break;
            State.player.arts.spells.push_back(Spell.id); State.player.arts.knownSpells.push_back(Spell.id);
        }
        check(mfSweepDungeon(State, SiteId).empty(), "当前最高境界可扫低一大境界，真实槽位照常建牌组");
        if (State.dungeons.run) {
            checkEq(State.dungeons.run->deck.size(), 10 + State.player.arts.spells.size() * 2, "扫荡只复制合法携带神通，无沿途临时增牌");
            check(dungeonStateError(State).empty(), "最高境界全槽扫荡保持路线和牌组合法");
        }
    }
    {
        Game State = mfSweepFixture(); auto& Frozen = State.dungeons.sites.front().bossRewards;
        Frozen.erase(std::remove_if(Frozen.begin(), Frozen.end(), [](const auto& Reward) { return Reward.kind != "stones"; }), Frozen.end());
        const string SiteId = State.dungeons.sites.front().id;
        check(mfSweepDungeon(State, SiteId).empty(), "冻结奖励不足三项时仍能扫荡");
        check(State.dungeons.run && State.dungeons.run->rewards.size() == 1, "最终奖励选择保留原一项，不捏造传承");
    }
    {
        Game State = mfSweepFixture(3, 3);
        State.player.traits = {"greenBottle"}; State.player.arts.GreenBottleGrade = 3;
        State.player.cave = State.player.location = "wuling"; State.player.caveTier = 2;
        check(mfOpenBottleMaterialDungeon(State, 4).empty(), "共用原子提交后专属灵材秘境仍正常进入");
        if (State.dungeons.run) {
            const string SiteId = State.dungeons.run->siteId;
            checkEq(State.dungeons.sites.back().stage, State.player.stage, "瓶秘境难度仍同步当前人物，不产生低阶扫荡入口");
            mfCheckSweepRejected(State, SiteId, "当前同境专属秘境已经进入");
        }
    }
}
} // namespace nztest
