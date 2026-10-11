// 宁州一梦 · C++ 版 — 人物名录与建号数据
//
// 补三块玩家要的东西：
//   1. 好友通讯录：认识的人（不限当前地点），能筛选排序
//   2. 建号面板数据：全部天赋 + 五项可分配属性
//   3. 丹毒明细：来源、当前影响、化解进度
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace nz::play {

// ============================================================
// 好友通讯录
// ============================================================
struct RosterEntry {
    std::string id;
    std::string name;
    std::string gender;
    int age = 0;
    int stage = 0;
    std::string realmName;
    std::string sectName;
    std::string personality;
    std::string goal;
    std::string location;       // 当前所在地 id
    std::string locationName;
    int relation = 0;
    std::string relationLabel;
    bool here = false;          // 是否与玩家同地
    bool alive = true;
    int deathYear = 0;      // 逝者：离世年份
    bool isMaster = false;
    bool isPartner = false;
    bool isDisciple = false;
    int sword = 0;
    int insight = 0;
    int apt = 0;
    // 远程能做的事（同地才能做全部）
    bool canMessage = false;    // 传讯问候
    bool canInvite = false;     // 邀来同游
};

// 名录。includeStrangers=false 时只列出「认识的人」（好感≠0 或有名分）
std::vector<RosterEntry> roster(const Game& g, bool includeStrangers,
                                const std::string& sortBy);

// 远程传讯：加一点好感，不消耗天数
struct MessageResult {
    bool ok = false;
    std::string error;
    std::string text;
    int relationDelta = 0;
};
MessageResult sendMessage(Game& g, const std::string& npcId);

// 邀请对方来自己所在地（消耗天数与灵石）
MessageResult inviteToMe(Game& g, const std::string& npcId);

// ============================================================
// 建号面板
// ============================================================
struct TalentOption {
    std::string id;
    std::string name;
    std::string category;    // 剑道 / 丹道 / 气运 / 磨砺 ...
    int cost = 0;            // 负数代表磨砺，退还点数
    std::string effect;
};

// 全部天赋，按类别分组返回
std::vector<TalentOption> allTalents();

// 建号校验：算已花点数、剩余点数，以及是否合法
struct BuildCheck {
    bool ok = false;
    std::string error;
    int budget = 0;          // 总点数
    int spent = 0;           // 天赋消耗
    int allocSpent = 0;      // 属性消耗
    int remain = 0;          // 剩余
    int traitCount = 0;
};
BuildCheck checkBuild(const std::vector<std::string>& traits,
                      const std::map<std::string, int>& allocation,
                      int budget);

// 五项可分配属性（id/名称/说明/上限）
struct AllocOption {
    std::string id;
    std::string name;
    std::string desc;
    int max = 0;
    int value = 0;           // 某次分配方案里已投入的点数
};
std::vector<AllocOption> allocOptions(const std::map<std::string, int>& allocation);

// ============================================================
// 丹毒明细
// ============================================================
struct PoisonInfo {
    int current = 0;         // 当前丹毒
    int threshold = 20;      // 超过这个值开始影响修炼
    double cultPenalty = 1.0;  // 当前修炼效率折扣
    double detoxPerYear = 0;   // 每年自然化解
    int daysToClear = 0;       // 按当前速度清空需要多少天
    bool affecting = false;    // 是否已经在影响修炼
    std::string summary;
};
PoisonInfo poisonInfo(const Game& g);

} // namespace nz::play
