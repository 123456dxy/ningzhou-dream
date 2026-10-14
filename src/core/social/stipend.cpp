#include "core/society.h"
#include "core/sim.h"
#include "core/foundation/state_limits.h"
#include <algorithm>

namespace nz {
// 用途：统一远程领取资格和有限府库实付；参与展示/执行；传只读状态，返回报价，不写工资、不推进时间或RNG。
StipendQuote mfQuoteStipend(const Game& State) {
    StipendQuote Quote;
    Quote.Owed = std::max(0, State.bonds.payroll.balance);
    if (State.ended || State.pending) Quote.Reason = "请先处理当前事件。";
    else if (State.player.arts.battle) Quote.Reason = "请先完成当前斗法。";
    else if (State.job) Quote.Reason = "请先完成或中止当前安排。";
    else if (State.dungeons.run) Quote.Reason = "请先结束当前秘境探索。";
    else if (State.player.sect.empty()) Quote.Reason = "散修没有俸禄。";
    else if (State.clock.days < 0) Quote.Reason = "俸禄日历无效。";
    else if (State.bonds.stipend == State.clock.days / 30) Quote.Reason = "本月已经领过俸禄。";
    else if (State.bonds.notoriety >= 40) Quote.Reason = "恶名过高，宗门暂停发放；旧欠保留。";
    else if (State.bonds.payroll.balance <= 0) Quote.Reason = "暂无可领俸禄。";
    if (!Quote.Reason.empty()) return Quote;

    const auto Treasury = State.world.sects.find(State.player.sect);
    if (!findSect(State.player.sect) || Treasury == State.world.sects.end() ||
        State.bonds.payroll.sect != State.player.sect) {
        Quote.Reason = "宗门或俸禄账簿无效。";
        return Quote;
    }
    if (Treasury->second.treasury < 0 || Treasury->second.treasury > StateLimits::MaxPersistedInteger ||
        State.bonds.payroll.balance > StateLimits::MaxPersistedInteger || State.player.stones < 0 ||
        State.player.stones > StateLimits::MaxPersistedInteger || State.eventSeq < 0 ||
        State.eventSeq >= StateLimits::MaxPersistedInteger) {
        Quote.Reason = "俸禄账目或纪事超出支持范围。";
        return Quote;
    }
    Quote.Paid = std::min(State.bonds.payroll.balance, Treasury->second.treasury);
    if (Quote.Paid == 0) Quote.Reason = "宗门府库暂空，欠额保留，本月领取机会不消耗。";
    else if (Quote.Paid > StateLimits::MaxPersistedInteger - State.player.stones) {
        Quote.Reason = "领取后灵石超出存档支持范围。";
        Quote.Paid = 0;
    }
    Quote.Enabled = Quote.Reason.empty();
    Quote.Owed = State.bonds.payroll.balance - Quote.Paid;
    return Quote;
}

// 用途：按同一报价预先构造纪事后一次提交；参与远程俸禄动作；返回结算结果，不拷贝全州NPC，预期拒绝无副作用。
StipendResult mfCollectStipend(Game& State) {
    const auto Quote = mfQuoteStipend(State);
    if (!Quote.Enabled) return {false, Quote.Reason, 0, Quote.Owed};
    StipendResult Result{true, "", Quote.Paid, Quote.Owed};
    Game Journal;
    Journal.year = State.year;
    Journal.clock.days = State.clock.days;
    Journal.player.age = State.player.age;
    Journal.eventSeq = State.eventSeq;
    Journal.logs = State.logs;
    const string Text = "领取俸禄 " + std::to_string(Quote.Paid) + " 灵石。" +
        (Quote.Owed > 0 ? "府库不足，尚欠 " + std::to_string(Quote.Owed) + " 灵石，欠额保留。" : "");
    logEvent(Journal, "self", Text, Quote.Owed > 0);
    State.world.sects.at(State.player.sect).treasury -= Quote.Paid;
    State.player.stones += Quote.Paid;
    State.bonds.payroll.balance = Quote.Owed;
    State.bonds.stipend = State.clock.days / 30;
    State.logs.swap(Journal.logs);
    State.eventSeq = Journal.eventSeq;
    return Result;
}
} // namespace nz
