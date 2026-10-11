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
ConflictResult doConflict(Game& g, const std::string& npcId,
                          const std::string& intent);

// 校验（对应 socActionError 的 conflict 分支）
std::string conflictError(const Game& g, const std::string& npcId,
                          const std::string& intent);

// 掠夺（对应 socLoot）
void loot(Game& g, Npc& n, double ratio);
// 犯罪后果（对应 socCrime）：恶名、声望、交情、宗门惩罚、牵连友人
void crime(Game& g, Npc& n, const std::string& intent);
void battleEnd(Game& g, const Battle& b);

// 恶名带来的后果说明（界面上要看得见）
std::string notorietyNote(const Game& g);

// 关系清理：师尊/道侣/弟子离世后解除名分（对应 socRefresh）
void refresh(Game& g);

} // namespace nz::conflict
