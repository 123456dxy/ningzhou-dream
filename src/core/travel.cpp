#include "core/travel.h"

#include <cmath>
#include <limits>

#include "core/sim.h"
#include "core/arts.h"
#include "core/battle.h"

namespace nz {

bool travelModeKnown(const string& mode) {
    return mode == "walk" || mode == "boat" || mode == "flight";
}

string travelModeName(const string& mode) {
    if (mode == "walk") return "步行赶路";
    if (mode == "flight") return "御器飞行";
    if (mode == "boat") return "驿站灵舟";
    return "赶路";
}

TravelPlan travelPlan(const Game& g, const string& to) {
    TravelPlan out;
    out.from = g.player.location;
    out.to = to;
    const LocationDef* fromLoc = findLoc(out.from);
    const LocationDef* toLoc = findLoc(to);
    if (!fromLoc || !toLoc) return out;
    const bool here = out.from == out.to;
    const bool sea = out.from == "sea" || out.to == "sea";
    // 先转double再相减，坐标扩展不会发生整型减法溢出。
    // 上限让最慢路线也落在存档允许的36万天内。
    const double distance = std::hypot(static_cast<double>(fromLoc->x) - toLoc->x,
                                      static_cast<double>(fromLoc->y) - toLoc->y);
    out.distance = here ? 0 : std::max(1, jsRound(std::min(900000.0, distance)));
    out.escapeSpeed = artEscapeSpeed(g.player);
    const double bonus = std::max(0, out.escapeSpeed - (20 + clampInt(g.player.stage, 0, 12) * 8));
    const double speedFactor = 1.0 + std::min(200.0, bonus) / 100.0;
    const auto daysAt = [&](double speed) {
        return std::max(1, static_cast<int>(std::ceil(out.distance / speed)));
    };
    const int major = clampInt(g.player.stage / 3, 0, 4);
    out.options = {
        {"walk", travelModeName("walk"), daysAt(3 * speedFactor), 0, false, "",
         "沿陆路步行，不花灵石；基础日行3格，额外遁速提高行进速度。无尽海没有步行通路。"},
        {"boat", travelModeName("boat"), daysAt(9), 4 + (out.distance + 4) / 5 + (sea ? 4 : 0), false, "",
         "搭乘驿站灵舟，每9格约需1天；船资按距离收取，海路另加4灵石。"},
        {"flight", travelModeName("flight"), daysAt((12 + 6 * major) * speedFactor), 2 + (out.distance + 9) / 10, false, "",
         "筑基初期解锁御器飞行，每大境界增加6格基础日行距离；额外遁速提高行进速度，灵石用于御器消耗。"}
    };
    const double risk = 0.012 + std::max(fromLoc->danger, toLoc->danger) * 0.0002;
    for (auto& option : out.options) {
        option.speed = option.id == "walk" ? 3 * speedFactor :
                       option.id == "boat" ? 9 : (12 + 6 * major) * speedFactor;
        const double modeRisk = option.id == "boat" ? 0.45 : option.id == "flight" ? 0.75 : 1.0;
        option.ambushChance = here ? 0.0 : clamp(risk * modeRisk / speedFactor, 0.0, 0.05);
        option.desc += " 途中可能遭遇截杀；灵舟有护送，额外遁速降低被截停的概率。";
    }
    string blocked;
    if (g.player.name.empty()) blocked = "请先创建角色或读取存档。";
    else if (g.ended || g.player.health <= 0 || g.player.age >= g.player.lifespan)
        blocked = "这一世已经结束，无法赶路。";
    else if (g.player.arts.battleActive()) blocked = "请先完成当前斗法。";
    else if (g.pending.has_value()) blocked = "请先处理当前抉择。";
    else if (g.job.has_value()) blocked = "已有进行中的安排，请先完成或中止。";
    else if (here) blocked = "你已在此地。";
    for (auto& option : out.options) {
        option.reason = blocked;
        if (option.reason.empty() && option.id == "walk" && sea)
            option.reason = "无尽海没有步行通路，请搭乘灵舟或御器飞行。";
        if (option.reason.empty() && option.id == "flight" && g.player.stage < 3)
            option.reason = "御器飞行需达到筑基初期。";
        if (option.reason.empty() && g.player.stones < option.cost)
            option.reason = "灵石不足，需要" + std::to_string(option.cost) + "灵石。";
        if (option.reason.empty() && (g.clock.days < 0 ||
                g.clock.days > std::numeric_limits<int>::max() - option.days))
            option.reason = "日历已超出可继续赶路的范围。";
        option.enabled = option.reason.empty();
        if (here) option.days = option.cost = 0;
    }
    return out;
}

const TravelOption* travelOption(const TravelPlan& plan, const string& mode) {
    for (const auto& option : plan.options) if (option.id == mode) return &option;
    return nullptr;
}

string travelBegin(Game& g, const string& to, const string& mode) {
    if (!travelModeKnown(mode)) return "没有这种赶路方式。";
    if (!findLoc(to)) return "没有这个地方。";
    if (!findLoc(g.player.location)) return "出发地点无效。";
    const TravelPlan plan = travelPlan(g, to);
    const TravelOption* option = travelOption(plan, mode);
    if (!option || !option->enabled) return option ? option->reason : "无法规划这段路程。";
    Job job;
    job.actionType = "travel";
    job.origin = plan.from;
    job.destination = to;
    job.travelMode = mode;
    job.name = option->name + "前往" + findLoc(to)->name;
    job.total = job.remaining = option->days;
    job.started = g.clock.days;
    job.cost = option->cost;
    job.paid = true;
    g.player.stones -= option->cost;
    g.job = std::move(job);
    logEvent(g, "self", "从" + string(findLoc(plan.from)->name) + "出发，" +
             option->name + "前往" + findLoc(to)->name + "，预计" +
             std::to_string(option->days) + "天，支付" + std::to_string(option->cost) +
             "灵石。", true);
    return "";
}

bool travelAmbushTick(Game& g) {
    if (g.ended || g.pending || g.player.arts.battleActive() || !g.job ||
        g.job->actionType != "travel" || g.job->remaining <= 0) return false;
    Job& job = *g.job;
    const int elapsed = job.total - job.remaining;
    if (elapsed <= job.travelChecks) return false;
    job.travelChecks = elapsed;
    // 每段行程至多一次截杀；保存已检查天数，重复继续/读档不重掷。
    if (job.travelAmbushes >= 1) return false;
    const TravelPlan plan = travelPlan(g, job.destination);
    const TravelOption* option = travelOption(plan, job.travelMode);
    if (!option || randomOf(g) >= option->ambushChance) return false;

    EnemySpec enemy;
    enemy.kind = "ambush";
    enemy.lethal = true;
    enemy.name = "劫道散修";
    enemy.stage = clampInt(g.player.stage, 0, 12);
    enemy.health = 100;
    enemy.style = "剑";
    // 使用真实附近恶缘修士的构筑；无合适对象则生成同境界弱传承散修。
    vector<const Npc*> candidates;
    for (const auto& n : g.npcs) {
        const auto relation = g.player.relations.find(n.id);
        const bool hostile = relation != g.player.relations.end() && relation->second < -20;
        if (n.alive && !n.beast && n.health > 0 && n.age < n.lifespan &&
            n.stage <= g.player.stage + 1 && hostile &&
            (n.location == job.origin || n.location == job.destination)) candidates.push_back(&n);
    }
    if (!candidates.empty()) {
        const Npc* npc = pickOf(g, candidates);
        enemy.name = npc->name; enemy.id = npc->id;
        enemy.stage = npc->stage; enemy.health = npc->health;
        enemy.style = npc->dao; enemy.sect = npc->sect; enemy.gender = npc->gender;
    }
    job.travelAmbushes++;
    logEvent(g, "self", "赶路途中遭遇" + enemy.name + "截杀！行程暂停，尚余" +
             std::to_string(job.remaining) + "天；脱身后可继续原路。", true);
    battleStart(g, enemy);
    return true;
}

} // namespace nz
