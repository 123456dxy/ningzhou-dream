// 宁州一梦 · C++ 版 — 野外冲突（实现）
#include "core/conflict.h"

#include <algorithm>

#include "core/arts.h"
#include "core/battle.h"
#include "core/economy.h"
#include "core/sim.h"
#include "core/society.h"
#include "data/reference_tables.h"

namespace nz::conflict {

using std::string;

namespace {

Npc* findNpc(Game& g, const string& id) {
    for (auto& n : g.npcs)
        if (n.id == id) return &n;
    return nullptr;
}

int playerRelation(const Game& g, const string& id) {
    auto it = g.player.relations.find(id);
    return it == g.player.relations.end() ? 0 : it->second;
}

void setRelation(Game& g, const string& id, int v) {
    g.player.relations[id] = std::max(-100, std::min(100, v));
}

bool isWildLocation(const string& loc) {
    return loc == "fanghu" || loc == "qingshi" || loc == "sea";
}

bool isBonded(const Game& g, const string& id) {
    const Bonds& b = g.bonds;
    if (b.master == id || b.partner == id) return true;
    return std::find(b.disciples.begin(), b.disciples.end(), id) !=
           b.disciples.end();
}

} // namespace

// ============================================================
// 对方反应（对应 socResponse）
// ============================================================
string response(const Game& g, const Npc& n, const string& intent) {
    const int diff = (g.player.stage / 3) - (n.stage / 3);
    const int sub = g.player.stage - n.stage;
    const bool brave = n.personality == "好胜争先";
    const bool loyal = n.personality == "重情重义";

    if (intent == "kill")
        return (diff >= 1 && !brave) ? "拼命逃离" : "决死反击";
    if (diff >= 2) return "交出财物";
    if (diff >= 1 && !brave) return "交出财物";
    if (sub > 0 && n.personality == "逐利而行") return "交出财物";
    if (diff < 0 || brave) return "反击";
    return loyal ? "反击" : "试图逃离";
}

// ============================================================
// 掠夺（对应 socLoot）
// ============================================================
void loot(Game& g, Npc& n, double ratio) {
    const int coins = std::min({n.stones,
                                static_cast<int>(n.stones * ratio),
                                80 + (n.stage / 3) * 120});
    const int herbs = std::min(n.herbs, static_cast<int>(n.herbs * ratio));
    n.stones -= coins;
    n.herbs -= herbs;
    g.player.stones += coins;
    g.player.herbs += herbs;
    logEvent(g, "self",
             "从" + n.name + "处夺得 " + std::to_string(coins) + " 灵石与 " +
                 std::to_string(herbs) + " 份未分类灵药，来自对方真实行囊。",
             true, n.id);
}

// ============================================================
// 犯罪后果（对应 socCrime）
// ============================================================
void crime(Game& g, Npc& n, const string& intent) {
    Bonds& b = g.bonds;
    Player& p = g.player;
    const bool kill = (intent == "kill");

    b.notoriety = std::min(100, b.notoriety + (kill ? 20 : 8));
    p.renown = std::max(0, p.renown - (kill ? 10 : 4));
    setRelation(g, n.id, playerRelation(g, n.id) - 40);

    if (!p.sect.empty()) {
        g.economy.contribution = std::max(0, g.economy.contribution - 15);
        const int fine = std::min(p.stones, kill ? 40 : 15);
        p.stones -= fine;
        // sects 是 map<string, SectState>，按宗门 id 取
        auto sit = g.world.sects.find(p.sect);
        if (sit != g.world.sects.end()) sit->second.treasury += fine;
        logEvent(g, "sect",
                 "山门因野外争端扣贡献 15、罚款 " + std::to_string(fine) +
                     " 灵石，恶名 " + std::to_string(b.notoriety) + "。",
                 true);
    }

    // 牵连其友人：与目标交情 ≥30 的人也会记恨你
    for (const auto& [id, v] : n.relations) {
        if (v < 30) continue;
        setRelation(g, id, playerRelation(g, id) - 15);
    }
}

// ============================================================
// 校验
// ============================================================
string conflictError(const Game& g, const string& npcId,
                     const string& intent) {
    if (intent != "rob" && intent != "kill") return "未知冲突方式。";
    if (g.ended) return "这一世已经结束。";
    if (g.pending.has_value()) return "请先完成当前事件。";
    if (g.player.arts.battle.has_value()) return "斗法中无法另起冲突。";

    const Player& p = g.player;
    if (!isWildLocation(p.location))
        return "城池与山门不能发起野外冲突，请前往方壶山、青石灵脉或无尽海。";

    const Npc* n = nullptr;
    for (const auto& x : g.npcs)
        if (x.id == npcId) { n = &x; break; }
    if (!n || !n->alive) return "此地未遇到该修士。";
    if (n->location != p.location) return "对方不在此地。";
    if (isBonded(g, npcId)) return "须先解除师徒或道侣关系。";
    if (p.health < 35) return "伤势过重，先休养。";
    return "";
}

// ============================================================
// 执行
// ============================================================
ConflictResult doConflict(Game& g, const string& npcId,
                          const string& intent) {
    ConflictResult r;
    const string err = conflictError(g, npcId, intent);
    if (!err.empty()) {
        r.error = err;
        return r;
    }

    Npc* n = findNpc(g, npcId);
    if (!n) {
        r.error = "此地未遇到该修士。";
        return r;
    }

    const string resp = response(g, *n, intent);
    r.responseText = resp;
    const bool kill = (intent == "kill");
    const string actName = kill ? "野外截杀" : "拦路索财";

    // ---- 对方服软：直接夺取 ----
    if (resp == "交出财物") {
        loot(g, *n, kill ? 0.6 : 0.35);
        crime(g, *n, intent);
        r.ok = true;
        r.lootStones = 0;   // 具体数字在日志里
        r.notoriety = g.bonds.notoriety;
        r.text = actName + "：" + n->name + "交出了财物。";
        return r;
    }

    // ---- 对方逃离 ----
    if (resp == "拼命逃离" || resp == "试图逃离") {
        const double escapeChance = (resp == "拼命逃离") ? 0.75 : 0.5;
        if (randomOf(g) < escapeChance) {
            r.ok = true;
            r.fled = true;
            r.text = actName + "：" + n->name + "脱身而去，未能得手。";
            logEvent(g, "self", r.text, true, n->id);
            return r;
        }
        // 没逃掉：照样夺财
        loot(g, *n, kill ? 0.6 : 0.35);
        crime(g, *n, intent);
        r.ok = true;
        r.notoriety = g.bonds.notoriety;
        r.text = actName + "：" + n->name + "试图逃离，被截了下来。";
        return r;
    }

    // ---- 对方反击：进入斗法 ----
    r.ok = true;
    r.fought = true;
    r.text = actName + "：" + n->name + "决意反击，斗法开始。";

    EnemySpec enemy;
    enemy.name = n->name;
    enemy.stage = n->stage;
    enemy.kind = kill ? "kill" : "rob";
    enemy.lethal = kill;      // 截杀是生死斗
    enemy.id = n->id;
    battleStart(g, enemy);

    // 犯罪后果在开战时先记账（JS 也是出手即记）
    crime(g, *n, intent);
    r.notoriety = g.bonds.notoriety;

    logEvent(g, "self", r.text, true, n->id);
    return r;
}

// ============================================================
// 恶名说明
// ============================================================
void battleEnd(Game& g, const Battle& b) {
    if (!b.won || (b.kind != "rob" && b.kind != "kill")) return;
    Npc* n = findNpc(g, b.npcId);
    if (!n || !n->alive) return;
    loot(g, *n, b.kind == "kill" ? 0.6 : 0.35);
    if (b.kind == "kill") {
        npcDies(g, *n, "殁于" + g.player.name + "的截杀");
        refresh(g);
    }
}

string notorietyNote(const Game& g) {
    const int n = g.bonds.notoriety;
    if (n <= 0) return "";
    if (n >= 40)
        return "恶名 " + std::to_string(n) +
               "：宗门已暂停发放俸禄，需行善或缴纳罚金方可恢复。";
    return "恶名 " + std::to_string(n) + "：尚在可控范围，达到 40 会暂停俸禄。";
}

// ============================================================
// 关系清理（对应 socRefresh）
// ============================================================
void refresh(Game& g) {
    Bonds& b = g.bonds;
    auto dead = [&](const string& id) {
        for (const auto& n : g.npcs)
            if (n.id == id) return !n.alive;
        return true;   // 找不到也算离世
    };

    if (!b.master.empty() && dead(b.master)) {
        logEvent(g, "sect",
                 "师尊已坐化，师徒名分随之而解。旧日因缘留在旧卷。", true);
        b.master.clear();
        b.lessons = 0;
    }
    if (!b.partner.empty() && dead(b.partner)) {
        logEvent(g, "self", "道侣已离世，此约自然消解。", true);
        b.partner.clear();
    }
    for (auto it = b.disciples.begin(); it != b.disciples.end();) {
        if (dead(*it)) {
            logEvent(g, "sect", "一位弟子已离世，名录中划去。", true);
            it = b.disciples.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace nz::conflict
