// 宁州一梦 · C++ 版 — 转世与传承（实现）
#include "core/legacy.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <limits>
#include <random>
#include <sstream>

#include "core/arts.h"
#include "core/json.h"
#include "core/sim.h"
#include "data/reference_tables.h"

namespace nz::legacy {

using std::string;
using std::vector;

namespace {

// 里程碑定义（对应 JS settleLegacy 里的几组循环）
struct MilestoneDef {
    const char* key;      // 存进 archive 的 key
    const char* label;    // 界面文案
    int points;
};

const MilestoneDef REALM_MS[] = {
    {"realm-3", "首次筑基", 2},
    {"realm-6", "首次金丹", 2},
    {"realm-9", "首次元婴", 2},
    {"realm-12", "首次化神", 2},
};
const MilestoneDef ALCHEMY_MS[] = {
    {"alchemy-40", "丹术达到 40", 1},
    {"alchemy-80", "丹术达到 80", 1},
};
const MilestoneDef FRIEND_MS[] = {
    {"friends-3", "结下 3 位知交", 1},
    {"friends-8", "结下 8 位知交", 1},
};
const MilestoneDef RENOWN_MS[] = {
    {"renown-20", "声望达到 20", 1},
    {"renown-60", "声望达到 60", 1},
};

// 用途：检查传承字符串集合是否包含指定ID。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const vector<string>& v, const string& s，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool has(const vector<string>& v, const string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

// 用途：创建空的账号传承档案。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：Archive结果，包含说明所述状态或计算数据。
Archive create() { return Archive{}; }

// 用途：创建独立人生结算标识。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入uint32_t seed，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string newLifeId(uint32_t seed) {
    static const uint64_t processNonce = [] {
        try {
            std::random_device entropy;
            return (static_cast<uint64_t>(entropy()) << 32) | entropy();
        } catch (...) {
            return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        }
    }();
    static std::atomic<uint64_t> sequence{0};
    const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream id;
    id << "life-" << seed << '-' << std::hex << timestamp << '-' << processNonce << '-'
       << sequence.fetch_add(1, std::memory_order_relaxed);
    return id.str();
}

// 用途：把传承点换算为新世可分配天赋预算。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Archive& a，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int talentBudget(const Archive& a) {
    return std::min(MAX_POINTS, BASE_POINTS + a.earned);
}

// 用途：校验账号传承的结构与点数范围。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Archive& a，按本模块入口调用。
// 返回值：校验失败原因；合法档案返回空串。
string validate(const Archive& a) {
    if (a.version != 1) return "传承档案版本不对。";
    if (a.earned < 0 || a.earned > 30) return "传承点数超出范围（0-30）。";
    if (a.runs < 0 || a.runs > 10000) return "转世次数超出范围。";
    if (a.claimed.size() > 10000) return "已结算记录过多。";
    if (a.milestones.size() > 50) return "里程碑记录过多。";
    {
        vector<string> s = a.claimed;
        std::sort(s.begin(), s.end());
        if (std::adjacent_find(s.begin(), s.end()) != s.end())
            return "已结算记录有重复。";
    }
    {
        vector<string> s = a.milestones;
        std::sort(s.begin(), s.end());
        if (std::adjacent_find(s.begin(), s.end()) != s.end())
            return "里程碑记录有重复。";
    }
    return "";
}

// 用途：列出本世达成情况与账号已领取里程碑。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Game& g, const Archive& a，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
vector<Milestone> milestonesOf(const Game& g, const Archive& a) {
    vector<Milestone> out;
    const Player& p = g.player;

    auto add = [&](const string& key, const string& label, int pts,
                   bool reached) {
        Milestone m;
        m.key = key;
        m.label = label;
        m.points = pts;
        m.reached = reached;
        m.alreadyHad = has(a.milestones, key);
        out.push_back(m);
    };

    for (const auto& d : REALM_MS) {
        const int stage = std::atoi(d.key + 6);   // "realm-3" → 3
        add(d.key, d.label, d.points, p.highestStage >= stage);
    }
    for (const auto& d : ALCHEMY_MS) {
        const int lv = std::atoi(d.key + 8);
        add(d.key, d.label, d.points, p.alchemy >= lv);
    }
    for (const auto& d : FRIEND_MS) {
        const int lv = std::atoi(d.key + 8);
        const int friends = g.ending.has_value() ? g.ending->friends : 0;
        add(d.key, d.label, d.points, friends >= lv);
    }
    for (const auto& d : RENOWN_MS) {
        const int lv = std::atoi(d.key + 7);
        add(d.key, d.label, d.points, p.renown >= lv);
    }
    // 洞府成就
    for (const auto& ach : data::AchievementDef_TABLE()) {
        const int prog = artProgress(p, ach.key);
        add(ach.id, ach.name, ach.points > 0 ? ach.points : 1,
            prog >= ach.target);
    }
    return out;
}

