// 仅诊断工具：链接本轮nzcore，使用真实Battle与AI；不进入游戏目标，不访问玩家存档或服务。
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_lab.h"
#include "core/combat/battle_scaling.h"
#include "core/content/game_ids.h"
#include "core/content/npc_combat_profiles.h"
#include "core/json.h"
#include "core/talents.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
using namespace nz;
using Json = json::Value;
constexpr std::array Seeds{31U,73U,137U};
constexpr int SwissRoundCount = 7;

struct Entrant {
    string Id, Name, Bucket, Style;
    Player Snapshot;
    bool Beast = false;
    int ActionCap = 0, ScoreHalfPoints = 0, MatchWins = 0, MatchDraws = 0, MatchLosses = 0, Byes = 0;
    vector<int> Opponents;
};
struct LegResult {
    int LeftHalfPoints = 0, RightHalfPoints = 0;
    bool Counted = false;
    Json Details{json::Object{}};
};
struct Counters {
    int Games = 0, LegitimateGames = 0, TrueDraws = 0, MutualDeathDraws = 0, DiagnosticAborts = 0;
    int IllegalActions = 0, DirectChecks = 0, DirectMismatches = 0;
    int IncomingChecks = 0, IncomingMismatches = 0, NetHpDeferredChecks = 0, TerminalUnobservable = 0;
    int RepeatedMatches = 0, ForbiddenBeastMatches = 0;
};

// 用途：为诊断准备八种境界合法的初始/完整快照；流程：参赛者目录；返回：真实试炼配装，无模板数值复制。
Player mfSnapshot(const string& BuildId, const string& Tier, int Stage) {
    BattleLab::Session Session;
    BattleLab::Selection Selection;
    Selection.BuildId = BuildId; Selection.LoadoutTier = Tier; Selection.Stage = Stage;
    Selection.OpponentId = "beast-body"; Selection.Seed = Seeds.front();
    const auto Started = BattleLab::mfStart(Session, Selection);
    if (!Started.Succeeded) throw std::runtime_error(Started.Error);
    Player Snapshot = Session.CurrentGame().player;
    Snapshot.arts.battle.reset();
    // 仅移除座位先手混杂：两方统一到本境基础遁速+200，真实伤害/功法/牌组完全不变。
    const int PassiveSpeed = static_cast<int>(std::floor(talentMods(Snapshot).escapeSpeed));
    if (PassiveSpeed < 0 || PassiveSpeed > 200) throw std::runtime_error("被动遁速超出诊断统一先手的可用范围。");
    Snapshot.arts.speed = 200 - PassiveSpeed;
    return Snapshot;
}

// 用途：建立16修士与5种专属妖兽；流程：每境界瑞士赛初始化；返回：独立快照，普通/完整行动预算引用核心常量。
vector<Entrant> mfLabEntrants(int Stage) {
    vector<Entrant> Entries;
    for (const auto& Build : artBuilds()) for (const string Tier : {"initial", "advanced"}) {
        Entrant Entry;
        Entry.Id = Tier + ":" + Build.id;
        Entry.Name = Build.name + (Tier == "advanced" ? "·完整传承" : "·初始配装");
        Entry.Bucket = Tier == "advanced" ? "complete" : "ordinary";
        Entry.Snapshot = mfSnapshot(Build.id, Tier, Stage); Entry.Snapshot.name = Entry.Name;
        Entry.ActionCap = std::min(NpcSectActionLimit,
            (Tier == "advanced" ? ChampionSectActionBase : CommonSectActionBase) + Stage / 3);
        Entries.push_back(std::move(Entry));
    }
    for (const auto& [Id, Style] : {std::pair{"beast-body", "体"}, {"beast-fire", "火"},
             {"beast-wood", "木"}, {"beast-mind", "神"}, {"beast-metal", "金"}}) {
        Entrant Entry; Entry.Id = Id; Entry.Name = string(Style) + "系妖兽";
        Entry.Bucket = "beast"; Entry.Style = Style; Entry.Beast = true;
        Entry.ActionCap = BeastCombat::mfActionLimit(Stage); Entries.push_back(std::move(Entry));
    }
    return Entries;
}

// 用途：读取真实六宗NPC普通和完整传承配装；流程：主瑞士赛；返回：不套用Lab建议、不追加隐藏被动的独立角色。
vector<Entrant> mfNpcEntrants(int Stage) {
    vector<Entrant> Entries;
    for (const auto& Profile : content::npcCombatProfiles()) for (const bool Complete : {false,true}) {
        Npc Character;
        Character.id = Profile.sect + (Complete ? ":complete" : ":ordinary");
        Character.name = string(findSect(Profile.sect)->name) + (Complete ? "·完整传承" : "·普通弟子");
        Character.sect = Profile.sect; Character.stage = Stage; Character.health = 100;
        Character.body = 0; Character.lifespan = realmOf(Stage).life;
        Character.dao = string(findSect(Profile.sect)->element);
        if (Complete) Character.combatProfileId = Profile.id;
        Entrant Entry;
        Entry.Id = Character.id; Entry.Name = Character.name;
        Entry.Bucket = Complete ? "complete" : "ordinary";
        auto& Snapshot = Entry.Snapshot;
        Snapshot.name = Character.name; Snapshot.sect = Character.sect;
        Snapshot.stage = Snapshot.highestStage = Stage; Snapshot.age = 20;
        Snapshot.lifespan = Character.lifespan; Snapshot.body = Character.body;
        Snapshot.health = 100; Snapshot.talent.clear(); Snapshot.traits.clear();
        Snapshot.arts = artNpcLoadout(Character);
        const int PassiveSpeed = static_cast<int>(std::floor(talentMods(Snapshot).escapeSpeed));
        if (PassiveSpeed < 0 || PassiveSpeed > 200) throw std::runtime_error("真实NPC被动遁速超出统一先手范围。");
        Snapshot.arts.speed = 200 - PassiveSpeed;
        Entry.ActionCap = std::min(NpcSectActionLimit,
            (Complete ? ChampionSectActionBase : CommonSectActionBase) + Stage / 3);
        Entries.push_back(std::move(Entry));
    }
    for (auto& Entry : mfLabEntrants(Stage)) if (Entry.Beast) Entries.push_back(std::move(Entry));
    return Entries;
}

