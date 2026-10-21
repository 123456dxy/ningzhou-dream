#include "core/exploration/ascension_tournament.h"

#include <algorithm>
#include <set>

#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/foundation/state_limits.h"
#include "core/sim.h"

namespace nz {
namespace {
int mfTournamentFirstYear(const Game& GameState) {
    return std::max(0, GameState.player.AscensionTournament.FirstYear);
}

bool mfTournamentEligible(const Game& GameState) {
    return !GameState.ended && GameState.player.stage >= 0 && GameState.player.stage < 3 &&
        GameState.player.age < AscensionTournamentMaximumAge;
}

bool mfTournamentRewardFits(const Game& GameState) {
    const auto Pill = GameState.player.arts.potions.find(AscensionTournamentPillId);
    return GameState.player.stones <= StateLimits::MaxPersistedInteger - AscensionTournamentRewardStones &&
        (Pill == GameState.player.arts.potions.end() || Pill->second < StateLimits::MaxPersistedInteger);
}

string mfTournamentOpponentId(int Round) {
    return "ascension-round-" + std::to_string(Round);
}

void mfStartAscensionTournamentRound(Game& GameState) {
    const auto& Tournament = GameState.player.AscensionTournament;
    EnemySpec Enemy;
    Enemy.id = mfTournamentOpponentId(Tournament.Round);
    Enemy.kind = AscensionTournamentBattleKind;
    Enemy.stage = std::max(0, Tournament.Round - 1);
    Enemy.gender = Tournament.Round % 2 == 0 ? "男" : "女";
    if (Tournament.Round == 0) {
        Enemy.name = "升仙大会·炼气初期散修";
        Enemy.style = "体";
    } else {
        Enemy.sect = Tournament.OpponentSects[static_cast<size_t>(Tournament.Round - 1)];
        const auto* Sect = findSect(Enemy.sect);
        Enemy.name = "升仙大会·" + string(Sect->name) + "弟子（" + realmName(Enemy.stage) + "）";
    }
    battleStart(GameState, Enemy);
}
} // namespace

bool mfIsAscensionTournamentYear(const Game& GameState) {
    const int ElapsedYears = GameState.year - mfTournamentFirstYear(GameState);
    return ElapsedYears >= 0 && ElapsedYears % AscensionTournamentIntervalYears == 0;
}

string mfAscensionTournamentEntryError(const Game& GameState) {
    const auto& Tournament = GameState.player.AscensionTournament;
    if (GameState.ended) return "此世已经结束。";
    if (GameState.player.stage < 0 || GameState.player.stage >= 3) return "升仙大会仅限炼气修士参加。";
    if (GameState.player.age >= AscensionTournamentMaximumAge) return "升仙大会仅限未满40岁的修士参加。";
    if (!mfIsAscensionTournamentYear(GameState)) return "今年不是升仙大会之年。";
    if (Tournament.Active) return "本届升仙大会正在连战。";
    if (Tournament.AttemptedYear == GameState.year) return "本届升仙大会已经参赛，不能再次报名。";
    if (battleActive(GameState)) return "请先完成当前斗法。";
    if (GameState.job) return "请先完成当前安排再报名。";
    if (GameState.dungeons.run) return "请先离开秘境。";
    if (GameState.player.Zhuji && (GameState.player.Zhuji->Status == ZhujiStatus::Preparing ||
        GameState.player.Zhuji->Status == ZhujiStatus::Active)) return "请先完成筑基。";
    if (GameState.pending && GameState.pending->type != AscensionTournamentBattleKind)
        return "请先处理当前抉择。";
    if (GameState.player.health < HealthGateMinimum) return "当前伤势过重，请先调息。";
    if (!mfTournamentRewardFits(GameState))
        return "灵石或筑基丹库存过多，无法承接通关奖励。";
    for (const auto& NpcState : GameState.npcs)
        for (int Round = 0; Round < AscensionTournamentRoundCount; ++Round)
            if (NpcState.id == mfTournamentOpponentId(Round)) return "大会对手标识与现有修士冲突。";
    return "";
}

void mfInitializeAscensionTournament(Game& GameState) {
    auto& Tournament = GameState.player.AscensionTournament;
    // A life begins at year zero. Old saves use the same offset and receive no
    // missed tournaments or rewards when this state is first initialized.
    if (Tournament.FirstYear < 0) Tournament.FirstYear = 0;
}

bool mfOfferAscensionTournament(Game& GameState) {
    if (GameState.player.AscensionTournament.FirstYear < 0 ||
        !mfTournamentEligible(GameState) || !mfIsAscensionTournamentYear(GameState) ||
        GameState.pending || GameState.job || battleActive(GameState) || GameState.dungeons.run ||
        GameState.player.AscensionTournament.Active ||
        GameState.player.AscensionTournament.OfferedYear == GameState.year ||
        GameState.player.AscensionTournament.AttemptedYear == GameState.year) return false;
    if (GameState.player.Zhuji && (GameState.player.Zhuji->Status == ZhujiStatus::Preparing ||
        GameState.player.Zhuji->Status == ZhujiStatus::Active)) return false;
    mfInitializeAscensionTournament(GameState);
    Pending Invitation;
    Invitation.type = AscensionTournamentBattleKind;
    Invitation.title = "升仙大会";
    Invitation.subtitle = "五年一会 · 四战问仙途";
    Invitation.text = "六宗开坛，招纳未满四十岁的炼气修士。擂台四战依次迎战炼气初期散修、炼气初期、"
        "中期与后期宗门弟子。参赛后连续斗法，不能换装或穿插行动；每胜一场后进入下一战前恢复20%气血上限，"
        "落败或认输即结束本届参赛，不会陨落。四战全胜，得灵石500与筑基丹一枚。可先整备，年内再报名。";
    const string EntryError = mfAscensionTournamentEntryError(GameState);
    Invitation.options.push_back({"accept", "报名登擂", EntryError.empty()
        ? "以当前功法、神通和法器连续挑战四场。" : EntryError, !EntryError.empty()});
    Invitation.options.push_back({"decline", "先行整备", "保留本年报名资格；准备好后可从大会入口报名。", false});
    GameState.pending = std::move(Invitation);
    GameState.player.AscensionTournament.OfferedYear = GameState.year;
    logEvent(GameState, "self", "升仙大会开坛：六宗邀未满四十岁的炼气修士登擂，四战全胜可获筑基丹。", true);
    return true;
}

bool mfEnterAscensionTournament(Game& GameState, string* Error) {
    const string Reason = mfAscensionTournamentEntryError(GameState);
    if (!Reason.empty()) {
        if (Error) *Error = Reason;
        return false;
    }
    // Freeze the whole admission on a candidate: failures while allocating or
    // initializing a battle do not consume an attempt, resources, or live RNG.
    Game Candidate = GameState;
    mfInitializeAscensionTournament(Candidate);
    auto& Tournament = Candidate.player.AscensionTournament;
    Tournament.OfferedYear = Candidate.year;
    Tournament.AttemptedYear = Candidate.year;
    Tournament.Round = 0;
    Tournament.Active = true;
    Tournament.OpponentSects.clear();
    vector<string> Sects;
    for (const auto& Sect : SECTS()) Sects.emplace_back(Sect.id);
    for (int OpponentIndex = 0; OpponentIndex < AscensionTournamentRoundCount - 1; ++OpponentIndex) {
        const int Index = randIntOf(Candidate, 0, static_cast<int>(Sects.size()) - 1);
        Tournament.OpponentSects.push_back(Sects[static_cast<size_t>(Index)]);
        Sects.erase(Sects.begin() + Index);
    }
    Candidate.pending.reset();
    logEvent(Candidate, "self", "你报名升仙大会：四场连续较量开始，本届不可重新报名。", true);
    mfStartAscensionTournamentRound(Candidate);
    GameState = std::move(Candidate);
    if (Error) Error->clear();
    return true;
}

bool mfChooseAscensionTournament(Game& GameState, const string& OptionId) {
    if (!GameState.pending || GameState.pending->type != AscensionTournamentBattleKind) return false;
    if (OptionId == "accept") {
        string Error;
        if (!mfEnterAscensionTournament(GameState, &Error)) logEvent(GameState, "self", Error);
    } else if (OptionId == "decline") {
        GameState.pending.reset();
        logEvent(GameState, "self", "你暂未登擂，可于本年整备后再报名升仙大会。");
    }
    return true;
}

void mfCompleteAscensionTournamentBattle(Game& GameState, const Battle& BattleState, bool Win, bool Escape) {
    auto& Tournament = GameState.player.AscensionTournament;
    if (BattleState.kind != AscensionTournamentBattleKind || !Tournament.Active ||
        Tournament.Round >= AscensionTournamentRoundCount ||
        BattleState.npcId != mfTournamentOpponentId(Tournament.Round)) return;
    if (!Win || Escape || GameState.ended) {
        Tournament.Active = false;
        logEvent(GameState, "self", Escape ? "你向大会裁判认输，本届参赛结束，来届再战。"
            : "你在升仙大会止步，本届参赛结束，性命无虞，来届再战。", true);
        return;
    }
    ++Tournament.Round;
    if (Tournament.Round == AscensionTournamentRoundCount) {
        ++GameState.player.arts.potions[AscensionTournamentPillId];
        GameState.player.stones += AscensionTournamentRewardStones;
        Tournament.Active = false;
        logEvent(GameState, "self", "升仙大会四战全胜！裁判授予灵石500与筑基丹一枚。", true);
        return;
    }
    const int MaximumHealth = mfPlayerHealthMaximum(GameState.player);
    const int RestoredHealth = mfRecoverPlayerHealth(GameState,
        mfHealthPercentAmount(MaximumHealth, AscensionTournamentRecoveryPercent));
    logEvent(GameState, "self", "大会医修短暂调息，恢复气血" + std::to_string(RestoredHealth) +
        "，随即开始第" + std::to_string(Tournament.Round + 1) + "场。", true);
    mfStartAscensionTournamentRound(GameState);
}

string mfAscensionTournamentStateError(const Game& GameState) {
    const auto& Tournament = GameState.player.AscensionTournament;
    const auto IsScheduledYear = [&](int Year) {
        return Year >= Tournament.FirstYear && Year <= GameState.year &&
            (Year - Tournament.FirstYear) % AscensionTournamentIntervalYears == 0;
    };
    const bool HasTournamentBattle = GameState.player.arts.battle &&
        GameState.player.arts.battle->kind == AscensionTournamentBattleKind;
    const bool HasInvitation = GameState.pending && GameState.pending->type == AscensionTournamentBattleKind;
    if (Tournament.FirstYear == -1) {
        if (Tournament.OfferedYear != -1 || Tournament.AttemptedYear != -1 || Tournament.Round != 0 ||
            Tournament.Active || !Tournament.OpponentSects.empty() || HasTournamentBattle || HasInvitation)
            return "升仙大会未初始化状态含活动数据。";
        return "";
    }
    if (Tournament.FirstYear < 0 || Tournament.FirstYear > GameState.year ||
        Tournament.Round < 0 || Tournament.Round > AscensionTournamentRoundCount ||
        (Tournament.OfferedYear != -1 && !IsScheduledYear(Tournament.OfferedYear)) ||
        (Tournament.AttemptedYear != -1 && (!IsScheduledYear(Tournament.AttemptedYear) ||
            Tournament.AttemptedYear > Tournament.OfferedYear))) return "升仙大会届次或场次无效。";
    if (Tournament.AttemptedYear == -1) {
        if (Tournament.Round != 0 || Tournament.Active || !Tournament.OpponentSects.empty())
            return "未参赛的升仙大会含战斗进度。";
    } else {
        if (Tournament.OpponentSects.size() != 3) return "升仙大会冻结对手数量无效。";
        std::set<string> UniqueSects;
        for (const auto& SectId : Tournament.OpponentSects)
            if (!findSect(SectId) || !UniqueSects.insert(SectId).second) return "升仙大会冻结宗门无效。";
    }
    if (Tournament.Active != HasTournamentBattle) return "升仙大会活动与斗法状态不一致。";
    if (Tournament.Active) {
        if (!mfTournamentRewardFits(GameState)) return "升仙大会在途库存无法承接通关奖励。";
        if (!mfTournamentEligible(GameState) || Tournament.AttemptedYear != GameState.year ||
            Tournament.Round >= AscensionTournamentRoundCount || GameState.job || GameState.pending || GameState.dungeons.run ||
            (GameState.player.Zhuji && (GameState.player.Zhuji->Status == ZhujiStatus::Preparing ||
                GameState.player.Zhuji->Status == ZhujiStatus::Active))) return "升仙大会连战与其他活动冲突。";
        const auto& BattleState = *GameState.player.arts.battle;
        const string ExpectedSect = Tournament.Round == 0 ? "" :
            Tournament.OpponentSects[static_cast<size_t>(Tournament.Round - 1)];
        if (BattleState.npcId != mfTournamentOpponentId(Tournament.Round) ||
            BattleState.enemyStage != std::max(0, Tournament.Round - 1) || BattleState.enemySect != ExpectedSect ||
            BattleState.lethal || BattleState.shadowPlayer || BattleState.enemyBeast ||
            !BattleState.advKind.empty() || !BattleState.dungeonSite.empty()) return "升仙大会当前对手与冻结进度不一致。";
    }
    if (HasInvitation) {
        if (!mfTournamentEligible(GameState) || !mfIsAscensionTournamentYear(GameState) ||
            Tournament.OfferedYear != GameState.year || Tournament.AttemptedYear == GameState.year ||
            GameState.job || battleActive(GameState) || GameState.dungeons.run)
            return "升仙大会邀请与届次或活动冲突。";
        const auto& Invitation = *GameState.pending;
        std::set<string> OptionIds;
        for (const auto& Option : Invitation.options) OptionIds.insert(Option.id);
        if (Invitation.options.size() != 2 || OptionIds != std::set<string>{"accept", "decline"} ||
            !Invitation.npcId.empty() || Invitation.adventure) return "升仙大会邀请选项无效。";
    }
    return "";
}
} // namespace nz
