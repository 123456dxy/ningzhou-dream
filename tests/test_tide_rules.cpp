#include "harness.h"
#include "core/tide.h"
#include "core/simulation/tide_rules.h"
#include "core/content/npc_combat_profiles.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/play.h"
#include "core/save.h"
#include "core/json.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace nztest {
using namespace nz;
namespace {
// 用途：创建隔离短战期；流程：兽潮规则回归；用法：传测试境界；返回：不触及真实存档的游戏。
Game tideGame(int stage = 9) {
    Game game;
    game.player.name = "兽潮测试者";
    game.player.stage = stage; game.player.location = "wuling";
    game.player.health = 100; game.player.lifespan = 5000; game.player.stones = 100000;
    game.clock.days = 1080; game.clock.ageBase = 16; game.year = 3;
    game.tide.active = true;
    tide::ensureTide(game);
    return game;
}
// 用途：按稳定人物 ID 读积分；流程：日模拟回归；用法：传榜单和 ID；返回：对应积分或 -1。
int npcScore(const Game& game, const string& id) {
    for (const auto& participant : game.tide.participants) if (participant.actorId == id) return participant.score;
    return -1;
}
// 用途：创建已结算市场；流程：交易回归；用法：无参数；返回：仅两种有限材料的隔离游戏。
Game marketGame() {
    auto game = tideGame();
    game.tide.playerJoined = true; game.tide.playerScore = 40; game.tide.playerEntryStage = 9;
    game.clock.days = game.tide.endDay;
    tide::dayTick(game);
    game.tide.marketStock = {{"herb", "6201", 8}, {"material", "gold", 8}};
    return game;
}
// 用途：验证失败交易完全无副作用；流程：交易回归；用法：传游戏与非法请求；返回：无，记录两项断言。
void rejectedPurchase(Game& game, const string& event, const string& kind, const string& id,
                      int count, int quotedPrice, const string& label) {
    const string before = saveToJson(game);
    check(!tide::buyMarket(game, event, kind, id, count, quotedPrice).ok, label + "拒绝");
    checkStr(saveToJson(game), before, label + "不改变余额库存背包时间或随机数");
}

// 用途：建立固定种子的全州参战样本；流程：六十天NPC节奏模拟；返回：32名普通与五宗各一名真实精英的独立游戏。
Game mfNpcPaceGame(std::uint32_t Seed) {
    auto State = tideGame(); State.seed = State.rng = Seed;
    for (int NpcIndex = 0; NpcIndex < 32; ++NpcIndex) {
        Npc Normal; Normal.id = "pace-normal-" + std::to_string(NpcIndex); Normal.name = Normal.id;
        Normal.stage = 9; Normal.lifespan = 1000; State.npcs.push_back(std::move(Normal));
    }
    for (const auto& Profile : content::npcCombatProfiles()) {
        Npc Elite; Elite.id = "pace-elite-" + Profile.id; Elite.name = Elite.id;
        Elite.stage = 9; Elite.lifespan = 1000; Elite.sect = Profile.sect; Elite.combatProfileId = Profile.id;
        State.npcs.push_back(std::move(Elite));
    }
    State.tide = BeastTide{}; State.tide.active = true; tide::ensureTide(State);
    State.tide.playerJoined = true; State.tide.playerEntryStage = 9;
    return State;
}

// 用途：用真实胜场入口登记13次元婴战场击杀；流程：稳定榜首策略v1；返回：无，不重写积分或排名公式。
void mfScoreThirteenHighestKills(Game& State) {
    Battle Victory; Victory.kind = "tide"; Victory.tideEventId = State.tide.eventId;
    Victory.tideBattlefield = 3; Victory.enemyStage = 9;
    for (int KillCount = 0; KillCount < 13; ++KillCount) tide::tideReward(State, true, &Victory);
}

// 用途：将节奏边界样例限定为一个真实参战者；参与封顶及出战概率测试。
// 使用：传隔离游戏，保留第一名NPC和其账本；返回：无，不修改真实存档。
void mfRetainFirstNpcParticipant(Game& State) {
    State.tide.participants.resize(1);
    const auto ActorId = State.tide.participants.front().actorId;
    State.npcs.erase(std::remove_if(State.npcs.begin(), State.npcs.end(),
        [&](const Npc& Actor) { return Actor.id != ActorId; }), State.npcs.end());
}

// 用途：固定12种子测量普通/精英日节奏、战期上限和玩家13胜排名；流程：无渲染真实dayTick模拟v1；返回：断言与每种子JSON结果。
void mfCheckNpcTidePace() {
    constexpr std::array<std::uint32_t, 12> Seeds{{1, 7, 19, 37, 73, 127, 211, 337, 509, 997, 20261013, 314159}};
    int NormalTotalKills = 0, EliteTotalKills = 0, NormalSampleCount = 0, EliteSampleCount = 0;
    for (const auto Seed : Seeds) {
        auto State = mfNpcPaceGame(Seed); mfScoreThirteenHighestKills(State);
        auto Replay = State;
        std::vector<int> PreviousScores(State.tide.participants.size(), 0);
        bool DailyIncrementsValid = true, EventCapsValid = true;
        for (int Day = 1; Day <= 60; ++Day) {
            State.clock.days = State.tide.startDay + Day; tide::dayTick(State);
            for (std::size_t ParticipantIndex = 0; ParticipantIndex < State.tide.participants.size(); ++ParticipantIndex) {
                const auto& Participant = State.tide.participants[ParticipantIndex];
                const int Gain = Participant.score - PreviousScores[ParticipantIndex];
                DailyIncrementsValid &= Gain == 0 || Gain == 27 || (Participant.elite && Gain == 54);
                EventCapsValid &= Participant.score <= (Participant.elite ? 12 : 9) * 27;
                PreviousScores[ParticipantIndex] = Participant.score;
            }
        }
        int NormalMin = 9, NormalMax = 0, EliteMin = 12, EliteMax = 0;
        int SeedNormalKills = 0, SeedEliteKills = 0, SeedNormalCount = 0, SeedEliteCount = 0;
        for (const auto& Participant : State.tide.participants) {
            const int Kills = Participant.score / 27;
            if (Participant.elite) {
                EliteMin = std::min(EliteMin, Kills); EliteMax = std::max(EliteMax, Kills);
                SeedEliteKills += Kills; ++SeedEliteCount;
            } else {
                NormalMin = std::min(NormalMin, Kills); NormalMax = std::max(NormalMax, Kills);
                SeedNormalKills += Kills; ++SeedNormalCount;
            }
        }
        const auto SeedLabel = std::to_string(Seed);
        check(DailyIncrementsValid, "普通每天最多1杀、精英最多2杀且积分倍率不变：种子" + SeedLabel);
        check(EventCapsValid, "60天普通最多9杀、精英最多12杀：种子" + SeedLabel);
        check(State.tide.playerScore == 351 && State.tide.playerRank == 1,
            "最高战场13次真实击杀稳定超过全州NPC：种子" + SeedLabel);
        check(SeedNormalCount == 32 && SeedEliteCount == 5, "精英身份仍来自五宗真实构筑：种子" + SeedLabel);
        Replay.clock.days = Replay.tide.endDay; tide::dayTick(Replay);
        check(saveToJson(State) == saveToJson(Replay), "同种子逐日与批量推进60天确定一致：种子" + SeedLabel);
        NormalTotalKills += SeedNormalKills; EliteTotalKills += SeedEliteKills;
        NormalSampleCount += SeedNormalCount; EliteSampleCount += SeedEliteCount;
        json::Value Summary(json::Object{});
        Summary.set("balance", "npc-active-v2"); Summary.set("policy", "thirteen-highest-kills-v1");
        Summary.set("seed", static_cast<double>(Seed)); Summary.set("durationDays", 60);
        Summary.set("win", State.tide.playerRank == 1); Summary.set("finalScore", State.tide.playerScore);
        Summary.set("normalMinKills", NormalMin); Summary.set("normalMaxKills", NormalMax);
        Summary.set("eliteMinKills", EliteMin); Summary.set("eliteMaxKills", EliteMax);
        Summary.set("normalMeanKills", static_cast<double>(SeedNormalKills) / SeedNormalCount);
        Summary.set("eliteMeanKills", static_cast<double>(SeedEliteKills) / SeedEliteCount);
        Summary.set("failureReason", State.tide.playerRank == 1 ? "" : "player_not_first");
        std::printf("%s\n", Summary.dump().c_str());
    }
    const double NormalMean = static_cast<double>(NormalTotalKills) / NormalSampleCount;
    const double EliteMean = static_cast<double>(EliteTotalKills) / EliteSampleCount;
    check(NormalMean >= 4.0 && NormalMean <= 6.0, "12固定种子普通NPC典型60天击杀均值在4至6之间");
    check(EliteMean >= 10.0 && EliteMean <= 12.0, "12固定种子精英NPC典型60天击杀均值在10至12之间");
    check(EliteMean > NormalMean, "完整构筑优势体现为跨样本更高平均击杀，不保证单次运气胜出");

    auto Capped = mfNpcPaceGame(1);
    for (auto& Participant : Capped.tide.participants) Participant.score = (Participant.elite ? 12 : 9) * 27;
    Capped.tide.participants.front().score = 10000;
    const auto CappedRng = Capped.rng;
    ++Capped.clock.days; tide::dayTick(Capped);
    checkEq(Capped.rng, CappedRng, "全部NPC达到战期上限后不消耗出战或击杀RNG");
    checkEq(Capped.tide.participants.front().score, 10000, "既有大积分不削减且超过上限后不再增长");

    auto Dead = mfNpcPaceGame(7);
    for (auto& Npc : Dead.npcs) Npc.alive = false;
    Npc Outsider; Outsider.id = "beast-not-a-participant"; Outsider.stage = 9; Outsider.beast = true;
    Dead.npcs.push_back(Outsider);
    const auto DeadRng = Dead.rng;
    ++Dead.clock.days; tide::dayTick(Dead);
    checkEq(Dead.rng, DeadRng, "死亡参战者与不可参战妖兽不消耗NPC出战RNG");

    auto Frozen = mfNpcPaceGame(19);
    auto Promoted = Frozen;
    for (auto& Participant : Frozen.tide.participants) Participant.entryStage = 0;
    Promoted.tide.participants = Frozen.tide.participants;
    for (auto& Npc : Frozen.npcs) Npc.stage = 0;
    for (int Day = 1; Day <= 60; ++Day) {
        Frozen.clock.days = Promoted.clock.days = Frozen.tide.startDay + Day;
        tide::dayTick(Frozen); tide::dayTick(Promoted);
    }
    bool FrozenScoresMatch = true;
    for (std::size_t ParticipantIndex = 0; ParticipantIndex < Frozen.tide.participants.size(); ++ParticipantIndex)
        FrozenScoresMatch &= Frozen.tide.participants[ParticipantIndex].score == Promoted.tide.participants[ParticipantIndex].score;
    check(FrozenScoresMatch && Frozen.rng == Promoted.rng, "NPC同期晋阶不改变冻结战场积分或上限计数");

    auto LastKill = mfNpcPaceGame(1978); mfRetainFirstNpcParticipant(LastKill);
    LastKill.tide.participants.front().score = 11 * 27;
    ++LastKill.clock.days; tide::dayTick(LastKill);
    checkEq(LastKill.tide.participants.front().score, 12 * 27, "精英抽中一日2杀但仅余1杀时不越过12杀上限");
    const auto LastKillRng = LastKill.rng;
    ++LastKill.clock.days; tide::dayTick(LastKill);
    checkEq(LastKill.rng, LastKillRng, "最后1杀结算后后续日不再抽无必要RNG");

    auto EliteDay = mfNpcPaceGame(5); mfRetainFirstNpcParticipant(EliteDay);
    auto NormalDay = EliteDay;
    for (auto& Npc : NormalDay.npcs)
        if (Npc.id == NormalDay.tide.participants.front().actorId) Npc.combatProfileId.clear();
    ++NormalDay.clock.days; tide::dayTick(NormalDay);
    ++EliteDay.clock.days; tide::dayTick(EliteDay);
    check(NormalDay.tide.participants.front().score == 0 && NormalDay.rng == 1022226848u,
        "同一固定抽样落在六分之一与四分之一之间时普通不出战且仅抽一次RNG");
    check(EliteDay.tide.participants.front().score == 54 && EliteDay.rng == 3144284287u,
        "同一固定抽样使精英出战并按第二次RNG抽中2杀");
}
}

