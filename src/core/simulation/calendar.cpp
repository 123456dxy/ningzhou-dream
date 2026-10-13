// 宁州一梦 · C++ 版 — 月历实现（对应 JS 版 calendar.mjs）
//
// JS 版用一个 ctx 对象把 sim 的回调注入 calendar：
//   ctx.worldYear / ctx.hasDan / ctx.finish / ctx.refresh / ctx.complete / ctx.choice
// C++ 里改成在 sim.h 中声明的自由函数，calendar 直接调用。
#include "core/calendar.h"
#include "core/crafting/cave_garden.h"

#include "core/sim.h"
#include "core/tide.h"
#include "core/travel.h"
#include "core/dungeon.h"
#include "core/checkpoint.h"
#include "core/talents.h"

namespace nz {

// 用途：补齐旧存档的日历与月度结算状态。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ensureCalendar(Game& g) {
    // JS: s.clock={days:s.year*360, ageBase:s.player.age-s.year, carry:{}, job:null}
    if (g.clock.days == 0 && g.year > 0) g.clock.days = g.year * 360;
    if (g.clock.ageBase == 0)
        g.clock.ageBase = g.player.age - g.clock.days / 360;
}

// 用途：把绝对天数或游戏日历转换为年月日。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入int days，按本模块入口调用。
// 返回值：CalDate结果，包含说明所述状态或计算数据。
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

// 用途：把绝对天数或游戏日历转换为年月日。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：CalDate结果，包含说明所述状态或计算数据。
CalDate calendarDate(const Game& g) {
    return calendarDate(g.clock.days);
}

// 用途：生成游戏年月日中文标记。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string calendarLabel(const Game& g) {
    CalDate d = calendarDate(g);
    return "入道第" + std::to_string(d.year) + "年 · " +
           std::to_string(d.month) + "月" + std::to_string(d.day) + "日";
}

// 用途：累计小数消耗并取整保持长期数值一致。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入Game& g, const string& key, double amount，按本模块入口调用。
// 返回值：本次应付整数份数，并保留未取整余量。
int calendarTake(Game& g, const string& key, double amount) {
    // JS: value=(carry[key]||0)+amount; n=floor(value+1e-9);
    //     carry[key]=max(0,value-n); return n
    // 用 1e-9 抵消浮点误差，保证 0.9999999 之类被当成 1。
    double value = g.clock.carry[key] + amount;
    int n = jsFloor(value + 1e-9);
    g.clock.carry[key] = std::max(0.0, value - static_cast<double>(n));
    return n;
}

// 用途：读取功法天赋加成后的年度排毒量。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入const Player& player，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double calendarDetoxPerYear(const Player& player) {
    return hasDan(player) ? 5.0 : 1.0;
}

// 用途：推进一个真实世界日；工作和兽潮回合共用月年/化毒/寿尽流程。
// 使用：不持有跨日的Battle/Npc引用；无返回值，恢复点或人生结束可替换当前状态。
static void advanceWorldDay(Game& game, const string& endingCause = {}) {
    const int restores = game.checkpoint.restores;
    if (game.tide.active && game.tide.rulesVersion==0) tide::ensureTide(game);
    ++game.clock.days;
    const int year = game.clock.days / 360;
    const bool crossedYear = year != game.year;
    if (crossedYear) {
        game.year = year;
        game.player.age = game.clock.ageBase + year;
    }
    crafting::mfGardenDayTick(game);
    tide::dayTick(game);
    if (game.clock.days % 30 == 0) tide::monthTick(game);
    if (crossedYear) {
        if (!game.ended && game.player.health > 0) {
            worldYear(game);
            if (game.checkpoint.restores != restores) return;
            dungeonYearTick(game);
            game.player.arts.repel = std::max(0, game.player.arts.repel - 1);
        }
    }
    const int detox = calendarTake(game, "detox", calendarDetoxPerYear(game.player) / 360.0);
    game.player.arts.poison = std::max(0, game.player.arts.poison - detox);
    if (!game.ended) refreshDaily(game);
    if (!game.ended && (game.player.health <= 0 || game.player.age >= game.player.lifespan))
        finish(game, game.player.health <= 0 ? (endingCause.empty() ? "伤势不治" : endingCause) : "寿终正寝");
    if (game.ended && game.ending) {
        game.ending->year = game.year;
        game.ending->age = game.player.age;
    }
    if (!game.ended && game.clock.days % 30 == 0) talentMonthTick(game);
    checkpointTick(game);
}

// 用途：结算兽潮已完成回合的日历成本；不执行工作、不创建假Job，无返回值。
void calendarBattleDay(Game& game, const string& endingCause) { advanceWorldDay(game, endingCause); }

// 用途：推进工作日历并处理月年边界与事件死亡。
// 参与流程：日历推进与月年边界结算。
// 使用方式：传入Game& g, int maxDays，按本模块入口调用。
// 返回值：CalRunResult结果，包含说明所述状态或计算数据。
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
        j.remaining--;
        ++out.days;
        advanceWorldDay(g);
        if (g.checkpoint.restores != restores) {
            out.ok = true; out.restored = true; return out;
        }
        if (g.ended) { out.ok = true; out.ended = true; return out; }
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