// 用途：仅结算一次已结束人生的新增传承收益。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Game& g, Archive& a，按本模块入口调用。
// 返回值：新增点数、理由与是否有资格；重复人生不重复领取。
SettleResult settle(const Game& g, Archive& a) {
    SettleResult r;
    if (!g.ended) {
        r.note = "这一世尚未结束。";
        return r;
    }
    if (has(a.claimed, g.lifeId)) {
        r.note = "此世已经结算过了。";
        return r;
    }

    a.claimed.push_back(g.lifeId);
    a.runs++;

    int earned = 0;
    for (const auto& m : milestonesOf(g, a)) {
        if (!m.reached) continue;
        if (has(a.milestones, m.key)) continue;
        a.milestones.push_back(m.key);
        earned += m.points;
        r.reasons.push_back(m.label + " +" + std::to_string(m.points));
    }

    const int points = std::min(earned, 30 - a.earned);
    a.earned += points;
    r.points = points;
    r.eligible = true;

    if (points == 0) {
        r.reasons.clear();
        r.reasons.push_back(a.earned >= 30
                                ? "传承点数已达上限"
                                : "本世尚无新的成就，单纯熬年份不增加点数");
    }
    return r;
}

// 用途：按种子与刷新次数生成天赋候选列表。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入uint32_t seed, int roll，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
vector<string> draft(uint32_t seed, int roll) {
    // 与 JS 同构：seed + (roll+1)*2654435761 作为初始状态，然后走 LCG
    uint32_t rng = seed + (static_cast<uint32_t>(std::max(0, roll)) + 1u) * 2654435761u;
    auto rand01 = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return rng / 4294967296.0;
    };

    vector<string> pool;
    for (const auto& t : TALENTS()) pool.push_back(t.id);

    vector<string> out;
    while (out.size() < 12 && !pool.empty()) {
        // 权重：贵的天赋更难抽到，负面天赋更容易
        vector<double> weights;
        double total = 0;
        for (const auto& id : pool) {
            const TalentDef* t = findTalent(id);
            double w = 1.0;
            if (t) {
                if (t->cost >= 7) w = 0.35;
                else if (t->cost < 0) w = 0.7;
            }
            weights.push_back(w);
            total += w;
        }
        double v = rand01() * total;
        size_t idx = 0;
        while (idx + 1 < weights.size() && v > weights[idx]) {
            v -= weights[idx];
            ++idx;
        }
        out.push_back(pool[idx]);
        pool.erase(pool.begin() + static_cast<long>(idx));
    }
    return out;
}

// 用途：序列化账号传承档案。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Archive& a，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string toJson(const Archive& a) {
    json::Value o(json::Object{});
    o.set("version", a.version);
    o.set("earned", a.earned);
    o.set("runs", a.runs);
    json::Array claimed, milestones;
    for (const auto& id : a.claimed) claimed.emplace_back(id);
    for (const auto& id : a.milestones) milestones.emplace_back(id);
    o.set("claimed", std::move(claimed));
    o.set("milestones", std::move(milestones));
    return o.dump();
}

// 用途：解析并校验账号传承JSON。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const string& text, bool* ok，按本模块入口调用。
// 返回值：Archive结果，包含说明所述状态或计算数据。
Archive fromJson(const string& text, bool* ok) {
    Archive a;
    if (ok) *ok = false;
    std::string err;
    const json::Value o = json::Value::parse(text, &err);
    if (!err.empty() || !o.isObject()) return a;
    for (const char* key : {"version", "earned", "runs"}) {
        const auto& value = o[key];
        if (!value.isNumber() || !std::isfinite(value.asNumber()) ||
            value.asNumber() < 0 || value.asNumber() > std::numeric_limits<int>::max() ||
            std::floor(value.asNumber()) != value.asNumber()) return a;
    }
    a.version = o["version"].asInt();
    a.earned = o["earned"].asInt();
    a.runs = o["runs"].asInt();
    for (const char* key : {"claimed", "milestones"}) {
        if (!o[key].isArray()) return a;
        auto& list = std::string(key) == "claimed" ? a.claimed : a.milestones;
        for (const auto& value : o[key].asArray()) {
            if (!value.isString() || value.asString().empty()) return a;
            list.push_back(value.asString());
        }
    }
    if (ok) *ok = validate(a).empty();
    return a;
}

} // namespace nz::legacy
