// 宁州一梦 · C++ 版 — 人生与天下基础实现
// 本文件覆盖 sim.mjs 的：纪事、数值公式、世界年度推进、NPC 年度行动、
// 宗门刷新、人口修剪、新游戏生成、结算。
#include "core/sim.h"
#include "core/dungeon.h"
#include "core/checkpoint.h"
#include "core/talents.h"
#include "core/source_limits.h"
#include "core/sect_trial.h"

#include <algorithm>
#include <stdexcept>
#include <set>

#include "core/adventure.h"
#include "core/arts.h"
#include "core/calendar.h"
#include "core/battle.h"
#include "core/conflict.h"
#include "core/cultivate.h"
#include "core/renown.h"
#include "core/economy.h"
#include "core/society.h"
#include "core/tide.h"
#include "core/npc_learning.h"
#include "core/location_services.h"
#include "core/friendship.h"
#include "core/play.h"
#include "data/reference_tables.h"

namespace nz {

// ============================================================
// 纪事
// ============================================================
void logEvent(Game& g, const string& category, const string& text,
              bool important, const string& npcId, const string& delta) {
    LogEntry e;
    e.id = ++g.eventSeq;
    e.year = g.year;
    e.age = g.player.age;
    e.category = category;
    e.text = text;
    e.important = important;
    if (!npcId.empty()) e.npcId = npcId;
    e.delta = delta;
    e.days = g.clock.days;
    g.logs.push_back(e);
    // 只保留最近 650 条，避免存档无限膨胀（与 JS 一致）
    if (g.logs.size() > 650) g.logs.erase(g.logs.begin());
}

void npcHistory(Game& g, Npc& n, const string& text) {
    n.history.push_back({g.year, text});
    // 关系好或高境界的 NPC 保留更多履历
    int rel = 0;
    auto it = g.player.relations.find(n.id);
    if (it != g.player.relations.end()) rel = it->second;
    const size_t cap = (rel > 0 || n.stage >= 9) ? 20 : 8;
    if (n.history.size() > cap) n.history.erase(n.history.begin());
}

void npcNews(Game& g, Npc& n, const string& text, bool important) {
    // 报道规则（对应 JS 的 npcNews）：
    //   认识的人（好感 ≥15）、高境界修士（≥9 境），
    //   或者「今年还没播满 5 条」时挑几条让玩家看看天下动静。
    // 最后这一条之前漏了：玩家一个 NPC 都不认识时，
    // 世界演算照跑（NPC 会突破、会老死），但纪事里一条都看不到，
    // 感觉整个天下是静止的。
    int rel = 0;
    auto it = g.player.relations.find(n.id);
    if (it != g.player.relations.end()) rel = it->second;
    const bool known = rel >= 15;

    bool quotaLeft = false;
    if (!known && n.stage < 9) {
        int thisYear = 0;
        for (const auto& e : g.logs)
            if (e.category == "npc" && e.year == g.year) ++thisYear;
        quotaLeft = thisYear < 5;
    }

    if (known || n.stage >= 9 || quotaLeft) {
        logEvent(g, "npc", text, important, n.id);
    }
}

static Npc* findNpc(Game& g, const string& id) {
    for (auto& n : g.npcs)
        if (n.id == id) return &n;
    return nullptr;
}

// 玩家受到的伤害。体魄每点降低 2%，上限 30%（对应 JS 的 damage）。
void applyDamage(Game& g, int amount) {
    const int actual = jsRound(
        amount * 1.0);   // 体魄不再减伤（改为提供气血上限）
    g.player.health = std::max(0, g.player.health - actual);
}

// 剑意增长已移到 sword.cpp（Demo06 的 swordGain 会记录剑道进境），
// 这里不再重复定义。

static const Npc* findNpc(const Game& g, const string& id) {
    for (const auto& n : g.npcs)
        if (n.id == id) return &n;
    return nullptr;
}

int relationOf(const Game& g, const string& npcId) {
    auto it = g.player.relations.find(npcId);
    return it == g.player.relations.end() ? 0 : it->second;
}

void addRelation(Game& g, const string& npcId, int delta) {
    // 第一次打交道时，名声会先替你说话：
    // 声望高的人，陌生人初始就有所耳闻（对应 renown::firstImpression）
    auto it = g.player.relations.find(npcId);
    if (it == g.player.relations.end()) {
        const int first = renown::firstImpression(g.player.renown);
        g.player.relations[npcId] = clampInt(first + delta, -100, 100);
        return;
    }
    it->second = clampInt(it->second + delta, -100, 100);
}

// ============================================================
// 数值公式（逐条对应 sim.mjs）
// ============================================================
int powerOf(const Person& p, const Arts* arts) {
    // JS: round(realm.power*(.75+health/400) + sqrt(sword)*1.7
    //            + sqrt(insight)*.8 + (artMods.attack-1)*realm.power)
    const RealmDef& r = realmOf(p.stage);
    double attackMod = 1.0;
    if (arts) attackMod = artModsOf(*arts).attack;
    double v = r.power * (0.75 + p.health / 400.0) +
               jsSqrt(static_cast<double>(p.insight)) * 0.8 +
               (attackMod - 1.0) * r.power;
    return jsRound(v);
}

int power(const Person& p) {
    return powerOf(p, nullptr);
}

bool hasDan(const Player& p) {
    return artHas(p.arts, "丹", 2);
}

double legacyEfficiencyOf(const Player& p) {
    const int budget = p.build.budget > 0 ? p.build.budget : BASE_POINTS;
    return 1.0 + std::max(0, budget - BASE_POINTS) * 0.015;
}

double cultivationRate(const Player& p) {
    // 重构后：功法基础 × 资质 × 悟性 × 境界 × 传承 × 配给
    // 拆解逻辑都在 core/cultivate.cpp，那边也负责给界面展示各项因子。
    return cultivate::rate(p);
}

// NPC 用的版本：按它自己的境界与资质算，不带洞府加成。
// 之前 npcTurn 里写的是 cultivationRate(Player{})，传了个默认玩家，
// 于是所有 NPC 都按「炼气初期 / 资质 55」的速率涨修为 ——
// 高境界的修为需求大得多，永远填不满，境界分布十年不变。
double cultivationRateFor(int stage, int apt) {
    static const double groupMul[5] = {1.0, 1.38, 3.2, 4.5, 8.0};
    const int group = std::min(4, stage / 3);
    return (6.0 + apt * 0.2) * groupMul[group];
}

// NPC 用的闭关开销：按它自己的境界与宗门
int cultivationCostFor(int stage, const std::string& sect) {
    return std::max(4, 8 + (stage / 3) * 5 - (sect == "huachen" ? 4 : 0));
}

double alchemyChance(const Player& p) {
    // JS: clamp(.42 + sqrt(alchemy)*.052 + (sect==='lihuo' ? .1 : 0), .1, .9)
    double v = 0.42 + jsSqrt(static_cast<double>(p.alchemy)) * 0.052 +
               (p.sect == "lihuo" ? 0.1 : 0.0);
    return clamp(v, 0.1, 0.9);
}

int cultivationCost(const Player& p) {
    // JS: max(4, 8 + floor(stage/3)*5 - (sect==='huachen'?4:0))
    int v = 8 + (p.stage / 3) * 5 - (p.sect == "huachen" ? 4 : 0);
    return std::max(4, v);
}

int marketPrice(const Game& g, const string& item) {
    int base = 0;
    if (item == "herbs") base = 10;
    else if (item == "pills") base = 85;
    else if (item == "cores") base = 70;
    return jsRound(base * g.world.market);
}

int socialGiftCost(const Person& p) {
    return 2 + (p.stage / 3) * 2;
}

int breakthroughCost(const Game& g) {
    const Player& p = g.player;
    const bool major = (p.stage + 1) % 3 == 0;
    const int group = std::min(3, p.stage / 3);
    if (major) {
        static const int table[4] = {100, 240, 550, 1000};
        return table[group];
    }
    return 20 + group * 15;
}

double breakthroughChance(const Game& g) {
    const Player& p = g.player;
    const bool major = (p.stage + 1) % 3 == 0;
    const int group = std::min(3, p.stage / 3);

    // 资质与悟性对突破的加成统一由 cultivate::breakthroughBonus 给出，
    // 避免这里再抄一份公式、两处各调各的。
    const double bonus = cultivate::breakthroughBonus(p);

    double base;
    if (major) {
        static const double table[4] = {0.42, 0.29, 0.18, -0.28};
        base = table[group];
    } else {
        base = 0.8;
    }

    double chance = base + bonus +
                    (hasTalent(p, "steady") ? 0.04 : 0.0) +
                    (major && p.pills > 0 ? 0.18 : 0.0) +
                    (major && p.cores > 0 ? 0.1 : 0.0);
    if (major && p.arts.breakBuff == "stage") chance += 0.06;
    if (major) {
        chance += std::min(0.05, p.arts.heart * 0.0005);
        chance += artHas(p.arts, "神", 3) ? 0.03 : 0.0;
    }
    return clamp(chance, 0.05, 0.95);
}

vector<int> populationCounts(const Game& g) {
    vector<int> counts(5, 0);
    for (const auto& n : g.npcs)
        if (n.alive) counts[std::min(4, n.stage / 3)]++;
    return counts;
}

// ------------------------------------------------------------
// 地图种子：由 seed 生成这一世的宁州
//
// 同一颗种子必得同一个世界（可复现），不同种子给出不同的
// 行情、灵脉归属、宗门格局与资源基线 —— 换一世就是换一个天下。
// ------------------------------------------------------------
void worldFromSeed(Game& g) {
    Rng rng(mixSeed(g.seed));

    // 繁荣度与行情：这一世的宁州是富庶还是萧条
    g.world.prosperity = 55 + static_cast<int>(rng.next() * 41);   // 55..95
    // 行情围绕 1.0 波动 ±15%
    g.world.market = 0.88 + rng.next() * 0.28;

    // 灵脉初始归属：五宗之一
    {
        const auto& sects = SECTS();
        const int idx = static_cast<int>(rng.next() * sects.size());
        g.world.mineOwner = sects[std::min<int>(idx, sects.size() - 1)].id;
    }

    // 资源基线
    g.world.herbs = 2400 + static_cast<int>(rng.next() * 1600);
    g.world.coreStock = 80 + static_cast<int>(rng.next() * 90);
    g.world.pillStock = 60 + static_cast<int>(rng.next() * 70);

    // 宗门格局：各宗初始影响力与府库有差异
    for (auto& [id, st] : g.world.sects) {
        st.influence = 35 + static_cast<int>(rng.next() * 45);
        st.treasury = 2200 + static_cast<int>(rng.next() * 2200);
    }
    // 灵脉主额外有影响力
    auto mine = g.world.sects.find(g.world.mineOwner);
    if (mine != g.world.sects.end()) mine->second.influence += 8;

    // 天气基调
    static const char* WEATHER[] = {"春和景明", "烟雨绵延", "秋高气爽",
                                    "朔风凛冽", "云蒸霞蔚"};
    g.world.weather = WEATHER[static_cast<int>(rng.next() * 5) % 5];

    refreshSects(g);
}
void refreshSects(Game& g) {
    for (const auto& se : SECTS()) {
        SectState& d = g.world.sects[se.id];
        int members = 0;
        for (const auto& n : g.npcs)
            if (n.alive && n.sect == se.id) ++members;
        d.members = members +
                    ((g.player.sect == se.id && !g.ended) ? 1 : 0);
        double sum = 20.0;
        for (const auto& n : g.npcs)
            if (n.alive && n.sect == se.id)
                sum += power(n) * 0.07;
        d.influence = jsRound(sum + (g.world.mineOwner == se.id ? 8 : 0));
    }
}

// ============================================================
// NPC 生成（对应 sim.mjs makeNpc）
// ============================================================
// 姓与双字名的组合，排除重名。
const vector<string>& npcSurnames();

string socNPCName(int index);

static Npc makeNpc(Game& g, int i, int stageOverride = -1) {
    const int st = stageOverride >= 0 ? stageOverride : randIntOf(g, 0, 2);
    const int group = st / 3;
    const int life = realmOf(st).life;
    const string gender = (i % 2) ? "女" : "男";

    // 女性不会去只收女弟子的门派之外…（与 JS 版保持一致的筛选）
    vector<string> sectPool;
    for (const auto& s : SECTS()) {
        if (gender == "女" || string(s.id) != "xinghe") sectPool.push_back(s.id);
    }
    sectPool.push_back("");   // 散修
    sectPool.push_back("");
    const string se = pickOf(g, sectPool);

    Npc n;
    n.id = "npc-" + std::to_string(i);
    n.name = socNPCName(i);
    n.gender = gender;
    if (g.year == 0) {
        static const int ageLow[5] = {18, 35, 90, 230, 600};
        n.age = randIntOf(g, ageLow[std::min(4, group)],
                          static_cast<int>(life * 0.85));
    } else {
        n.age = 16;
        n.isNewborn = true;
        n.bornYear = g.year;
    }
    n.stage = st;
    n.lifespan = life;
    n.xp = randIntOf(g, 0, static_cast<int>(realmOf(st).need * 0.65));
    n.apt = randIntOf(g, group == 0 ? 38 : 58, group >= 3 ? 98 : 85);
    n.luck = randIntOf(g, 25, 85);
    n.insight = randIntOf(g, 5 + group * 8, 25 + group * 14);
    n.sword = randIntOf(g, 3, 20 + group * 12);
    n.alchemy = randIntOf(g, 0, 15 + group * 12);
    n.health = 100;
    n.stones = randIntOf(g, 30 + group * 180, 140 + group * 420);
    n.herbs = randIntOf(g, 1, 5);
    n.pills = 0;
    n.sect = se;
    if (!se.empty() && randomOf(g) < 0.6) {
        n.location = se;
    } else {
        vector<string> cities;
        for (const auto& l : LOCATIONS())
            if (string(l.type) == "city") cities.push_back(l.id);
        n.location = pickOf(g, cities);
    }
    static const char* personas[5] = {"潜心问道", "重情重义", "好胜争先",
                                      "逐利而行", "洒脱随性"};
    n.personality = personas[randIntOf(g, 0, 4)];
    n.goal = "修炼";
    n.alive = true;
    n.deathYear = -1;

    // 重名时换一个名字，仍然重名就加序号
    std::set<string> occupied;
    occupied.insert(g.player.name);
    for (const auto& x : g.npcs) occupied.insert(x.name);
    if (occupied.count(n.name)) {
        string next = socNPCName(i + 2001);
        n.name = occupied.count(next) ? next + "·" + std::to_string(i) : next;
    }

    npcHistory(g, n, "出身" + (se.empty() ? string("散修") :
                              string(findSect(se)->name)) +
                     "，愿在宁州求得一线长生。");
    return n;
}

// ============================================================
// NPC 陨落
// ============================================================
static void killNpc(Game& g, Npc& n, const string& reason);
void npcDies(Game& g, Npc& n, const string& reason) { killNpc(g, n, reason); }

static void killNpc(Game& g, Npc& n, const string& reason) {
    if (!n.alive) return;
    n.alive = false;
    n.health = 0;
    n.deathYear = g.year;
    n.goal = reason;
    npcHistory(g, n, reason);

    const int rel = relationOf(g, n.id);
    npcNews(g, n, n.name + "（" + realmName(n.stage) + "）" + reason + "。" +
                  (rel >= 45 ? "一位知交就此离去。" : ""), true);

    // 交情深的朋友会暂停游历
    for (const auto& [fid, v] : n.relations) {
        if (v <= 30) continue;
        Npc* f = findNpc(g, fid);
        if (f && f->alive) {
            f->goal = "守静悼友";
            npcHistory(g, *f, "得知" + n.name + "陨落，暂缓了游历。");
        }
    }
}

// ============================================================
// NPC 年度行动（对应 sim.mjs npcTurn）
// ============================================================
static void gainXp(Person& p, double n) {
    const int need = realmOf(p.stage).need;
    p.xp = std::min(need, p.xp + std::max(0, jsRound(n)));
}

static void damagePerson(Person& p, int n) {
    // 体魄每点降低 2% 伤害，上限 30%
    const int body = (p.stage >= 0) ? 0 : 0;
    (void)body;
    p.health = std::max(0, p.health - n);
}

static void npcTurn(Game& g, Npc& n) {
    if (!n.alive) return;
    if (n.isNewborn && n.bornYear == g.year) return;

    n.age++;
    if (n.age >= n.lifespan) {
        killNpc(g, n, "寿元已尽，坐化于故地");
        return;
    }
    if (n.health <= 0) {
        killNpc(g, n, "在争斗中陨落");
        return;
    }

    const int group = n.stage / 3;
    const bool major = (n.stage + 1) % 3 == 0;
    static const int majorCost[4] = {100, 240, 550, 1000};
    const int cost = major ? majorCost[std::min(3, group)] : 20 + group * 15;
    const bool ready = n.stage < 12 && n.xp >= realmOf(n.stage).need;

    if (ready && n.stones >= cost) {
        n.goal = "冲击境界";
        n.stones -= cost;

        static const int aptGate[4] = {48, 67, 80, 93};
        const bool gifted = n.apt >= aptGate[std::min(3, group)];
        static const double majorChance[4] = {0.36, 0.24, 0.14, 0.07};
        double chance = major ? majorChance[std::min(3, group)] : 0.8;
        chance += std::min(0.1, jsSqrt(n.insight) * 0.01);
        chance += clamp((n.apt - 55) * 0.001, -0.02, 0.05);

        if (major) {
            if (n.pills) { n.pills--; chance += 0.18; }
            chance *= gifted ? 1.0 : 0.2;
            // 高位名额有限：人越多越难突破
            int crowd = 0;
            for (const auto& x : g.npcs)
                if (x.alive && std::min(4, x.stage / 3) == std::min(4, group + 1))
                    ++crowd;
            const int cap = POPULATION[std::min(4, group + 1)];
            chance *= std::min(1.0, static_cast<double>(cap) /
                                        std::max(1, crowd));
        }

        if (randomOf(g) < chance) {
            n.stage++;
            n.xp = 0;
            n.lifespan = realmOf(n.stage).life;
            n.health = std::min(100, n.health + 12);
            npcHistory(g, n, "突破至" + realmName(n.stage) +
                             "，寿元上限" + std::to_string(n.lifespan) + "岁。");
            if (n.stage % 3 == 0 || relationOf(g, n.id) >= 30)
                npcNews(g, n, n.name + "突破至" + realmName(n.stage) +
                              "，继续自己的道途。", n.stage % 3 == 0);
        } else {
            n.xp = jsFloor(n.xp * 0.8);
            n.health = std::max(1, n.health - (major ? 12 : 5));
            npcHistory(g, n, "冲关未成，折损修为后重新积累。");
        }
        return;
    }

    if (n.health < 55) {
        n.goal = "休养调息";
        n.health = std::min(100, n.health + 20);
        gainXp(n, 4);
        return;
    }

    if (major && n.stage < 12 && n.xp >= realmOf(n.stage).need * 0.75 &&
        !n.pills && n.stones >= marketPrice(g, "pills") + cost &&
        g.world.pillStock > 0) {
        n.goal = "筹备丹药";
        n.stones -= marketPrice(g, "pills");
        n.pills++;
        g.world.pillStock--;
        npcHistory(g, n, "在坊市购得凝元丹，为大境界突破做准备。");
        return;
    }

    const double roll = randomOf(g);
    const int upkeep = cultivationCostFor(n.stage, n.sect);
    if (n.stones < cost + upkeep ||
        (n.personality == "逐利而行" && roll < 0.42)) {
        n.goal = "坊市营生";
        n.stones += jsRound(randIntOf(g, 22, 35) * (1 + group * 0.18) *
                            g.world.market);
        gainXp(n, 3 + group);
    } else if (n.personality == "潜心问道" || n.lifespan - n.age < 25 ||
               roll < 0.54) {
        n.goal = "闭关修炼";
        n.stones -= upkeep;
        gainXp(n, cultivationRateFor(n.stage, n.apt) * (n.sect.empty() ? 1.0 : 1.12));
        n.insight = std::min(500, n.insight + 1);
    } else if (roll < 0.7) {
        n.goal = "游历寻缘";
        n.location = pickOf(g, LOCATIONS()).id;
        gainXp(n, cultivationRateFor(n.stage, n.apt) * 0.3);
        npcHistory(g, n, "游历至" + string(findLoc(n.location)->name) + "。");
        if (findLoc(n.location)->danger > power(n) && randomOf(g) < 0.14) {
            damagePerson(n, randIntOf(g, 12, 30));
            if (n.health <= 0)
                killNpc(g, n, "在" + string(findLoc(n.location)->name) + "遇险陨落");
        }
    } else {
        // 与同地修士互动
        vector<Npc*> others;
        for (auto& x : g.npcs)
            if (x.alive && x.id != n.id && x.location == n.location)
                others.push_back(&x);
        if (!others.empty()) {
            Npc* other = others[static_cast<size_t>(randomOf(g) * others.size())];
            const bool hostile =
                n.personality == "好胜争先" && (n.relations[other->id]) < 0;
            if (hostile) {
                n.goal = "争夺机缘";
                Npc* victor = power(n) > power(*other) ? &n : other;
                Npc* loser = victor == &n ? other : &n;
                const int taken = std::min(15 + group * 6, loser->stones);
                loser->stones -= taken;
                victor->stones += taken;
                damagePerson(*loser, 12);
                n.relations[other->id] = other->relations[n.id] =
                    std::max(-100, n.relations[other->id] - 10);
                npcHistory(g, n, "与" + other->name + "争夺资源，" +
                                 (victor == &n ? "得胜" : "落败") + "。");
                if (loser->health <= 0)
                    killNpc(g, *loser, "在资源争夺中陨落");
            } else {
                n.goal = "同道论道";
                n.insight = std::min(500, n.insight + 2);
                gainXp(n, 5 + group);
                n.relations[other->id] = other->relations[n.id] =
                    clampInt(n.relations[other->id] + 5, -100, 100);
                npcHistory(g, n, "与" + other->name + "论道，交情渐深。");
            }
        } else {
            n.goal = "采药修行";
            const int harvest = std::min(1, g.world.herbs);
            g.world.herbs -= harvest;
            n.herbs += harvest;
            gainXp(n, 6);
        }
    }

    // 知交可能赠予灵石
    if (n.alive && n.location == g.player.location &&
        relationOf(g, n.id) >= 45 && randomOf(g) < 0.08 && n.stones >= 30) {
        n.stones -= 12;
        g.player.stones += 12;
        npcNews(g, n, "知交" + n.name + "赠来 12 枚灵石，资粮来自对方自己的行囊。");
    }

    // 限制关系图规模，保留最强的联系
    if (n.relations.size() > 14) {
        vector<std::pair<string, int>> entries(n.relations.begin(),
                                               n.relations.end());
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) {
                      return std::abs(a.second) > std::abs(b.second);
                  });
        map<string, int> kept;
        for (size_t i = 0; i < entries.size() && i < 10; ++i)
            kept[entries[i].first] = entries[i].second;
        n.relations = kept;
    }
}

