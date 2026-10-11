// 月历自测：360 天一年的时间换算与跨月推进。
#include "harness.h"

#include "core/calendar.h"
#include "core/model.h"

using namespace nz;

namespace nztest {

void runCalendarTests() {
    std::printf("\n-- 月历（对照 JS 版 calendar.mjs）--\n");

    // 换算：一年 360 天，一月 30 天
    {
        CalDate d = calendarDate(0);
        checkEq(d.year, 1, "第 0 天为第 1 年的第 1 月第 1 日");
        checkEq(d.month, 1, "第 0 天月份为 1");
        checkEq(d.day, 1, "第 0 天日期为 1");
        checkEq(d.left, 30, "第 0 天本月剩余 30 天");
    }
    {
        CalDate d = calendarDate(29);
        checkEq(d.day, 30, "第 29 天是 30 日");
        checkEq(d.left, 1, "第 29 天本月还剩 1 天");
    }
    {
        CalDate d = calendarDate(30);
        checkEq(d.year, 1, "第 30 天仍是第 1 年");
        checkEq(d.month, 2, "第 30 天进入 2 月");
        checkEq(d.day, 1, "第 30 天是 1 日");
        checkEq(d.left, 30, "第 30 天本月剩余 30 天");
    }
    {
        CalDate d = calendarDate(359);
        checkEq(d.year, 1, "第 359 天是第 1 年");
        checkEq(d.month, 12, "第 359 天是 12 月");
        checkEq(d.day, 30, "第 359 天是 30 日");
    }
    {
        CalDate d = calendarDate(360);
        checkEq(d.year, 2, "第 360 天进入第 2 年");
        checkEq(d.month, 1, "第 360 天是 1 月");
        checkEq(d.day, 1, "第 360 天是 1 日");
    }
    {
        CalDate d = calendarDate(360 * 7 + 30 * 5 + 12);
        checkEq(d.year, 8, "折算第 8 年");
        checkEq(d.month, 6, "折算 6 月");
        checkEq(d.day, 13, "折算 13 日");
    }

    // calendarTake：小数收益按整天发放，余数留回 carry
    {
        Game g;
        ensureCalendar(g);
        // 连续三天各 0.4，第一天给 0，第二天给 0，第三天累计 1.2 给 1
        // 注意 JS 用 floor(value+1e-9)，所以 0.9999999 也算 1
        int total = 0;
        total += calendarTake(g, "xp", 0.4);
        total += calendarTake(g, "xp", 0.4);
        total += calendarTake(g, "xp", 0.4);
        checkEq(total, 1, "三天各 0.4 共发放 1 点");
        checkNear(g.clock.carry["xp"], 0.2, "余数 0.2 留在 carry", 1e-9);
    }
    {
        // 恰好在整数边界上不应该因为浮点误差少发
        Game g;
        ensureCalendar(g);
        int got = 0;
        for (int i = 0; i < 10; ++i) got += calendarTake(g, "k", 0.1);
        checkEq(got, 1, "0.1 累加十次正好发放 1 点");
    }
    {
        // 单次大额收益
        Game g;
        ensureCalendar(g);
        checkEq(calendarTake(g, "x", 7.9), 7, "7.9 发放 7 点");
        checkNear(g.clock.carry["x"], 0.9, "余数 0.9 留在 carry", 1e-9);
    }

    // ensureCalendar 按年折算天数
    {
        Game g;
        g.year = 5;
        g.player.age = 21;
        ensureCalendar(g);
        checkEq(g.clock.days, 1800, "第 5 年折算 1800 天");
        checkEq(g.clock.ageBase, 16, "入道年龄基准为 16");
    }
}

} // namespace nztest
