// 宁州一梦 · C++ 版 — 兽潮（实现）
#include "core/tide.h"

#include <algorithm>
#include <cmath>

#include "core/adventure.h"
#include "core/arts.h"
#include "core/base.h"
#include "core/battle.h"
#include "core/renown.h"
#include "core/sim.h"
#include "data/reference_tables.h"

namespace nz::tide {

using std::string;

namespace {

// 险地：兽潮的源头。这些地方越多、越危险，兽潮越凶。
vector<const LocationDef*> wilds() {
    vector<const LocationDef*> out;
    for (const auto& l : LOCATIONS())
        if (string(l.type) == "wild") out.push_back(&l);
    return out;
}

// 兽潮会优先扑向人多的地方，也会扫过险地
bool tideHits(const Npc& n, const Game& g) {
    // 宗门与城池里的相对安全，但挡不住高severity
    const bool remote = (n.location == g.player.location) ? false : true;
    return remote || g.tide.severity >= 2;
}

} // namespace

int dangerBonus(const Game& g) {
    if (!g.tide.active) return 0;
    return 8 * g.tide.severity;
}

string statusText(const Game& g) {
    if (!g.tide.active) return "宁州暂安，各处险地未见异动。";
    const char* sev = g.tide.severity >= 3 ? "滔天"
                      : g.tide.severity == 2 ? "猖獗"
                                             : "初起";
    return "兽潮" + string(sev) + "：妖物自险地涌出，波及全州。" +
           "已历 " + std::to_string(g.tide.wavesFought) + " 战，" +
           "陨落修士 " + std::to_string(g.tide.npcDeaths) + " 人。" +
           "预计还有 " + std::to_string(g.tide.monthsLeft) + " 个月。";
}

// ============================================================
// 起潮
// ============================================================
void yearTick(Game& g) {
    if (g.tide.active) return;
    if (g.year < 3) return;      // 头三年让玩家先站稳

    // 基础概率随年份与世道衰败上升；灵石行情差时也更容易出事
    const double base = 0.10 + std::min(0.22, g.year * 0.004);
    const double poor = (g.world.market < 0.95) ? 0.08 : 0.0;
    if (randomOf(g) >= base + poor) return;

    BeastTide& t = g.tide;
    t.active = true;
    t.startYear = g.year;
    t.monthsLeft = 12 + randIntOf(g, 0, 12);
    // 世道越差、年份越久，兽潮越凶
    t.severity = clampInt(1 + static_cast<int>(randomOf(g) * 3.0), 1, 3);
    t.wavesFought = 0;
    t.wavesWon = 0;
    t.npcDeaths = 0;
    t.fallen.clear();
    t.warned = true;

    // 兽潮自某处险地涌出 —— 说清楚源头，玩家才有「去哪儿打」的概念
    string origin = "险地";
    {
        const auto w = wilds();
        if (!w.empty()) {
            const LocationDef* src = pickOf(g, w);
            origin = src->name;
            // 险地越危险，兽潮越凶
            if (src->danger >= 50 && t.severity < 3) t.severity += 1;
        }
    }

    const char* sev = t.severity >= 3 ? "滔天" : t.severity == 2 ? "猖獗" : "初起";
    logEvent(g, "world",
             "【兽潮】妖物自" + origin + "涌出，声势" + string(sev) +
                 "，波及全州。各地宗门已发下悬赏。",
             true);
}

// ============================================================
// 月度蔓延
// ============================================================
void monthTick(Game& g) {
    BeastTide& t = g.tide;
    if (!t.active) return;

    t.monthsLeft -= 1;

    // 每月都有人死。死多少看severity，且**优先死低境界的** ——
    // 高境界修士能扛过去，炼气期的散修顶不住。
    const int rolls = t.severity * 2;
    for (int i = 0; i < rolls; ++i) {
        // 挑一个活着的、境界偏低的
        vector<Npc*> pool;
        for (auto& n : g.npcs) {
            if (!n.alive) continue;
            if (n.stage > 6) continue;           // 金丹以上基本无虞
            if (!tideHits(n, g)) continue;
            pool.push_back(&n);
        }
        if (pool.empty()) break;

        // 低境界权重高：权重 = (7 - stage)^2
        double total = 0.0;
        vector<double> w(pool.size());
        for (size_t k = 0; k < pool.size(); ++k) {
            const int s = clampInt(pool[k]->stage, 0, 6);
            w[k] = static_cast<double>((7 - s) * (7 - s));
            total += w[k];
        }
        double r = randomOf(g) * total;
        size_t pick = 0;
        for (; pick < pool.size(); ++pick) {
            r -= w[pick];
            if (r <= 0) break;
        }
        if (pick >= pool.size()) pick = pool.size() - 1;

        Npc* victim = pool[pick];
        // 境界越低越容易死；高一点的有机会逃过
        const double survive = clamp(0.08 + victim->stage * 0.10, 0.0, 0.72);
        if (randomOf(g) < survive) continue;

        // 先记账再动手 —— 否则统计永远是 0（这个坑踩过一次）
        t.npcDeaths += 1;
        if (t.fallen.size() < 40) t.fallen.push_back(victim->name);
        npcDies(g, *victim, "殁于兽潮");
    }

    if (t.monthsLeft <= 0) {
        // 潮退
        const int deaths = t.npcDeaths;
        const int fought = t.wavesFought;
        t.active = false;
        logEvent(g, "world",
                 "【兽潮】妖潮退去。此役宁州陨落修士 " +
                     std::to_string(deaths) + " 人，你迎战 " +
                     std::to_string(fought) + " 场。",
                 true);
    }
}

// ============================================================
// 迎战
// ============================================================
FightResult fight(Game& g) {
    FightResult r;
    if (!g.tide.active) { r.error = "眼下没有兽潮。"; return r; }

    Player& p = g.player;
    g.tide.wavesFought += 1;

    // 对手强度：按玩家境界 + 兽潮severity，略高于同境界
    const int stage = clampInt(p.stage + (g.tide.severity >= 3 ? 1 : 0), 0, 12);
    const char* names[3] = {"兽潮先锋", "妖将", "妖王麾下"};
    const string nm = names[clampInt(g.tide.severity - 1, 0, 2)];

    // 迎战有陨落风险；战败后由结局入口选择接受死亡或恢复最近存档。
    EnemySpec e;
    e.name = nm;
    e.kind = "tide";
    e.lethal = false;
    e.stage = stage;
    e.health = 100;
    e.style = "体";                 // 妖兽走「力大砖飞」
    battleStart(g, e);

    r.ok = true;
    r.lethal = true;
    r.text = nm + "扑至，你拔剑迎了上去。";
    logEvent(g, "world", "【兽潮】你在" + (findLoc(p.location) ? findLoc(p.location)->name : p.location) + "迎战" + nm + "。", true);
    return r;
}

// 战后的赏赐（由 battleFinish 的 hunt 分支调用）
void tideReward(Game& g, bool won) {
    BeastTide& t = g.tide;
    Player& p = g.player;
    if (won) {
        t.wavesWon += 1;
        const int stones = 40 + t.severity * 45 + p.stage * 18;
        const int renown = 3 + t.severity * 2;
        p.stones += stones;
        p.renown += renown;
        // 掉一份材料
        vector<const data::MaterialDef*> pool;
        for (const auto& m : data::MaterialDef_TABLE())
            if (m.grade <= clampInt(1 + p.stage / 3 + t.severity, 1, 5))
                pool.push_back(&m);
        string got;
        if (!pool.empty()) {
            const data::MaterialDef* m = pickOf(g, pool);
            p.arts.materials[m->id] += 1;
            got = m->name;
        }
        logEvent(g, "world",
                 "【兽潮】击退" + string("来犯妖物") + "，得灵石 " +
                     std::to_string(stones) + "、声望 " + std::to_string(renown) +
                     (got.empty() ? "" : "、" + got + " 一份") + "。",
                 true);
    } else {
        logEvent(g, "world", "【兽潮】你力有不逮，退回阵中。", false);
    }
}

string hunkerDown(Game& g) {
    if (!g.tide.active) return "眼下没有兽潮。";
    logEvent(g, "world",
             "【兽潮】你固守不出，静待妖潮退去。", false);
    return "你闭门固守，妖潮在外肆虐。";
}

void ensureTide(Game& g) {
    // 旧档没有 tide 字段，默认就是「没有兽潮」，无需补
    (void)g;
}

} // namespace nz::tide