// ============================================================
// 人口修剪（对应 prunePopulation）
// ============================================================
static void prunePopulation(Game& g) {
    vector<Npc*> dead;
    for (auto& n : g.npcs)
        if (!n.alive) dead.push_back(&n);
    if (dead.size() <= 180) return;

    std::set<string> retain;
    for (auto* n : dead) {
        if (relationOf(g, n->id) != 0) retain.insert(n->id);
    }
    // 保留最近的 180 位
    std::sort(dead.begin(), dead.end(), [](const Npc* a, const Npc* b) {
        return a->deathYear > b->deathYear;
    });
    for (size_t i = 0; i < dead.size() && i < 180; ++i)
        retain.insert(dead[i]->id);

    std::set<string> removed;
    for (auto* n : dead)
        if (!retain.count(n->id)) removed.insert(n->id);

    vector<Npc> kept;
    for (auto& n : g.npcs)
        if (!removed.count(n.id)) kept.push_back(n);
    g.npcs = std::move(kept);

    // 清理指向已删除 NPC 的关系与纪事
    for (const auto& id : removed) {
        g.player.relations.erase(id);
        g.player.npcBonds.erase(id);
    }
    for (auto& n : g.npcs) {
        for (const auto& id : removed) n.relations.erase(id);
    }
    for (auto& e : g.logs)
        if (e.npcId && removed.count(*e.npcId)) e.npcId.reset();
}