// 用途：核验固定战期、积分、完整构筑和原子市场；流程：核心自测；用法：由test_main调用；返回：无，失败进入harness计数。
void runTideRuleTests() {
    std::printf("\n--- 兽潮战期、金榜与有限坊市 ---\n");
    mfCheckNpcTidePace();
    {
        auto game = tideGame();
        checkEq(game.tide.endDay - game.tide.startDay, 60, "新规则兽潮固定六十天");
        checkEq(tide::daysLeft(game), 60, "新战期显示实际六十天");
        const string saved = saveToJson(game); const auto rng = game.rng;
        tide::ensureTide(game);
        checkStr(saveToJson(game), saved, "现代兽潮初始化不重置事实");
        checkEq(game.rng, rng, "初始化不重新抽取兽潮结果");
        for (int id = 0; id < 4; ++id) {
            const auto* field = tide::rules::battlefield(id);
            check(field && field->minStage == id * 3, "四战场最低境界稳定");
            const auto attempt = tide::fight(game, id);
            check(attempt.ok && game.player.arts.battle.has_value(), "可迎战不高于自身境界的战场");
            if (game.player.arts.battle) {
                const auto battle = *game.player.arts.battle;
                check(battle.enemyBeast && battle.enemyStage >= id * 3 && battle.enemyStage <= id * 3 + 2,
                      "妖兽仅来自所选大境界");
                check(battle.tideEventId == game.tide.eventId && battle.tideBattlefield == id, "战果上下文在开局已登记");
                tide::tideReward(game, true, &battle);
            }
            game.player.arts.battle.reset(); game.player.health = 100;
        }
        checkEq(game.tide.playerScore, 40, "四境击杀积分1加3加9加27");
        checkEq(game.tide.playerEntryStage, 9, "首次参加时冻结真实境界");
        auto low = tideGame(0);
        const string before = saveToJson(low);
        check(!tide::fight(low, 3).ok && !tide::fight(low, -2).ok && !tide::fight(low, 4).ok, "越境和非法战场不能入场");
        checkStr(saveToJson(low), before, "非法入场不扣时间不投骰不登记参战");
        const auto queryBefore = saveToJson(game);
        tide::battlefields(game); tide::leaderboard(game); tide::statusText(game); tide::marketItems(game);
        checkStr(saveToJson(game), queryBefore, "活动与市场只读查询没有副作用");
    }
    {
        auto game = tideGame();
        game.tide.playerJoined = true; game.tide.playerEntryStage = 3; game.tide.playerScore = 9;
        TideParticipant tied; tied.actorId = "npc-tie"; tied.name = "同分修士"; tied.entryStage = 3; tied.score = 9;
        TideParticipant higher; higher.actorId = "npc-high"; higher.name = "领先修士"; higher.entryStage = 6; higher.score = 20;
        game.tide.participants = {tied, higher};
        auto board = tide::leaderboard(game);
        int playerRank = -1, tiedRank = -2;
        for (const auto& row : board) {
            if (row.actorId == "player") playerRank = row.rank;
            if (row.actorId == "npc-tie") tiedRank = row.rank;
        }
        checkEq(playerRank, 2, "金榜按严格高分人数排名");
        checkEq(tiedRank, playerRank, "同分修士与玩家并列");
        game.player.stage = 12;
        const int before = game.player.stones;
        game.clock.days = game.tide.endDay;
        tide::dayTick(game);
        check(game.tide.settled && game.tide.rewardPaid && !game.tide.active, "期末自动结榜发奖并关闭战场");
        checkEq(game.tide.rewardStones, 16 * 12 * 25, "前三奖励按入场筑基俸禄二十五年计算");
        checkEq(game.player.stones, before + game.tide.rewardStones, "奖励真实进入账户");
        checkEq(game.tide.totalScore, 38, "全体积分包含玩家与所有参战记录");
        checkEq(game.tide.marketExpiresDay - game.tide.endDay, 30, "战后坊市固定三十天");
        auto atNewYear = game;
        atNewYear.tide.startDay = 1740; atNewYear.tide.endDay = 1800;
        atNewYear.tide.lastProcessedDay = 1800; atNewYear.tide.marketExpiresDay = 1830;
        atNewYear.clock.days = 1800; atNewYear.year = 5;
        const auto retained = saveToJson(atNewYear);
        tide::yearTick(atNewYear);
        checkStr(saveToJson(atNewYear), retained, "潮退跨年当天不覆盖金榜和仍开放的市场");
        const auto paid = saveToJson(game);
        tide::settleIfDue(game); tide::dayTick(game);
        checkStr(saveToJson(game), paid, "重复日钩结榜不重发奖金或刷新库存");
        checkEq(tide::rules::rewardStones(9, 1), 45 * 12 * 50, "元婴榜首获五十年基础俸禄");
        checkEq(tide::rules::rewardStones(6, 11), 28 * 12 * 5, "金丹有效参战保底五年基础俸禄");
        auto ended = tideGame(); ended.ended = true; ended.tide.playerJoined = true; ended.tide.playerScore = 5;
        const int endedStones = ended.player.stones;
        ended.clock.days = ended.tide.endDay; tide::dayTick(ended);
        check(ended.tide.settled && !ended.tide.rewardPaid && !tide::marketOpen(ended), "寿尽死亡后只保留榜单不再交易或发奖");
        checkEq(ended.player.stones, endedStones, "人生结束后账户不变");
        auto nearlyFull = tideGame();
        nearlyFull.player.stones = tide::rules::InventoryLimit - 10;
        nearlyFull.tide.playerJoined = true; nearlyFull.tide.playerScore = 1;
        nearlyFull.clock.days = nearlyFull.tide.endDay; tide::dayTick(nearlyFull);
        checkEq(nearlyFull.player.stones, tide::rules::InventoryLimit, "金榜奖金遵循存档十亿余额上限");
        checkEq(nearlyFull.tide.rewardStones, 10, "接近余额上限仅记录实际发放奖金");
        tide::settleIfDue(nearlyFull);
        checkEq(nearlyFull.player.stones, tide::rules::InventoryLimit, "余额封顶后重复结算不溢出");
    }
    {
        auto game = tideGame();
        game.tide.playerJoined = true;
        game.tide.playerScore = 1;
        Battle unfinished; unfinished.kind = "tide"; unfinished.tideEventId = game.tide.eventId;
        unfinished.tideBattlefield = 3; unfinished.enemyStage = 9;
        game.player.arts.battle = unfinished;
        game.clock.days = game.tide.endDay;
        tide::dayTick(game);
        check(!game.tide.active && !game.tide.settled, "末日关闭战场但先等待正在进行的战果");
        tide::tideReward(game, true, &unfinished);
        checkEq(game.tide.playerScore, 28, "最后一天有效击杀仍计入金榜");
        game.player.arts.battle.reset(); tide::settleIfDue(game);
        check(game.tide.settled && game.tide.playerRank == 1, "最后战局清理后才冻结榜单");
        const int score = game.tide.playerScore;
        tide::tideReward(game, true, &unfinished);
        checkEq(game.tide.playerScore, score, "结榜以后旧战果不能污染积分");
        auto wrongEvent = tideGame(); unfinished.tideEventId = "other-event";
        tide::tideReward(wrongEvent, true, &unfinished);
        checkEq(wrongEvent.tide.playerScore, 0, "其他场次战果不计入本期");
    }
    {
        Game old; old.clock.days = 47; old.player.name = "旧档";
        old.tide.active = true; old.tide.monthsLeft = 19; old.tide.wavesFought = 7; old.tide.wavesWon = 3;
        old.tide.npcDeaths = 2; old.tide.fallen = {"故人"};
        const auto rng = old.rng;
        tide::ensureTide(old);
        checkEq(old.tide.endDay, 107, "旧兽潮从载入日转换为六十天");
        check(old.tide.wavesFought == 7 && old.tide.wavesWon == 3 && old.tide.npcDeaths == 2 && old.tide.fallen.size() == 1,
              "迁移保留迎战击退和陨落旧记录");
        checkEq(old.tide.playerScore, 0, "迁移不凭旧记录虚构新积分");
        checkEq(old.rng, rng, "旧战期迁移不消耗随机数");
    }
    {
        Game game;
        Npc champion; champion.id = "npc-a"; champion.name = "完整流派修士"; champion.sect = "jinhong"; champion.stage = 9;
        Npc normal = champion; normal.id = "npc-b"; normal.name = "普通修士";
        game.npcs = {normal, champion};
        const auto rng = game.rng;
        content::ensureChampionNpcs(game);
        check(content::isChampion(game.npcs[1]) && !content::isChampion(game.npcs[0]), "每宗选最小稳定ID赋完整传承");
        checkEq(game.rng, rng, "特殊传承选择不消耗随机数");
        const auto profileId = game.npcs[1].combatProfileId;
        content::ensureChampionNpcs(game);
        check(game.npcs[1].combatProfileId == profileId, "重复初始化不更换特殊NPC");
        const Arts full = artNpcLoadout(game.npcs[1]), ordinary = artNpcLoadout(game.npcs[0]);
        check(full.aux.size() >= ordinary.aux.size() && full.spells.size() > ordinary.spells.size(), "特殊NPC在真实斗法获得完整功法神通构筑");
        for (const auto& profile : content::npcCombatProfiles()) {
            bool valid = true;
            for (const auto& id : profile.manuals) { ManualInfo info; valid = findManualInfo(id, info) && valid; }
            for (const auto& id : profile.spells) valid = battleSpell(id) != nullptr && valid;
            check(valid, "五宗完整构筑只引用真实功法神通：" + profile.id);
        }
        game.player.location = "wuling"; game.tide.active = true;
        tide::ensureTide(game);
        for (int day = 1; day <= 10; ++day) {
            const int previousNormal = npcScore(game, "npc-b"), previousChampion = npcScore(game, "npc-a");
            game.clock.days = day; tide::dayTick(game);
            const int normalGain = npcScore(game, "npc-b") - previousNormal, championGain = npcScore(game, "npc-a") - previousChampion;
            check((normalGain == 0 || normalGain == 27) && (championGain == 0 || championGain == 27 || championGain == 54),
                  "NPC日击杀按境界1或2上限折算");
        }
        const auto afterDay = saveToJson(game);
        tide::dayTick(game);
        checkStr(saveToJson(game), afterDay, "同一天不会重复模拟NPC积分");
        check(npcScore(game, "npc-a") <= 12 * 27 && npcScore(game, "npc-b") <= 9 * 27,
              "短战期积分同样不超过本期精英和普通击杀上限");
    }
    {
        auto game = marketGame();
        game.player.renown = 1000; game.player.traits = {"merchant"}; game.world.market = 0.88;
        const auto view = tide::marketItems(game);
        bool floorCorrect = true;
        for (const auto& row : view) floorCorrect &= row.basePrice == play::normalMaterialBuyPrice(game, row.kind, row.id) &&
            row.unitPrice >= (row.basePrice + 1) / 2 && row.unitPrice >= 1;
        check(floorCorrect && tide::discountPercent(game) == 50, "榜首最低价是当前声望天赋行情正常价的五折");
        const int famed = play::normalMaterialBuyPrice(game, "material", "gold");
        auto unknown = game; unknown.player.renown = 0; unknown.player.traits.clear();
        check(famed < play::normalMaterialBuyPrice(unknown, "material", "gold"), "价格下限计入真实声望和商贾天赋");
        checkEq(tide::rules::discountedPrice(1, 0), 1, "最低价不能变成零灵石");
        checkEq(tide::rules::discountedPrice(73, 50), 37, "奇数正常价五折向上取整");
        const string event = game.tide.eventId;
        rejectedPurchase(game, "stale-event", "material", "gold", 1, -1, "过期场次");
        rejectedPurchase(game, event, "material", "gold", 0, -1, "零数量");
        rejectedPurchase(game, event, "material", "gold", 9, -1, "超出库存");
        rejectedPurchase(game, event, "material", "iron", 1, -1, "普通低阶材料");
        rejectedPurchase(game, event, "material", "gold", 1, 1, "伪造旧报价");
        auto poor = game; poor.player.stones = 0;
        rejectedPurchase(poor, event, "material", "gold", 1, -1, "余额不足");
        auto full = game; full.player.arts.materials["gold"] = tide::rules::InventoryLimit;
        rejectedPurchase(full, event, "material", "gold", 1, -1, "器材背包满");
        auto fullHerbs = game; fullHerbs.player.herbs = tide::rules::InventoryLimit;
        rejectedPurchase(fullHerbs, event, "herb", "6201", 1, -1, "药材总量满");
        const int before = game.player.stones;
        const auto bought = tide::buyMarket(game, event, "herb", "6201", 2);
        check(bought.ok && game.player.arts.herbs["紫猴花"] == 2 && game.player.herbs == 5, "药材购买同时更新名称背包和总量");
        checkEq(game.player.stones, before - bought.cost, "成功成交一次扣除报价总额");
        checkEq(game.tide.marketStock[0].stock, 6, "成功成交扣除本期有限库存");
        const int remaining = game.tide.marketStock[0].stock;
        tide::initializeMarketStock(game);
        checkEq(game.tide.marketStock[0].stock, remaining, "再次初始化不能刷新已消耗库存");
        game.clock.days = game.tide.marketExpiresDay;
        rejectedPurchase(game, event, "material", "gold", 1, -1, "结束当日市场");
        check(tide::rules::stockPerItem(30000, 3) > tide::rules::stockPerItem(0, 3), "全体积分提高材料数量");
        check(tide::rules::stockPerItem(std::numeric_limits<int>::max(), 3) <= tide::rules::MaximumStock, "库存随积分增长但有有限上限");
    }
}
} // namespace nztest
