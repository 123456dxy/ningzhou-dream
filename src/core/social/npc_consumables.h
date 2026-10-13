#pragma once

#include "core/model.h"

namespace nz {

// 纯视图：把服丹增量合入已生成的 NPC 构筑，不推进世界或消耗库存。
void applyNpcPillState(const Npc& npc, Arts& arts);

// 真实动作入口：库存、流派、收益、耐药、丹毒均通过后才扣一枚。
// 战斗丹只在 beforeBattle=true 的真实战斗开局使用。
bool npcUsePill(Npc& npc, const string& id, bool beforeBattle = false);

// 每个动作最多三枚，先救急与化毒，再选有实际收益的本流派或通用丹。
int npcConsumeUsefulPills(Npc& npc, bool beforeBattle = false);

} // namespace nz