// ============================================================
// 世界年度推进（对应 worldTurn）
// ============================================================
void worldYear(Game& g) {
    World& w = g.world;

    // 兽潮：全图大灾，可能在这一年爆发
    tide::yearTick(g);

    int alive = 0;
    for (const auto& n : g.npcs)
        if (n.alive) ++alive;

    w.herbs = std::min(HERB_CAP, w.herbs + randIntOf(g, 130, 210));
    w.coreStock = std::min(CORE_CAP, w.coreStock + 8);
    const int brewed = std::min({randIntOf(g, 6, 12), w.herbs / 3,
                                 PILL_CAP - w.pillStock});
    w.pillStock += brewed;
    w.herbs -= brewed * 3;

    static const char* weathers[5] = {"春和景明", "灵雨润山", "长风过境",
                                      "薄雾锁江", "丹霞映天"};
    w.weather = weathers[randIntOf(g, 0, 4)];

    for (const auto& se : SECTS()) {
        SectState& d = w.sects[se.id];
        d.treasury += d.members * 2 + (w.mineOwner == se.id ? 60 : 0);
    }

    // 每五年行情起落
    if (g.year % 5 == 0) {
        const bool boom = randomOf(g) > 0.4;
        w.market = boom ? 1.16 : 0.88;
        w.prosperity = clampInt(w.prosperity + (boom ? 4 : -5), 20, 100);
        logEvent(g, "world",
                 boom ? "宁州坊市迎来商潮，灵药与丹药价格上涨，修士纷纷筹措资粮。"
                      : "宁州坊市行情回落，药材与丹药售价下降。修士趁此补给行囊。",
                 true);
    }

    // 每十年重议灵脉归属
    if (g.year % 10 == 0) {
        struct Score { string id; int score; };
        vector<Score> sorted;
        for (const auto& se : SECTS())
            sorted.push_back({se.id, w.sects[se.id].influence + randIntOf(g, -60, 60)});
        std::sort(sorted.begin(), sorted.end(),
                  [](const Score& a, const Score& b) { return a.score > b.score; });
        const string prev = w.mineOwner;
        w.mineOwner = sorted[0].id;
        w.coreStock = std::min(CORE_CAP, w.coreStock + 20);
        logEvent(g, "world",
                 "五宗重议青石灵脉，" + string(findSect(w.mineOwner)->name) +
                     (prev == w.mineOwner
                          ? "保住管领之位"
                          : "接替" + string(findSect(prev)->name) + "掌管矿脉") +
                     "。新开矿层增加了二十枚灵核供给。",
                 true);
    }

    // 补充新修士
    const int births = std::min(20, std::max(0, 720 - alive));
    for (int i = 0; i < births; ++i) {
        Npc n = makeNpc(g, g.npcSeq++, 0);
        n.bornYear = g.year;
        n.isNewborn = true;
        g.npcs.push_back(n);
    }
    if (births)
        logEvent(g, "world",
                 std::to_string(births) +
                     " 位年轻散修踏入宁州。有人拜入山门，有人留在城池，开始各自的一生。");
    // ---- NPC 年度演算 ----
    // 这一段之前一直没接上：npcTurn / prunePopulation 写好了却没人调用，
    // 结果整个天下是静止的 —— NPC 不增龄、不突破、不老死、不游历、不结交，
    // 宗门人数与影响力也常年不变。玩家再怎么修炼，世界毫无回应。
    // 顺序对齐 JS 的 calendarWorldYear：演算 -> 刷新宗门 -> 清理亡者。
    for (auto& n : g.npcs) {
        if (n.alive) npcTurn(g, n);
    }
    refreshSects(g);
    prunePopulation(g);
    // 师尊/道侣/弟子离世后解除名分，否则死人会一直挂在关系里
    conflict::refresh(g);
}