// 用途：计算已实际交手对手的胜平分总和；流程：瑞士排序；返回：Buchholz，不把空轮算胜场。
int mfBuchholz(const vector<Entrant>& Entries, int Index) {
    int Total = 0;
    for (const int Opponent : Entries[Index].Opponents) Total += Entries[Opponent].ScoreHalfPoints;
    return Total;
}

// 用途：按胜平分和Buchholz排序；流程：每轮配对与最终金榜；返回：稳定索引序列，不改参赛者。
vector<int> mfRanked(const vector<Entrant>& Entries) {
    vector<int> Order(Entries.size()); std::iota(Order.begin(), Order.end(), 0);
    std::stable_sort(Order.begin(), Order.end(), [&](int Left, int Right) {
        const auto& A = Entries[Left]; const auto& B = Entries[Right];
        if (A.ScoreHalfPoints != B.ScoreHalfPoints) return A.ScoreHalfPoints > B.ScoreHalfPoints;
        const int BuchholzA = mfBuchholz(Entries, Left), BuchholzB = mfBuchholz(Entries, Right);
        if (BuchholzA != BuchholzB) return BuchholzA > BuchholzB;
        return A.Id < B.Id;
    });
    return Order;
}

// 用途：查实际交手记录；流程：防重赛；返回：存在时为真。
bool mfHavePlayed(const vector<Entrant>& Entries, int Left, int Right) {
    const auto& Opponents = Entries[Left].Opponents;
    return std::find(Opponents.begin(), Opponents.end(), Right) != Opponents.end();
}

// 用途：以有限回溯按积分接近配对；流程：受约束瑞士赛；返回：成功配对，永不允许妖兽互战。
bool mfPair(const vector<Entrant>& Entries, const vector<int>& Order, vector<std::pair<int,int>>& Pairs, bool AllowRepeat) {
    vector<bool> Used(Order.size());
    int SearchNodes = 0;
    const std::function<bool()> Search = [&]() {
        if (++SearchNodes > 100000) return false;
        std::size_t First = 0;
        while (First < Used.size() && Used[First]) ++First;
        if (First == Used.size()) return true;
        Used[First] = true;
        const int Left = Order[First];
        vector<std::size_t> Candidates;
        for (std::size_t Other = First + 1; Other < Order.size(); ++Other)
            if (!Used[Other] && !(Entries[Left].Beast && Entries[Order[Other]].Beast) &&
                (AllowRepeat || !mfHavePlayed(Entries, Left, Order[Other]))) Candidates.push_back(Other);
        std::stable_sort(Candidates.begin(), Candidates.end(), [&](std::size_t A, std::size_t B) {
            const auto Penalty = [&](std::size_t Candidate) {
                const int Right = Order[Candidate];
                return (mfHavePlayed(Entries, Left, Right) ? 1000000 : 0) +
                    std::abs(Entries[Left].ScoreHalfPoints - Entries[Right].ScoreHalfPoints) * 1000 +
                    std::abs(mfBuchholz(Entries, Left) - mfBuchholz(Entries, Right)) * 10 + static_cast<int>(Candidate);
            };
            return Penalty(A) < Penalty(B);
        });
        for (const auto Other : Candidates) {
            Used[Other] = true; Pairs.emplace_back(Left, Order[Other]);
            if (Search()) return true;
            Pairs.pop_back(); Used[Other] = false;
        }
        Used[First] = false; return false;
    };
    return Search();
}

// 用途：为忘机选一张真实基础牌；流程：AI候选资格；返回：先守势后攻击，和核心敌方目标策略一致。
string mfRemoveTarget(const Battle& State, const SpellDef& Spell) {
    if (!battleNeedsTarget(Spell)) return "";
    for (const string Wanted : {"defend", "strike"})
        for (const auto* Pile : {&State.hand, &State.draw, &State.discard}) for (const auto& Uid : *Pile) {
            const auto Card = std::find_if(State.deck.begin(), State.deck.end(), [&](const auto& Item) { return Item.uid == Uid; });
            if (Card != State.deck.end() && Card->spell == Wanted) return Uid;
        }
    return "";
}

// 用途：只从当前可见手牌用核心同一评分选下一张；流程：人类一侧自动施法；返回：UID/目标，不额外抽牌或窥视洗牌。
std::pair<string,string> mfChooseCard(const Player& Actor, const Battle& State) {
    int BestScore = -1; std::pair<string,string> Choice;
    for (const auto& Uid : State.hand) {
        const auto Card = std::find_if(State.deck.begin(), State.deck.end(), [&](const auto& Entry) { return Entry.uid == Uid; });
        const auto* Spell = Card == State.deck.end() ? nullptr : battleSpell(Card->spell);
        if (!Spell || Spell->stage > Actor.stage || State.mana < battleCost(Actor, *Spell)) continue;
        const auto Cooldown = State.cooldowns.find(Spell->id);
        if (Cooldown != State.cooldowns.end() && Cooldown->second > 0) continue;
        const string Target = mfRemoveTarget(State, *Spell);
        if (battleNeedsTarget(*Spell) && Target.empty()) continue;
        const int Score = mfBattleNpcCardPriority(Actor, State, *Spell);
        if (Score > BestScore) { BestScore = Score; Choice = {Uid,Target}; }
    }
    return Choice;
}

// 用途：取实际手牌神通定义；流程：动作证据；返回：当前牌稳定ID，未知为空。
string mfSpellId(const Battle& State, const string& Uid) {
    for (const auto& Card : State.deck) if (Card.uid == Uid) return Card.spell;
    return "";
}

