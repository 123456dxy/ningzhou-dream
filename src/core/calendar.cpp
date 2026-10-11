// 宁州一梦 · C++ 版 — 月历实现（对应 JS 版 calendar.mjs）
//
// JS 版用一个 ctx 对象把 sim 的回调注入 calendar：
//   ctx.worldYear / ctx.hasDan / ctx.finish / ctx.refresh / ctx.complete / ctx.choice
// C++ 里改成在 sim.h 中声明的自由函数，calendar 直接调用。
#include "core/calendar.h"

#include "core/sim.h"
#include "core/tide.h"
#include "core/travel.h"
#include "core/dungeon.h"
#include "core/checkpoint.h"
#include "core/talents.h"

namespace nz {

void ensureCalendar(Game& g) {
    // JS: s.clock={days:s.year*360, ageBase:s.player.age-s.year, carry:{}, job:null}
    if (g.clock.days == 0 && g.year > 0) g.clock.days = g.year * 360;
    if (g.clock.ageBase == 0)
        g.clock.ageBase = g.player.age - g.clock.days / 360;
}

CalDate calendarDate(int days) {
    // JS: year=floor(d/360)+1, month=floor(d%360/30)+1, day=d%30+1, left=30-d%30
    days = std::max(0, days);
    CalDate d;
    d.year = jsFloor(static_cast<double>(days) / 360.0) + 1;
    const int rem = days % 360;
    d.month = rem / 30 + 1;
    d.day = rem % 30 + 1;
    d.left = 30 - rem % 30;
    return d;
}

CalDate calendarDate(const Game& g) {
    return calendarDate(g.clock.days);
}

string calendarLabel(const Game& g) {
    CalDate d = calendarDate(g);
    return "入道第" + std::to_string(d.year) + "年 · " +
           std::to_string(d.month) + "月" + std::to_string(d.day) + "日";
}

int calendarTake(Game& g, const string& key, double amount) {
    // JS: value=(carry[key]||0)+amount; n=floor(value+1e-9);
    //     carry[key]=max(0,value-n); return n
    // 用 1e-9 抵消浮点误差，保证 0.9999999 之类被当成 1。
    double value = g.clock.carry[key] + amount;
    int n = jsFloor(value + 1e-9);
    g.clock.carry[key] = std::max(0.0, value - static_cast<double>(n));
    return n;
}

CalRunResult calendarRun(Game& g, int maxDays) {
    CalRunResult out;
    const int restores = g.checkpoint.restores;
    out.ok = false;
    if (g.ended || g.pending.has_value() || g.player.arts.battleActive()) {
        out.ended = g.ended;
        out.pending = g.pending.has_value();
        out.error = g.ended ? "此世已结束。" : "请先处理当前抉择或斗法。";
        return out;
    }
    if (g.player.health <= 0 || g.player.age >= g.player.lifespan) {
        finish(g, g.player.health <= 0 ? "伤势不治" : "寿终正寝");
        out.ok = true;
        out.ended = g.ended;
        out.restored = g.checkpoint.restores != restores;
        return out;
    }
    if (!g.job.has_value()) {
        out.ok = false;
        out.error = "没有正在进行的安排。";
        return out;
    }
    Job& j = *g.job;
    if (maxDays <= 0 || j.total <= 0 || j.remaining <= 0 || j.remaining > j.total) {
        out.error = "在途安排的天数无效。";
        return out;
    }

    // 只推进到本月结束为止，跨月由上层再次调用。
    const int left = calendarDate(g).left;
    const int duration = std::min({j.remaining, left, maxDays});

    for (int i = 0; i < duration; ++i) {
        g.clock.days++;
        j.remaining--;
        ++out.days;

        // 世界事件按实际月界推进，短行动与长工作都遵守同一月历。
        if (g.clock.days % 30 == 0) tide::monthTick(g);

        // 跨年结算：世界推进、驱邪符效力递减、年龄增长
        const int year = jsFloor(static_cast<double>(g.clock.days) / 360.0);
        if (year != g.year) {
            g.year = year;
            g.player.age = g.clock.ageBase + year;
            worldYear(g);
            if (g.checkpoint.restores != restores) {
                out.ok = true; out.restored = true; return out;
            }
            dungeonYearTick(g);
            g.player.arts.repel = std::max(0, g.player.arts.repel - 1);
        }

        // 丹毒每天自然化解；服过解毒丹（丹道二层）则更快
        const double rate = hasDan(g.player) ? 5.0 : 1.0;
        const int detox = calendarTake(g, "detox", rate / 360.0);
        g.player.arts.poison = std::max(0, g.player.arts.poison - detox);

        refreshDaily(g);

        if (g.player.health <= 0 || g.player.age >= g.player.lifespan) {
            finish(g, g.player.health <= 0 ? "伤势不治" : "寿终正寝");
            out.ok = true;
            out.ended = g.ended;
            out.restored = g.checkpoint.restores != restores;
            return out;
        }
        if (g.clock.days % 30 == 0) talentMonthTick(g);
        checkpointTick(g);
        if (g.job && g.job->actionType == "travel" && g.job->remaining > 0 && travelAmbushTick(g)) {
            out.ok = true;
            out.working = true;
            out.restored = g.checkpoint.restores != restores;
            return out;
        }
    }

    if (j.remaining == 0) {
        Job finishedJob = j;
        g.job.reset();
        monthComplete(g, finishedJob);
        if (g.checkpoint.restores != restores) {
            out.ok = true; out.restored = true; return out;
        }
        if (g.player.health <= 0) {
            finish(g, "游历遇险");
            if (g.checkpoint.restores != restores) {
                out.ok = true; out.restored = true; return out;
            }
        } else if (!g.player.arts.battleActive()) {
            maybeChoice(g);
        }
    }

    out.ok = true;
    out.pending = g.pending.has_value();
    out.ended = g.ended;
    out.working = g.job.has_value();
    checkpointTick(g);
    return out;
}

} // namespace nz
