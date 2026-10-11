// 宁州一梦 · C++ 版 — 游历与后山（对应 Demo 06 的 adventure.mjs）
//
// 六类遇事：妖兽 / 劫修 / 行侠 / 后山遗藏 / 残碑功诀 / 行商。
// 游历按实际天数累计，满 20 天开始有遇事机会，连续 60 天必遇一段。
// 另有主动探访后山：每月一次、7 天、8 灵石，需击败守阵灵影才兑现奖励。
#pragma once

#include "core/model.h"

namespace nz {

// 六类遇事类型
const vector<string>& advKinds();

// 初始化游历状态
void ensureAdventure(Game& g);

// ---- 开局物品（五项天赋，对应 adventureBirth）----
// 只在创建新世时调用；读旧档不补发
void adventureBirth(Game& g);

// 发放一件法器（对应 adventureGear）。返回是否成功（袋满则不发放）
bool adventureGear(Game& g, const string& name, const string& slot, int grade,
                   double bonus, const string& element,
                   const string& source);

// ---- 遇事 ----
// 触发一段遇事（写入 g.pending）
void adventureEvent(Game& g, const string& kind);

// 游历推进：按天数累计并可能触发遇事（对应 adventureExplore）
void adventureExplore(Game& g, int days);

// 后山探访的校验；返回空字符串表示可以出发
string adventureActionError(Game& g, const string& id);

// 开始后山探访（建在途工作）
void adventureBegin(Game& g, const string& id);
// 探访完成 -> 直接进入后山遗藏遇事
void adventureComplete(Game& g);

// 玩家做出选择（对应 adventureChoose）。
// 返回 false 表示不是游历抉择，交给上层处理。
bool adventureChoose(Game& g, const string& optionId);

// 游历斗法结束后的战利品结算（对应 adventureBattleEnd）
void adventureBattleEnd(Game& g, const Battle& b);

} // namespace nz
