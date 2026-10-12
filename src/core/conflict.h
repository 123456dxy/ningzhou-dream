// 宁州一梦 · C++ 版 — 野外冲突（拦路索财 / 野外截杀）
//
// 对应 JS society.mjs 的 conflict 分支 + socResponse / socLoot / socCrime。
//
// 之前整条线都不存在。后果是 bonds.notoriety（恶名）字段、
// 「恶名 ≥40 暂停俸禄」的逻辑、以及恶名在界面上的显示全都写好了，
// 却没有任何一处让恶名增加 —— 整套系统实际不可触发。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace nz::conflict {

// 对方的反应（对应 socResponse）
//   性格 × 境界差 → 交出财物 / 反击 / 试图逃离 / 拼命逃离 / 决死反击
// 用途：按境界差和性格确定 NPC 面对抢劫或截杀的反应。
// 参与流程：冲突分支选择。
// 使用方式：传双方状态和 rob/kill 意图。
// 返回值：交财、逃离或反击的反应文案。
std::string response(const Game& g, const Npc& n, const std::string& intent);

struct ConflictResult {
    bool ok = false;
    std::string error;
    std::string text;
    std::string responseText;   // 对方的反应
    int lootStones = 0;
    int lootHerbs = 0;
    int notoriety = 0;
    bool fought = false;        // 是否进入斗法
    bool fled = false;          // 对方逃掉了
};
// 用途：执行夺财、逃离或开始冲突斗法。
// 参与流程：野外抢劫与截杀动作入口。
// 使用方式：传 NPC ID 与 rob/kill，内部先校验再执行。
// 返回值：是否成功、逃离、开战及错误说明的结果。
ConflictResult doConflict(Game& g, const std::string& npcId,
                          const std::string& intent);

// 校验（对应 socActionError 的 conflict 分支）
// 用途：验证冲突意图、地点、角色状态与名分条件。
// 参与流程：doConflict 执行前置。
// 使用方式：只读查询游戏和目标 ID，不扣资源。
// 返回值：拒绝原因；空字符串表示允许。
std::string conflictError(const Game& g, const std::string& npcId,
                          const std::string& intent);

// 掠夺（对应 socLoot）
// 用途：按比例从目标真实行囊转移财物。
// 参与流程：直接夺财或冲突战胜后的奖励。
// 使用方式：传游戏、目标与掠夺比例，遵守原有灵石上限。
// 返回值：无；减少 NPC 财物，增加玩家财物并记日志。
void loot(Game& g, Npc& n, double ratio);
// 犯罪后果（对应 socCrime）：恶名、声望、交情、宗门惩罚、牵连友人
// 用途：登记恶名、声望、宗门罚款与关系后果。
// 参与流程：成功夺财或冲突开战时记账。
// 使用方式：传游戏、目标及 rob/kill 意图。
// 返回值：无；修改名分、玩家资源、宗门库藏与关系。
void crime(Game& g, Npc& n, const std::string& intent);
// 用途：处理抢劫或截杀斗法获胜的战后结果。
// 参与流程：battleFinish 的冲突奖励阶段。
// 使用方式：传已完成战斗；仅存活目标和胜局生效。
// 返回值：无；转移财物，截杀时登记 NPC 死亡并清理名分。
void battleEnd(Game& g, const Battle& b);

// 恶名带来的后果说明（界面上要看得见）
// 用途：将当前恶名与停发俸禄门槛解释为文案。
// 参与流程：社会状态展示。
// 使用方式：传游戏只读状态。
// 返回值：恶名说明；没有恶名时为空。
std::string notorietyNote(const Game& g);

// 关系清理：师尊/道侣/弟子离世后解除名分（对应 socRefresh）
// 用途：移除已死亡或缺失 NPC 的现有名分。
// 参与流程：NPC 死亡后的关系维护。
// 使用方式：传游戏，逐项检查师尊、道侣和弟子。
// 返回值：无；修改 bonds 名分、授业次数并记离世日志。
void refresh(Game& g);

} // namespace nz::conflict
