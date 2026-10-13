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
// 用途：写入带类别时间与实际变化的岁月纪事。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const string& category, const string& text, bool important = false, const string& npcId = "", const string& delta = ""，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void logEvent(Game& g, const string& category, const string& text,
              bool important = false, const string& npcId = "",
              const string& delta = "");

// NPC 个人履历（JS 的 npcHistory）
// 用途：记录NPC自身生平事件。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, Npc& n, const string& text，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void npcHistory(Game& g, Npc& n, const string& text);

// NPC 传闻；只记录玩家知道或高位修士的消息（JS 的 npcNews）
// 用途：把NPC事件同步为玩家可见纪事。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, Npc& n, const string& text, bool important = false，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void npcNews(Game& g, Npc& n, const string& text, bool important = false);

// NPC 陨落（兽潮、仇杀等都用这个，保证履历/传闻/好友反应一致）
// 用途：公开NPC死亡结算入口。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, Npc& n, const string& reason，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void npcDies(Game& g, Npc& n, const string& reason);

// 玩家受伤（体魄减伤）与剑意增长
// 用途：扣除玩家气血并处理致命结局。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, int amount，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void applyDamage(Game& g, int amount);
// 用途：保留旧剑意成长兼容接口，实际斗法不再按剑意境界增减伤。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, int amount，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void swordGain(Game& g, int amount);

// ============================================================
// 世界推进（calendar 每天刷新与每年结算时调用）
// ============================================================
// 每 360 天：资源再生、坊市行情、灵脉归属、新修士出生
// 用途：推进天下年度经济宗门人口与秘境变化。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void worldYear(Game& g);

// 每天：刷新坊市任务与俸禄账目（对应 ctx.refresh）
// 用途：刷新玩家年度日常额度。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void refreshDaily(Game& g);

// ============================================================
// 在途工作完成后的结算（对应 calendar 的 ctx.complete）
// ============================================================
// 用途：按工作种类完成月度行动并发放实际收益。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const Job& job，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void monthComplete(Game& g, const Job& job);

// 是否产生待处理抉择（对应 calendar 的 ctx.choice）
// 用途：依据计划地点风险生成附近事件抉择。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void maybeChoice(Game& g);

// 是否服过解毒丹（丹道二层）
// 用途：检查玩家是否准备突破丹药。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& p，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool hasDan(const Player& p);

// ============================================================
// 结算与结束
// ============================================================
// 用途：结束当前人生并生成结局统计。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const string& cause = "暂别仙途"，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void finish(Game& g, const string& cause = "暂别仙途");

// ============================================================
// 数值公式（与 JS 版 sim.mjs 一致）
// ============================================================
// 用途：公开当前玩家修炼速度。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double cultivationRate(const Player& p);
// 用途：计算基础炼丹成功率。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double alchemyChance(const Player& p);
// 用途：读取角色当前年度修炼投入。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int cultivationCost(const Player& p);
// 用途：根据世界行情计算通用资源价格。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const string& item，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int marketPrice(const Game& g, const string& item);
// 用途：计算人物基础战力。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Person& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int power(const Person& p);
// 用途：计算与人物往来的基础赠礼成本。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Person& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int socialGiftCost(const Person& p);
// 用途：汇总天赋伤势资源影响的突破成功率。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double breakthroughChance(const Game& g);
// 用途：汇总玩家当前突破的灵石需求。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
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
// 用途：汇总突破需求成功率与可用状态。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：BreakthroughInfo结果，包含说明所述状态或计算数据。
BreakthroughInfo breakthroughInfo(const Game& g);

// ============================================================
// 新游戏
// ============================================================
struct CreateOptions {
    string name = "沈知微";
    string gender = "女";
    string portraitId;
    string talent = "sword";
    uint32_t seed = 20261004;
    vector<string> traits;              // 空则用 talent 预设
    // 调用方明确指定了天赋列表（哪怕是空的）。
    // 建号面板允许「一个天赋都不选、点数全给属性」，
    // 这时不能再偷偷塞一个默认天赋，否则预算会超、建号静默退回默认值。
    bool traitsExplicit = false;
    map<string, int> allocation;        // 未显式给出时按预算自动分配
    bool allocationExplicit = false;   // 明确给出空对象时保留零分配
    int budget = BASE_POINTS;
    string lifeId;
};
Game createGame(const CreateOptions& opts = {});

// 世界人口统计（各境界组人数）
// 用途：按境界统计世界在世修士数量。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
vector<int> populationCounts(const Game& g);

// 刷新宗门势力（人数与影响力）
// 用途：刷新宗门可见状态与培养资源。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void refreshSects(Game& g);
// 按地图种子生成世界（行情、灵脉归属、宗门格局、资源基线）
// 用途：按初始种子建立行情气候与宗门状态。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void worldFromSeed(Game& g);

// 天赋买点效率
// 用途：查询玩家旧传承速度加成。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double legacyEfficiencyOf(const Player& p);

// 战力（JS 的 power）
// 用途：计算人物基础战力。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Person& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int power(const Person& p);

// 与某个 NPC 的交情
// 用途：读取玩家与指定NPC的关系。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const string& npcId，按本模块入口调用。
// 返回值：当前玩家与指定NPC的关系数值，缺失为零。
int relationOf(const Game& g, const string& npcId);
// 用途：调整关系并同步友情成长记录。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const string& npcId, int delta，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void addRelation(Game& g, const string& npcId, int delta);

} // namespace nz
