// 宁州一梦 · C++ 版 — 兽潮
//
// 为什么要这个东西：中后期玩家会陷入「过月—闭关—过月」的空转，
// 没有外部压力，也没有非做不可的事。兽潮给一个全图事件：
//
//   妖潮自险地涌出，波及**所有**地点（不只是你在的那一处）。
//   低境界修士首当其冲 —— NPC 会真的死，你认识的人也会。
//   击杀获得积分和声望，潮退按金榜发灵石，并开放有限高阶材料坊市。
//
// 三条设计约束：
//   1. 全图起效 —— 不是「你所在的地点刷怪」，而是整个世界在死人
//   2. 可致 NPC 死亡 —— 且优先死低境界的，境界高的能扛
//   3. 给成长动力 —— 迎战的收益要明显高于平时的野外悬赏
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace nz::tide {

// ------------------------------------------------------------
// 状态查询
// ------------------------------------------------------------
// 当前兽潮的危险加成（0 表示没有兽潮）
// 用途：读取当前妖潮的野外风险加成。
// 参与流程：妖潮开启、讨伐与避潮。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int dangerBonus(const Game& g);
// 一句话描述当前形势
// 用途：生成当前妖潮状态中文说明。
// 参与流程：妖潮开启、讨伐与避潮。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string statusText(const Game& g);

// ------------------------------------------------------------
// 推进
// ------------------------------------------------------------
// 年度钩子：决定要不要起兽潮；战期截止由真实日历管理。
// 用途：按年度概率开启新的固定战期，保留尚在开放的上期市场。
// 参与流程：妖潮开启、讨伐与避潮。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void yearTick(Game& g);
// 月度钩子：保留兽潮期间NPC伤亡；日倒计时由dayTick管理。
// 用途：结算妖潮月度影响与损失。
// 参与流程：妖潮开启、讨伐与避潮。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void monthTick(Game& g);

// ------------------------------------------------------------
// 玩家迎战
// ------------------------------------------------------------
struct FightResult {
    bool ok = false;
    std::string text;
    std::string error;
    int rewardStones = 0;
    int rewardRenown = 0;
    std::string rewardItem;      // 掉落的材料名
    int killed = 0;              // 这一波杀了多少妖兽
    bool lethal = false;         // 是否已开打（转战斗）
};
// 迎战所选大境界的妖兽；胜利积分由稳定战场编号决定。
// 用途：构造当前妖潮讨伐遭遇与门槛。
// 参与流程：妖潮开启、讨伐与避潮。
// 使用方式：传游戏和战场0..3；省略战场或传-1时选择本人最高合法战场。
// 返回值：FightResult结果，包含说明所述状态或计算数据。
FightResult fight(Game& g, int battlefield = -1);
// 战果登记（由battleFinish的兽潮分支调用）；排名灵石在期末结算。
// 用途：按本期来源与战场校验击杀，登记积分和声望，不推进日历。
// 参与流程：妖潮开启、讨伐与避潮。
// 使用方式：传游戏、胜负和结束前的Battle快照；省略快照时读取当前对局。
// 返回值：无；完成说明所述写入或展示操作。
void tideReward(Game& g, bool won, const Battle* context = nullptr);
// 固守（安全，但没有收益）
// 用途：记录固守选择；不扣钱、不加分，玩家之后照常安排时间。
// 参与流程：妖潮开启、讨伐与避潮。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string hunkerDown(Game& g);

// ------------------------------------------------------------
// 初始化
// ------------------------------------------------------------
// 用途：补齐旧存档的妖潮状态。
// 参与流程：妖潮开启、讨伐与避潮。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ensureTide(Game& g);

// 用途：按真实日历推进积分模拟和战期；流程：普通安排和兽潮斗法；用法：日历更新后传游戏；返回：无。
void dayTick(Game& g);
// 用途：到期冻结金榜、发奖和生成市场；流程：日历及战斗收尾；用法：可重复传游戏；返回：无，不重复付款。
void settleIfDue(Game& g);

struct BattlefieldInfo {
    int id = 0, minStage = 0, pointsPerKill = 1;
    string name, reason;
    bool enabled = false;
};
// 用途：查询四战场入场条件；流程：兽潮状态和迎战；用法：传只读游戏；返回：境界、积分、可用性及原因列表。
vector<BattlefieldInfo> battlefields(const Game& g);
// 用途：生成含玩家的并列金榜；流程：状态和结榜；用法：传只读游戏；返回：按分数排序的副本，不改原名册。
vector<TideParticipant> leaderboard(const Game& g);
// 用途：查询战期剩余天数；流程：状态、入场和日历；用法：传只读游戏；返回：非负余天，无活动为0。
int daysLeft(const Game& g);
// 用途：查询排名实付比例；流程：状态和材料报价；用法：传只读游戏；返回：正常购价的50..100%，不是减免量。
int discountPercent(const Game& g);
// 用途：判断战后市场开放期；流程：状态和成交；用法：传只读游戏；返回：未结束人生且仍在期限内时为真。
bool marketOpen(const Game& g);

struct MarketItem {
    string kind, id, name, reason;
    int grade = 0, basePrice = 0, unitPrice = 0, stock = 0, owned = 0;
    bool enabled = false;
};
struct MarketResult {
    bool ok = false;
    string error, text;
    int cost = 0;
};
// 用途：只读生成战后市场目录；流程：坊市展示和报价；用法：传只读游戏；返回：有限库存、正常价、排名价和购买门槛。
vector<MarketItem> marketItems(const Game& g);
// 用途：全部预检后一次性成交；流程：坊市动作；用法：传场次、类别、材料ID、数量和可选旧报价；返回：成本与说明或失败原因，失败不改库存资金。
MarketResult buyMarket(Game& g, const string& eventId, const string& kind,
                       const string& id, int count, int expectedUnitPrice = -1);

} // namespace nz::tide
