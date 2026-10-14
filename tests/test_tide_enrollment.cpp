#include "harness.h"
#include "core/tide.h"
#include "core/sim.h"
#include "core/save.h"
#include "core/interface/views.h"
#include "core/simulation/tide_rules.h"
#include <algorithm>
#include <array>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立无玩家档案副作用的有效兽潮；流程：名单增量和奖励回归；用法：无参数；返回：独立游戏快照。
Game mfEnrollmentGame() {
    CreateOptions Options;
    Options.seed = 20261014;
    Options.traitsExplicit = true;
    auto State = createGame(Options);
    State.npcs.clear();
    State.pending.reset();
    State.job.reset();
    State.checkpoint.enabled = false;
    State.tide.active = true;
    tide::ensureTide(State);
    State.npcs.clear();
    State.tide.participants.clear();
    return State;
}

// 用途：构造可保存的年轻修士；流程：增量名单回归；用法：传稳定ID和境界；返回：无关系引用的存活NPC。
Npc mfEnrollmentNpc(const string& ActorId, int Stage = 3) {
    Npc Actor;
    Actor.id = ActorId;
    Actor.name = "增量测试修士";
    Actor.location = "wuling";
    Actor.stage = Stage;
    Actor.lifespan = 1000;
    return Actor;
}

// 用途：只读按身份查询本期冻结账目；流程：名单断言；用法：传游戏与NPC ID；返回：借用账目或空，不改变随机数。
const TideParticipant* mfFindEnrollment(const Game& State, const string& ActorId) {
    const auto Found = std::find_if(State.tide.participants.begin(), State.tide.participants.end(),
        [&](const TideParticipant& Entry) { return Entry.actorId == ActorId; });
    return Found == State.tide.participants.end() ? nullptr : &*Found;
}

// 用途：构造指定正分名次的未结算榜单；流程：真实排名奖励回归；用法：传名次；返回：冻结筑基入场、当前金丹的独立游戏。
Game mfRankRewardGame(int Rank) {
    auto State = mfEnrollmentGame();
    State.player.stage = 6;
    State.player.highestStage = 6;
    State.player.renown = 40;
    State.tide.playerJoined = true;
    State.tide.playerEntryStage = 3;
    State.tide.playerScore = 100;
    for (int AheadCount = 1; AheadCount < Rank; ++AheadCount) {
        const auto ActorId = "ahead-" + std::to_string(AheadCount);
        State.tide.participants.push_back({ActorId, "榜前修士", "", "", 3, 100 + AheadCount, 0, false});
    }
    State.clock.days = State.tide.endDay;
    State.tide.lastProcessedDay = State.clock.days;
    return State;
}
}

