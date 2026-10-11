// 宁州一梦 · C++ 版 — 人生与天下（对应 JS 版 sim.mjs）
//
// sim 是整个游戏的中枢：它持有 Game 状态，并向其他模块暴露
// 年度结算、行动执行、纪事记录等入口。
// 其他模块（arts/economy/society/battle）通过本头文件互相调用。
#pragma once

#include "core/model.h"

namespace nz {

// ============================================================
// 纪事与日志
// ============================================================
// 对应 JS 的 log(s, category, text, {important, npcId, delta})
void logEvent(Game& g, const string& category, const string& text,
              bool important = false, const string& npcId = "",
              const string& delta = "");

// NPC 个人履历（JS 的 npcHistory）
void npcHistory(Game& g, Npc& n, const string& text);

// NPC 传闻；只记录玩家知道或高位修士的消息（JS 的 npcNews）
void npcNews(Game& g, Npc& n, const string& text, bool important = false);

// NPC 陨落（兽潮、仇杀等都用这个，保证履历/传闻/好友反应一致）
void npcDies(Game& g, Npc& n, const string& reason);

// 玩家受伤（体魄减伤）与剑意增长
void applyDamage(Game& g, int amount);
void swordGain(Game& g, int amount);

// ============================================================
// 世界推进（calendar 每天刷新与每年结算时调用）
// ============================================================
// 每 360 天：资源再生、坊市行情、灵脉归属、新修士出生
void worldYear(Game& g);

// 每天：刷新坊市任务与俸禄账目（对应 ctx.refresh）
void refreshDaily(Game& g);

// ============================================================
// 在途工作完成后的结算（对应 calendar 的 ctx.complete）
// ============================================================
void monthComplete(Game& g, const Job& job);

// 是否产生待处理抉择（对应 calendar 的 ctx.choice）
void maybeChoice(Game& g);

// 是否服过解毒丹（丹道二层）
bool hasDan(const Player& p);

// ============================================================
// 结算与结束
// ============================================================
void finish(Game& g, const string& cause = "暂别仙途");

// ============================================================
// 数值公式（与 JS 版 sim.mjs 一致）
// ============================================================
double cultivationRate(const Player& p);
double alchemyChance(const Player& p);
int cultivationCost(const Player& p);
int marketPrice(const Game& g, const string& item);
int power(const Person& p);
int socialGiftCost(const Person& p);
double breakthroughChance(const Game& g);
int breakthroughCost(const Game& g);

// 突破详情（对应 JS 的 breakthroughInfo）
struct BreakthroughInfo {
    bool major = false;       // 是否大境界突破
    int cost = 0;             // 所需灵石
    double failureLoss = 0.2; // 失败损失的修为比例
    bool ready = false;       // 修为是否圆满
    double chance = 0.5;      // 成功率
    string next;              // 下一境界名
};
BreakthroughInfo breakthroughInfo(const Game& g);

// ============================================================
// 新游戏
// ============================================================
struct CreateOptions {
    string name = "沈知微";
    string gender = "女";
    string talent = "sword";
    uint32_t seed = 20261004;
    vector<string> traits;              // 空则用 talent 预设
    // 调用方明确指定了天赋列表（哪怕是空的）。
    // 建号面板允许「一个天赋都不选、点数全给属性」，
    // 这时不能再偷偷塞一个默认天赋，否则预算会超、建号静默退回默认值。
    bool traitsExplicit = false;
    map<string, int> allocation;        // 空则按预算自动分配
    int budget = BASE_POINTS;
    string lifeId;
};
Game createGame(const CreateOptions& opts = {});

// 世界人口统计（各境界组人数）
vector<int> populationCounts(const Game& g);

// 刷新宗门势力（人数与影响力）
void refreshSects(Game& g);
// 按地图种子生成世界（行情、灵脉归属、宗门格局、资源基线）
void worldFromSeed(Game& g);

// 天赋买点效率
double legacyEfficiencyOf(const Player& p);

// 战力（JS 的 power）
int power(const Person& p);

// 与某个 NPC 的交情
int relationOf(const Game& g, const string& npcId);
void addRelation(Game& g, const string& npcId, int delta);

} // namespace nz