// 用途：执行一次有完整证据的真实牌局；流程：瑞士配对的种子/双腿；返回：胜平积分与明示中断原因。
LegResult mfPlay(const Entrant& Left, const Entrant& Right, int Stage, unsigned Seed, int Leg, Counters& Totals) {
    ++Totals.Games;
    LegResult Result;
    auto& Details = Result.Details;
    Details.set("left", Left.Id); Details.set("right", Right.Id); Details.set("seed", static_cast<double>(Seed)); Details.set("leg", Leg);
    const bool AgainstBeast = Left.Beast || Right.Beast;
    const Entrant& Actor = Left.Beast ? Right : Left;
    const Entrant& Foe = Left.Beast ? Left : Right;
    const bool ActorIsLeft = !Left.Beast;
    Game Encounter;
    if (AgainstBeast) {
        Encounter.seed = Encounter.rng = Seed; Encounter.player = Actor.Snapshot;
        Encounter.clock.ageBase = Encounter.player.age;
        EnemySpec Enemy;
        Enemy.name = Foe.Name; Enemy.kind = "spar"; Enemy.stage = Stage; Enemy.style = Foe.Style; Enemy.beast = true;
        Enemy.speed = Leg == 0 ? 0 : 10000; // 两腿分别检验修士先手/兽先手，仅诊断控序，不增加伤害。
        battleStart(Encounter, Enemy);
    } else {
        // Leg1由调用方交换Left/Right，因此此处始终让本腿左席先行，消除PvE隐藏血量乘区。
        battleStartShadow(Encounter, Left.Snapshot, Right.Snapshot, Seed);
    }
    Details.set("mode", AgainstBeast ? "real-pve-beast" : "symmetric-shadow-pvp");
    Details.set("actor", Actor.Id); Details.set("foe", Foe.Id);
    Details.set("actorActionCap", Actor.ActionCap); Details.set("foeActionCap", Foe.ActionCap);
    Details.set("initiative", AgainstBeast ? (Leg == 0 ? "human" : "beast") : "left-seat");
    json::Array Trace;
    int ActionCount = 0, EndCount = 0, LegalErrorCount = 0;
    long long DirectDamage = 0, RawDirectDamage = 0, PlayerDotPotential = 0, IncomingForecast = 0;
    bool Aborted = false;
    string AbortReason;
    while (battleActive(Encounter) && EndCount < 28 && ActionCount < 500) {
        auto& State = *Encounter.player.arts.battle;
        if (!AgainstBeast && !State.playerFirst) {
            Aborted = true; AbortReason = "uncontrolled-initial-initiative"; break;
        }
        if (!AgainstBeast && State.enemyCards.planned.size() > static_cast<std::size_t>(Foe.ActionCap))
            State.enemyCards.planned.resize(Foe.ActionCap);
        const auto Choice = State.played < Actor.ActionCap ? mfChooseCard(Encounter.player, State) : std::pair<string,string>{};
        Json Action(json::Object{});
        Action.set("turn", State.turn); Action.set("actorMana", State.mana); Action.set("actorHpBefore", State.hp);
        Action.set("foeHpBefore", State.enemyHp); Action.set("playedThisTurn", State.played);
        if (!Choice.first.empty()) {
            const string SpellId = mfSpellId(State, Choice.first);
            const auto Preview = battlePreview(Encounter.player, State, SpellId);
            const int FoeHpBefore = State.enemyHp;
            Action.set("action", SpellId); Action.set("uid", Choice.first); Action.set("target", Choice.second);
            Action.set("previewRawDamage", Preview.RawDamage); Action.set("previewDamage", Preview.damage);
            Action.set("previewHeal", Preview.heal); Action.set("previewShield", Preview.shield);
            const auto Turn = battleTurn(Encounter, Choice.first, Choice.second);
            ++ActionCount;
            if (!Turn.ok) { ++LegalErrorCount; Action.set("error", Turn.error); Aborted = true; AbortReason = "illegal-policy-action"; }
            if (Encounter.player.arts.battle) {
                const int ActualDamage = std::max(0, FoeHpBefore - Encounter.player.arts.battle->enemyHp);
                Action.set("actualDamage", ActualDamage); ++Totals.DirectChecks;
                if (ActualDamage != Preview.damage) ++Totals.DirectMismatches;
                DirectDamage += ActualDamage;
            } else { ++Totals.TerminalUnobservable; Action.set("actualDamage", Json()); }
            RawDirectDamage += Preview.RawDamage;
        } else {
            const auto Preview = battleEnemyPreview(Encounter);
            const int HpBefore = State.hp;
            const int DotIncoming = (State.playerPoisonTurns > 0 ? State.playerPoison : 0) +
                (State.enemyCards.burnTurns > 0 ? State.enemyCards.burn : 0);
            const int DotPotential = (State.poisonTurns > 0 ? std::max(0, State.poisonDamage) : 0) + (State.burnTurns > 0 ? State.burn : 0);
            const auto Mods = talentMods(Encounter.player);
            const bool HasPostHeal = (!State.attacked && Mods.quietHeal > 0) || artHas(Encounter.player.arts, "木", 2) || hasTalent(Encounter.player, "dragonBlood");
            Action.set("action", "end"); Action.set("incomingPreview", Preview.damage);
            Action.set("incomingDotPotential", DotIncoming); Action.set("outgoingDotPotential", DotPotential);
            Action.set("postHealMayApply", HasPostHeal);
            Action.set("foePlannedCount", static_cast<int>(State.enemyCards.planned.size()));
            json::Array PlannedCards;
            for (const auto& Uid : State.enemyCards.planned) {
                Json Item(json::Object{}); Item.set("uid", Uid);
                for (const auto& Card : State.enemyCards.deck) if (Card.uid == Uid) { Item.set("id", Card.spell); break; }
                PlannedCards.push_back(std::move(Item));
            }
            Action.set("foePlannedCards", Json(std::move(PlannedCards)));
            json::Array BeastSteps;
            for (const auto& Step : Preview.BeastActions) {
                Json Item(json::Object{}); Item.set("id", string(BeastCombat::mfActionId(Step.Action.Kind)));
                Item.set("rawDamage", Step.Action.RawDamage); Item.set("damage", Step.Damage); Item.set("blocked", Step.Blocked);
                Item.set("executed", Step.Executed); BeastSteps.push_back(std::move(Item));
            }
            Action.set("beastActions", Json(std::move(BeastSteps)));
            const auto Turn = battleTurn(Encounter, "end");
            ++EndCount; ++ActionCount;
            if (!Turn.ok) { ++LegalErrorCount; Action.set("error", Turn.error); Aborted = true; AbortReason = "illegal-end-action"; }
            if (Encounter.player.arts.battle) {
                const int HpAfter = Encounter.player.arts.battle->hp;
                Action.set("actorHpAfter", HpAfter); Action.set("netHpLoss", HpBefore - HpAfter);
                // 终局先被DOT击败对手时不发生来伤；预览本身已按相同顺序跳过。
                if (!HasPostHeal || !Encounter.player.arts.battle->shadowOutcome.empty()) {
                    ++Totals.IncomingChecks;
                    if (std::max(0, HpBefore - HpAfter) != Preview.damage) ++Totals.IncomingMismatches;
                } else { ++Totals.NetHpDeferredChecks; Action.set("comparison", "net-hp-includes-post-action-heal"); }
            } else { ++Totals.TerminalUnobservable; Action.set("comparison", "pve-terminal-battle-cleared"); }
            PlayerDotPotential += DotPotential; IncomingForecast += Preview.damage;
        }
        Trace.push_back(std::move(Action));
        if (Aborted) break;
    }
    const string ShadowOutcome = battleShadowOutcome(Encounter);
    const bool StillActive = battleActive(Encounter);
    if (StillActive) { Aborted = true; if (AbortReason.empty()) AbortReason = "diagnostic-limit-interruption"; }
    if (!AgainstBeast && ShadowOutcome != "win" && ShadowOutcome != "loss" && ShadowOutcome != "draw") {
        Aborted = true; if (AbortReason.empty()) AbortReason = "unknown-or-missing-shadow-outcome";
    }
    Totals.IllegalActions += LegalErrorCount;
    if (Aborted) ++Totals.DiagnosticAborts;
    else {
        Result.Counted = true; ++Totals.LegitimateGames;
        bool ActorWon = false, Draw = false;
        if (AgainstBeast) ActorWon = Encounter.player.arts.progress.wins > 0;
        else { Draw = ShadowOutcome == "draw"; ActorWon = ShadowOutcome == "win"; }
        if (Draw) {
            Result.LeftHalfPoints = Result.RightHalfPoints = 1;
            if (Encounter.player.arts.battle->turn >= 24) { ++Totals.TrueDraws; Details.set("drawReason", "true-24-round-limit"); }
            else { ++Totals.MutualDeathDraws; Details.set("drawReason", "mutual-death"); }
        }
        else if (ActorWon == ActorIsLeft) Result.LeftHalfPoints = 2;
        else Result.RightHalfPoints = 2;
        Details.set("outcome", Draw ? "draw" : Result.LeftHalfPoints == 2 ? "left-win" : "right-win");
    }
    Details.set("counted", Result.Counted); Details.set("aborted", Aborted); Details.set("abortReason", AbortReason);
    Details.set("shadowOutcome", ShadowOutcome); Details.set("turns", EndCount); Details.set("actions", ActionCount);
    Details.set("directDamageObserved", static_cast<double>(DirectDamage)); Details.set("rawDirectDamage", static_cast<double>(RawDirectDamage));
    Details.set("outgoingDotPotential", static_cast<double>(PlayerDotPotential)); Details.set("incomingForecast", static_cast<double>(IncomingForecast));
    Details.set("trace", Json(std::move(Trace)));
    return Result;
}

