#include "core/tide.h"
#include "core/calendar.h"
#include "core/simulation/tide_rules.h"
#include "core/content/game_ids.h"
#include "core/content/npc_combat_profiles.h"
#include "core/exploration/beast_loot.h"
#include "core/battle.h"
#include "core/economy.h"
#include "core/sim.h"
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace nz::tide {
namespace {
// 用途：安全创建截止日期；流程：开潮与迁移；用法：传起始日和长度；返回：不溢出的日期。
int deadline(int day, int duration) {
    return static_cast<int>(std::min(static_cast<long long>(std::max(0, day)) + duration,
                                    static_cast<long long>(std::numeric_limits<int>::max())));
}
// 用途：统一参战名册与日模拟的修士资格；流程：开潮、增量报名及积分模拟；用法：传只读NPC；返回：有效身份的存活非妖兽修士为真。
bool mfIsEligibleTideNpc(const Npc& Actor) {
    return Actor.alive && !Actor.beast && !Actor.id.empty() && Actor.id != "player" && !Actor.name.empty();
}
// 用途：冻结本期参战身份；流程：开启和迁移；用法：新活动初始化时调用；返回：无，不消耗随机数。
void registerNpcs(Game& game) {
    content::ensureChampionNpcs(game);
    game.tide.participants.clear();
    mfEnrollEligibleNpcs(game);
}
// 用途：初始化固定战期事实；流程：开潮和旧档迁移；用法：调用前决定是否保留旧记录；返回：无，设置期限与名单。
void initializeEvent(Game& game) {
    BeastTide& tide = game.tide;
    tide.rulesVersion = rules::Version; tide.durationDays = rules::BattleDays;
    tide.startDay = std::max(0, game.clock.days);
    tide.endDay = deadline(tide.startDay, rules::BattleDays);
    tide.lastProcessedDay = tide.startDay;
    tide.eventId = "tide-" + std::to_string(game.seed) + "-" + std::to_string(tide.startDay) + "-" + std::to_string(game.eventSeq);
    tide.monthsLeft = (tide.durationDays+29)/30;
    tide.playerEntryStage = clampInt(game.player.stage, 0, 12);
    tide.playerJoined = false;
    tide.playerScore = tide.playerRank = tide.totalScore = tide.rewardStones = 0;
    tide.RewardRenown = 0;
    tide.settled = tide.rewardPaid = false; tide.marketExpiresDay = -1; tide.marketStock.clear();
    registerNpcs(game);
}
// 用途：检查末日是否仍有本期战斗；流程：延迟结榜；用法：传只读游戏；返回：本期活跃斗法存在时为真。
bool hasUnfinishedBattle(const Game& game) {
    const auto& battle = game.player.arts.battle;
    return battle && battle->shadowOutcome.empty() &&
        content::parseBattleKind(battle->kind) == content::BattleKind::Tide &&
        battle->tideEventId == game.tide.eventId;
}
// 用途：按集中出战概率及本期上限模拟NPC积分；流程：dayTick；返回：无，冻结入场战场倍率，死人/未参战/已封顶不投骰。
void simulateNpcDay(Game& game) {
    std::unordered_map<string, const Npc*> LivingNpcs;
    for (const auto& Npc : game.npcs) if (mfIsEligibleTideNpc(Npc)) LivingNpcs.emplace(Npc.id, &Npc);
    for (auto& Participant : game.tide.participants) {
        const auto Found = LivingNpcs.find(Participant.actorId);
        if (Found == LivingNpcs.end()) continue;
        const bool IsElite = content::isChampion(*Found->second);
        Participant.elite = IsElite;
        Participant.combatProfileId = Found->second->combatProfileId;
        const auto& Pace = IsElite ? rules::EliteNpcBattlePace : rules::NormalNpcBattlePace;
        const auto* Battlefield = rules::battlefield(rules::battlefieldForStage(Participant.entryStage));
        const int KillsSoFar = std::max(0, Participant.score) / Battlefield->pointsPerKill;
        // 保留已发生的大积分；只停止继续累加，不用新上限反向削减旧事实。
        if (KillsSoFar >= Pace.MaximumEventKills) continue;
        if (randIntOf(game, 1, Pace.BattleChanceDenominator) != 1) continue;
        const int RemainingKills = Pace.MaximumEventKills - KillsSoFar;
        const int KillCount = std::min(randIntOf(game, 0, Pace.MaximumDailyKills), RemainingKills);
        Participant.score = rules::addScore(Participant.score, KillCount * Battlefield->pointsPerKill);
    }
}
// 用途：保留既有月度伤亡；流程：实际月界；用法：战期内每月一次；返回：无，npcDies保持履历和关系一致。
void resolveMonthlyCasualties(Game& game) {
    for (int roll = 0; roll < clampInt(game.tide.severity, 1, 3) * 2; ++roll) {
        vector<Npc*> candidates; vector<int> weights; int totalWeight = 0;
        for (auto& npc : game.npcs) {
            if (!npc.alive || npc.beast || npc.stage > 6) continue;
            if (npc.location == game.player.location && game.tide.severity < 2) continue;
            const int lowRealm = 7 - clampInt(npc.stage, 0, 6), weight = lowRealm * lowRealm;
            candidates.push_back(&npc); weights.push_back(weight); totalWeight += weight;
        }
        if (candidates.empty()) return;
        int choice = randIntOf(game, 1, totalWeight); size_t index = 0;
        while (index + 1 < candidates.size() && choice > weights[index]) choice -= weights[index++];
        Npc& victim = *candidates[index];
        const double survive = clamp(0.08 + victim.stage * 0.10 + (content::isChampion(victim) ? 0.15 : 0.0), 0.0, 0.85);
        if (randomOf(game) < survive) continue;
        ++game.tide.npcDeaths;
        if (game.tide.fallen.size() < 40) game.tide.fallen.push_back(victim.name);
        npcDies(game, victim, "殁于兽潮");
    }
}
} // namespace

// 用途：增量冻结当日新修士的参战身份；流程：年度人口更新与日推进；用法：仅动作边界调用；返回：无，不投骰或重置老账。
void mfEnrollEligibleNpcs(Game& GameState) {
    auto& Event = GameState.tide;
    if (!Event.active || Event.settled || Event.rulesVersion == 0 ||
        GameState.clock.days < Event.startDay || GameState.clock.days >= Event.endDay ||
        Event.lastProcessedDay != GameState.clock.days) return;

    std::unordered_set<string> EnrolledIds{"player"};
    EnrolledIds.reserve(Event.participants.size() + GameState.npcs.size() + 1);
    for (const auto& Entry : Event.participants) EnrolledIds.insert(Entry.actorId);
    bool HasNewEnrollment = false;
    for (const auto& Actor : GameState.npcs) {
        if (!mfIsEligibleTideNpc(Actor) || !EnrolledIds.insert(Actor.id).second) continue;
        TideParticipant Entry;
        Entry.actorId = Actor.id;
        Entry.name = Actor.name;
        Entry.sect = Actor.sect;
        Entry.combatProfileId = Actor.combatProfileId;
        Entry.entryStage = clampInt(Actor.stage, 0, 12);
        Entry.elite = content::isChampion(Actor);
        Event.participants.push_back(std::move(Entry));
        HasNewEnrollment = true;
    }
    if (HasNewEnrollment) {
        // 名单顺序决定日模拟投骰顺序；仅在新增时稳定排序，已登记的人物事实不变。
        std::sort(Event.participants.begin(), Event.participants.end(),
            [](const TideParticipant& Left, const TideParticipant& Right) { return Left.actorId < Right.actorId; });
    }
}

// 用途：读取战期危险增量；流程：游历赶路；用法：传只读游戏；返回：仅有效战期内的风险。
int dangerBonus(const Game& game) {
    return game.tide.active && daysLeft(game) > 0 ? 8 * clampInt(game.tide.severity, 1, 3) : 0;
}
// 用途：读取固定战期余日；流程：视图与门槛；用法：传只读游戏；返回：不小于零的天数。
int daysLeft(const Game& game) {
    if (!game.tide.active) return 0;
    if (game.tide.rulesVersion == 0) return rules::BattleDays;
    return std::max(0, game.tide.endDay - game.clock.days);
}
// 用途：生成活动状态说明；流程：纪事和详情；用法：传只读游戏；返回：战期、金榜或平安文字。
string statusText(const Game& game) {
    const BeastTide& tide = game.tide;
    if (tide.active) return "兽潮固定持续" + std::to_string(tide.durationDays) + "天，尚余" + std::to_string(daysLeft(game)) +
        "天。每回合推进一天，炼气、筑基、金丹、元婴战场每杀得1、3、9、27分；当前讨伐积分" + std::to_string(tide.playerScore) + "。";
    if (tide.settled) return "兽潮已退，金榜已结算。你的讨伐积分" + std::to_string(tide.playerScore) +
        (tide.playerRank > 0 ? "，排名第" + std::to_string(tide.playerRank) + "。" : "，本期未获得有效讨伐积分。") +
        (marketOpen(game) ? "战后坊市尚余" + std::to_string(std::max(0, tide.marketExpiresDay - game.clock.days)) + "天。" : "战后坊市已结束。");
    if (tide.rulesVersion > 0 && tide.endDay >= 0 && game.clock.days >= tide.endDay)
        return "兽潮战期已结束，请完成当前战局收尾，随后公布金榜。";
    return "宁州暂安，各处险地未见异动。";
}
// 用途：按年度概率开启固定战期；流程：世界年度演化；用法：传游戏；返回：无，不覆盖本期或等待收尾的活动。
void yearTick(Game& game) {
    ensureTide(game);
    if (game.tide.active || marketOpen(game) || hasUnfinishedBattle(game) || game.year < 3) return;
    if (!game.tide.settled && game.tide.rulesVersion > 0 && game.tide.endDay >= 0) {
        settleIfDue(game);
        if (!game.tide.settled) return;
    }
    const double probability = 0.10 + std::min(0.22, game.year * 0.004) + (game.world.market < 0.95 ? 0.08 : 0.0);
    if (randomOf(game) >= probability) return;
    game.tide = BeastTide{};
    game.tide.active = true; game.tide.startYear = game.year;
    game.tide.severity = randIntOf(game, 1, 3); game.tide.warned = true;
    initializeEvent(game);
    logEvent(game, "world", "【兽潮】妖潮涌入宁州，四境战场开放。本期持续" + std::to_string(rules::BattleDays) + "天，讨伐金榜将在潮退时结算，随后开放" + std::to_string(rules::MarketDays) + "天战后坊市。", true);
}
// 用途：按实际月界结算伤亡；流程：日历月钩；用法：战期内调用；返回：无，日倒计时由dayTick统一负责。
void monthTick(Game& game) {
    ensureTide(game);
    if (!game.tide.active || game.clock.days > game.tide.endDay) return;
    resolveMonthlyCasualties(game);
}
// 用途：逐真实日模拟积分并关闭战期；流程：普通日历和兽潮战；用法：日历更新后调用；返回：无，同一天不重复投骰。
void dayTick(Game& game) {
    ensureTide(game);
    BeastTide& tide = game.tide;
    if (tide.rulesVersion == 0 || tide.settled || tide.endDay < tide.startDay) return;
    const int target = std::min(game.clock.days, tide.endDay);
    const int first = std::max(tide.startDay, tide.lastProcessedDay);
    const int days = clampInt(target - first, 0, tide.durationDays);
    for (int day = 0; day < days; ++day) simulateNpcDay(game);
    tide.lastProcessedDay = std::max(tide.lastProcessedDay, target);
    // 先补算旧名单，再登记当前新修士；新人只从下一次真实日推进开始计分，不追溯历史。
    mfEnrollEligibleNpcs(game);
    tide.monthsLeft = (std::max(0, tide.endDay - game.clock.days) + 29) / 30;
    if (game.clock.days >= tide.endDay) tide.active = false;
    settleIfDue(game);
}
// 用途：给出四战场权威门槛；流程：选择与入场；用法：传只读游戏；返回：对应积分及不可入场原因。
vector<BattlefieldInfo> battlefields(const Game& game) {
    vector<BattlefieldInfo> result;
    for (const auto& field : rules::Battlefields) {
        BattlefieldInfo item;
        item.id = field.id; item.minStage = field.minStage; item.pointsPerKill = field.pointsPerKill; item.name = field.name;
        if (game.ended) item.reason = "此世已结束。";
        else if (!game.tide.active || daysLeft(game) <= 0) item.reason = "当前没有开放的兽潮战场。";
        else if (game.pending) item.reason = "请先处理当前抉择。";
        else if (game.player.arts.battleActive()) item.reason = "请先完成当前斗法。";
        else if (game.job) item.reason = "请先完成或取消当前安排。";
        else if (game.dungeons.run) item.reason = "请先结束秘境探索。";
        else if (game.player.health <= 0) item.reason = "当前伤势无法迎战。";
        else if (game.player.stage < field.minStage) item.reason = "境界不足，不能进入" + item.name + "。";
        item.enabled = item.reason.empty(); result.push_back(std::move(item));
    }
    return result;
}
// 用途：创建对应境界妖兽并冻结参加身份；流程：迎战入口；用法：-1选最高战场，其他值须0..3；返回：开战成功或错误。
FightResult fight(Game& game, int battlefieldId) {
    FightResult result;
    const int selected = battlefieldId == -1 ? rules::battlefieldForStage(game.player.stage) : battlefieldId;
    const auto* field = rules::battlefield(selected);
    if (!field) { result.error = "兽潮战场编号无效。"; return result; }
    const auto entries = battlefields(game);
    if (!entries[selected].enabled) { result.error = entries[selected].reason; return result; }
    ensureTide(game);
    BeastTide& tide = game.tide;
    if (!tide.playerJoined) { tide.playerJoined = true; tide.playerEntryStage = clampInt(game.player.stage, 0, 12); }
    EnemySpec enemy;
    enemy.name = field->name + string("妖兽"); enemy.kind = "tide";
    enemy.style = exploration::mfRollBeastMaterialElement(game);
    enemy.beast = true; enemy.lethal = true;
    enemy.stage = field->minStage + randIntOf(game, 0, 2); enemy.health = 100;
    enemy.tideEventId = tide.eventId; enemy.tideBattlefield = selected;
    tide.wavesFought = rules::addScore(tide.wavesFought, 1);
    const int openingDay=game.clock.days;
    battleStart(game, enemy);
    result.ok = true; result.lethal = true;
    result.text = "你进入" + string(field->name) + "，本场击杀获得" + std::to_string(field->pointsPerKill) + "分。";
    logEvent(game, "world", "【兽潮】" + result.text, true);
    // 开局先手或反震也可能直接结束对局；没有后续出牌入口时在这里补记首日。
    if (!game.player.arts.battle && game.clock.days==openingDay) {
        calendarBattleDay(game);
        settleIfDue(game);
    }
    return result;
}
// 用途：登记本期有效击杀；流程：battleFinish唯一战果出口；用法：传战斗快照；返回：无，不提前推进世界时间。
void tideReward(Game& game, bool won, const Battle* context) {
    const Battle* source = context ? context : game.player.arts.battle ? &*game.player.arts.battle : nullptr;
    if (!source || !won || source->retreat || source->shadowPlayer || game.tide.settled ||
        source->tideEventId.empty() || source->tideEventId != game.tide.eventId ||
        content::parseBattleKind(source->kind) != content::BattleKind::Tide || game.clock.days > game.tide.endDay) return;
    const auto* field = rules::battlefield(source->tideBattlefield);
    if (!field || source->enemyStage < field->minStage || source->enemyStage > field->minStage + 2) return;
    game.tide.wavesWon = rules::addScore(game.tide.wavesWon, 1);
    game.tide.playerScore = rules::addScore(game.tide.playerScore, field->pointsPerKill);
    const int renown = 3 + clampInt(game.tide.severity, 1, 3) * 2;
    game.player.renown = rules::addScore(game.player.renown, renown);
    logEvent(game, "world", "【兽潮】击杀" + string(field->name) + "妖兽，讨伐积分 +" + std::to_string(field->pointsPerKill) +
        "，声望 +" + std::to_string(renown) + "；本期累计" + std::to_string(game.tide.playerScore) + "分。灵石奖励在潮退后按金榜发放。", true);
}
// 用途：生成同分并列金榜；流程：预览与结算；用法：传只读游戏；返回：包含玩家的稳定排序副本。
vector<TideParticipant> leaderboard(const Game& game) {
    auto board = game.tide.participants;
    TideParticipant player;
    player.actorId = "player"; player.name = game.player.name; player.sect = game.player.sect;
    player.entryStage = game.tide.playerJoined ? game.tide.playerEntryStage : clampInt(game.player.stage, 0, 12);
    player.score = std::max(0, game.tide.playerScore); board.push_back(std::move(player));
    std::sort(board.begin(), board.end(), [](const TideParticipant& left, const TideParticipant& right) {
        return left.score == right.score ? left.actorId < right.actorId : left.score > right.score;
    });
    int previousScore = -1, rank = 0;
    for (size_t index = 0; index < board.size(); ++index) {
        if (board[index].score <= 0) { board[index].rank = 0; continue; }
        if (board[index].score != previousScore) rank = static_cast<int>(index) + 1;
        board[index].rank = rank; previousScore = board[index].score;
    }
    return board;
}
// 用途：冻结排名奖励和有限库存；流程：期末日、战斗收尾；用法：可重复调用；返回：无，付款和库存只生成一次。
void settleIfDue(Game& game) {
    BeastTide& tide = game.tide;
    if (tide.rulesVersion == 0 || tide.settled || tide.endDay < 0 || game.clock.days < tide.endDay) return;
    tide.active = false;
    if (hasUnfinishedBattle(game)) return;
    tide.totalScore = 0;
    for (const auto& participant : leaderboard(game)) {
        tide.totalScore = rules::addScore(tide.totalScore, participant.score);
        if (participant.actorId == "player") tide.playerRank = participant.rank;
        else for (auto& stored : tide.participants) if (stored.actorId == participant.actorId) { stored.rank = participant.rank; break; }
    }
    tide.settled = true; tide.monthsLeft = 0;
    tide.marketExpiresDay = deadline(tide.endDay, rules::MarketDays);
    initializeMarketStock(game);
    // 人生已结束时保留榜单，避免结束结算以后改变遗产和账户。
    if (!tide.rewardPaid && tide.playerJoined && tide.playerScore > 0 && !game.ended &&
        game.player.health > 0 && game.player.age < game.player.lifespan) {
        const int intended = rules::rewardStones(tide.playerEntryStage, tide.playerRank);
        const int room = std::max(0, rules::InventoryLimit - std::max(0, game.player.stones));
        tide.rewardStones = std::min(intended, room);
        const int BeforeRenown = std::max(0, game.player.renown);
        game.player.renown = rules::addScore(BeforeRenown, rules::mfRewardRenown(tide.playerRank));
        tide.RewardRenown = std::max(0, game.player.renown - BeforeRenown);
        game.player.stones += tide.rewardStones; tide.rewardPaid = true;
    }
    logEvent(game, "world", "【兽潮金榜】妖潮退去，全体修士累计" + std::to_string(tide.totalScore) + "分。" +
        (tide.playerRank > 0 ? "你以" + std::to_string(tide.playerScore) + "分列第" + std::to_string(tide.playerRank) + "名，" +
          (tide.rewardPaid ? "获得" + std::to_string(tide.rewardStones) + "灵石，声望 +" + std::to_string(tide.RewardRenown) + "。" : "本期奖励未发放。") : "你本期未获得有效讨伐积分。") + "战后坊市开放" + std::to_string(rules::MarketDays) + "天，供给随总积分增加，排名影响售价。", true);
}
// 用途：记录避战选择；流程：兽潮纪事按钮；用法：活动内调用；返回：文字，不推进时间或加分。
string hunkerDown(Game& game) {
    if (!game.tide.active || daysLeft(game) <= 0) return "眼下没有兽潮。";
    logEvent(game, "world", "【兽潮】你固守不出，静待妖潮退去。", false);
    return "你闭门固守；可继续安排修行，时间照常推进。";
}
// 用途：幂等迁移旧战期与在途兽潮对局；流程：建号、加载和旧入口；用法：传游戏；返回：无，保留迎战记录，不重分配NPC构筑或虚构积分。
void ensureTide(Game& game) {
    if (game.tide.rulesVersion != 0 || !game.tide.active) return;
    initializeEvent(game);
    if (game.player.arts.battle && content::parseBattleKind(game.player.arts.battle->kind) == content::BattleKind::Tide) {
        Battle& battle = *game.player.arts.battle;
        battle.tideEventId = game.tide.eventId;
        battle.tideBattlefield = rules::battlefieldForStage(battle.enemyStage);
        game.tide.playerJoined = true;
    }
    logEvent(game, "world", "【兽潮】旧战期已改为自今日起" + std::to_string(rules::BattleDays) + "天，既有迎战记录保留；讨伐积分从本次更新后开始记录。", true);
}
} // namespace nz::tide
