#include "core/sect_trial.h"

#include <algorithm>
#include <climits>

#include "core/battle.h"
#include "core/friendship.h"
#include "core/renown.h"
#include "core/sim.h"
#include "core/society.h"

namespace nz {
namespace {
const string prefix = "sect-trial:";
string activeKey(const string& sect) { return "sect-trial-active:" + sect; }
int flagValue(const Game& g, const string& key) {
    const auto found = g.flags.find(key);
    return found == g.flags.end() ? 0 : found->second;
}
bool trialContext(const Battle& battle, string& sect, int& serial) {
    if (battle.kind != "sectTrial" || battle.quest.compare(0, prefix.size(), prefix) != 0) return false;
    const size_t colon = battle.quest.find(':', prefix.size());
    if (colon == string::npos) return false;
    sect = battle.quest.substr(prefix.size(), colon - prefix.size());
    if (!findSectTrial(sect) || colon + 1 >= battle.quest.size()) return false;
    serial = 0;
    for (size_t i = colon + 1; i < battle.quest.size(); ++i) {
        const char c = battle.quest[i];
        if (c < '0' || c > '9' || serial > (INT_MAX - (c - '0')) / 10) return false;
        serial = serial * 10 + c - '0';
    }
    return serial > 0 && std::to_string(serial) == battle.quest.substr(colon + 1);
}
void addOnce(vector<string>& entries, const string& id) {
    if (std::find(entries.begin(), entries.end(), id) == entries.end()) entries.push_back(id);
}
}

const vector<SectTrialDef>& sectTrialDefs() {
    static const vector<SectTrialDef> defs = {
        {"jinhong", "试剑问锋", "击败试剑弟子：金芒蓄势后以剑诀破盾，考验攻守与爆发时机。胜出授《重剑诀》与紫微印。", "金", "heavy", "metalSeal"},
        {"xinghe", "水月试剑", "击败水月弟子：罡水印积蓄水势、浪击爆发并穿插水盾，考验连续出牌。胜出授《化雨诀》与罡水印。", "水", "rainform", "waterSeal"},
        {"zhushan", "青竹试道", "击败药园弟子：腐毒咒积毒、回春恢复，以毒伤消磨气血，考验速攻和休整。胜出授《鸩羽诀》与腐毒咒。", "木", "venom", "corrode"},
        {"lihuo", "赤焰炼心", "击败传火弟子：丹阳印衔接吐焰积攒灼烧，考验护盾与火势节奏。胜出授《烈焰诀》与丹阳印。", "火", "flame", "fireSeal"},
        {"huachen", "镇岳守关", "击败守关弟子：三山印蓄势、岩甲护体并以八极崩反击，考验破盾和耐心。胜出授《撼山诀》与三山印。", "土", "mountain", "mountainSeal"},
    };
    return defs;
}

const SectTrialDef* findSectTrial(const string& sect) {
    for (const auto& def : sectTrialDefs()) if (def.sect == sect) return &def;
    return nullptr;
}

string sectTrialError(const Game& g, const string& sect) {
    const auto* def = findSectTrial(sect);
    if (!def) return "没有这处可拜入的宗门。";
    if (g.ended || g.player.health <= 0) return "此世已结束或伤势过重。";
    if (g.job || g.pending || g.player.arts.battle || g.dungeons.run) return "请先完成当前安排、抉择或斗法。";
    if (!g.player.sect.empty()) return "已经拜入宗门，不能重复参加入门考核。";
    if (g.player.location != sect) return "请先赶往" + string(findSect(sect)->name) + "山门报名。";
    if (g.player.stage < def->minStage) return "入门考核至少需要" + realmName(def->minStage) + "。";
    if (sect == "xinghe" && g.player.gender != "女") return "星河剑派仅招收女修。";
    const int requiredRenown = renown::sectRequirement(sect);
    if (g.player.renown < requiredRenown) return "这处山门需要声望达到 " + std::to_string(requiredRenown) + "。";
    if (g.player.health < 30) return "气血不足30%，请先休养再参加入门考核。";
    if (g.player.stones < def->fee) return "每次入门考核需 " + std::to_string(def->fee) + " 灵石。";
    if (flagValue(g, "sect-trial-serial") >= 1000000) return "入门考核次数已达到上限。";
    for (const auto& other : sectTrialDefs())
        if (flagValue(g, activeKey(other.sect)) != 0) return "上一场入门考核尚未结算。";
    return "";
}

string sectTrialBegin(Game& g, const string& sect) {
    if (const string error = sectTrialError(g, sect); !error.empty()) return error;
    const auto& def = *findSectTrial(sect);
    const int serial = flagValue(g, "sect-trial-serial") + 1;
    g.flags["sect-trial-serial"] = serial;
    g.flags[activeKey(sect)] = serial;
    g.player.stones -= def.fee;
    Job job;
    job.actionType = "socialElapsed"; job.kind = "sectTrial";
    job.name = string(findSect(sect)->name) + "·" + def.name;
    job.total = job.remaining = def.days; job.started = g.clock.days;
    job.cost = def.fee; job.origin = sect; job.sect = sect;
    g.job = std::move(job);
    EnemySpec enemy;
    enemy.kind = "sectTrial"; enemy.quest = prefix + sect + ":" + std::to_string(serial);
    enemy.name = string(findSect(sect)->name) + "·考核弟子";
    enemy.sect = sect; enemy.style = def.style; enemy.gender = sect == "xinghe" ? "女" : "男";
    // 入门弟子至多炼气后期；只使用本宗低阶构筑，气血减半且不致命。
    enemy.stage = clampInt(g.player.stage - 1, 0, 2); enemy.health = 50;
    enemy.speed = 12; enemy.lethal = false;
    logEvent(g, "sect", "缴纳" + std::to_string(def.fee) + "灵石参加「" + def.name + "」。需实际击败本宗考核弟子；失败或撤离仍耗时" + std::to_string(def.days) + "天。", true);
    // 标记和耗时安排先写入，保证先手造成即时胜负也能正确结算。
    battleStart(g, enemy);
    return "";
}

void sectTrialBattleComplete(Game& g, const Battle& battle, bool won, bool escaped) {
    string sect; int serial = 0;
    if (!trialContext(battle, sect, serial) || flagValue(g, activeKey(sect)) != serial) return;
    const auto& def = *findSectTrial(sect);
    if (!g.job || g.job->actionType != "socialElapsed" || g.job->kind != "sectTrial" ||
        g.job->sect != sect || g.job->origin != sect || g.job->cost != def.fee ||
        g.player.location != sect || battle.enemySect != sect || battle.lethal) return;
    g.flags.erase(activeKey(sect));
    if (!won || escaped || battle.retreat || !g.player.sect.empty()) {
        logEvent(g, "sect", "「" + def.name + "」未通过。仍为散修；休养后可以重新缴费应试。", true);
        return;
    }
    g.player.sect = sect;
    g.player.renown += 3;
    g.flags["sect-trial-passed:" + sect] = serial;
    addOnce(g.economy.books, def.manual);
    addOnce(g.player.arts.bookList, def.manual);
    addOnce(g.player.arts.knownSpells, def.spell);
    socInitPayroll(g);
    refreshSects(g);
    logEvent(g, "sect", "通过「" + def.name + "」，正式拜入" + string(findSect(sect)->name) + "。已获本门入门功法抄本与神通，可在藏经阁继续兑换本门传承。", true, "", "声望 +3");
}

void sectTrialLegacyComplete(Game& g, const Job& job) {
    // 旧版本已经扣除报名费，在转入实战前先退回，避免第二次扣费。
    if (job.paid && job.cost > 0) g.player.stones += job.cost;
    const string error = sectTrialBegin(g, job.sect);
    if (!error.empty()) logEvent(g, "sect", "旧入门安排已结束，未直接入门。" + error + "已支付的旧报名费用退回。", true);
}

string sectTrialStateError(const Game& g) {
    const int sequence = flagValue(g, "sect-trial-serial");
    if (sequence < 0 || sequence > 1000000) return "宗门考核序号无效。";
    if (g.job && g.job->kind == "sectTrial") {
        const auto* def = findSectTrial(g.job->sect);
        if (!def || sequence == 0 || g.job->actionType != "socialElapsed" || !g.job->paid ||
            g.job->origin != def->sect || g.player.location != def->sect || g.job->cost != def->fee ||
            g.job->total != def->days || g.job->remaining <= 0 || g.job->remaining > def->days ||
            g.job->started < 0 || g.job->started > g.clock.days) return "入门考核后续耗时记录无效。";
    }
    string active;
    for (const auto& def : sectTrialDefs()) {
        const int serial = flagValue(g, activeKey(def.sect));
        const int passed = flagValue(g, "sect-trial-passed:" + def.sect);
        if (passed < 0 || passed > sequence) return "宗门考核通过记录无效。";
        if (serial == 0) continue;
        if (serial < 0 || serial != sequence || !active.empty()) return "同时存在多场入门考核。";
        active = def.sect;
    }
    const Battle* battle = g.player.arts.battle ? &*g.player.arts.battle : nullptr;
    if (active.empty()) {
        if (battle && battle->kind == "sectTrial") return "入门斗法缺少有效报名记录。";
        return "";
    }
    string sect; int serial = 0;
    if (!battle || !trialContext(*battle, sect, serial) || sect != active || serial != sequence ||
        battle->enemySect != sect || battle->enemyBeast || battle->lethal || !battle->dungeonSite.empty()) return "入门考核斗法上下文无效。";
    const auto& def = *findSectTrial(sect);
    if (g.ended || g.pending || g.dungeons.run || !g.player.sect.empty() || g.player.location != sect ||
        g.player.stage < def.minStage || (sect == "xinghe" && g.player.gender != "女")) return "入门考核人物或地点无效。";
    if (!g.job || g.job->actionType != "socialElapsed" || g.job->kind != "sectTrial" ||
        !g.job->paid || g.job->sect != sect || g.job->origin != sect || g.job->cost != def.fee ||
        g.job->total != def.days || g.job->remaining != def.days || g.job->started != g.clock.days) return "入门考核费用或耗时记录无效。";
    return "";
}

vector<const Npc*> sectMentorCandidates(const Game& g) {
    vector<const Npc*> mentors;
    if (!findSect(g.player.sect)) return mentors;
    for (const auto& npc : g.npcs) {
        if (npc.beast || !npc.alive || npc.health <= 0 || npc.age >= npc.lifespan ||
            npc.sect != g.player.sect) continue;
        if (npc.stage > g.player.stage || npc.id == g.bonds.master) mentors.push_back(&npc);
    }
    std::sort(mentors.begin(), mentors.end(), [&](const Npc* a, const Npc* b) {
        const bool aMaster = a->id == g.bonds.master, bMaster = b->id == g.bonds.master;
        if (aMaster != bMaster) return aMaster;
        const bool aHere = a->location == g.player.location, bHere = b->location == g.player.location;
        if (aHere != bHere) return aHere;
        if (relationOf(g, a->id) != relationOf(g, b->id)) return relationOf(g, a->id) > relationOf(g, b->id);
        if (a->stage != b->stage) return a->stage > b->stage;
        return a->id < b->id;
    });
    return mentors;
}

string sectMasterError(const Game& g, const string& npcId) {
    if (const string error = friendship::interactionError(g, npcId); !error.empty()) return error;
    const auto found = std::find_if(g.npcs.begin(), g.npcs.end(), [&](const Npc& npc) { return npc.id == npcId; });
    if (found == g.npcs.end()) return "找不到这位师长。";
    const auto& npc = *found;
    if (g.player.sect.empty()) return "请先通过入门考核，正式拜入宗门。";
    if (npc.sect != g.player.sect) return "只能拜本门境界更高的修士为师。";
    if (npc.id == g.bonds.master) return "已拜此人为师；可在师门日常接受授业。";
    if (!g.bonds.master.empty()) return "已经拜师，请先辞别现任师尊。";
    if (npc.id == g.bonds.partner) return "道侣不能再结为师徒。";
    if (std::find(g.bonds.disciples.begin(), g.bonds.disciples.end(), npcId) != g.bonds.disciples.end())
        return "门下弟子不能再拜为师尊。";
    if (npc.stage <= g.player.stage) return "师尊的境界须高于你的当前境界。";
    if (relationOf(g, npcId) < 40) return "好感不足40（当前" + std::to_string(relationOf(g, npcId)) + "），请先见礼、往来或赠礼。";
    const int required = renown::masterRequirement();
    if (g.player.renown < required) return "拜师需声望" + std::to_string(required) + "（当前" + std::to_string(g.player.renown) + "）。";
    if (g.player.stones < 20) return "拜师礼需20灵石（当前" + std::to_string(g.player.stones) + "）。";
    return "";
}

const vector<SectTaskDef>& sectTaskDefs() {
    static const vector<SectTaskDef> tasks = {
        {"sect", "山门差事", "维护药田与山门；完成后获得灵石与贡献。", 10, 25, 0, 15, 0},
        {"sect-patrol", "巡守山门", "巡视本门山道，熟悉本门道法；完成后获得灵石、贡献与本宗大道经验。", 7, 18, 0, 10, 4},
        {"sect-records", "整理本门传承", "为藏经阁校录本门传承；完成后获得灵石、贡献与更多本宗大道经验。", 10, 22, 0, 12, 8},
        {"sect-array", "维护护宗灵阵", "协助执事修缮护宗灵阵；筑基起可接，完成后获得灵石、较多贡献与本宗大道经验。", 12, 30, 3, 18, 6},
    };
    return tasks;
}

const SectTaskDef* findSectTask(const string& id) {
    for (const auto& task : sectTaskDefs()) if (task.id == id) return &task;
    return nullptr;
}

int sectTasksTakenThisMonth(const Game& g) {
    if (g.economy.month != g.clock.days / 30) return 0;
    long long total = 0;
    for (const auto& task : sectTaskDefs()) {
        const auto found = g.economy.taskCounts.find(task.id);
        if (found != g.economy.taskCounts.end()) total += std::max(0, found->second);
    }
    return static_cast<int>(std::min<long long>(total, INT_MAX));
}

} // namespace nz
