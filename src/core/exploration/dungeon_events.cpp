#include "core/exploration/dungeon_events.h"
#include "core/exploration/dungeon_reward_rules.h"
#include "core/battle.h"
#include "core/content/game_ids.h"
#include "core/dungeon.h"
#include "core/foundation/state_limits.h"
#include "core/sim.h"
#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <type_traits>

namespace nz {
namespace {
enum class EventAction { Heal, Stones, Card, Relic, Remove };
struct EventOffer {
    DungeonReward Reward;
    EventAction Action = EventAction::Heal;
    int HealthCost = 0, StoneCost = 0, Gain = 0;
};
// 用途：产生事件局部稳定索引；流程：主题与奖励挑选；用法：传稳定标签；返回：哈希值，不访问游戏RNG。
uint32_t mfEventHash(const string& Text) {
    uint32_t Result = 2166136261u;
    for (unsigned char Byte : Text) Result = (Result ^ Byte) * 16777619u;
    return Result;
}
// 用途：定位当前奇遇；流程：报价/结算/读档；用法：传探索；返回：事件节点或空，不修改状态。
const DungeonNode* mfCurrentEvent(const DungeonRun& Run) {
    const auto Found = std::find_if(Run.nodes.begin(), Run.nodes.end(), [&](const auto& Node) {
        return Node.id == Run.current && Node.type == "event";
    });
    return Found == Run.nodes.end() ? nullptr : &*Found;
}
// 用途：从真实适境神通中选攻击或经营牌；流程：事件冻结；用法：传境界、稳定种子与类别；返回：目录指针或空。
const SpellDef* mfEventCard(int Stage, uint32_t Seed, bool Utility) {
    vector<const SpellDef*> Pool;
    for (const auto& Spell : allSpells()) {
        if (Spell.id == "strike" || Spell.id == "defend" || Spell.stage > Stage) continue;
        const auto Kind = content::parseSpellKind(Spell.kind);
        const bool IsUtility = Kind == content::SpellKind::Draw || Kind == content::SpellKind::Foresee ||
            Kind == content::SpellKind::Redraw || Kind == content::SpellKind::Mana;
        const bool IsAttack = Kind == content::SpellKind::Attack || Kind == content::SpellKind::Fire ||
            Kind == content::SpellKind::Sword || Kind == content::SpellKind::Drain ||
            Kind == content::SpellKind::Heavy || Kind == content::SpellKind::Poison ||
            Kind == content::SpellKind::Soul;
        if ((Utility && IsUtility) || (!Utility && IsAttack)) Pool.push_back(&Spell);
    }
    return Pool.empty() ? nullptr : Pool[Seed % Pool.size()];
}
// 用途：从未持有的真实遗物选一件；流程：血祭/游商事件；用法：传探索与稳定索引；返回：遗物指针或空。
const DungeonRelicDef* mfEventRelic(const DungeonRun& Run, uint32_t Seed) {
    vector<const DungeonRelicDef*> Pool;
    for (const auto& Relic : dungeonRelics())
        if (std::find(Run.relics.begin(), Run.relics.end(), Relic.id) == Run.relics.end()) Pool.push_back(&Relic);
    return Pool.empty() ? nullptr : Pool[Seed % Pool.size()];
}
// 用途：建立事件的冻结选项与可执行事实；流程：生成/预览/执行共用；用法：传只读探索节点；返回：带代价的权威选项。
vector<EventOffer> mfEventOffers(const Game& State, const DungeonRun& Run, const DungeonNode& Node) {
    vector<EventOffer> Offers;
    const int RewardStage = mfDungeonEntryRewardStage(State, Run);
    if (RewardStage < 0) return Offers;
    const auto Rules = mfDungeonRewardRulesForStage(RewardStage, Run.type);
    const auto Info = mfDescribeDungeonEvent(Run, Node);
    const uint32_t Seed = mfEventHash(Run.siteId + ":event:" + Node.id);
    const auto Add = [&](const string& Id, const string& Name, const string& Description, EventAction Action,
                         int Gain, int HealthCost = 0, int StoneCost = 0, const string& Target = "") {
        EventOffer Offer; Offer.Action = Action; Offer.Gain = Gain;
        Offer.HealthCost = HealthCost; Offer.StoneCost = StoneCost;
        Offer.Reward.id = "event:" + Node.id + ":" + Id;
        Offer.Reward.kind = "eventChoice"; Offer.Reward.name = Name; Offer.Reward.desc = Description;
        Offer.Reward.amount = StoneCost;
        if (Action == EventAction::Card || Action == EventAction::Remove) Offer.Reward.spell = Target;
        if (Action == EventAction::Relic) Offer.Reward.relic = Target;
        Offers.push_back(std::move(Offer));
    };
    const auto AddCard = [&](const string& Id, bool Utility, int Copies, int HealthCost, int StoneCost) {
        if (const auto* Spell = mfEventCard(RewardStage, Seed + (Utility ? 7u : 13u), Utility))
            Add(Id, "取法·" + Spell->name,
                "将" + std::to_string(Copies) + "张「" + Spell->name + "」加入本次临时牌组，离境消失。" +
                (HealthCost ? "付出健康度" + std::to_string(HealthCost) + "点，必须至少保留1点。" : "") +
                (StoneCost ? "消耗探索灵石" + std::to_string(StoneCost) + "。" : "") + "\n" + battleSpellDescription(*Spell),
                EventAction::Card, Copies, HealthCost, StoneCost, Spell->id);
    };
    const auto AddRelic = [&](const string& Id, int HealthCost, int StoneCost) {
        if (const auto* Relic = mfEventRelic(Run, Seed + 31u))
            Add(Id, "取宝·" + Relic->name,
                "获得本次探索遗物「" + Relic->name + "」，离境消失。" +
                (HealthCost ? "付出健康度" + std::to_string(HealthCost) + "点，必须至少保留1点。" : "") +
                (StoneCost ? "消耗探索灵石" + std::to_string(StoneCost) + "。" : "") + "\n" + Relic->desc,
                EventAction::Relic, 1, HealthCost, StoneCost, Relic->id);
    };
    const int Bounty = 45 * Rules.CurrencyScale;
    switch (Info.Kind) {
    case DungeonEventKind::Spring:
        Add("drink", "汲取清泉", "恢复健康度25点，最多恢复至100。", EventAction::Heal, 25);
        Add("drain", "抽取泉髓", "付出健康度10点，获得" + std::to_string(Bounty) + "探索灵石。", EventAction::Stones, Bounty, 10);
        break;
    case DungeonEventKind::Altar:
        AddRelic("blood-relic", 15, 0);
        Add("meditate", "稳固神魂", "恢复健康度10点，放弃血祭宝物。", EventAction::Heal, 10);
        break;
    case DungeonEventKind::Reflection: {
        AddCard("reflection", true, 1, 0, 0);
        std::set<string> Seen;
        for (const auto& Card : Run.deck) {
            if (Card.spell == "strike" || Card.spell == "defend" || !Seen.insert(Card.spell).second) continue;
            const auto* Spell = battleSpell(Card.spell);
            if (!Spell) continue;
            Add("forget-" + Card.uid, "忘却·" + Spell->name,
                "消耗" + std::to_string(10 * Rules.CurrencyScale) + "探索灵石，移除本次临时牌组中的这一张牌；不会影响永久神通或其他副本。",
                EventAction::Remove, 1, 0, 10 * Rules.CurrencyScale, Card.uid);
            if (Seen.size() >= 8) break;
        }
        break;
    }
    case DungeonEventKind::Tablet:
        AddCard("copy-attack", false, 1, 0, 0);
        AddCard("copy-utility", true, 1, 0, 12 * Rules.CurrencyScale);
        break;
    case DungeonEventKind::Remains:
        AddCard("inherit", false, 2, 8, 0);
        Add("collect", "收取散落灵石", "安全获得" + std::to_string(Bounty / 2) + "探索灵石，放弃残留战技。", EventAction::Stones, Bounty / 2);
        break;
    case DungeonEventKind::Caravan:
        AddRelic("barter", 0, 25 * Rules.CurrencyScale);
        Add("medicine", "换取疗伤灵露", "消耗探索灵石" + std::to_string(18 * Rules.CurrencyScale) + "，恢复健康度30点，最多恢复至100。", EventAction::Heal, 30, 0, 18 * Rules.CurrencyScale);
        break;
    }
    return Offers;
}
// 用途：验证冻结奖励未被修改；流程：报价/读档；用法：比较全字段；返回：是否完全一致。
bool mfSameEventReward(const DungeonReward& Left, const DungeonReward& Right) {
    return Left.id == Right.id && Left.kind == Right.kind && Left.name == Right.name && Left.desc == Right.desc &&
        Left.spell == Right.spell && Left.relic == Right.relic && Left.manual == Right.manual &&
        Left.amount == Right.amount && Left.grade == Right.grade;
}
// 用途：报价单个已验证事件事实；流程：预览和真实扣费；用法：传探索与选项；返回：资格、健康代价与探索灵石费用。
DungeonEventQuote mfQuoteEventOffer(const DungeonRun& Run, const EventOffer& Offer) {
    DungeonEventQuote Quote; Quote.HealthCost = Offer.HealthCost; Quote.StoneCost = Offer.StoneCost;
    if (Run.health <= Offer.HealthCost) Quote.Reason = "气血不足，选择后必须至少保留1点健康度。";
    else if (Run.stones < Offer.StoneCost) Quote.Reason = "本次探索获得的灵石不足。";
    else if (Offer.Action == EventAction::Heal && Run.health >= 100) Quote.Reason = "气血已满，可选择其他收益或跳过。";
    else if (Offer.Action == EventAction::Card && Run.deck.size() + Offer.Gain > 60) Quote.Reason = "临时牌组不足以容纳这些牌。";
    else if (Offer.Action == EventAction::Card && Run.seq > std::numeric_limits<int>::max() - Offer.Gain) Quote.Reason = "临时牌编号已达上限。";
    else if (Offer.Action == EventAction::Stones && Run.stones > 1000000 - Offer.Gain) Quote.Reason = "探索灵石已达上限。";
    if (Quote.Reason.empty() && Offer.Action == EventAction::Card) {
        for (int Offset = 1; Offset <= Offer.Gain; ++Offset) {
            const string Uid = "dungeon-card-" + std::to_string(Run.seq + Offset);
            if (std::any_of(Run.deck.begin(), Run.deck.end(), [&](const auto& Card) { return Card.uid == Uid; })) {
                Quote.Reason = "临时牌编号冲突，不能加入重复实例。"; break;
            }
        }
    }
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
}

DungeonEventInfo mfDescribeDungeonEvent(const DungeonRun& Run, const DungeonNode& Node) {
    static const std::array<DungeonEventInfo, 6> Catalog = {{
        {DungeonEventKind::Spring, "涸泽灵泉", "休整气血，或损血抽取泉髓换取探索灵石。"},
        {DungeonEventKind::Altar, "噬血祭坛", "以气血换取一件临时遗物，也可保守调息。"},
        {DungeonEventKind::Reflection, "问心幻镜", "获得经营手牌的神通，或付费精简临时牌组。"},
        {DungeonEventKind::Tablet, "残缺道碑", "攻击传承免费临摹；付探索灵石可选择经营神通。"},
        {DungeonEventKind::Remains, "古修遗蜕", "损血继承两张战技，或安全收取少量探索灵石。"},
        {DungeonEventKind::Caravan, "迷途货郎", "用探索灵石购买临时遗物或疗伤；不能花背包灵石。"},
    }};
    return Catalog[mfEventHash(Run.siteId + ":event:" + Node.id) % Catalog.size()];
}
vector<DungeonReward> mfDungeonEventRewards(const Game& State, const DungeonRun& Run, const DungeonNode& Node) {
    vector<DungeonReward> Rewards;
    for (const auto& Offer : mfEventOffers(State, Run, Node)) Rewards.push_back(Offer.Reward);
    return Rewards;
}
DungeonEventQuote mfQuoteDungeonEvent(const Game& State, const string& RewardId) {
    DungeonEventQuote Quote;
    if (!State.dungeons.run || State.dungeons.run->phase != "reward" || State.dungeons.run->cleared || State.player.arts.battle) {
        Quote.Reason = "当前没有可选择的秘境事件。"; return Quote;
    }
    const auto& Run = *State.dungeons.run;
    if (State.ended || State.pending || State.job || State.player.health <= 0 || State.player.location != Run.location) {
        Quote.Reason = "请先完成当前事件，并留在秘境入口所在地。"; return Quote;
    }
    if (State.eventSeq >= StateLimits::MaxPersistedInteger) {
        Quote.Reason = "纪事编号已达上限，不能结算新的秘境事件。"; return Quote;
    }
    const auto* Node = mfCurrentEvent(Run);
    if (!Node) { Quote.Reason = "当前节点不是奇遇。"; return Quote; }
    const auto Offers = mfEventOffers(State, Run, *Node);
    const auto Found = std::find_if(Offers.begin(), Offers.end(), [&](const auto& Offer) { return Offer.Reward.id == RewardId; });
    if (Found == Offers.end() || std::none_of(Run.rewards.begin(), Run.rewards.end(), [&](const auto& Reward) {
        return mfSameEventReward(Reward, Found->Reward);
    })) { Quote.Reason = "事件选项已变化，请重新查看。"; return Quote; }
    return mfQuoteEventOffer(Run, *Found);
}
string mfDungeonEventStateError(const Game& State) {
    if (!State.dungeons.run) return "";
    const auto& Run = *State.dungeons.run;
    const bool HasEventChoices = std::any_of(Run.rewards.begin(), Run.rewards.end(), [](const auto& Reward) {
        return Reward.kind == "eventChoice" || Reward.id.starts_with("event:");
    });
    const auto* Node = mfCurrentEvent(Run);
    const bool ExpectsEventChoices = Run.LayoutVersion >= 2 && Run.phase == "reward" && !Run.cleared && Node;
    if (!HasEventChoices && !ExpectsEventChoices) return "";
    if (Run.phase != "reward" || Run.cleared || !Node) return "秘境事件奖励与当前节点不一致。";
    const auto Expected = mfDungeonEventRewards(State, Run, *Node);
    if (Expected.size() != Run.rewards.size() || !std::equal(Expected.begin(), Expected.end(), Run.rewards.begin(), mfSameEventReward))
        return "秘境事件选项不能通过读档改写。";
    return "";
}
string mfClaimDungeonEvent(Game& State, const string& RewardId) {
    const auto Quote = mfQuoteDungeonEvent(State, RewardId);
    if (!Quote.Enabled) return Quote.Reason;
    if (const string Error = mfDungeonEventStateError(State); !Error.empty()) return Error;
    const auto& Current = *State.dungeons.run;
    const auto Offers = mfEventOffers(State, Current, *mfCurrentEvent(Current));
    const auto Found = std::find_if(Offers.begin(), Offers.end(), [&](const auto& Offer) { return Offer.Reward.id == RewardId; });
    if (Found == Offers.end()) return "事件选项已失效。";
    DungeonRun Candidate = Current;
    Candidate.health -= Found->HealthCost; Candidate.stones -= Found->StoneCost;
    switch (Found->Action) {
    case EventAction::Heal: Candidate.health = std::min(100, Candidate.health + Found->Gain); break;
    case EventAction::Stones: Candidate.stones += Found->Gain; break;
    case EventAction::Card:
        for (int Count = 0; Count < Found->Gain; ++Count)
            Candidate.deck.push_back({"dungeon-card-" + std::to_string(++Candidate.seq), Found->Reward.spell});
        break;
    case EventAction::Relic: Candidate.relics.push_back(Found->Reward.relic); break;
    case EventAction::Remove: {
        const auto Card = std::find_if(Candidate.deck.begin(), Candidate.deck.end(), [&](const auto& Entry) { return Entry.uid == Found->Reward.spell; });
        if (Card == Candidate.deck.end() || Card->spell == "strike" || Card->spell == "defend") return "待遗忘的临时神通已失效。";
        Candidate.deck.erase(Card); break;
    }
    }
    Candidate.rewards.clear(); Candidate.phase = "route";
    // 只复制日志写集，不复制全州NPC；日志分配成功后才提交资源与牌组。
    Game Journal;
    Journal.year = State.year; Journal.player.age = State.player.age; Journal.clock.days = State.clock.days;
    Journal.eventSeq = State.eventSeq; Journal.logs = State.logs;
    logEvent(Journal, "self", "秘境奇遇：「" + Found->Reward.name + "」。选择已兑现，临时神通与遗物仅在本次探索生效。", false);
    static_assert(std::is_nothrow_move_assignable_v<DungeonRun>);
    static_assert(noexcept(State.logs.swap(Journal.logs)));
    *State.dungeons.run = std::move(Candidate);
    State.player.health = State.dungeons.run->health;
    State.logs.swap(Journal.logs); State.eventSeq = Journal.eventSeq;
    return "";
}
}