// ============================================================
// 每日刷新
// ============================================================
void refreshDaily(Game& g) {
    ecoRefresh(g);
    socAccrue(g);
}

// ============================================================
// 突破信息（对应 breakthroughInfo）
// ============================================================
BreakthroughInfo breakthroughInfo(const Game& g) {
    const Player& p = g.player;
    const bool major = (p.stage + 1) % 3 == 0;
    const int group = std::min(3, p.stage / 3);

    BreakthroughInfo info;
    info.major = major;
    info.cost = breakthroughCost(g);
    if (major && group == 3)
        info.failureLoss = hasTalent(p, "steady") ? 0.25 : 0.30;
    else
        info.failureLoss = hasTalent(p, "steady") ? 0.15 : 0.20;
    info.ready = p.stage < static_cast<int>(REALMS().size()) - 1 &&
                 p.xp >= realmOf(p.stage).need;
    info.chance = breakthroughChance(g);
    info.next = (p.stage + 1 < static_cast<int>(REALMS().size()))
                    ? realmName(p.stage + 1)
                    : string("已臻化神");
    return info;
}

// ============================================================
// 结算
// ============================================================
void finish(Game& g, const string& cause) {
    if (g.ended) return;
    g.ended = true;
    // 秘境意外死亡不结算临时战利品；是否恢复进度交由结算页选择。
    dungeonAbandon(g);
    g.pending.reset();
    g.job.reset();
    g.player.arts.battle.reset();

    Ending e;
    e.cause = cause;
    e.year = g.year;
    e.age = g.player.age;
    e.realm = realmName(g.player.stage);

    // 统计知交：交情 >= 45 且对方仍在人口表中
    int friends = 0;
    for (const auto& [id, v] : g.player.relations) {
        if (v < 45) continue;
        for (const auto& n : g.npcs)
            if (n.id == id) { ++friends; break; }
    }
    e.friends = friends;
    e.score = g.player.highestStage * 120 + g.player.renown * 4 +
              g.player.insight + g.player.stones / 4;
    g.ending = e;

    logEvent(g, "self",
             g.player.name + "的这一世于 " + std::to_string(g.player.age) +
                 " 岁落笔。" + cause + "，最终境界为" +
                 realmName(g.player.stage) + "。",
             true);
}

