#include "core/npc_learning.h"
#include <algorithm>
#include <cstdint>
#include <set>
#include "core/arts.h"
#include "core/battle.h"
#include "core/friendship.h"
#include "core/play.h"
#include "core/sim.h"
namespace nz {
namespace {
const Npc* teacherOf(const Game& g, const string& id) {
    for (const auto& npc : g.npcs) if (npc.id == id) return &npc;
    return nullptr;
}

uint32_t knowledgeHash(const string& teacher, const string& kind, const string& id) {
    uint32_t value = 2166136261u;
    for (const unsigned char c : teacher + ":" + kind + ":" + id) {
        value ^= c; value *= 16777619u;
    }
    return value;
}

// 知识不等同于本场携带槽位；保留实装构筑，补充本门大道的稳定传承。
vector<NpcLesson> knowledgeOf(const Npc& npc) {
    vector<NpcLesson> result;
    if (npc.beast) return result;
    const Arts carried = artNpcLoadout(npc);
    const string dao = daoIndex(carried.focus) >= 0 ? carried.focus : npc.dao;
    std::set<string> seen;
    const auto addSpell = [&](const string& id) {
        const auto* sp = battleSpell(id);
        if (!sp || sp->stage > npc.stage || id == "strike" || id == "defend" ||
            !seen.insert("spell:" + id).second) return;
        NpcLesson lesson; lesson.kind = "spell"; lesson.id = id; lesson.name = sp->name;
        lesson.dao = sp->dao; lesson.stage = sp->stage;
        lesson.creditCost = 8 + sp->stage * 3 + sp->cost * 2;
        result.push_back(std::move(lesson));
    };
    const auto addManual = [&](const string& id) {
        ManualInfo info;
        if (!findManualInfo(id, info) || info.stage > npc.stage ||
            !seen.insert("manual:" + id).second) return;
        NpcLesson lesson; lesson.kind = "manual"; lesson.id = id; lesson.name = info.name;
        lesson.dao = info.dao; lesson.stage = info.stage;
        lesson.creditCost = std::max(6, friendship::creditFromValue(info.price) * 9 / 10);
        result.push_back(std::move(lesson));
    };
    for (const auto& id : carried.spells) addSpell(id);
    for (const auto& [id, level] : carried.manuals) if (level > 0) addManual(id);

    int highestSpell = 0;
    for (const auto& sp : allSpells())
        if (sp.dao == dao && sp.stage <= npc.stage) highestSpell = std::max(highestSpell, sp.stage);
    for (const auto& sp : allSpells()) {
        if (sp.dao != dao || sp.stage > npc.stage) continue;
        if (sp.stage == 0 || sp.stage == highestSpell ||
            knowledgeHash(npc.id, "spell", sp.id) % 3 != 0) addSpell(sp.id);
    }
    std::set<string> manuals;
    for (const auto& info : data::BaseManualDef_TABLE()) manuals.insert(info.id);
    for (const auto& info : data::ManualDef_TABLE()) manuals.insert(info.id);
    int highestManual = 0;
    vector<string> matching;
    for (const auto& id : manuals) {
        ManualInfo info;
        if (!findManualInfo(id, info) || info.dao != dao || info.stage > npc.stage) continue;
        bool otherSect = false;
        for (const auto& entry : data::ManualDef_TABLE())
            if (entry.id == id && findSect(entry.source) && entry.source != npc.sect) otherSect = true;
        if (otherSect) continue;
        highestManual = std::max(highestManual, info.stage); matching.push_back(id);
    }
    for (const auto& id : matching) {
        ManualInfo info; findManualInfo(id, info);
        if (info.stage == 0 || info.stage == highestManual ||
            knowledgeHash(npc.id, "manual", id) % 3 != 0) addManual(id);
    }
    for (auto& lesson : result) {
        lesson.days = 2 + lesson.stage / 3;
        lesson.creditCost = std::max(6, lesson.creditCost);
        lesson.stoneCost = 3 + lesson.stage * 2;
    }
    return result;
}

string settlementKey(const Job& job) {
    return "npcLearnSettled:" + job.npcId + ":" + job.kind + ":" + job.plan + ":" + std::to_string(job.started);
}

void refundLesson(Game& g, const Job& job, const string& reason) {
    if (job.paid) {
        g.player.stones = static_cast<int>(std::min(1000000000LL,
            static_cast<long long>(g.player.stones) + std::max(0, job.cost)));
        auto& bond = g.player.npcBonds[job.npcId];
        const int credit = std::min(std::max(0, job.gift), bond.spent);
        friendship::addCredit(g, job.npcId, credit);
        bond.spent = std::max(0, bond.spent - credit);
        if (bond.learnMonth == job.started / 30)
            bond.learnedThisMonth = std::max(0, bond.learnedThisMonth - 1);
    }
    const auto* teacher = teacherOf(g, job.npcId);
    logEvent(g, "npc", (teacher ? teacher->name : string("道友")) + "的授业中断：" + reason +
        "已付学费与交情已退还。", false, job.npcId);
}
} // namespace

string personPortrait(const string& gender, const string& sect, bool player) {
    const string root = "/assets/portraits/";
    if (player) return root + (gender == "女" ? "player-female.webp" : "player-male.webp");
    if (sect.empty() || !findSect(sect)) return root + (gender == "女" ? "rogue-female.webp" : "rogue.webp");
    const bool femaleDefault = sect == "xinghe" || sect == "zhushan";
    return root + "sect-" + sect + ((gender == "女") == femaleDefault ? "" : (gender == "女" ? "-female" : "-male")) + ".webp";
}
string beastPortrait(int stage) {
    return "/assets/portraits/beast-" + std::to_string(clampInt(stage / 3, 0, 4)) + ".webp";
}
vector<NpcLesson> npcLessons(const Game& g, const string& npcId) {
    vector<NpcLesson> result;
    const Npc* n = teacherOf(g, npcId);
    if (!n || n->beast) return result;
    const Player& p = g.player;
    auto add = [&](NpcLesson lesson) {
        lesson.affinityNeed = (lesson.stage >= 6 ? 50 : lesson.stage >= 3 ? 35 : 20);
        if (!p.sect.empty() && p.sect == n->sect) lesson.affinityNeed -= 10;
        if (g.bonds.master == npcId) lesson.affinityNeed = 0;
        auto bi = p.npcBonds.find(npcId);
        if (p.name.empty()) lesson.reason = "请先创建角色。";
        else if (!n->alive || n->health <= 0 || n->age >= n->lifespan) lesson.reason = "斯人已逝。";
        else if (g.ended || p.health <= 0 || p.age >= p.lifespan) lesson.reason = "这一世已经结束。";
        else if (g.pending || p.arts.battle || g.job) lesson.reason = "请先完成当前安排、事件或斗法。";
        else if (n->location != p.location) lesson.reason = "请先前往道友所在地点。";
        else if (lesson.learned) lesson.reason = "已经领悟。";
        else if (p.stage < lesson.stage) lesson.reason = "需达到" + realmName(lesson.stage) + "。";
        else if (relationOf(g, npcId) < lesson.affinityNeed) lesson.reason = "好感不足（需" + std::to_string(lesson.affinityNeed) + "）。";
        else if (bi != p.npcBonds.end() && bi->second.learnMonth == g.clock.days / 30 && bi->second.learnedThisMonth >= 3) lesson.reason = "本月已向此人请教三次。";
        else if (friendship::creditOf(g, npcId) < lesson.creditCost) lesson.reason = "交情不足（需" + std::to_string(lesson.creditCost) + "）。";
        else if (p.stones < lesson.stoneCost) lesson.reason = "授业灵石不足。";
        lesson.enabled = lesson.reason.empty();
        result.push_back(std::move(lesson));
    };
    for (auto lesson : knowledgeOf(*n)) {
        lesson.learned = lesson.kind == "spell"
            ? std::find(p.arts.knownSpells.begin(), p.arts.knownSpells.end(), lesson.id) != p.arts.knownSpells.end()
            : p.arts.manuals.count(lesson.id) && p.arts.manuals.at(lesson.id) > 0;
        add(std::move(lesson));
    }
    return result;
}
string npcLearnBegin(Game& g, const string& npcId, const string& kind, const string& id) {
    const auto lessons = npcLessons(g, npcId);
    for (const auto& lesson : lessons) {
        if (lesson.kind != kind || lesson.id != id) continue;
        if (!lesson.enabled) return lesson.reason;
        if (!friendship::spendCredit(g, npcId, lesson.creditCost)) return "交情不足。";
        g.player.stones -= lesson.stoneCost;
        auto& bond = g.player.npcBonds[npcId];
        const int month = g.clock.days / 30;
        if (bond.learnMonth != month) { bond.learnMonth = month; bond.learnedThisMonth = 0; }
        ++bond.learnedThisMonth;
        Job job; job.actionType = "npcLearn"; job.name = "请教·" + lesson.name;
        job.plan = lesson.id; job.kind = lesson.kind; job.npcId = npcId;
        job.total = job.remaining = lesson.days; job.started = g.clock.days;
        job.cost = lesson.stoneCost; job.gift = lesson.creditCost;
        g.job = job;
        logEvent(g, "npc", "请道友传授「" + lesson.name + "」，用时" + std::to_string(lesson.days) + "天；中止授业不退学费。", false, npcId);
        return "";
    }
    return "这位道友不会传授此术。";
}
void npcLearnComplete(Game& g, const Job& job) {
    const string settled = settlementKey(job);
    if (g.flags.count(settled)) return;
    g.flags[settled] = 1;
    const Npc* n = teacherOf(g, job.npcId);
    string error;
    optional<NpcLesson> lesson;
    if (job.actionType != "npcLearn" || (job.kind != "spell" && job.kind != "manual")) error = "授业安排无效。";
    else if (g.player.name.empty() || g.ended || g.player.health <= 0 || g.player.age >= g.player.lifespan) error = "这一世已经结束。";
    else if (!n || !n->alive || n->health <= 0 || n->age >= n->lifespan) error = "老师已经离世。";
    else if (n->beast) error = "妖兽不能传授道法。";
    else if (n->location != g.player.location) error = "老师已经离开此地。";
    else {
        for (const auto& candidate : knowledgeOf(*n))
            if (candidate.kind == job.kind && candidate.id == job.plan) { lesson = candidate; break; }
        if (!lesson) error = "老师不会传授这门道法。";
        else if (g.player.stage < lesson->stage || n->stage < lesson->stage) error = "授业双方境界不足。";
        else if (!job.paid || job.cost != lesson->stoneCost || job.gift != lesson->creditCost ||
                 job.total != lesson->days || job.remaining != 0 ||
                 g.clock.days < static_cast<long long>(job.started) + job.total) error = "授业安排尚未完成或费用不符。";
    }
    if (!error.empty()) { refundLesson(g, job, error); return; }
    if (job.kind == "spell") {
        if (std::find(g.player.arts.knownSpells.begin(), g.player.arts.knownSpells.end(), job.plan) == g.player.arts.knownSpells.end())
            g.player.arts.knownSpells.push_back(job.plan);
    } else if (job.kind == "manual") {
        g.player.arts.manuals[job.plan] = std::max(1, g.player.arts.manuals[job.plan]);
        if (std::find(g.economy.books.begin(), g.economy.books.end(), job.plan) == g.economy.books.end()) g.economy.books.push_back(job.plan);
        if (std::find(g.player.arts.bookList.begin(), g.player.arts.bookList.end(), job.plan) == g.player.arts.bookList.end()) g.player.arts.bookList.push_back(job.plan);
    }
    addRelation(g, job.npcId, 2);
    artGain(g.player, lesson->dao, 4);
    logEvent(g, "npc", "从" + n->name + "处领悟了「" + lesson->name + "」。可在功法或神通面板配置。", true, job.npcId);
}
}