// 用途：执行一境界七轮受约束瑞士赛；流程：主程序五境界验收；返回：配对、种子、全动作证据与排名。
Json mfTournament(int Stage, Counters& Totals) {
    auto Entries = mfNpcEntrants(Stage);
    Json Tournament(json::Object{}); Tournament.set("stage", Stage); Tournament.set("realm", realmName(Stage));
    json::Array EntrantCatalog;
    for (const auto& Entry : Entries) {
        Json Item(json::Object{}); Item.set("id", Entry.Id); Item.set("bucket", Entry.Bucket); Item.set("actionCap", Entry.ActionCap);
        if (!Entry.Beast) {
            Item.set("main", Entry.Snapshot.arts.main); Item.set("speed", battleSpeed(Entry.Snapshot));
            Item.set("maxHp", battleReferenceState(Entry.Snapshot).max);
            json::Array Spells, Manuals, Nodes;
            for (const auto& Id : Entry.Snapshot.arts.spells) Spells.push_back(Id);
            for (const auto& [Id, Level] : Entry.Snapshot.arts.manuals) { Json Manual(json::Object{}); Manual.set("id", Id); Manual.set("level", Level); Manuals.push_back(std::move(Manual)); }
            for (const auto& Id : Entry.Snapshot.arts.nodes) Nodes.push_back(Id);
            Item.set("spells", Json(std::move(Spells))); Item.set("manuals", Json(std::move(Manuals))); Item.set("nodes", Json(std::move(Nodes)));
        }
        EntrantCatalog.push_back(std::move(Item));
    }
    Tournament.set("entrants", Json(std::move(EntrantCatalog)));
    std::map<std::pair<string,string>,std::array<int,3>> BucketResults;
    json::Array Rounds;
    for (int Round = 0; Round < SwissRoundCount; ++Round) {
        auto Order = mfRanked(Entries);
        // 奇数参赛者每轮仅一名修士空轮，优先空轮少且积分低者；不计胜场或任何虚构实战。
        int Bye = -1;
        for (auto It = Order.rbegin(); It != Order.rend(); ++It)
            if (!Entries[*It].Beast && (Bye < 0 || Entries[*It].Byes < Entries[Bye].Byes)) Bye = *It;
        if (Bye < 0) throw std::runtime_error("没有合法修士空轮候选。");
        ++Entries[Bye].Byes; Order.erase(std::remove(Order.begin(), Order.end(), Bye), Order.end());
        vector<std::pair<int,int>> Pairs;
        bool AllowedRepeat = false;
        if (!mfPair(Entries, Order, Pairs, false)) {
            Pairs.clear(); AllowedRepeat = true;
            if (!mfPair(Entries, Order, Pairs, true)) throw std::runtime_error("受约束瑞士赛无法形成非兽兽配对。");
        }
        Json RoundView(json::Object{}); RoundView.set("round", Round + 1); RoundView.set("bye", Entries[Bye].Id);
        RoundView.set("repeatFallbackUsed", AllowedRepeat);
        json::Array Matches;
        for (const auto& [Left, Right] : Pairs) {
            if (Entries[Left].Beast && Entries[Right].Beast) { ++Totals.ForbiddenBeastMatches; throw std::runtime_error("禁止妖兽互战。"); }
            Json Match(json::Object{}); Match.set("left", Entries[Left].Id); Match.set("right", Entries[Right].Id);
            Match.set("leftBucket", Entries[Left].Bucket); Match.set("rightBucket", Entries[Right].Bucket);
            const bool Repeated = mfHavePlayed(Entries, Left, Right); Match.set("repeated", Repeated);
            if (Repeated) ++Totals.RepeatedMatches;
            int LeftScore = 0, RightScore = 0, Counted = 0;
            json::Array Legs;
            for (const unsigned Seed : Seeds) for (int Leg = 0; Leg < 2; ++Leg) {
                const bool Swap = Leg == 1 && !Entries[Left].Beast && !Entries[Right].Beast;
                auto Outcome = mfPlay(Entries[Swap ? Right : Left], Entries[Swap ? Left : Right], Stage, Seed, Leg, Totals);
                if (Outcome.Counted) {
                    ++Counted;
                    const int LeftPoints = Swap ? Outcome.RightHalfPoints : Outcome.LeftHalfPoints;
                    const int RightPoints = Swap ? Outcome.LeftHalfPoints : Outcome.RightHalfPoints;
                    LeftScore += LeftPoints; RightScore += RightPoints;
                    if (Entries[Left].Bucket != Entries[Right].Bucket) {
                        const bool BucketSwap = Entries[Left].Bucket > Entries[Right].Bucket;
                        auto& Bucket = BucketResults[{BucketSwap ? Entries[Right].Bucket : Entries[Left].Bucket,
                            BucketSwap ? Entries[Left].Bucket : Entries[Right].Bucket}];
                        if (LeftPoints == RightPoints) ++Bucket[1];
                        else if ((LeftPoints > RightPoints) != BucketSwap) ++Bucket[0];
                        else ++Bucket[2];
                    }
                }
                Legs.push_back(std::move(Outcome.Details));
            }
            Match.set("leftGameHalfPoints", LeftScore); Match.set("rightGameHalfPoints", RightScore);
            Match.set("countedGames", Counted); Match.set("legs", Json(std::move(Legs)));
            Entries[Left].Opponents.push_back(Right); Entries[Right].Opponents.push_back(Left);
            if (Counted == 6) {
                if (LeftScore > RightScore) { Entries[Left].ScoreHalfPoints += 2; ++Entries[Left].MatchWins; ++Entries[Right].MatchLosses; }
                else if (LeftScore < RightScore) { Entries[Right].ScoreHalfPoints += 2; ++Entries[Right].MatchWins; ++Entries[Left].MatchLosses; }
                else { ++Entries[Left].ScoreHalfPoints; ++Entries[Right].ScoreHalfPoints; ++Entries[Left].MatchDraws; ++Entries[Right].MatchDraws; }
                Match.set("ranked", true);
            } else Match.set("ranked", false); // 中断/非法不是落败，不悄悄污染积分。
            Matches.push_back(std::move(Match));
        }
        RoundView.set("matches", Json(std::move(Matches))); Rounds.push_back(std::move(RoundView));
        std::cout << "stage=" << Stage << " round=" << Round + 1 << " games=" << Totals.Games << '\n';
    }
    Tournament.set("rounds", Json(std::move(Rounds)));
    json::Array Ranking;
    int Rank = 0;
    for (const int Index : mfRanked(Entries)) {
        const auto& Entry = Entries[Index]; Json Row(json::Object{});
        Row.set("rank", ++Rank); Row.set("id", Entry.Id); Row.set("name", Entry.Name); Row.set("bucket", Entry.Bucket);
        Row.set("points", Entry.ScoreHalfPoints / 2.0); Row.set("buchholz", mfBuchholz(Entries, Index) / 2.0);
        Row.set("wins", Entry.MatchWins); Row.set("draws", Entry.MatchDraws); Row.set("losses", Entry.MatchLosses); Row.set("byes", Entry.Byes);
        Row.set("actionCap", Entry.ActionCap); Ranking.push_back(std::move(Row));
    }
    Tournament.set("ranking", Json(std::move(Ranking)));
    json::Array BucketRows;
    for (const auto& [Key, Counts] : BucketResults) {
        Json Item(json::Object{}); Item.set("leftBucket", Key.first); Item.set("rightBucket", Key.second);
        Item.set("leftWins", Counts[0]); Item.set("draws", Counts[1]); Item.set("rightWins", Counts[2]);
        Item.set("leftWinFraction", Counts[0] / static_cast<double>(Counts[0]+Counts[1]+Counts[2]));
        Item.set("leftScoreFraction", (Counts[0] + Counts[1] * .5) / (Counts[0]+Counts[1]+Counts[2]));
        BucketRows.push_back(std::move(Item));
    }
    Tournament.set("bucketHeadToHead", Json(std::move(BucketRows))); return Tournament;
}

