#pragma once

#include "core/model.h"

namespace nz {

enum class NpcLifePillUrgency { None, Regular, Priority, Urgent, Critical };
// 纯查询：按剩余寿元50/25/10/5年返回续命档位，不服丹或补库存。
[[nodiscard]] NpcLifePillUrgency mfNpcLifePillUrgency(const Npc& Person);

// 纯视图：把服丹增量合入已生成的 NPC 构筑，不推进世界或消耗库存。
void applyNpcPillState(const Npc& npc, Arts& arts);

// 真实动作入口：库存、流派、收益、耐药、丹毒均通过后才扣一枚。
// 战斗丹只在 beforeBattle=true 的真实战斗开局使用。
bool npcUsePill(Npc& npc, const string& id, bool beforeBattle = false);

// 日常先化毒一次，再在丹毒不超过40内持续服用；战前仅战斗丹、至多三枚。
int npcConsumeUsefulPills(Npc& npc, bool beforeBattle = false);

// 真实年度动作：需要时按正常成品报价采购一枚化毒/续命丹，资源不足不部分扣费。
// 不在查询、加载或构筑预览中调用；无新增随机抽取。
int mfAcquireNpcSurvivalPills(Game& State, Npc& Person);

} // namespace nz