// ============================================================
// 待处理抉择（对应 maybeChoice）
// ============================================================
void maybeChoice(Game& g) {
    if (g.pending.has_value() || g.ended || g.year == 0) return;

    auto& flags = g.flags;
    auto it = flags.find("choiceYear");
    if (it != flags.end() && it->second == g.year) return;

    Player& p = g.player;

    // 第 3 年的山门来信
    if (g.year == 3 && p.sect.empty() && !flags.count("invitation")) {
        flags["choiceYear"] = g.year;
        flags["invitation"] = 1;
        Pending ev;
        ev.type = "sectInvitation";
        ev.title = "山门来信";
        ev.subtitle = "机缘 · 宗门";
        ev.text =
            "武陵城传来五宗招收新弟子的消息。散修之路自由，宗门之路有庇护与牵绊。你愿意如何选择？";
        ev.options = {
            {"join", "赴金虹山门", "收到考核邀请 · 需赶往山门报名实战试炼", false},
            {"study", "先访各宗，再作打算", "悟性最多 +8 · 可稍后前往山门报名考核", false},
            {"free", "暂作散修，自寻前路", "灵石 +20 · 保持散修身份", false},
        };
        g.pending = ev;
        return;
    }

    // 每逢 9 的倍数年，或游历时的小概率事件
    if (g.year % 9 == 0 || (g.plan == "explore" && randomOf(g) < 0.02)) {
        flags["choiceYear"] = g.year;

        Npc* near = nullptr;
        for (auto& n : g.npcs)
            if (n.alive && n.location == p.location) { near = &n; break; }

        Pending ev;
        ev.type = "valley";
        if (near) ev.npcId = near->id;
        ev.title = near ? "药谷中的求援" : "山间遗府";
        ev.subtitle = "抉择 · 风险与机缘";
        ev.text = near
            ? "游历途中，" + near->name +
                  "困于药谷禁制。你可以合力破阵，也可以独自采走附近的灵药。"
            : "旧日遗府的石门半掩，里面灵气未散。但阵法仍在运转，贸然进入或将受伤。";
        ev.options = {
            {"help", near ? "合力救援" : "谨慎探阵",
             string(near ? "好感最多 +20 · " : "") + "灵药最多 +3 · 有机会受伤", false},
            {"take", "采药后离开",
             string("灵药最多 +5") + (near ? " · 好感 −15" : ""), false},
            {"leave", "记下方位，保全自身", "悟性最多 +5 · 无风险", false},
        };
        g.pending = ev;
    }
}