// 用途：补足八种玩家流派建议的真实牌局；流程：独立于NPC主赛的辅助评估；返回：初始对完整和各自对体兽，不冒称真实NPC人口。
Json mfLabSupplement(int Stage, Counters& Totals) {
    const auto Entries = mfLabEntrants(Stage);
    const auto Beast = std::find_if(Entries.begin(), Entries.end(), [](const auto& Entry) { return Entry.Id == "beast-body"; });
    if (Beast == Entries.end()) throw std::runtime_error("补充测试缺少体兽。");
    Json Result(json::Object{}); Result.set("stage", Stage); Result.set("kind", "lab-suggestions-supplement");
    json::Array Rows;
    for (const auto& Build : artBuilds()) {
        const auto Initial = std::find_if(Entries.begin(), Entries.end(), [&](const auto& Entry) { return Entry.Id == "initial:" + Build.id; });
        const auto Advanced = std::find_if(Entries.begin(), Entries.end(), [&](const auto& Entry) { return Entry.Id == "advanced:" + Build.id; });
        if (Initial == Entries.end() || Advanced == Entries.end()) throw std::runtime_error("补充测试缺少流派层级。");
        for (const auto& Pair : {std::pair{&*Initial,&*Advanced}, std::pair{&*Initial,&*Beast}, std::pair{&*Advanced,&*Beast}}) {
            Json Row(json::Object{}); Row.set("left", Pair.first->Id); Row.set("right", Pair.second->Id);
            int LeftPoints = 0, RightPoints = 0, Counted = 0; json::Array Legs;
            for (const unsigned Seed : Seeds) for (int Leg = 0; Leg < 2; ++Leg) {
                const bool Swap = Leg == 1 && !Pair.second->Beast;
                auto Outcome = mfPlay(Swap ? *Pair.second : *Pair.first, Swap ? *Pair.first : *Pair.second, Stage, Seed, Leg, Totals);
                if (Outcome.Counted) { ++Counted; LeftPoints += Swap ? Outcome.RightHalfPoints : Outcome.LeftHalfPoints; RightPoints += Swap ? Outcome.LeftHalfPoints : Outcome.RightHalfPoints; }
                Legs.push_back(std::move(Outcome.Details));
            }
            Row.set("leftGameHalfPoints", LeftPoints); Row.set("rightGameHalfPoints", RightPoints);
            Row.set("countedGames", Counted); Row.set("legs", Json(std::move(Legs))); Rows.push_back(std::move(Row));
        }
    }
    Result.set("matches", Json(std::move(Rows))); return Result;
}

