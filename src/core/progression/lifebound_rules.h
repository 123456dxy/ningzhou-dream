#pragma once

#include "core/model.h"

namespace nz {

// 单槽本命的纯资格快照；不负责携带槽、不创建战斗牌实例。
struct LifeboundQuote {
    bool Enabled = false;
    bool IsCurrent = false;
    bool IsCarried = false;
    string Reason;
    string SpellId;
    string Kind;
};

[[nodiscard]] LifeboundQuote mfQuoteLifeboundSpell(const Game& GameState, const string& SpellId);
[[nodiscard]] string mfSetLifeboundSpell(Game& GameState, const string& SpellId);

} // namespace nz
