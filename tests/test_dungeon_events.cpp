#include "harness.h"
#include "core/dungeon.h"
#include "core/battle.h"
#include "core/content/game_ids.h"
#include "core/exploration/dungeon_events.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <limits>
#include <set>

namespace nztest {
namespace {
using namespace nz;
// 用途：建立隔离事件夹具；流程：费用/分支定向检查；用法：传稳定标签；返回：不读写玩家文件的内存探索。
Game mfEventFixture(int Index) {
    CreateOptions Options; Options.seed = 27431;
    Game State = createGame(Options); State.pending.reset(); State.job.reset(); State.npcs.clear();
    DungeonRun Run; Run.siteId = "event-fixture-" + std::to_string(Index); Run.stage = 3; Run.type = "wild";
    Run.LayoutVersion = 2; Run.location = State.player.location;
    Run.phase = "reward"; Run.current = "event-node"; Run.health = 70; Run.stones = 100;
    Run.deck = {{"a", "strike"}, {"b", "gather"}, {"c", "refresh"}}; Run.seq = 20;
    DungeonSite Site; Site.id = Run.siteId; Site.stage = Run.stage; State.dungeons.sites.push_back(Site);
    DungeonNode Node; Node.id = Run.current; Node.type = "event"; Node.visited = true; Run.nodes.push_back(Node);
    Run.rewards = mfDungeonEventRewards(State, Run, Node); State.dungeons.run = std::move(Run);
    return State;
}
// 用途：定位指定主题的确定性事件；流程：边界夹具创建；用法：传目录枚举；返回：纯内存事件，不推进活动RNG。
Game mfEventFixtureForKind(DungeonEventKind Kind) {
    auto State = mfEventFixture(0);
    auto& Run = *State.dungeons.run;
    for (int Index = 0; Index < 512; ++Index) {
        Run.siteId = "event-fixture-" + std::to_string(Index);
        State.dungeons.sites.back().id = Run.siteId;
        if (mfDescribeDungeonEvent(Run, Run.nodes.front()).Kind != Kind) continue;
        Run.rewards = mfDungeonEventRewards(State, Run, Run.nodes.front());
        return State;
    }
    check(false, "指定事件主题必须能从固定标签中找到");
    return State;
}
// 用途：取得事件动作的稳定奖励ID；流程：定向边界检查；用法：传已冻结选项和动作后缀；返回：对应ID或空。
string mfEventRewardId(const DungeonRun& Run, const string& Suffix) {
    const auto Found = std::find_if(Run.rewards.begin(), Run.rewards.end(), [&](const auto& Reward) {
        return Reward.id.ends_with(":" + Suffix);
    });
    return Found == Run.rewards.end() ? "" : Found->id;
}
// 用途：构建指定张数的合法临时牌组；流程：容量与UID边界；用法：保持五攻五防且扩充真实低阶牌。
// 返回值：无，只改隔离夹具牌组并重新冻结依赖牌组的选项，不触及玩家永久装备。
void mfSetEventDeckCount(Game& State, int CardCount) {
    auto& Run = *State.dungeons.run;
    Run.deck.clear();
    for (int CopyIndex = 0; CopyIndex < 5; ++CopyIndex) {
        Run.deck.push_back({"boundary-strike-" + std::to_string(CopyIndex), "strike"});
        Run.deck.push_back({"boundary-defend-" + std::to_string(CopyIndex), "defend"});
    }
    while (Run.deck.size() < static_cast<std::size_t>(CardCount))
        Run.deck.push_back({"boundary-water-" + std::to_string(Run.deck.size()), "water"});
    Run.rewards = mfDungeonEventRewards(State, Run, Run.nodes.front());
}
// 用途：验证拒绝事件不产生任何部分结算；流程：篡改、容量和资源边界；用法：传隔离状态和原奖励ID。
// 返回值：无，分别核对报价/命令拒绝及全状态不变。
void mfCheckEventRejected(Game& State, const string& RewardId, const string& Label) {
    const string Before = saveToJson(State);
    check(!mfQuoteDungeonEvent(State, RewardId).Enabled, Label + "：报价禁用");
    check(!mfClaimDungeonEvent(State, RewardId).empty(), Label + "：实际命令拒绝");
    checkStr(saveToJson(State), Before, Label + "：不扣费、不扣血、不改牌和RNG");
}
}
// 用途：覆盖不同事件、选择兑现与拒绝原子性；流程：秘境改良回归；用法：统一测试调用；返回：断言记录。
void mfRunDungeonEventTests() {
    using namespace nz;
    std::set<DungeonEventKind> Kinds;
    bool Paid = false, Blood = false, Card = false, Relic = false, Removal = false;
    for (int Index = 0; Index < 80; ++Index) {
        Game State = mfEventFixture(Index);
        const auto Info = mfDescribeDungeonEvent(*State.dungeons.run, State.dungeons.run->nodes.front());
        Kinds.insert(Info.Kind);
        const string Before = saveToJson(State);
        check(mfDungeonEventStateError(State).empty(), "冻结事件选项由权威目录校验");
        for (const auto& Reward : State.dungeons.run->rewards) (void)mfQuoteDungeonEvent(State, Reward.id);
        checkStr(saveToJson(State), Before, "事件预告及反复报价不改变状态或RNG");
        check(!mfClaimDungeonEvent(State, "unknown-choice").empty(), "未知事件选择拒绝");
        checkStr(saveToJson(State), Before, "未知选择拒绝原子");
        const auto Offers = State.dungeons.run->rewards;
        for (const auto& Reward : Offers) {
            Game Trial = State;
            const auto Quote = mfQuoteDungeonEvent(Trial, Reward.id);
            if (!Quote.Enabled) continue;
            const int OldHealth = Trial.dungeons.run->health, OldStones = Trial.dungeons.run->stones;
            const auto OldDeck = Trial.dungeons.run->deck.size(), OldRelics = Trial.dungeons.run->relics.size();
            check(mfClaimDungeonEvent(Trial, Reward.id).empty(), "可用事件选项真实兑现");
            check(Trial.dungeons.run->phase == "route" && Trial.dungeons.run->rewards.empty(), "选择后返回路线且清空其他选项");
            check(Trial.dungeons.run->health >= 1 && Trial.dungeons.run->health <= 100, "事件气血边界合法");
            if (Quote.HealthCost) { Blood = true; checkEq(Trial.dungeons.run->health, OldHealth - Quote.HealthCost, "血量代价与报价一致"); }
            if (Quote.StoneCost) { Paid = true; checkEq(Trial.dungeons.run->stones, OldStones - Quote.StoneCost, "探索灵石代价与报价一致"); }
            check(Trial.player.arts.knownSpells == State.player.arts.knownSpells &&
                Trial.player.arts.spells == State.player.arts.spells && Trial.player.arts.manuals == State.player.arts.manuals &&
                Trial.player.arts.bookList == State.player.arts.bookList && Trial.economy.books == State.economy.books,
                "沿途事件只影响临时牌组或遗物，不增加永久神通与功法");
            checkEq(Trial.player.insight, State.player.insight, "事件不增加永久悟性属性");
            checkEq(Trial.dungeons.run->insight, State.dungeons.run->insight, "事件也不暗中增加离境悟道点收益");
            Card |= Trial.dungeons.run->deck.size() > OldDeck; Removal |= Trial.dungeons.run->deck.size() < OldDeck;
            Relic |= Trial.dungeons.run->relics.size() > OldRelics;
            const string After = saveToJson(Trial);
            check(!mfClaimDungeonEvent(Trial, Reward.id).empty(), "事件不能重复领取");
            checkStr(saveToJson(Trial), After, "重复领取不改变资源");
            if (Quote.StoneCost) {
                Game Poor = State; Poor.dungeons.run->stones = Quote.StoneCost - 1;
                const string PoorBefore = saveToJson(Poor);
                check(!mfClaimDungeonEvent(Poor, Reward.id).empty(), "探索灵石不足拒绝");
                checkStr(saveToJson(Poor), PoorBefore, "不足费用不部分扣血或给卡");
            }
            if (Quote.HealthCost) {
                Game Fragile = State; Fragile.dungeons.run->health = Quote.HealthCost;
                const string FragileBefore = saveToJson(Fragile);
                check(!mfClaimDungeonEvent(Fragile, Reward.id).empty(), "血量不足以保留一点时拒绝");
                checkStr(saveToJson(Fragile), FragileBefore, "危险选择拒绝不改变状态");
            }
        }
    }
    checkEq(static_cast<int>(Kinds.size()), 6, "六种事件主题均可出现");
    check(Paid && Blood && Card && Relic && Removal, "事件包含灵石/血量/临时卡/遗物/删牌实际取舍");
    {
        const auto Original = mfEventFixtureForKind(DungeonEventKind::Tablet);
        const auto RewardId = mfEventRewardId(*Original.dungeons.run, "copy-attack");
        check(!RewardId.empty(), "残缺道碑确实提供攻击选项");
        for (const string Field : {"id", "kind", "name", "desc", "spell", "relic", "manual", "amount", "grade"}) {
            auto Invalid = Original;
            auto& Reward = Invalid.dungeons.run->rewards.front();
            if (Field == "id") Reward.id = "choice-999";
            else if (Field == "kind") Reward.kind = "cache";
            else if (Field == "name") Reward.name += "伪造";
            else if (Field == "desc") Reward.desc += "伪造";
            else if (Field == "spell") Reward.spell = "wood";
            else if (Field == "relic") Reward.relic = "spirit-bead";
            else if (Field == "manual") Reward.manual = "gold";
            else if (Field == "amount") Reward.amount = 100000;
            else Reward.grade = 2;
            check(!mfDungeonEventStateError(Invalid).empty(), "持久事件字段" + Field + "篡改被明确识别");
            mfCheckEventRejected(Invalid, RewardId, "篡改" + Field);
        }
        for (const string LegacyKind : {"cache", "rest", "risk"}) {
            auto Invalid = Original;
            for (std::size_t RewardIndex = 0; RewardIndex < Invalid.dungeons.run->rewards.size(); ++RewardIndex) {
                auto& Reward = Invalid.dungeons.run->rewards[RewardIndex];
                Reward.id = "choice-" + std::to_string(900 + RewardIndex);
                Reward.kind = LegacyKind; Reward.amount = 100000;
            }
            check(!mfDungeonEventStateError(Invalid).empty(), "V2事件全部降级成旧奖励也不能绕过冻结校验");
            mfCheckEventRejected(Invalid, RewardId, "全部伪装为旧" + LegacyKind);
        }
        auto PrefixGuard = Original;
        PrefixGuard.dungeons.run->LayoutVersion = 1;
        for (auto& Reward : PrefixGuard.dungeons.run->rewards) Reward.kind = "cache";
        check(!mfDungeonEventStateError(PrefixGuard).empty(), "event前缀在旧版本标签下仍触发全字段校验");
        auto Missing = Original; Missing.dungeons.run->rewards.clear();
        check(!mfDungeonEventStateError(Missing).empty(), "V2事件奖励被全部删除也明确拒绝");
        auto Duplicate = Original; Duplicate.dungeons.run->rewards.push_back(Duplicate.dungeons.run->rewards.front());
        check(!mfDungeonEventStateError(Duplicate).empty(), "重复事件选项不能通过冻结集合校验");
    }
    {
        auto State = mfEventFixtureForKind(DungeonEventKind::Remains);
        mfSetEventDeckCount(State, 59);
        const auto RewardId = mfEventRewardId(*State.dungeons.run, "inherit");
        check(!RewardId.empty(), "古修遗蜕提供一次两张的继承选项");
        mfCheckEventRejected(State, RewardId, "59张牌不能加入两张越过60上限");
        mfSetEventDeckCount(State, 58);
        const int BeforeHealth = State.dungeons.run->health;
        check(mfQuoteDungeonEvent(State, RewardId).Enabled, "58张牌可以准确加入两张");
        check(mfClaimDungeonEvent(State, RewardId).empty(), "58加2真实提交成功");
        checkEq(State.dungeons.run->deck.size(), 60, "继承两张后牌组恰为60");
        checkEq(State.dungeons.run->health, BeforeHealth - 8, "恰好满牌只扣一次明确血量代价");
        std::set<string> Uids;
        for (const auto& Card : State.dungeons.run->deck) Uids.insert(Card.uid);
        checkEq(Uids.size(), State.dungeons.run->deck.size(), "批量加入的每张牌UID唯一");
        auto Full = mfEventFixtureForKind(DungeonEventKind::Reflection);
        mfSetEventDeckCount(Full, 60);
        mfCheckEventRejected(Full, mfEventRewardId(*Full.dungeons.run, "reflection"), "60张牌不能再加入单张");
    }
    {
        auto State = mfEventFixtureForKind(DungeonEventKind::Remains);
        mfSetEventDeckCount(State, 10);
        const auto RewardId = mfEventRewardId(*State.dungeons.run, "inherit");
        for (const int Offset : {1, 2}) {
            auto Collision = State;
            Collision.dungeons.run->deck.push_back({"dungeon-card-" + std::to_string(Collision.dungeons.run->seq + Offset), "water"});
            Collision.dungeons.run->rewards = mfDungeonEventRewards(Collision, *Collision.dungeons.run, Collision.dungeons.run->nodes.front());
            mfCheckEventRejected(Collision, RewardId, "第一或第二个待分配UID已占用");
        }
        for (const int Seq : {std::numeric_limits<int>::max(), std::numeric_limits<int>::max() - 1}) {
            auto Overflow = State; Overflow.dungeons.run->seq = Seq;
            mfCheckEventRejected(Overflow, RewardId, "批量生成UID不能溢出int序号");
        }
        auto Last = State; Last.dungeons.run->seq = std::numeric_limits<int>::max() - 2;
        check(mfQuoteDungeonEvent(Last, RewardId).Enabled, "剩余两个合法序号仍可报价");
        check(mfClaimDungeonEvent(Last, RewardId).empty(), "剩余两个合法序号可完整提交");
        checkEq(Last.dungeons.run->seq, std::numeric_limits<int>::max(), "最后一个序号准确停在INT_MAX");
    }
    {
        for (const auto Kind : {DungeonEventKind::Altar, DungeonEventKind::Caravan}) {
            auto State = mfEventFixtureForKind(Kind);
            const auto OriginalRelic = std::find_if(State.dungeons.run->rewards.begin(), State.dungeons.run->rewards.end(),
                [](const auto& Reward) { return !Reward.relic.empty(); });
            check(OriginalRelic != State.dungeons.run->rewards.end(), "未持有遗物时可以选择换宝");
            const string OldRewardId = OriginalRelic == State.dungeons.run->rewards.end() ? "" : OriginalRelic->id;
            for (const auto& RelicDefinition : dungeonRelics()) State.dungeons.run->relics.push_back(RelicDefinition.id);
            State.dungeons.run->rewards = mfDungeonEventRewards(State, *State.dungeons.run, State.dungeons.run->nodes.front());
            check(std::none_of(State.dungeons.run->rewards.begin(), State.dungeons.run->rewards.end(),
                [](const auto& Reward) { return !Reward.relic.empty(); }), "遗物目录耗尽后不再给重复遗物选项");
            check(mfDungeonEventStateError(State).empty(), "遗物耗尽后的保守选项仍为合法冻结事件");
            mfCheckEventRejected(State, OldRewardId, "过期换宝按钮不能在目录耗尽后发放遗物");
            check(!State.dungeons.run->rewards.empty(), "遗物耗尽仍保留疗伤或调息选项");
            const string FallbackId = State.dungeons.run->rewards.front().id;
            check(mfClaimDungeonEvent(State, FallbackId).empty(), "目录耗尽后仍能实际选择保守分支离开事件");
            checkEq(State.dungeons.run->relics.size(), dungeonRelics().size(), "保守分支不复制已持有遗物");
        }
    }
    {
        auto Base = mfEventFixtureForKind(DungeonEventKind::Tablet);
        bool SawTablet = false;
        for (int Index = 0; Index < 512; ++Index) {
            auto& Run = *Base.dungeons.run;
            Run.siteId = "attack-pool-" + std::to_string(Index);
            Base.dungeons.sites.back().id = Run.siteId;
            if (mfDescribeDungeonEvent(Run, Run.nodes.front()).Kind != DungeonEventKind::Tablet) continue;
            SawTablet = true;
            Run.rewards = mfDungeonEventRewards(Base, Run, Run.nodes.front());
            for (const auto& Reward : Run.rewards) {
                const auto* Spell = battleSpell(Reward.spell);
                check(Spell != nullptr && Spell->stage <= Run.stage, "道碑传承始终来自真实适境神通");
                if (!Spell) continue;
                const auto Kind = content::parseSpellKind(Spell->kind);
                const bool IsUtility = Kind == content::SpellKind::Draw || Kind == content::SpellKind::Foresee ||
                    Kind == content::SpellKind::Redraw || Kind == content::SpellKind::Mana;
                const bool IsAttack = Kind == content::SpellKind::Attack || Kind == content::SpellKind::Fire ||
                    Kind == content::SpellKind::Sword || Kind == content::SpellKind::Drain ||
                    Kind == content::SpellKind::Heavy || Kind == content::SpellKind::Poison || Kind == content::SpellKind::Soul;
                if (Reward.id.ends_with(":copy-attack")) check(IsAttack && !IsUtility, "免费攻击传承不能误给治疗、护盾或经营牌");
                if (Reward.id.ends_with(":copy-utility")) check(IsUtility, "付费经营传承确实用于补牌、换手或回灵");
            }
        }
        check(SawTablet, "多标签检查实际覆盖道碑事件");
    }
}
}
