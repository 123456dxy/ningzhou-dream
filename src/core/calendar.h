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
CalDate calendarDate(int days);
CalDate calendarDate(const Game& g);
string calendarLabel(const Game& g);

// 把连续的少量收益按「整天」发放，零头留在 carry 里下次再用。
// 对应 JS 的 calendarTake：先累加，再取整数部分，余数留回 carry。
int calendarTake(Game& g, const string& key, double amount);

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
CalRunResult calendarRun(Game& g, int maxDays = 30);

// 初始化时钟（只在读旧档或建新档时调用）
void ensureCalendar(Game& g);

} // namespace nz