// ============================================================
// 新游戏（对应 createGame）
// ============================================================
Game createGame(const CreateOptions& opts) {
    Game g;
    g.seed = opts.seed;
    g.rng = opts.seed;
    g.lifeId = opts.lifeId.empty()
                   ? ("life-" + std::to_string(opts.seed))
                   : opts.lifeId;

    // 天赋与属性
    vector<string> traits = opts.traits;
    // 只有调用方没明确给天赋时，才用预设补一个。
    // 建号面板允许「一个天赋都不选、点数全给属性」，
    // 这时不能再偷偷塞一个默认天赋 —— 否则预算会超，
    // buildCharacter 抛异常，建号会静默退回默认值。
    if (traits.empty() && !opts.traitsExplicit) {
        const TalentDef* preset = findTalent(opts.talent);
        traits.push_back(preset ? preset->id : TALENTS()[0].id);
    }
    map<string, int> alloc = opts.allocation;
    if (alloc.empty()) {
        int cost = 0;
        for (const auto& t : traits) {
            const TalentDef* td = findTalent(t);
            if (td) cost += td->cost;
        }
        const int apt = std::min(10, std::max(0, opts.budget - cost));
        if (apt > 0) alloc["apt"] = apt;
    }
    // buildCharacter 对非法入参会抛异常。建号是程序入口，不该因为
    // 一个配点错误就整个崩掉，所以这里退回「只用第一个天赋」重试一次。
    Build build;
    try {
        build = buildCharacter(traits, alloc, opts.budget, opts.seed);
    } catch (const std::invalid_argument&) {
        traits = {traits.empty() ? TALENTS()[0].id : traits[0]};
        build = buildCharacter(traits, {}, opts.budget, opts.seed);
    }

    // 玩家
    Player& p = g.player;
    // 去掉尖括号，并按字符（不是字节）截断到 12 个字
    string clean;
    {
        const string& name = opts.name;
        size_t i = 0;
        int chars = 0;
        while (i < name.size() && chars < 12) {
            const unsigned char c = static_cast<unsigned char>(name[i]);
            size_t len = 1;
            if (c >= 0xF0) len = 4;
            else if (c >= 0xE0) len = 3;
            else if (c >= 0xC0) len = 2;
            const string piece = name.substr(i, len);
            if (piece != "<" && piece != ">") {
                clean += piece;
                ++chars;
            }
            i += len;
        }
    }
    p.name = clean.empty() ? "沈知微" : clean;
    p.gender = (opts.gender == "男") ? "男" : "女";
    p.talent = build.traits.empty() ? "plain" : build.traits[0];
    p.traits = build.traits;
    p.build = build;
    p.age = 16;
    p.lifespan = 100 + build.lifeBonus;
    p.stage = 0;
    p.xp = 0;
    p.apt = build.apt;
    p.luck = build.luck;
    p.insight = build.insight;
    p.sword = build.sword;
    p.alchemy = build.alchemy;
    p.health = 100;
    p.stones = build.stones;
    p.herbs = build.herbs;
    p.pills = 0;
    p.cores = 0;
    p.sect.clear();
    p.location = "wuling";
    p.renown = 0;
    p.highestStage = 0;
    p.body = build.body;
    p.lifeBonus = build.lifeBonus;

    // 世界：基线先摆好，再交给种子做差异化
    g.world.prosperity = 72;
    g.world.market = 1.0;
    g.world.mineOwner = "jinhong";
    g.world.herbs = 3200;
    g.world.coreStock = 120;
    g.world.pillStock = 90;
    g.world.weather = "春和景明";
    for (const auto& s : SECTS())
        g.world.sects[s.id] = SectState{3000, 50, 0};
    // 地图种子：同一颗种子必得同一个宁州
    worldFromSeed(g);

    // 天下修士：低阶多、高阶少
    for (int group = 0; group < 5; ++group) {
        for (int i = 0; i < POPULATION[group]; ++i) {
            const int st = (group == 4) ? 12
                                        : randIntOf(g, group * 3, group * 3 + 2);
            g.npcs.push_back(makeNpc(g, g.npcSeq++, st));
        }
    }

    // 开局身边有两个人，但并非所有人都在等玩家
    g.npcs[0].location = "wuling";
    g.npcs[0].stage = 0;
    g.npcs[0].age = 21;
    g.npcs[0].lifespan = 100;
    g.npcs[0].xp = 25;
    g.npcs[1].location = "wuling";
    g.npcs[1].personality = "重情重义";

    // 随机关系网
    const size_t n = g.npcs.size();
    for (size_t i = 0; i < n; ++i) {
        const int v = randIntOf(g, -20, 45);
        g.npcs[i].relations[g.npcs[(i + 7) % n].id] = v;
        g.npcs[(i + 7) % n].relations[g.npcs[i].id] = v;
    }

    refreshSects(g);
    ensureArts(g);
    artGain(p, "剑", std::max(0, std::max(0, build.sword - 2) * 2 - (hasTalent(p, "gold") ? 6 : 0)));
    if (hasTalent(p, "gold")) artGain(p, "金", 6);
    ensureCalendar(g);
    ensureEconomy(g);
    ensureSociety(g);
    ensureAdventure(g);
    ensureDungeons(g);
    socInitPayroll(g);
    // 五项开局天赋会发放实际物品（法器/丹炉/材料）
    adventureBirth(g);
    talentBirth(g);

    logEvent(g, "self",
             "十六岁，你离开故乡，在武陵城落脚。行囊里是 " +
                 std::to_string(g.player.stones) +
                 " 枚灵石，心里是一个长生的念头。",
             true);
    // 开局神通是自动装备的。之前这件事只在数据里发生、没有任何提示，
    // 玩家进游戏后不知道自己的斗法牌组里有什么，看着像「没有神通」。
    {
        string names;
        for (const auto& id : g.player.arts.spells) {
            const SpellDef* sp = battleSpell(id);
            if (!names.empty()) names += "、";
            names += (sp ? sp->name : id);
        }
        logEvent(g, "self",
                 "已自动装备神通：" + names +
                     "。可在「洞府 · 道法」的携带神通里更换（最多四个）。",
                 true);
    }
    logEvent(g, "world",
             "青石灵脉暂由金虹剑派管领。灵脉产出会进入宗门府库，也影响坊市灵核供应。",
             true);
    logEvent(g, "npc",
             g.npcs[0].name + "与你同住城中客栈；" + g.npcs[1].name +
                 "刚结束一段游历。你可以在「同道」中结识他们。",
             false, g.npcs[0].id);
    logEvent(g, "sim",
             "每月30天，安排短行动可以在同月做多次。默认计划执行至下月；长工作分月继续。");
    return g;
}

// ============================================================
// 营生类行动：这些 actionType 由 economy.cpp 的 ecoComplete 负责结算
// ============================================================
// ecoComplete 早就写好了，但 monthComplete 从来没有把营生类工作交给它，
// 于是「主城接取战斗任务」这类行动到期后什么都没发生：
//   猎妖委托不开打、山门差事不发工钱、委托不交工、炼丹炼器不出货。
// 这里列出全部交给 economy 的行动类型，逐个转交。
static bool isEcoJob(const string& type) {
    return type == "task" || type == "commission" || type == "hunt" ||
           type == "mine" || type == "arena" || type == "quest" ||
           type == "brew" || type == "forge" || type == "learn" ||
           type == "meditate";
}