// 用途：用均匀对手验证真实NPC梯度；流程：定量验收；返回：六宗同宗完整对普通，以及12修士各对5兽的三种子双腿。
Json mfUniformNpcMatrix(int Stage, Counters& Totals) {
    const auto Entries = mfNpcEntrants(Stage);
    vector<std::pair<int,int>> Pairs;
    for (std::size_t Index = 0; Index < Entries.size(); ++Index) {
        if (Entries[Index].Beast) continue;
        if (Entries[Index].Bucket == "complete") {
            for (std::size_t Other = 0; Other < Entries.size(); ++Other)
                if (Entries[Other].Bucket == "ordinary" && Entries[Index].Snapshot.sect == Entries[Other].Snapshot.sect)
                    Pairs.emplace_back(static_cast<int>(Index), static_cast<int>(Other));
        }
        for (std::size_t Other = 0; Other < Entries.size(); ++Other)
            if (Entries[Other].Beast) Pairs.emplace_back(static_cast<int>(Index), static_cast<int>(Other));
    }
    Json Result(json::Object{}); Result.set("stage", Stage); Result.set("kind", "uniform-real-npc-matrix");
    Result.set("design", "6宗各普通/完整×5兽；另6同宗完整vs普通；每比较3种子双腿；无Swiss配对选择偏差");
    std::map<std::pair<string,string>,std::array<int,3>> Buckets;
    json::Array Rows;
    for (const auto& [LeftIndex, RightIndex] : Pairs) {
        const auto& Left = Entries[LeftIndex]; const auto& Right = Entries[RightIndex];
        Json Row(json::Object{}); Row.set("left", Left.Id); Row.set("right", Right.Id);
        Row.set("leftBucket", Left.Bucket); Row.set("rightBucket", Right.Bucket);
        int Wins = 0, Draws = 0, Losses = 0, Counted = 0; json::Array Legs;
        for (const unsigned Seed : Seeds) for (int Leg = 0; Leg < 2; ++Leg) {
            const bool Swap = Leg == 1 && !Right.Beast;
            auto Outcome = mfPlay(Swap ? Right : Left, Swap ? Left : Right, Stage, Seed, Leg, Totals);
            if (Outcome.Counted) {
                ++Counted;
                const int LeftPoints = Swap ? Outcome.RightHalfPoints : Outcome.LeftHalfPoints;
                const int RightPoints = Swap ? Outcome.LeftHalfPoints : Outcome.RightHalfPoints;
                auto& Bucket = Buckets[{Left.Bucket,Right.Bucket}];
                if (LeftPoints == RightPoints) { ++Draws; ++Bucket[1]; }
                else if (LeftPoints > RightPoints) { ++Wins; ++Bucket[0]; }
                else { ++Losses; ++Bucket[2]; }
            }
            Legs.push_back(std::move(Outcome.Details));
        }
        Row.set("leftWins", Wins); Row.set("draws", Draws); Row.set("rightWins", Losses); Row.set("countedGames", Counted);
        Row.set("leftScoreFraction", Counted ? (Wins + Draws * .5) / Counted : 0);
        Row.set("legs", Json(std::move(Legs))); Rows.push_back(std::move(Row));
    }
    json::Array BucketRows;
    for (const auto& [Key, Counts] : Buckets) {
        const int Games = Counts[0]+Counts[1]+Counts[2];
        Json Row(json::Object{}); Row.set("leftBucket", Key.first); Row.set("rightBucket", Key.second);
        Row.set("leftWins", Counts[0]); Row.set("draws", Counts[1]); Row.set("rightWins", Counts[2]); Row.set("games", Games);
        Row.set("leftWinFraction", Counts[0] / static_cast<double>(Games));
        Row.set("leftScoreFraction", (Counts[0] + Counts[1] * .5) / Games); BucketRows.push_back(std::move(Row));
    }
    Result.set("bucketHeadToHead", Json(std::move(BucketRows))); Result.set("comparisons", Json(std::move(Rows))); return Result;
}