// 用途：覆盖新修士入场、查询纯度与名单持久性；流程：独立定向及核心测试入口；用法：直接调用；返回：无，失败写入测试计数。
void mfRunTideEnrollmentTests() {
    using namespace nz;
    std::printf("\n--- 兽潮增量参战与声望结算 ---\n");
    {
        auto State = mfEnrollmentGame();
        State.npcs.push_back(mfEnrollmentNpc("late-npc"));
        const auto BeforeRng = State.rng;
        tide::dayTick(State);
        const auto* Entry = mfFindEnrollment(State, "late-npc");
        check(Entry && Entry->entryStage == 3 && Entry->score == 0, "开潮后新增NPC在真实动作边界加入本期名单");
        checkEq(State.rng, BeforeRng, "同日增量登记不消耗随机数");
        const auto Snapshot = saveToJson(State);
        tide::dayTick(State);
        checkStr(saveToJson(State), Snapshot, "同日重复推进不重报名、不改积分");
    }
    {
        auto State = mfEnrollmentGame();
        State.npcs.push_back(mfEnrollmentNpc("catchup-new"));
        State.clock.days = State.tide.startDay + 40;
        tide::mfEnrollEligibleNpcs(State);
        check(mfFindEnrollment(State, "catchup-new") == nullptr, "存在未补算旧日期时直接报名延后，防止先登记再追溯刷分");
        tide::dayTick(State);
        const auto* Entry = mfFindEnrollment(State, "catchup-new");
        check(Entry && Entry->score == 0, "补算过去40天不会给当前新NPC虚构历史积分");
        // 固定一个确实出战且击杀的局部种子；只验证新报名者会进入真实模拟，不把概率事件当必然。
        for (std::uint32_t Seed = 0; Seed < 100000; ++Seed) {
            Game Probe; Probe.rng = Seed;
            if (randIntOf(Probe, 1, tide::rules::NormalNpcBattlePace.BattleChanceDenominator) == 1 &&
                randIntOf(Probe, 0, tide::rules::NormalNpcBattlePace.MaximumDailyKills) > 0) {
                State.rng = Seed;
                break;
            }
        }
        State.clock.days++;
        tide::dayTick(State);
        Entry = mfFindEnrollment(State, "catchup-new");
        check(Entry && Entry->score > 0, "新报名修士在后续真实日正常参与积分模拟");
    }
    {
        auto State = mfEnrollmentGame();
        State.npcs.push_back(mfEnrollmentNpc("existing", 9));
        State.tide.participants.push_back({"existing", "冻结姓名", "", "", 3, 18, 2, false});
        auto Dead = mfEnrollmentNpc("dead"); Dead.alive = false;
        auto Beast = mfEnrollmentNpc("beast"); Beast.beast = true;
        State.npcs.push_back(Dead); State.npcs.push_back(Beast);
        State.npcs.push_back(mfEnrollmentNpc(""));
        State.npcs.push_back(mfEnrollmentNpc("player"));
        State.npcs.push_back(mfEnrollmentNpc("duplicate", 3));
        State.npcs.push_back(mfEnrollmentNpc("duplicate", 9));
        auto NamelessFirst = mfEnrollmentNpc("valid-after-nameless", 9);
        NamelessFirst.name.clear();
        State.npcs.push_back(NamelessFirst);
        State.npcs.push_back(mfEnrollmentNpc("valid-after-nameless", 3));
        tide::dayTick(State);
        const auto* Existing = mfFindEnrollment(State, "existing");
        check(Existing && Existing->entryStage == 3 && Existing->score == 18 && Existing->rank == 2 && Existing->name == "冻结姓名",
            "追加名单保留老参战者境界、积分、名次和身份事实");
        checkEq(State.tide.participants.size(), 3, "死亡、妖兽、空ID、玩家保留ID、空姓名和重复NPC不会污染名单");
        const auto* Duplicate = mfFindEnrollment(State, "duplicate");
        check(Duplicate && Duplicate->entryStage == 3, "重复世界ID稳定选择首次有效修士并只登记一次");
        const auto* RepairedIdentity = mfFindEnrollment(State, "valid-after-nameless");
        check(RepairedIdentity && RepairedIdentity->entryStage == 3 && !RepairedIdentity->name.empty(),
            "空姓名的首次同ID记录不会挡住后续有效修士报名");
    }
    {
        auto State = mfEnrollmentGame();
        State.npcs.push_back(mfEnrollmentNpc("view-only"));
        const auto Before = saveToJson(State);
        (void)tide::leaderboard(State);
        (void)api::view::tideState(State);
        checkStr(saveToJson(State), Before, "读取金榜与状态不补报名、不推进RNG或奖励");
    }
    {
        auto State = mfEnrollmentGame();
        State.npcs.push_back(mfEnrollmentNpc("roundtrip"));
        tide::dayTick(State);
        Game Loaded;
        string Error;
        check(loadFromJson(saveToJson(State), Loaded, &Error), "增量参战名单可存档重载：" + Error);
        check(mfFindEnrollment(Loaded, "roundtrip") != nullptr, "重载保留新增NPC的冻结身份");
        State.clock.days++; Loaded.clock.days++;
        tide::dayTick(State); tide::dayTick(Loaded);
        checkStr(saveToJson(State), saveToJson(Loaded), "存档往返后下一日积分与随机序列一致");
    }
    {
        auto State = mfEnrollmentGame();
        State.clock.days = State.tide.endDay;
        State.tide.lastProcessedDay = State.clock.days;
        State.npcs.push_back(mfEnrollmentNpc("expired-new"));
        tide::dayTick(State);
        check(mfFindEnrollment(State, "expired-new") == nullptr, "战期到期不再把新修士补入旧榜");
        const auto Snapshot = saveToJson(State);
        tide::dayTick(State);
        checkStr(saveToJson(State), Snapshot, "已结算战期重复推进保留旧名单");
    }
    {
        auto State = mfEnrollmentGame();
        worldYear(State);
        bool AllLivingEnrolled = true;
        for (const auto& Actor : State.npcs) {
            if (Actor.alive && !Actor.beast && !Actor.id.empty()) AllLivingEnrolled &= mfFindEnrollment(State, Actor.id) != nullptr;
        }
        check(!State.npcs.empty() && AllLivingEnrolled, "同年开潮后人口补充的存活新修士在年度动作收尾立即登记");
    }
    {
        constexpr std::array<std::array<int, 3>, 6> Cases{{
            {{1, 50, 50}}, {{2, 25, 20}}, {{3, 25, 20}}, {{4, 15, 10}}, {{10, 15, 10}}, {{11, 5, 1}}
        }};
        for (const auto& [Rank, StipendYears, RenownReward] : Cases) {
            auto State = mfRankRewardGame(Rank);
            const int BeforeStones = State.player.stones;
            tide::settleIfDue(State);
            checkEq(State.tide.playerRank, Rank, "真实金榜结算使用预期名次");
            checkEq(State.tide.rewardStones, 16 * 12 * StipendYears, "灵石奖按冻结筑基境界和50/25/15/5年俸禄计算");
            checkEq(State.player.stones, BeforeStones + State.tide.rewardStones, "金榜灵石实际入账与回执相等");
            checkEq(State.tide.RewardRenown, RenownReward, "名次声望回执为50/20/10/1");
            checkEq(State.player.renown, 40 + RenownReward, "名次声望真实入账");
            const auto Snapshot = saveToJson(State);
            tide::settleIfDue(State);
            tide::dayTick(State);
            checkStr(saveToJson(State), Snapshot, "奖励与声望一起幂等，不会重复发放");
        }
    }
    {
        auto State = mfRankRewardGame(1);
        State.tide.participants.push_back({"tied", "同分修士", "", "", 3, 100, 0, false});
        tide::settleIfDue(State);
        checkEq(State.tide.playerRank, 1, "玩家同分并列第一保持同档奖励");
        checkEq(State.tide.RewardRenown, 50, "并列第一实际得到50声望");
    }
    {
        auto State = mfRankRewardGame(1);
        State.player.stones = tide::rules::InventoryLimit - 2;
        State.player.renown = tide::rules::InventoryLimit - 3;
        tide::settleIfDue(State);
        checkEq(State.tide.rewardStones, 2, "背包接近上限时灵石回执只记实际入账");
        checkEq(State.tide.RewardRenown, 3, "声望接近上限时回执只记实际入账");
        const auto View = api::view::tideState(State);
        checkEq(View["rewardRenown"].asInt(), 3, "只读奖励视图显示实际声望回执");
        Game Loaded;
        string Error;
        check(loadFromJson(saveToJson(State), Loaded, &Error), "发奖事实可保存重载：" + Error);
        checkEq(Loaded.tide.RewardRenown, 3, "存档往返保留实际声望发奖数");
        const auto Before = saveToJson(Loaded);
        tide::settleIfDue(Loaded);
        checkStr(saveToJson(Loaded), Before, "重载奖励档不会二次发放声望");
    }
    {
        auto State = mfRankRewardGame(1);
        State.tide.playerScore = 0;
        const auto BeforeRenown = State.player.renown;
        tide::settleIfDue(State);
        check(!State.tide.rewardPaid && State.tide.RewardRenown == 0, "零积分不发保底灵石或声望奖励");
        checkEq(State.player.renown, BeforeRenown, "未获得正积分不改变声望");
    }
    {
        auto State = mfRankRewardGame(1);
        State.ended = true;
        const auto BeforeStones = State.player.stones;
        const auto BeforeRenown = State.player.renown;
        tide::settleIfDue(State);
        check(State.tide.settled && !State.tide.rewardPaid, "人生已结束仍保留金榜但不追加遗产奖励");
        checkEq(State.player.stones, BeforeStones, "结束人生不改灵石遗产");
        checkEq(State.player.renown, BeforeRenown, "结束人生不补发排名声望");
    }
    {
        auto State = mfRankRewardGame(1);
        State.tide.settled = true;
        State.tide.rewardPaid = true;
        State.tide.playerRank = 1;
        State.tide.rewardStones = 30 * 16 * 12;
        State.tide.marketExpiresDay = State.tide.endDay + tide::rules::MarketDays;
        State.tide.active = false;
        auto LegacyJson = json::Value::parse(saveToJson(State));
        auto TideJson = LegacyJson["tide"];
        TideJson.asObject().erase("rewardRenown");
        LegacyJson.set("tide", TideJson);
        Game Loaded;
        string Error;
        check(loadFromJson(LegacyJson.dump(), Loaded, &Error), "缺新声望回执字段的旧已结算档可以加载：" + Error);
        const auto Before = saveToJson(Loaded);
        tide::settleIfDue(Loaded);
        checkStr(saveToJson(Loaded), Before, "旧已付款30年奖励事实不升级补发、不追加声望");
    }
}
} // namespace nztest
