// 宁州一梦 · C++ 版 — 人物名录、建号数据、丹毒明细（实现）
#include "core/roster.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "core/arts.h"
#include "core/calendar.h"
#include "core/play.h"
#include "core/sim.h"
#include "core/sword.h"
#include "core/talents.h"
#include "data/reference_tables.h"

namespace nz::play {

using std::string;
using std::vector;

// ============================================================
// 好友通讯录
// ============================================================
// 用途：汇总 NPC 名录并按关系、境界等排序。
// 参与流程：通讯录与故人旧卷。
// 使用方式：传游戏、是否含陌生人及排序键；旧卷仅列认识的逝者。
// 返回值：含地点、名分和远程动作提示的名录列表。
vector<RosterEntry> roster(const Game& g, bool includeStrangers,
                           const string& sortBy) {
    vector<RosterEntry> out;

    // includeStrangers 为 true 时把逝者也列出来（故人旧卷）。
    // 之前一律跳过逝者，玩家看不到谁已经离世，
    // 师尊/道侣没了也无从查证。
    const bool wantDead = includeStrangers;
    for (const auto& n : g.npcs) {
        if (!n.alive && !wantDead) continue;
        // 逝者只列认识的，不然 720 人里大半是陌生人
        if (!n.alive && relationOf(g, n.id) < 15 &&
            g.bonds.master != n.id && g.bonds.partner != n.id)
            continue;

        const int rel = relationOf(g, n.id);
        const bool isMaster = (g.bonds.master == n.id);
        const bool isPartner = (g.bonds.partner == n.id);
        const bool isDisciple =
            std::find(g.bonds.disciples.begin(), g.bonds.disciples.end(),
                      n.id) != g.bonds.disciples.end();
        const bool known = (rel != 0) || isMaster || isPartner || isDisciple;
        const bool here = (n.location == g.player.location);

        // 不显示陌生人时，只保留认识的人
        if (!includeStrangers && !known) continue;

        RosterEntry e;
        e.id = n.id;
        e.name = n.name;
        e.gender = n.gender;
        e.age = n.age;
        e.stage = n.stage;
        e.realmName = realmName(n.stage);
        e.sectName = n.sect.empty()
                         ? "散修"
                         : (findSect(n.sect) ? findSect(n.sect)->name : n.sect);
        e.personality = n.personality;
        e.goal = n.goal;
        e.location = n.location;
        e.locationName = findLoc(n.location) ? findLoc(n.location)->name
                                             : n.location;
        e.relation = rel;
        e.relationLabel = relationLabel(rel);
        e.here = here;
        e.alive = n.alive;
        e.deathYear = n.deathYear;
        e.isMaster = isMaster;
        e.isPartner = isPartner;
        e.isDisciple = isDisciple;
        e.sword = n.sword;
        e.insight = n.insight;
        e.apt = n.apt;
        // 传讯不限地点；同游要求认识且有交情
        e.canMessage = known && rel < 100;
        e.canInvite = known && !here && rel >= 10;
        out.push_back(e);
    }

    // 排序
    if (sortBy == "relation") {
        std::sort(out.begin(), out.end(), [](const RosterEntry& a, const RosterEntry& b) {
            return a.relation > b.relation;
        });
    } else if (sortBy == "realm") {
        std::sort(out.begin(), out.end(), [](const RosterEntry& a, const RosterEntry& b) {
            return a.stage > b.stage;
        });
    } else if (sortBy == "name") {
        std::sort(out.begin(), out.end(), [](const RosterEntry& a, const RosterEntry& b) {
            return a.name < b.name;
        });
    } else if (sortBy == "sect") {
        std::sort(out.begin(), out.end(), [](const RosterEntry& a, const RosterEntry& b) {
            if (a.sectName != b.sectName) return a.sectName < b.sectName;
            return a.relation > b.relation;
        });
    } else {
        // 默认：同地的排前面，然后按好感
        std::sort(out.begin(), out.end(), [](const RosterEntry& a, const RosterEntry& b) {
            if (a.here != b.here) return a.here;
            return a.relation > b.relation;
        });
    }
    return out;
}

// 用途：向名录目标传讯并按当前好感增加关系。
// 参与流程：远程问候动作。
// 使用方式：传目标 ID，好感越深收益越少。
// 返回值：传讯结果；成功修改关系和日志，不推进天数。
MessageResult sendMessage(Game& g, const string& npcId) {
    MessageResult r;
    const Npc* n = findPerson(g, npcId);
    if (!n) {
        r.error = "找不到这个人。";
        return r;
    }
    const int rel = relationOf(g, npcId);
    // 传讯效果递减：好感越高越难靠传讯涨
    int gain = 2;
    if (rel >= 60) gain = 1;
    if (rel >= 85) gain = 0;

    if (gain == 0) {
        r.error = "交情已深，传讯难再增进。不如当面一见。";
        return r;
    }
    addRelation(g, npcId, gain);
    r.ok = true;
    r.relationDelta = gain;
    r.text = "你向" + n->name + "传去一封书信，对方回了寥寥数语。";
    logEvent(g, "npc", r.text + "（好感 +" + std::to_string(gain) + "）",
             false, npcId);
    return r;
}

// 用途：支付行装费用并邀请目标来到玩家地点。
// 参与流程：远程邀请动作。
// 使用方式：传游戏与目标 ID，要求关系至少十且不在同地。
// 返回值：邀请结果；成功扣灵石、移动 NPC、加关系并记日志。
MessageResult inviteToMe(Game& g, const string& npcId) {
    MessageResult r;
    const Npc* n = findPerson(g, npcId);
    if (!n) {
        r.error = "找不到这个人。";
        return r;
    }
    if (n->location == g.player.location) {
        r.error = n->name + "已经在此地了。";
        return r;
    }
    if (relationOf(g, npcId) < 10) {
        r.error = "交情不足 10，对方未必肯来。先传讯或当面叙叙。";
        return r;
    }
    if (g.player.stones < 15) {
        r.error = "打点行装需 15 灵石，你有 " + std::to_string(g.player.stones) + "。";
        return r;
    }

    g.player.stones -= 15;
    for (auto& x : g.npcs) {
        if (x.id != npcId) continue;
        x.location = g.player.location;
        break;
    }
    addRelation(g, npcId, 5);
    r.ok = true;
    r.relationDelta = 5;
    const string where = findLoc(g.player.location)
                             ? findLoc(g.player.location)->name
                             : g.player.location;
    r.text = n->name + "应邀来到" + where + "。";
    logEvent(g, "npc", r.text + "（好感 +5）", true, npcId,
             "灵石 −15");
    return r;
}

// ============================================================
// 建号面板
// ============================================================
// 用途：读取天赋定义并转为建号选项。
// 参与流程：建号面板。
// 使用方式：无参数，从既有 TALENTS 表复制公开信息。
// 返回值：包含类型、成本与效果的选项列表。
vector<TalentOption> allTalents() {
    vector<TalentOption> out;
    for (const auto& t : TALENTS()) {
        TalentOption o;
        o.id = t.id;
        o.name = t.name;
        o.category = t.tag;
        o.cost = t.cost;
        o.effect = t.desc;
        out.push_back(o);
    }
    // 按「成本从高到低」排，贵的（强的）在前，磨砺在后
    std::sort(out.begin(), out.end(), [](const TalentOption& a, const TalentOption& b) {
        if ((a.cost < 0) != (b.cost < 0)) return a.cost >= 0;   // 磨砺沉底
        if (a.cost != b.cost) return a.cost > b.cost;
        return a.name < b.name;
    });
    return out;
}

// 用途：核算天赋与属性支出并检查互斥和上限。
// 参与流程：建号方案预览校验。
// 使用方式：传天赋 ID 列表、分配方案与点数预算。
// 返回值：预算、余点和拒绝原因的 BuildCheck，不建角色。
BuildCheck checkBuild(const vector<string>& traits,
                      const std::map<std::string, int>& allocation,
                      int budget) {
    BuildCheck c;
    c.budget = budget;
    c.traitCount = static_cast<int>(traits.size());

    // 天赋成本
    for (const auto& id : traits) {
        bool found = false;
        for (const auto& t : TALENTS()) {
            if (t.id == id) { c.spent += t.cost; found = true; break; }
        }
        if (!found) {
            c.error = "没有这个天赋：" + id;
            return c;
        }
    }
    // 重复选择
    for (size_t i = 0; i < traits.size(); ++i)
        for (size_t j = i + 1; j < traits.size(); ++j)
            if (traits[i] == traits[j]) {
                c.error = "同一个天赋不能选两次。";
                return c;
            }
    // 天赋数量不设上限：只受点数约束（想选几项就选几项，够点就行）
    c.error = talentConflict(traits);
    if (!c.error.empty()) return c;

    // 属性点消耗
    for (const auto& [id, v] : allocation) {
        if (v < 0) {
            c.error = "分配点数不能为负。";
            return c;
        }
        c.allocSpent += v;
        // 单独一项不能超过上限
        for (const auto& s : BUILD_STATS()) {
            if (id == s.id && v > s.max) {
                c.error = std::string(s.name) + "最多 " + std::to_string(s.max) +
                          " 点，你分配了 " + std::to_string(v) + "。";
                return c;
            }
        }
    }

    c.remain = budget - c.spent - c.allocSpent;
    // Reuse the authoritative validator, including unknown keys and budget bounds.
    try { (void)buildCharacter(traits, allocation, budget); }
    catch (const std::invalid_argument& error) { c.error = error.what(); return c; }
    c.ok = (c.remain >= 0);
    if (!c.ok) {
        c.error = "点数超支 " + std::to_string(-c.remain) + " 点。";
    }
    return c;
}

// 用途：将属性定义和当前投入合成建号选项。
// 参与流程：建号属性分配面板。
// 使用方式：传分配方案，缺项按零展示。
// 返回值：五项属性的名称、上限和已投点数列表。
vector<AllocOption> allocOptions(const std::map<string, int>& allocation) {
    vector<AllocOption> out;
    for (const auto& s : BUILD_STATS()) {
        AllocOption o;
        o.id = s.id;
        o.name = s.name;
        o.desc = s.desc;
        o.max = s.max;
        auto it = allocation.find(s.id);
        o.value = (it == allocation.end()) ? 0 : it->second;
        out.push_back(o);
    }
    return out;
}

// ============================================================
// 丹毒明细
// ============================================================
// 用途：解释丹毒对修炼的影响与自然化解时间。
// 参与流程：丹毒详情页面。
// 使用方式：传游戏，计入日历已累计的化毒零数。
// 返回值：丹毒值、效率、化解速度和预计天数，不改丹毒。
PoisonInfo poisonInfo(const Game& g) {
    PoisonInfo p;
    const Arts& a = g.player.arts;
    p.current = a.poison;
    p.threshold = 20;
    p.affecting = (a.poison > p.threshold);
    // 与 artModsOf 里的公式保持一致
    p.cultPenalty = std::max(0.5, 1.0 - std::max(0, a.poison - 20) / 160.0);

    // 自然化解速度（对应 calendar.cpp 的 detox 速率）
    p.detoxPerYear = calendarDetoxPerYear(g.player);
    const auto stored = g.clock.carry.find("detox");
    const double carry = stored == g.clock.carry.end() ? 0.0 : stored->second;
    p.daysToClear = a.poison <= 0 ? 0 : static_cast<int>(std::ceil(
        std::max(0.0, a.poison - carry - 1e-9) * 360.0 / p.detoxPerYear));

    if (a.poison == 0) {
        p.summary = "体内清明，无丹毒积存。";
    } else if (!p.affecting) {
        p.summary = "丹毒 " + std::to_string(a.poison) +
                    "，尚未影响修炼（超过 " + std::to_string(p.threshold) +
                    " 才开始拖慢）。";
    } else {
        p.summary = "丹毒 " + std::to_string(a.poison) + "，修炼效率降到 ×" +
                    std::to_string(std::round(p.cultPenalty * 100) / 100.0).substr(0, 4) +
                    "。自然化解约需 " + std::to_string(p.daysToClear) + " 天。";
    }
    return p;
}

} // namespace nz::play