// 用途：均匀检验同层级六宗互打；流程：独立mirror诊断；返回：15pair真实六局与有方向的6×6胜平负矩阵，自身格为空。
Json mfMirrorMatrix(int Stage, const string& Bucket, Counters& Totals) {
    vector<Entrant> Entries;
    for (auto& Entry : mfNpcEntrants(Stage)) if (!Entry.Beast && Entry.Bucket == Bucket) Entries.push_back(std::move(Entry));
    if (Entries.size() != 6) throw std::runtime_error("六宗组内诊断需要恰好六位真实修士。");
    using Cell = std::array<int,3>;
    std::array<std::array<Cell,6>,6> Counts{};
    json::Array Pairs;
    for (std::size_t LeftIndex = 0; LeftIndex < Entries.size(); ++LeftIndex)
        for (std::size_t RightIndex = LeftIndex + 1; RightIndex < Entries.size(); ++RightIndex) {
            const auto& Left = Entries[LeftIndex]; const auto& Right = Entries[RightIndex];
            Json Pair(json::Object{}); Pair.set("left", Left.Id); Pair.set("right", Right.Id);
            json::Array Legs; int Counted = 0;
            for (const unsigned Seed : Seeds) for (int Leg = 0; Leg < 2; ++Leg) {
                const bool Swap = Leg == 1;
                auto Outcome = mfPlay(Swap ? Right : Left, Swap ? Left : Right, Stage, Seed, Leg, Totals);
                if (Outcome.Counted) {
                    ++Counted;
                    const int LeftPoints = Swap ? Outcome.RightHalfPoints : Outcome.LeftHalfPoints;
                    const int RightPoints = Swap ? Outcome.LeftHalfPoints : Outcome.RightHalfPoints;
                    if (LeftPoints == RightPoints) { ++Counts[LeftIndex][RightIndex][1]; ++Counts[RightIndex][LeftIndex][1]; }
                    else if (LeftPoints > RightPoints) { ++Counts[LeftIndex][RightIndex][0]; ++Counts[RightIndex][LeftIndex][2]; }
                    else { ++Counts[LeftIndex][RightIndex][2]; ++Counts[RightIndex][LeftIndex][0]; }
                }
                Legs.push_back(std::move(Outcome.Details));
            }
            const auto& Stats = Counts[LeftIndex][RightIndex];
            Pair.set("leftWins", Stats[0]); Pair.set("draws", Stats[1]); Pair.set("rightWins", Stats[2]);
            Pair.set("countedGames", Counted); Pair.set("legs", Json(std::move(Legs))); Pairs.push_back(std::move(Pair));
        }
    Json Result(json::Object{}); Result.set("stage", Stage); Result.set("realm", realmName(Stage)); Result.set("bucket", Bucket);
    Result.set("kind", "uniform-same-tier-real-npc-matrix");
    json::Array Participants, Matrix, Summary;
    for (std::size_t Index = 0; Index < Entries.size(); ++Index) {
        Json Actor(json::Object{}); Actor.set("id", Entries[Index].Id); Actor.set("name", Entries[Index].Name); Participants.push_back(Actor);
        Json Row = Actor; json::Array Opponents; Cell Total{};
        for (std::size_t Other = 0; Other < Entries.size(); ++Other) {
            if (Other == Index) { Opponents.push_back(Json()); continue; }
            const auto& Stats = Counts[Index][Other];
            const int Games = Stats[0]+Stats[1]+Stats[2];
            Json Column(json::Object{}); Column.set("opponent", Entries[Other].Id);
            Column.set("wins", Stats[0]); Column.set("draws", Stats[1]); Column.set("losses", Stats[2]); Column.set("games", Games);
            Column.set("winFraction", Games ? Stats[0] / static_cast<double>(Games) : 0);
            Column.set("scoreFraction", Games ? (Stats[0] + .5 * Stats[1]) / Games : 0);
            Opponents.push_back(std::move(Column));
            for (int Outcome = 0; Outcome < 3; ++Outcome) Total[Outcome] += Stats[Outcome];
        }
        Row.set("opponents", Json(std::move(Opponents))); Matrix.push_back(std::move(Row));
        const int Games = Total[0]+Total[1]+Total[2];
        Actor.set("wins", Total[0]); Actor.set("draws", Total[1]); Actor.set("losses", Total[2]); Actor.set("games", Games);
        Actor.set("winFraction", Games ? Total[0] / static_cast<double>(Games) : 0);
        Actor.set("scoreFraction", Games ? (Total[0] + .5 * Total[1]) / Games : 0);
        Summary.push_back(std::move(Actor));
    }
    Result.set("participants", Json(std::move(Participants))); Result.set("pairs", Json(std::move(Pairs)));
    Result.set("matrix", Json(std::move(Matrix))); Result.set("summary", Json(std::move(Summary)));
    return Result;
}
}

