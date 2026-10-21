#include "core/progression/lifebound_rules.h"

#include "core/battle.h"
#include "core/sim.h"

#include <algorithm>

namespace nz {

LifeboundQuote mfQuoteLifeboundSpell(const Game& GameState, const string& SpellId) {
    const auto& PlayerState = GameState.player;
    const auto& ArtsState = PlayerState.arts;
    LifeboundQuote Quote;
    Quote.SpellId = SpellId;
    Quote.Kind = ArtsState.LifeboundKind;
    Quote.IsCurrent = !SpellId.empty() && SpellId == ArtsState.LifeboundSpell;
    const bool IsMandatory = SpellId == "strike" || SpellId == "defend";
    Quote.IsCarried = IsMandatory || std::find(ArtsState.spells.begin(), ArtsState.spells.end(), SpellId) != ArtsState.spells.end();
    const auto* Spell = battleSpell(SpellId);
    if (!Spell || SpellId.empty()) Quote.Reason = "请选择存在的普通神通。";
    else if (PlayerState.stage < 3 || (Quote.Kind != "innate" && Quote.Kind != "retain"))
        Quote.Reason = "完成筑基后解锁本命槽位，特性由筑基方式决定。";
    else if (Spell->stage > PlayerState.stage || (!IsMandatory &&
        std::find(ArtsState.knownSpells.begin(), ArtsState.knownSpells.end(), SpellId) == ArtsState.knownSpells.end()))
        Quote.Reason = "只能选择当前境界已领悟的神通。";
    else if (GameState.ended || PlayerState.health <= 0) Quote.Reason = "这一世已经结束。";
    else if (ArtsState.battleActive() || GameState.dungeons.run || GameState.job || GameState.pending ||
        (PlayerState.Zhuji && !PlayerState.Zhuji->Settled)) Quote.Reason = "请先结束当前行动，再更换本命神通。";
    Quote.Enabled = Quote.Reason.empty();
    return Quote;
}

string mfSetLifeboundSpell(Game& GameState, const string& SpellId) {
    const auto Quote = mfQuoteLifeboundSpell(GameState, SpellId);
    if (!Quote.Enabled) return Quote.Reason;
    if (Quote.IsCurrent) return {};
    GameState.player.arts.LifeboundSpell = SpellId;
    if (GameState.player.Zhuji) ++GameState.player.Zhuji->Revision;
    logEvent(GameState, "self", "本命神通设为「" + battleSpell(SpellId)->name + "」，" +
        (Quote.Kind == "innate" ? "首张副本固有。" : "首张副本保留。"), true);
    return {};
}

} // namespace nz
