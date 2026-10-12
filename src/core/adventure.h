// 宁州一梦 · C++ 版 — 游历与后山（对应 Demo 06 的 adventure.mjs）
//
// 六类遇事：妖兽 / 劫修 / 行侠 / 后山遗藏 / 残碑功诀 / 行商。
// 游历按实际天数累计，满 20 天开始有遇事机会，连续 60 天必遇一段。
// 另有主动探访后山：每月一次、7 天、8 灵石，需击败守阵灵影才兑现奖励。
#pragma once

#include "core/model.h"

namespace nz {

// 六类遇事类型
// 用途：列出六种游历遇事类型。
// 参与流程：游历事件随机选择。
// 使用方式：无参数，列表顺序保持既有事件协议。
// 返回值：遇事类型常量列表引用。
const vector<string>& advKinds();

// 初始化游历状态
// 用途：保留奇遇状态初始化兼容入口。
// 参与流程：建号与读档初始化调用。
// 使用方式：默认构造/解码已用 -1 区分未探访；当前无需补值。
// 返回值：无；当前实现不修改游戏。
void ensureAdventure(Game& g);

// ---- 开局物品（五项天赋，对应 adventureBirth）----
// 只在创建新世时调用；读旧档不补发
// 用途：发放本世建号的奇遇类天赋资源。
// 参与流程：创建新角色的初始传承。
// 使用方式：仅新建时调用，读档不再次补发。
// 返回值：无；更新材料、悟道、抄本、丹炉与初始装备。
void adventureBirth(Game& g);

// 发放一件法器（对应 adventureGear）。返回是否成功（袋满则不发放）
// 用途：在行囊有空间时发放一件奇遇法器。
// 参与流程：天赋初始装备和奇遇战利品。
// 使用方式：传名称、槽位、品阶、收益、元素和来源。
// 返回值：成功为真并更新 gear、序号和日志；袋满为假。
bool adventureGear(Game& g, const string& name, const string& slot, int grade,
                   double bonus, const string& element,
                   const string& source);

// ---- 遇事 ----
// 触发一段遇事（写入 g.pending）
// 用途：生成冻结奖励上下文并提出游历抉择。
// 参与流程：游历触发及后山完成入口。
// 使用方式：传游戏与事件类型，按当前境界和已持抄本选奖励。
// 返回值：无；写 Pending、奇遇计数、抄本同步、RNG 和日志。
void adventureEvent(Game& g, const string& kind);

// 游历推进：按天数累计并可能触发遇事（对应 adventureExplore）
// 用途：累积真实游历天数并检查遇事。
// 参与流程：游历日历推进。
// 使用方式：传实际完成天数；未满二十天不触发，六十天必遇。
// 返回值：无；更新游历进度，可能生成抉择并推进 RNG。
void adventureExplore(Game& g, int days);

// 后山探访的校验；返回空字符串表示可以出发
// 用途：检查主动后山探访的地点、伤势与月额。
// 参与流程：探访动作前置。
// 使用方式：传游戏和 backhill ID，只做校验。
// 返回值：拒绝原因；可探访时为空。
string adventureActionError(Game& g, const string& id);

// 开始后山探访（建在途工作）
// 用途：收费并记录本月后山探访工作。
// 参与流程：已通过校验的主动探访。
// 使用方式：传游戏和探访 ID；调用前先校验，当前仅后山。
// 返回值：无；扣八灵石，记月额、建立七天 Job 并记日志。
void adventureBegin(Game& g, const string& id);
// 探访完成 -> 直接进入后山遗藏遇事
// 用途：在探访完成后生成后山遇事。
// 参与流程：后山 Job 到期回调。
// 使用方式：传游戏，调用 adventureEvent 的 backhill 分支。
// 返回值：无；更新奇遇抉择、计数、RNG 和日志。
void adventureComplete(Game& g);

// 玩家做出选择（对应 adventureChoose）。
// 返回 false 表示不是游历抉择，交给上层处理。
// 用途：消费当前游历抉择并执行选项。
// 参与流程：Pending 选择动作。
// 使用方式：传游戏与选项 ID，战斗奖励上下文先登记再开战。
// 返回值：属于游历事件时为真（含无效选择），否则假；有效选择改状态。
bool adventureChoose(Game& g, const string& optionId);

// 用途：把野外泛称灵药落实为可用药材；参与流程：药谷救援、采药奖励。
// 使用方式：传游戏与最多份数，受野外余量及背包容量约束；返回值：实际入袋份数，同步具体库存和总量。
// 用途：把泛称灵药奖励兑现为适境分类药材。
// 参与流程：药谷事件救援等实际奖励。
// 使用方式：传最大份数，受世界库存和背包上限限制，使用当前 RNG 值选药。
// 返回值：实际数量；更新分类药材、总量和野外余量，不推进 RNG。
int adventureHarvestHerbs(Game& game, int maximum);

// 游历斗法结束后的战利品结算（对应 adventureBattleEnd）
// 用途：按胜负兑现冻结的奇遇战利品。
// 参与流程：battleFinish 奇遇回调。
// 使用方式：传完成 Battle 快照，失败不发奖励，撤离另处理。
// 返回值：无；修改奇遇胜场、实际资源、抄本/法器及日志。
void adventureBattleEnd(Game& g, const Battle& b);

} // namespace nz