// 用途：链接本轮真实库执行离线数值验收；流程：开发诊断；使用：--output 路径 --build 标签 --library-sha SHA。
// 返回：0正常，2规则/预览异常；中断单独记录，不写档、不启动HTTP，不作为产品玩法。
int main(int ArgumentCount, char** Arguments) {
    try {
        string Output = "tmp/battle-swiss-current.json", BuildTag = "unlabelled-current-library", LibrarySha, Mode = "full";
        int OnlyStage = -1;
        for (int Index = 1; Index < ArgumentCount; ++Index) {
            const string Argument = Arguments[Index];
            if (Index + 1 >= ArgumentCount) throw std::runtime_error("诊断参数缺少值。");
            const string Value = Arguments[++Index];
            if (Argument == "--output") Output = Value;
            else if (Argument == "--build") BuildTag = Value;
            else if (Argument == "--library-sha") LibrarySha = Value;
            else if (Argument == "--mode") {
                if (Value != "full" && Value != "mirror" && Value != "matrix" && Value != "swiss")
                    throw std::runtime_error("诊断模式仅支持full/mirror/matrix/swiss。");
                Mode = Value;
            }
            else if (Argument == "--stage") {
                const auto Stages = BattleLab::mfStages();
                auto Found = std::find_if(Stages.begin(), Stages.end(), [&](int Stage) { return Value == std::to_string(Stage); });
                if (Found == Stages.end()) throw std::runtime_error("非法诊断境界。");
                OnlyStage = *Found;
            }
            else throw std::runtime_error("未知诊断参数。");
        }
        Json Report(json::Object{});
        Report.set("build", BuildTag); Report.set("librarySha256", LibrarySha);
        Report.set("mode", Mode);
        Report.set("policyVersion", "npc-priority-shared-v1-cap-prefix-swiss-npc17-matrix6x2x5-seeds31-73-137");
        Report.set("driverCompiled", string(__DATE__) + " " + __TIME__);
        Report.set("rules", "受约束PvE瑞士赛；六宗真实artNpcLoadout普通/完整各一人加五妖兽共17人；每境界七轮；三种子双腿；禁止兽兽；空轮不计分；影身同血量规则和双方控序；真实行动cap与AI评分；另含八流派Lab配装辅助牌局");
        Report.set("limitations", "主赛使用真实NPC配装但无随机装备和服丹；仅人类空轮0分使Swiss实际场数不等，榜仅展示，梯度判断以均匀矩阵为准；八流派Lab建议单列，不当作NPC梯度证据；PvP保留真实24轮平局；妖兽保留真实PvE1.45血量，双腿控制先手；回合净HP含静养者单独标明不冒充意图不符；PvE终局移除Battle则不伪造实际终击数值。");
        if (Mode == "mirror") {
            Report.set("policyVersion", "npc-priority-shared-v1-cap-prefix-sixsects-same-tier-seeds31-73-137");
            Report.set("rules", "六宗真实普通组内与完整组内分别15均匀pair，每pair三种子交换座位双腿；五境共900局；不含对自身，使用真实影身同血量规则/真实行动cap/核心评分");
            Report.set("limitations", "真实NPC模板，无随机装备或服丹；固定三种子与同一AI策略，不是玩家人口胜率；同组总体含平得分率必为50%，需比较各宗对其余五宗的分布；自身格为空；后续静养净HP与真实24轮平局另行记录。");
        }
        Counters Totals; json::Array Tournaments, Supplements, Matrices, Mirrors;
        for (const int Stage : BattleLab::mfStages())
            if (OnlyStage < 0 || Stage == OnlyStage) {
                if (Mode == "full" || Mode == "swiss") Tournaments.push_back(mfTournament(Stage, Totals));
                if (Mode == "full") Supplements.push_back(mfLabSupplement(Stage, Totals));
                if (Mode == "full" || Mode == "matrix") Matrices.push_back(mfUniformNpcMatrix(Stage, Totals));
                if (Mode == "mirror") for (const string Bucket : {"ordinary", "complete"}) Mirrors.push_back(mfMirrorMatrix(Stage, Bucket, Totals));
            }
        Report.set("tournaments", Json(std::move(Tournaments)));
        Report.set("labSupplements", Json(std::move(Supplements)));
        Report.set("uniformNpcMatrices", Json(std::move(Matrices)));
        Report.set("sameTierMatrices", Json(std::move(Mirrors)));
        Json Checks(json::Object{});
        Checks.set("games", Totals.Games); Checks.set("legitimateGames", Totals.LegitimateGames); Checks.set("true24RoundDraws", Totals.TrueDraws);
        Checks.set("mutualDeathDraws", Totals.MutualDeathDraws);
        Checks.set("diagnosticAborts", Totals.DiagnosticAborts); Checks.set("illegalActions", Totals.IllegalActions);
        Checks.set("directPreviewChecks", Totals.DirectChecks); Checks.set("directPreviewMismatches", Totals.DirectMismatches);
        Checks.set("incomingPreviewChecks", Totals.IncomingChecks); Checks.set("incomingPreviewMismatches", Totals.IncomingMismatches);
        Checks.set("postHealNetHpDeferredChecks", Totals.NetHpDeferredChecks); Checks.set("pveTerminalUnobservable", Totals.TerminalUnobservable);
        Checks.set("repeatedMatches", Totals.RepeatedMatches); Checks.set("forbiddenBeastMatches", Totals.ForbiddenBeastMatches);
        Report.set("checks", std::move(Checks));
        const auto Path = std::filesystem::u8path(Output);
        if (!Path.parent_path().empty()) std::filesystem::create_directories(Path.parent_path());
        std::ofstream Stream(Path, std::ios::binary); Stream << Report.dump();
        if (!Stream) throw std::runtime_error("无法写入诊断报告。");
        std::cout << "games=" << Totals.Games << " aborted=" << Totals.DiagnosticAborts << " illegal=" << Totals.IllegalActions
            << " directMismatches=" << Totals.DirectMismatches << " incomingMismatches=" << Totals.IncomingMismatches << '\n';
        return Totals.DiagnosticAborts || Totals.LegitimateGames != Totals.Games || Totals.IllegalActions ||
            Totals.DirectMismatches || Totals.IncomingMismatches || Totals.ForbiddenBeastMatches ? 2 : 0;
    } catch (const std::exception& Error) { std::cerr << Error.what() << '\n'; return 1; }
}