// ============================================================
// 在途工作完成后的结算（对应 monthComplete）
// ============================================================
void monthComplete(Game& g, const Job& job) {
    if (g.ended) return;
    Player& p = g.player;
    Arts& a = p.arts;
    const string& type = job.actionType;
    const int total = job.total;
    if (type == "story") {
        for (const auto& story : data::StoryDef_TABLE()) {
            if (story.id != job.plan) continue;
            const int choice = job.kind == "0" ? 0 : job.kind == "1" ? 1 : -1;
            if (choice >= 0 && total == story.days && job.origin == p.location &&
                storyError(g, &story).empty() && storyChoiceError(g, story, choice).empty())
                storyComplete(g, story, choice);
            else
                logEvent(g, "self", "因缘「" + story.name + "」的条件已变化，未结算奖励。", true);
            return;
        }
        logEvent(g, "self", "在途因缘不存在，未结算奖励。", true);
        return;
    }
    if (type == "npcLearn") { npcLearnComplete(g, job); return; }
    if (type == "locationService") { locationServiceComplete(g, job); return; }

    auto gainXpP = [&](double n) {
        const int need = realmOf(p.stage).need;
        p.xp = std::min(need, p.xp + calendarTake(g, "xp", std::max(0.0, n)));
    };

    // 营生类行动（含主城的战斗任务「猎妖委托」）统一交给 ecoComplete。
    // 必须放在最前面：ecoComplete 里 hunt 分支会 battleStart，
    // 后面的通用收尾不该再对这场战斗做任何假设。
    if (isEcoJob(type)) {
        ecoComplete(g, job);
        return;
    }

    // 「探访后山」完成后直接触发遗藏遇事
    if (type == "expedition") {
        adventureComplete(g);
        return;
    }
    // 关系往来（拜师 / 授业 / 收徒 / 道侣）
    if (type == "bond") {
        const Npc* npc = nullptr;
        string target = job.npcId;
        if (job.kind == "lesson") target = g.bonds.master;
        if (job.kind == "together") target = g.bonds.partner;
        for (const auto& n : g.npcs)
            if (n.id == target && n.alive && n.location == p.location) { npc = &n; break; }
        const bool requiresNpc = job.kind == "master" || job.kind == "disciple" ||
                                 job.kind == "partner" || job.kind == "lesson" ||
                                 job.kind == "together";
        bool valid = !requiresNpc || npc;
        if (job.kind == "master") valid = valid && g.bonds.master.empty();
        if (job.kind == "partner") valid = valid && g.bonds.partner.empty();
        if (job.kind == "disciple") {
            valid = valid && bondCanTakeDisciple(g) &&
                    std::find(g.bonds.disciples.begin(), g.bonds.disciples.end(), target) ==
                        g.bonds.disciples.end();
        }
        if (job.kind == "teach") {
            valid = false;
            for (const auto& id : g.bonds.disciples)
                for (const auto& n : g.npcs)
                    if (n.id == id && n.alive && n.location == p.location) valid = true;
        }
        if (!valid) {
            logEvent(g, "self", "约定的同道已离开或关系发生变化，此次往来未能完成。");
            return;
        }
        societyBondComplete(g, job.kind, npc);
        return;
    }
    // 野外悬赏：完成后进入斗法
    if (type == "wild") {
        societyWildStart(g, job);
        return;
    }

    if (type == "plan") {
        const double ratio = total / 360.0;
        const double month = total / 30.0;
        const LocationDef* here = findLoc(p.location);
        // 配给只在闭关分支按实际用量收费；其它行动的零散修为
        // 使用基础速度，不能仅因开启配给就免费获得加成。
        const double annual = cultivate::baseRate(p);
        int growth = 0;

        if (job.plan == "cultivate") {
            // 配给开销：按实际闭关天数比例折算（一个月为一期）。
            // 灵气只在洞府生效，不在洞府就只算灵石那份，也不收灵气的钱。
            //
            // 注意：这里必须按天比例收。之前写成 max(1, total/30)，
            // 结果闭关 1 天也收满一个月的配给费，玩家一推进就破产。
            const double monthsF = total / 30.0;
            int effectiveStone = std::min(p.stoneSupply, cultivate::maxSupplyLevel());
            int effectiveQi = cultivate::qiActive(p) ? std::min(p.qiSupply,cultivate::unlockedSupplyLevel(p)) : 0;
            bool shortOnSupply = false;

            auto supplyAmount = [&](int st, int qi) {
                return (cultivate::supplyCost(p, "stone", st) +
                        cultivate::supplyCost(p, "qi", qi)) * monthsF;
            };
            auto required = [&](int st, int qi) {
                // 剩余小数也是已使用的资源，必须有余额覆盖，不能拆成
                // 多次一天行动让每次账单四舍五入成零。
                return static_cast<int>(std::ceil(g.clock.carry["supplyCost"] +
                                                   supplyAmount(st, qi) - 1e-9));
            };
            if (required(effectiveStone, effectiveQi) > p.stones) {
                shortOnSupply = true;
                while (effectiveQi > 0 && required(effectiveStone, effectiveQi) > p.stones)
                    --effectiveQi;
                while (effectiveStone > 0 && required(effectiveStone, effectiveQi) > p.stones)
                    --effectiveStone;
            }
            const int supplyBill = calendarTake(g, "supplyCost",
                                               supplyAmount(effectiveStone, effectiveQi));
            p.stones = std::max(0, p.stones - supplyBill);

            // 本次闭关实际生效的配给乘数
            const double supplyNow =
                (1.0 + effectiveStone * 0.12) * (1.0 + effectiveQi * 0.30) * cultivate::caveMul(p);

            // 灵石不足时只有一半效果；带伤修炼也打折
            double mult = (p.sect.empty() ? 1.0 : 1.16) *
                          (job.paid ? 1.0 : 0.5) *
                          (p.health < 50 ? 0.6 : 1.0) *
                          ((p.sect.empty() && hasTalent(p, "solitary")) ? 1.08 : 1.0);
            // 用不含配给的基础速度，乘上本次实际生效的配给
            const double baseAnnual = cultivate::baseRate(p);
            growth = calendarTake(g, "xp", baseAnnual * supplyNow * ratio * mult);
            gainXpP(growth);
            p.insight = std::min(INSIGHT_MAX, p.insight +
                                          calendarTake(g, "insight", ratio));
            if (p.sect == "jinhong")
                swordGain(g, calendarTake(g, "sword", ratio * 2));

            std::string note = "修为 +" + std::to_string(growth);
            if (supplyBill > 0)
                note += " · 配给 −" + std::to_string(supplyBill) + " 灵石";
            if (shortOnSupply)
                note += "（灵石不足，配给已降到负担得起的档位）";
            if (p.qiSupply > 0 && !cultivate::qiActive(p))
                note += " · 不在洞府，灵气未生效";
            logEvent(g, "self",
                     "闭关" + std::to_string(total) + "天，" +
                         (job.paid ? "灵气充足" : "灵石不足，修炼减半") + "。",
                     false, "", note);
        } else if (job.plan == "work") {
            const int income = calendarTake(g, "work", randIntOf(g, 25, 37) * g.world.market *
                                       (hasTalent(p, "merchant") ? 1.18 : 1.0) *
                                       month);
            p.stones += income;
            gainXpP(annual * ratio * 0.18);
            logEvent(g, "self",
                     "在" + string(here->name) + "营生" +
                         std::to_string(total) + "天，收入 " +
                         std::to_string(income) + " 灵石。");
        } else if (job.plan == "gather") {
            int perDay = randIntOf(g, 2, 4) +
                         (string(here->type) == "wild" ? 2 : 0) +
                         (p.sect == "zhushan" ? 2 : 0) +
                         (artHas(a, "丹", 1) ? 1 : 0);
            const int found = std::min(
                calendarTake(g, "gather", static_cast<double>(perDay) * month),
                g.world.herbs);
            p.herbs += found;
            g.world.herbs -= found;
            // 按品阶上限随机分类到具体药材
            const int tier = ordinaryHerbGradeLimit(p);
            vector<const data::HerbDef*> pool;
            for (const auto& h : data::HerbDef_TABLE())
                if (h.grade <= tier &&
                    h.name.find("妖丹") == string::npos)
                    pool.push_back(&h);
            for (int i = 0; i < found && !pool.empty(); ++i) {
                const data::HerbDef* h = pickOf(g, pool);
                a.herbs[h->name]++;
            }
            gainXpP(annual * ratio * 0.3);
            if (here->danger > power(p) && randomOf(g) < 0.12 * month)
                applyDamage(g, 14);
            logEvent(g, "self",
                     "采药" + std::to_string(total) + "天，收获 " +
                         std::to_string(found) + " 份指定药材。",
                     false, "", "灵药 +" + std::to_string(found));
        } else if (job.plan == "rest") {
            const int restored = std::min(
                100 - p.health,
                calendarTake(g, "rest", (20 + (hasTalent(p, "body") ? 8 : 0)) *
                        static_cast<double>(month)));
            p.health += restored;
            gainXpP(annual * ratio * 0.2);
            logEvent(g, "self", "休养" + std::to_string(total) + "天，气血 +" +
                                    std::to_string(restored) + "。");
        } else if (job.plan == "explore") {
            gainXpP(annual * ratio * 0.4);
            p.insight = std::min(INSIGHT_MAX, p.insight +
                                          calendarTake(g, "insight", ratio * 2));
            // 游历给的是「涓流」：你在各处露过面，别人慢慢记住你。
            // 原版是一年 +1，200 年下来能到 188 —— 比突破、行侠、
            // 悬赏这些真正的「事迹」加起来还多，完全压过了它们。
            // 这里降到 0.4/年，让事迹成为声望的主要来源。
            p.renown += calendarTake(g, "renown", ratio * 0.4);
            const double risk =
                std::max(0.005, (a.repel ? 0.01 : 0.04) -
                                    clamp((p.luck - 45) * 0.001, -0.02, 0.03));
            if (here->danger > power(p) && randomOf(g) < risk * month) {
                applyDamage(g, randIntOf(g, 12, 30));
            } else {
                p.stones += calendarTake(g, "exploreStones", randIntOf(g, 8, 25) * month);
            }
            if (randomOf(g) <
                (0.04 + clamp((p.luck - 45) * 0.001, -0.03, 0.08)) * month)
                p.stones += 6;
            logEvent(g, "self", "游历" + string(here->name) + "周边" +
                                    std::to_string(total) +
                                    "天，积累修为与见闻。");
            // 游历要真的触发随机遇事。
            // adventureExplore 早就写好了却一直没人调用，
            // 结果「游历寻缘」只剩数值增长，遇不到任何事。
            if (p.health > 0) adventureExplore(g, total);
        }

        // 修炼/游历/休养都能积累主修大道的经验
        if (job.plan == "cultivate" || job.plan == "explore" ||
            job.plan == "rest") {
            const double daoRate = (job.plan == "rest") ? 1.0 : 3.0;
            artGain(p, a.focus, calendarTake(g, "dao", ratio * daoRate));
        }

    } else if (type == "breakthrough") {
        BreakthroughInfo b = breakthroughInfo(g);
        if (job.breakthroughPrepared) {
            b.major = job.breakthroughMajor;
            b.chance = job.breakthroughChance;
            b.failureLoss = job.breakthroughFailureLoss;
        }
        if (!b.ready) return;
        if (randomOf(g) < b.chance) {
            p.stage++;
            p.highestStage = std::max(p.highestStage, p.stage);
            p.xp = 0;
            g.clock.carry["xp"] = 0;
            p.lifespan = realmOf(p.stage).life + p.lifeBonus;
            p.health = std::min(100, p.health + 15);
            p.renown += b.major ? 12 : 3;
            if (b.major) p.achievements.emplace_back(realmName(p.stage), g.year);
            talentBreakthrough(g, b.major);
            logEvent(g, "self",
                     "闭关" + std::to_string(total) + "天，突破至" +
                         realmName(p.stage) + "，寿元上限 " +
                         std::to_string(p.lifespan) + " 岁。",
                     true);
        } else {
            p.xp = jsFloor(p.xp * (1.0 - b.failureLoss));
            applyDamage(g, b.major ? 18 : 8);
            p.health = std::max(1, p.health);
            logEvent(g, "self",
                     "闭关" + std::to_string(total) + "天，冲关未成，损失 " +
                         std::to_string(jsRound(b.failureLoss * 100)) + "% 修为。",
                     true);
        }
    } else if (type == "travel") {
        if (!findLoc(job.destination) || (!job.origin.empty() && p.location != job.origin)) {
            logEvent(g, "self", "赶路安排的出发地或目的地已失效，未抵达目的地。", true);
            return;
        }
        p.location = job.destination;
        gainXpP(cultivate::baseRate(p) * total / 360.0 * 0.2);
        logEvent(g, "self", "赶路" + std::to_string(total) + "天，抵达" +
                                string(findLoc(p.location)->name) + "。");
    } else if (type == "join") {
        sectTrialLegacyComplete(g, job);
    } else if (type == "social") {
        // 兼容旧版在途安排，共用即时往来的限制与月内计数。
        // 此处只结算成果；已支付的费用不能被解释成另一次收费请教。
        const string blocked = friendship::interactionError(g, job.npcId);
        if (!blocked.empty() || !job.paid || job.cost < 0 || total <= 0) {
            logEvent(g, "self", "此次约见未能完成：" +
                     (blocked.empty() ? string("约见安排参数无效。") : blocked));
            return;
        }
        if (job.kind == "gift") {
            const auto result = friendship::giftHerbs(g, job.npcId, job.gift);
            if (!result.ok) {
                logEvent(g, "self", "此次赠药未能完成：" + result.error);
                return;
            }
        } else if (job.kind == "talk") {
            const auto result = play::socialDo(g, job.npcId, "chat");
            if (!result.ok) {
                logEvent(g, "self", "此次论道未能完成：" + result.error);
                return;
            }
        } else {
            logEvent(g, "self", "此次约见安排的往来种类无效，未结算收益。");
            return;
        }
    }

    artSyncHerbs(p);
    a.progress.wealth = std::max(a.progress.wealth, p.stones);

}

} // namespace nz
