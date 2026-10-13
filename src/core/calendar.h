// 宁州一梦 · C++ 版 — 月历（对应 JS 版 calendar.mjs）
//
// 时间规则：一个月 30 天，一年 12 个月共 360 天。
// 所有耗时都是「模拟天数」，不是真实等待时间。
#pragma once

#include "core/model.h"

namespace nz {

// 日历上的一个位置
struct CalDate {
    int year = 1;    // 入道第几年（从 1 开始）
    int month = 1;   // 1..12
    int day = 1;     // 1..30
    int left = 0;    // 本月剩余天数（含当天为 0 时的 30）
};

// 取得日历位置。接受天数而不是 Game，方便单独测试。
// 用途：把绝对天数或游戏日历转换为年月日。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入int days，按本模块入口调用。
// 返回值：CalDate结果，包含说明所述状态或计算数据。
CalDate calendarDate(int days);
// 用途：把绝对天数或游戏日历转换为年月日。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：CalDate结果，包含说明所述状态或计算数据。
CalDate calendarDate(const Game& g);
// 用途：生成游戏年月日中文标记。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string calendarLabel(const Game& g);

// 把连续的少量收益按「整天」发放，零头留在 carry 里下次再用。
// 对应 JS 的 calendarTake：先累加，再取整数部分，余数留回 carry。
// 用途：累计小数消耗并取整保持长期数值一致。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入Game& g, const string& key, double amount，按本模块入口调用。
// 返回值：本次应付整数份数，并保留未取整余量。
int calendarTake(Game& g, const string& key, double amount);
// 同一自然化毒速率供每日结算与只读展示使用（每年360天）。
// 用途：读取功法天赋加成后的年度排毒量。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入const Player& player，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double calendarDetoxPerYear(const Player& player);
// 用途：兽潮实际完成一回合后推进世界一天；共用月年、化毒和寿尽逻辑，不执行日常计划或虚构工作。
// 使用：结束访问Battle引用后调用；无返回值，可能因寿尽结束人生。
void calendarBattleDay(Game& game, const string& endingCause = {});

// 执行在途工作，逐天推进。
// 返回本次实际推进的天数；结束条件与 JS 版一致。
struct CalRunResult {
    bool ok = true;
    string error;
    bool pending = false;   // 结束后是否产生了待处理抉择
    bool ended = false;     // 人生是否结束
    bool working = false;   // 工作是否仍在进行
    bool restored = false;  // 本次推进发生死亡回档，必须停止原安排
    int days = 0;
};
// 用途：推进工作日历并处理月年边界与事件死亡。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入Game& g, int maxDays = 30，按本模块入口调用。
// 返回值：CalRunResult结果，包含说明所述状态或计算数据。
CalRunResult calendarRun(Game& g, int maxDays = 30);

// 初始化时钟（只在读旧档或建新档时调用）
// 用途：补齐旧存档的日历与月度结算状态。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ensureCalendar(Game& g);

} // namespace nz
