// 宁州一梦 · C++ 版 — 道友往来（实现）
#include "core/friendship.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/arts.h"
#include "core/base.h"
#include "core/battle.h"
#include "core/play.h"
#include "core/renown.h"
#include "core/sim.h"
#include "core/sword.h"
#include "data/reference_tables.h"

namespace nz::friendship {

using std::string;

// ============================================================
// 交情
// ============================================================
namespace {

NpcBond& bondOf(Game& g, const string& npcId) {
    return g.player.npcBonds[npcId];
}
const NpcBond* findBond(const Game& g, const string& npcId) {
    auto it = g.player.npcBonds.find(npcId);
    return it == g.player.npcBonds.end() ? nullptr : &it->second;
}

// 按 id 找 NPC（可写）
Npc* findNpc(Game& g, const string& npcId) {
    for (auto& n : g.npcs)
        if (n.id == npcId) return &n;
    return nullptr;
}

// 一件法器的「便宜时期坊市价」。
// 用与 shop::salePrice 相同的基础公式，但不乘行情与声望 ——
// 交情折算要的是一个稳定尺度，不然同一个人的人情会随行情浮动。
int gearValue(const Gear& gr) {
    const double base = (gr.craftCost > 0 ? gr.craftCost
                                          : 60.0 * gr.grade * gr.grade) *
                            1.45 +
                        gr.grade * 20.0;
    return jsRound(base * (gr.inscribed ? 1.12 : 1.0));
}

int tradeCost(int value) {
    const int giftCredit = creditFromValue(value);
    // 取用人情有少量额外代价，不能将同一件礼物取回后无限循环刷好感。
    return giftCredit + std::max(1, (giftCredit + 9) / 10);
}

} // namespace

int creditOf(const Game& g, const string& npcId) {
    const NpcBond* b = findBond(g, npcId);
    return b ? b->credit : 0;
}

void addCredit(Game& g, const string& npcId, int delta) {
    NpcBond& b = bondOf(g, npcId);
    b.credit = static_cast<int>(std::max(0LL, std::min(1000000000LL,
                                                    static_cast<long long>(b.credit) + delta)));
}

bool spendCredit(Game& g, const string& npcId, int cost) {
    if (cost < 0) return false;
    NpcBond& b = bondOf(g, npcId);
    if (b.credit < cost) return false;
    b.credit -= cost;
    b.spent = static_cast<int>(std::min(1000000000LL,
                                      static_cast<long long>(b.spent) + cost));
    return true;
}

string interactionError(const Game& g, const string& npcId) {
    if (g.player.name.empty()) return "请先创建角色或读取存档。";
    if (g.ended || g.player.health <= 0 || g.player.age >= g.player.lifespan)
        return "这一世已经结束，无法往来。";
    if (g.pending || g.player.arts.battleActive() || g.job)
        return "请先完成当前安排、抉择或斗法。";
    const Npc* npc = nullptr;
    for (const auto& n : g.npcs) if (n.id == npcId) { npc = &n; break; }
    if (!npc) return "找不到这个人。";
    if (!npc->alive || npc->health <= 0 || npc->age >= npc->lifespan) return "斯人已逝。";
    if (npc->beast) return "妖兽无法进行道友往来。";
    if (npc->location != g.player.location) return "此人已不在附近，请先前往同一地点。";
    return "";
}

string creditLabel(int credit) {
    if (credit <= 0) return "素无往来";
    if (credit < 20) return "点头之交";
    if (credit < 60) return "略有交情";
    if (credit < 150) return "交情匪浅";
    if (credit < 320) return "人情深重";
    return "恩义如山";
}

// ============================================================
// 好感度
// ============================================================
string affinityLabel(int a) {
    if (a <= -60) return "视若仇雠";
    if (a <= -25) return "心存芥蒂";
    if (a < 0) return "略有不快";
    if (a == 0) return "素不相识";
    if (a < 20) return "点头之交";
    if (a < 40) return "颇为投契";
    if (a < 60) return "引为知己";
    if (a < 85) return "情谊深厚";
    return "生死之交";
}

bool affinityAtLeast(const Game& g, const string& npcId, int need) {
    return relationOf(g, npcId) >= need;
}

// ============================================================
// 打招呼
// ============================================================
GreetResult greet(Game& g, const string& npcId) {
    GreetResult r;
    r.error = interactionError(g, npcId);
    if (!r.error.empty()) return r;
    Npc* n = findNpc(g, npcId);
    if (!n) { r.error = "找不到这个人。"; return r; }
    if (!n->alive) { r.error = "斯人已逝。"; return r; }

    NpcBond& b = bondOf(g, npcId);
    const Player& p = g.player;

    if (b.greeted) {
        // 已经打过招呼：只剩寒暄，不再加好感。
        // 这条是需求里明确要的 —— 打招呼只有第一次有收益，
        // 否则「不耗时 + 可重复」就变成无限刷好感。
        r.ok = true;
        r.firstTime = false;
        r.affinityDelta = 0;
        r.creditDelta = 0;
        r.text = "你与" + n->name + "点头示意，算是旧识了。";
        return r;
    }

    b.greeted = true;
    b.greetYear = g.year;

    // 第一次见面：好感按双方声望与境界差来定，可能为负
    const int gap = p.stage - n->stage;         // 正数=你更高
    const int renownPull = renown::firstImpression(p.renown) / 5;  // 0..5

    int delta = 4 + renownPull;                 // 基础 4~9

    // 境界差：低太多是攀附，高太多是屈尊
    if (gap <= -6) delta -= 4;
    else if (gap <= -3) delta -= 2;
    else if (gap >= 6) delta += 2;

    // 性格
    const string& pe = n->personality;
    string why;
    if (pe == "重情重义") { delta += 2; why = "对方重情重义，乐于结交"; }
    else if (pe == "好胜争先") { delta -= 1; why = "对方好胜，不喜生人"; }
    else if (pe == "逐利而行") { delta -= 1; why = "对方逐利，先掂量你的分量"; }
    else if (pe == "潜心问道") { delta -= 1; why = "对方潜心问道，不耐俗礼"; }
    else { why = "对方洒脱随性，来者不拒"; }

    // 门派相同额外亲近
    if (!p.sect.empty() && p.sect == n->sect) { delta += 2; why += "，同门之谊"; }

    delta = clampInt(delta, -6, 12);
    addRelation(g, npcId, delta);

    r.ok = true;
    r.firstTime = true;
    r.affinityDelta = delta;
    r.text = "你上前与" + n->name + "见礼。";
    logEvent(g, "npc",
             r.text + "（好感 " + (delta >= 0 ? "+" : "") + std::to_string(delta) +
                 " · " + why + "）",
             false, npcId);
    return r;
}

// ============================================================
// 送礼
// ============================================================
int creditFromValue(int value) {
    if (value <= 0) return 1;
    // 开方递减：一件 1000 灵石的神器不会一次买断人情。
    // 100 灵石 ≈ 20 交情，400 灵石 ≈ 40 交情，1600 灵石 ≈ 80 交情
    return std::max(1, jsRound(std::sqrt(static_cast<double>(value)) * 2.0));
}

GiftOutcome giftOutcome(const Game& g, const Npc& npc, int value,
                        const string& kind, const string& id) {
    GiftOutcome o;
    const Player& p = g.player;

    // 礼物本身值多少：以「对方一个月的进项」为尺子
    const int scale = 60 + (npc.stage / 3) * 80;
    const double worth = clamp(static_cast<double>(value) / scale, 0.0, 3.0);

    // 基础好感：礼物越重越好，但边际递减
    int delta = std::max(1, jsRound(worth * 6.0));

    // 你的声望：名声在外，对方更当回事
    const int renownBonus = std::min(4, p.renown / 40);
    delta += renownBonus;

    // 境界影响礼物的相对价值，不把低境界送礼自动当作冒犯。
    string why = "礼物按价值与偏好折算";
    int preference = 0;
    if (kind == "herb" && (npc.dao == "木" || npc.dao == "丹" ||
                            npc.sect == "zhushan" || npc.sect == "lihuo")) preference = 3;
    if (kind == "material" && npc.dao == "器") preference = 3;
    if (kind == "potion") {
        const auto* recipe = findRecipe(id);
        preference = npc.dao == "丹" || npc.sect == "lihuo" ? 3 : 1;
        if (recipe && (recipe->dao == npc.dao || recipe->kind == "xp" ||
                       (recipe->kind == "heal" && npc.health < 100))) preference = std::max(preference, 3);
    }
    if (kind == "gear") {
        preference = 1;
        for (const auto& gear : p.arts.gear)
            if (gear.id == id && gear.element == npc.dao) preference = 3;
    }
    delta += preference;
    if (preference >= 3) why += "；符合其修行偏好";

    // 性格：这一项决定「同样的礼，不同人反应完全不同」
    const string& pe = npc.personality;
    if (pe == "重情重义") { delta += 4; why += "；对方重情重义，记你这份心"; }
    else if (pe == "逐利而行") {
        if (worth < 0.6) { why += "；对方逐利，礼物的价值有限"; }
        else { delta += 2; why += "；对方逐利，见礼甚喜"; }
    } else if (pe == "好胜争先") {
        delta -= 1;
        why += "；对方好胜，表达谢意较为克制";
    } else if (pe == "潜心问道") {
        if (value < 30) { delta += 1; why += "；对方潜心问道，不重俗物"; }
        else { why += "；对方潜心问道，更看重修行用途"; }
    } else {
        why += "；对方洒脱随性";
    }

    const NpcBond* bond = findBond(g, npc.id);
    const int repeated = bond && bond->giftMonth == g.clock.days / 30 ? bond->giftsThisMonth : 0;
    const int divisor = 1 + std::max(0, repeated - 2);
    o.affinity = static_cast<int>(std::floor(clampInt(delta, 1, 18) / static_cast<double>(divisor)));
    if (divisor > 1) why += "；本月反复赠礼，好感收益递减，交情不减";
    o.credit = creditFromValue(value);
    o.reason = why;
    return o;
}

GiftResult giftItem(Game& g, const string& npcId, const string& kind,
                    const string& id) {
    GiftResult r;
    r.error = interactionError(g, npcId);
    if (!r.error.empty()) return r;
    Npc* n = findNpc(g, npcId);
    if (!n) { r.error = "找不到这个人。"; return r; }
    if (!n->alive) { r.error = "斯人已逝。"; return r; }
    Player& p = g.player;

    int value = 0;
    string name;

    if (kind == "potion") {
        auto it = p.arts.potions.find(id);
        if (it == p.arts.potions.end() || it->second <= 0) {
            r.error = "你没有这味丹药。";
            return r;
        }
        const data::RecipeDef* rec = findRecipe(id);
        if (!rec) { r.error = "没有这种丹药。"; return r; }
        name = rec->name;
        value = rec->value;
    } else if (kind == "herb") {
        // 药材按名字存
        auto it = p.arts.herbs.find(id);
        if (it == p.arts.herbs.end() || it->second <= 0) {
            r.error = "你没有这味药材。";
            return r;
        }
        const data::HerbDef* h = findHerbByName(id);
        if (!h) { r.error = "没有这种药材。"; return r; }
        name = id;
        value = h ? (h->price > 0 ? h->price : h->sourcePrice) : 20;
    } else if (kind == "material") {
        auto it = p.arts.materials.find(id);
        if (it == p.arts.materials.end() || it->second <= 0) { r.error = "你没有这份炼器材料。"; return r; }
        for (const auto& material : data::MaterialDef_TABLE())
            if (material.id == id) { value = material.price; name = material.name; }
        if (name.empty()) { r.error = "没有这种材料。"; return r; }
    } else if (kind == "gear") {
        auto it = std::find_if(p.arts.gear.begin(), p.arts.gear.end(),
                               [&](const Gear& x) { return x.id == id; });
        if (it == p.arts.gear.end()) { r.error = "你没有这件法器。"; return r; }
        // 装备中的不能送（先卸下）
        for (const auto& [slot, gid] : p.arts.equipped)
            if (gid == id) { r.error = "这件法器正装备着，请先卸下。"; return r; }
        name = it->name;
        value = gearValue(*it);
    } else {
        r.error = "送不了这种东西。";
        return r;
    }

    const GiftOutcome o = giftOutcome(g, *n, value, kind, id);
    addRelation(g, npcId, o.affinity);
    addCredit(g, npcId, o.credit);
    NpcBond& bond = bondOf(g, npcId);
    if (bond.giftMonth != g.clock.days / 30) { bond.giftMonth = g.clock.days / 30; bond.giftsThisMonth = 0; }
    bond.giftsThisMonth = std::min(999999999, bond.giftsThisMonth) + 1;

    // 从玩家背包扣掉
    if (kind == "potion") {
        p.arts.potions[id] -= 1;
        n->bag["potion:" + id] += 1;
        const auto* recipe = findRecipe(id);
        const bool unused = std::find(n->pillsUsed.begin(), n->pillsUsed.end(), id) == n->pillsUsed.end();
        if (recipe && recipe->grade <= 1 + n->stage / 3 && unused) {
            Player proxy; static_cast<Person&>(proxy) = static_cast<const Person&>(*n);
            const int effect = artPillEffect(proxy, *recipe).value;
            bool used = false;
            if (recipe->kind == "heal" && n->health < 100) { n->health = std::min(100, n->health + effect); used = true; }
            else if (recipe->kind == "xp" && n->stage < 12 && n->xp < realmOf(n->stage).need) { n->xp = std::min(realmOf(n->stage).need, n->xp + effect); used = true; }
            else if (recipe->kind == "apt" && n->apt < APT_MAX) { n->apt = std::min(APT_MAX, n->apt + effect); used = true; }
            else if (recipe->kind == "insight" && n->insight < INSIGHT_MAX) { n->insight = std::min(INSIGHT_MAX, n->insight + effect); used = true; }
            else if (recipe->kind == "life") { n->lifespan += effect; n->lifeBonus += effect; used = true; }
            if (used) { n->bag["potion:" + id] -= 1; n->pillsUsed.push_back(id); r.text = n->name + "收下「" + name + "」并当即服用。"; }
        }
    }
    else if (kind == "herb") {
        p.arts.herbs[id] -= 1;
        long long herbs = 0;
        for (const auto& [name, count] : p.arts.herbs) herbs += count;
        p.herbs = static_cast<int>(std::min(static_cast<long long>(std::numeric_limits<int>::max()), herbs));
        n->bag["herb:" + id] += 1;
    }
    else if (kind == "material") { p.arts.materials[id] -= 1; n->bag["material:" + id] += 1; }
    else if (kind == "gear") {
        auto it = std::find_if(p.arts.gear.begin(), p.arts.gear.end(),
                               [&](const Gear& x) { return x.id == id; });
        Gear given = *it;
        p.arts.gear.erase(it);
        // NPC 会真的用起来：比自身装备好就换上
        const string swapped = npcTryEquip(g, *n, given);
        r.text = "你把「" + name + "」赠予" + n->name + "。";
        if (!swapped.empty())
            r.text += n->name + "当即将「" + swapped + "」换下，佩上了这件。";
        else {
            const auto worn = n->equipped.find(given.slot.empty() ? "weapon" : given.slot);
            if (worn != n->equipped.end() && worn->second == given.id) r.text += n->name + "当即佩上了这件。";
        }
    } else {
        r.text = "你把「" + name + "」赠予" + n->name + "。";
    }

    r.ok = true;
    r.affinityDelta = o.affinity;
    r.creditDelta = o.credit;
    if (r.text.empty()) r.text = "你把「" + name + "」赠予" + n->name + "。";

    logEvent(g, "npc",
             r.text + "（好感 " + (o.affinity >= 0 ? "+" : "") +
                 std::to_string(o.affinity) + " · 交情 +" +
                 std::to_string(o.credit) + " · " + o.reason + "）",
             o.affinity >= 6, npcId,
             name + " −1");
    return r;
}

GiftResult giftHerbs(Game& g, const string& npcId, int quantity) {
    GiftResult r;
    r.error = interactionError(g, npcId);
    if (!r.error.empty()) return r;
    if (quantity <= 0 || quantity > 1000) {
        r.error = "赠药份数无效。";
        return r;
    }
    Npc* n = findNpc(g, npcId);
    Player& p = g.player;
    map<string, int> selected;
    int needed = quantity;
    long long value = 0;
    for (const auto& [name, count] : p.arts.herbs) {
        const auto* herb = findHerbByName(name);
        if (!herb || count <= 0) continue;
        const int amount = std::min(count, needed);
        if (amount == 0) break;
        const auto bag = n->bag.find("herb:" + name);
        if (bag != n->bag.end() && static_cast<long long>(bag->second) + amount > 1000000000LL) {
            r.error = "对方已无法收存更多药材。";
            return r;
        }
        selected[name] = amount;
        value += static_cast<long long>(herb->price > 0 ? herb->price : herb->sourcePrice) * amount;
        needed -= amount;
    }
    if (needed > 0 || p.herbs < quantity) {
        r.error = "约定赠送的分类药材已不足，未送出礼物。";
        return r;
    }

    const auto outcome = giftOutcome(g, *n, static_cast<int>(std::min(1000000000LL, value)), "herb");
    addRelation(g, npcId, outcome.affinity);
    addCredit(g, npcId, outcome.credit);
    NpcBond& bond = bondOf(g, npcId);
    if (bond.giftMonth != g.clock.days / 30) { bond.giftMonth = g.clock.days / 30; bond.giftsThisMonth = 0; }
    bond.giftsThisMonth = std::min(999999999, bond.giftsThisMonth) + 1;
    for (const auto& [name, amount] : selected) {
        p.arts.herbs[name] -= amount;
        n->bag["herb:" + name] += amount;
    }
    long long herbs = 0;
    for (const auto& [name, count] : p.arts.herbs) herbs += count;
    p.herbs = static_cast<int>(std::min(static_cast<long long>(std::numeric_limits<int>::max()), herbs));
    r.ok = true;
    r.affinityDelta = outcome.affinity;
    r.creditDelta = outcome.credit;
    r.text = "你将约定的" + std::to_string(quantity) + "份药材赠予" + n->name + "。";
    logEvent(g, "npc", r.text + "（好感 +" + std::to_string(outcome.affinity) +
             " · 交情 +" + std::to_string(outcome.credit) + " · " + outcome.reason + "）",
             false, npcId, "药材 −" + std::to_string(quantity));
    return r;
}

// ============================================================
// NPC 背包
// ============================================================
namespace {

// NPC 背包里的物品：按其境界给「对应及更低境界」的东西。
// 低境界 NPC 不会揣着高阶货，高境界 NPC 也留着一堆用不上的低阶物。
void rollBag(Game& g, Npc& n) {
    n.bag.clear();
    const int stage = clampInt(n.stage, 0, 12);
    const int gradeCap = clampInt(1 + stage / 3, 1, 4);   // 品阶上限

    // 丹药：按丹方品阶筛
    int pillCount = 1 + static_cast<int>(randomOf(g) * 3) + stage / 4;
    for (int i = 0; i < pillCount; ++i) {
        vector<const data::RecipeDef*> pool;
        for (const auto& r : data::RecipeDef_TABLE())
            if (r.grade <= gradeCap) pool.push_back(&r);
        if (pool.empty()) break;
        const data::RecipeDef* r = pickOf(g, pool);
        n.bag["potion:" + string(r->id)] += 1;
    }

    // 药材：2~5 味
    int herbCount = 2 + static_cast<int>(randomOf(g) * 4);
    for (int i = 0; i < herbCount; ++i) {
        vector<const data::HerbDef*> pool;
        for (const auto& h : data::HerbDef_TABLE())
            if (h.grade <= gradeCap) pool.push_back(&h);
        if (pool.empty()) break;
        const data::HerbDef* h = pickOf(g, pool);
        n.bag["herb:" + string(h->name)] += 1;
    }

    // 材料：0~2 份
    if (randomOf(g) < 0.5) {
        vector<const data::MaterialDef*> pool;
        for (const auto& m : data::MaterialDef_TABLE())
            if (m.grade <= gradeCap) pool.push_back(&m);
        if (!pool.empty()) {
            const data::MaterialDef* m = pickOf(g, pool);
            n.bag["material:" + string(m->id)] += 1 + static_cast<int>(randomOf(g) * 2);
        }
    }
}

} // namespace

void ensureNpcBag(Game& g, Npc& npc) {
    if (!npc.bag.empty()) return;
    rollBag(g, npc);
    // 低概率身上带一件法器（境界越高越可能有）
    const double chance = 0.25 + npc.stage * 0.04;
    if (npc.gear.empty() && randomOf(g) < chance) {
        Gear gr;
        const int grade = clampInt(1 + npc.stage / 3, 1, 4);
        gr.id = "npcgear-" + npc.id + "-1";
        gr.name = string("粗制") + (npc.dao == "剑" ? "长剑" : "法器");
        gr.slot = "weapon";
        gr.element = npc.dao;
        gr.grade = grade;
        gr.bonus = 0.02 * grade;
        gr.craftCost = 60 * grade * grade;
        npc.gear.push_back(gr);
        npc.equipped["weapon"] = gr.id;
    }
}

std::vector<BagItem> npcBag(const Game& g, const string& npcId) {
    vector<BagItem> out;
    const Npc* n = nullptr;
    for (const auto& x : g.npcs)
        if (x.id == npcId) n = &x;
    if (!n) return out;
    // 惰性补背包：720 个 NPC 不必开局全部 roll，
    // 玩家真正翻谁的口袋时再生成（也保证「一定翻得到东西」）。
    if (n->bag.empty() && n->gear.empty()) {
        Npc& mut = const_cast<Npc&>(*n);
        ensureNpcBag(const_cast<Game&>(g), mut);
    }

    const int credit = creditOf(g, npcId);

    for (const auto& [key, cnt] : n->bag) {
        if (cnt <= 0) continue;
        const size_t colon = key.find(':');
        if (colon == string::npos) continue;
        const string kind = key.substr(0, colon);
        const string id = key.substr(colon + 1);

        BagItem it;
        it.kind = kind;
        it.id = id;
        it.count = cnt;

        if (kind == "potion") {
            const data::RecipeDef* r = findRecipe(id);
            it.name = r ? r->name : id;
            it.value = r ? r->value : 40;
            it.desc = r ? string("丹药 · ") + std::to_string(r->grade) + " 品"
                        : "丹药";
        } else if (kind == "herb") {
            const data::HerbDef* h = findHerbByName(id);
            it.name = id;
            it.value = h ? (h->price > 0 ? h->price : h->sourcePrice) : 20;
            it.desc = h ? string("药材 · ") + std::to_string(h->grade) + " 品"
                        : "药材";
        } else if (kind == "material") {
            const data::MaterialDef* m = nullptr;
            for (const auto& x : data::MaterialDef_TABLE())
                if (x.id == id) m = &x;
            it.name = m ? m->name : id;
            it.value = m ? m->price : 30;
            it.desc = m ? string("材料 · ") + std::to_string(m->grade) + " 品"
                        : "材料";
        } else {
            continue;
        }

        it.creditCost = tradeCost(it.value);
        it.affordable = credit >= it.creditCost;
        if (!it.affordable)
            it.reason = "交情不足（需 " + std::to_string(it.creditCost) +
                        "，有 " + std::to_string(credit) + "）";
        out.push_back(it);
    }

    // 身上的法器也能讨（但要花大价钱）
    for (const auto& gr : n->gear) {
        BagItem it;
        it.kind = "gear";
        it.id = gr.id;
        it.name = gr.name;
        it.count = 1;
        it.value = gearValue(gr);
        it.desc = "法器 · " + std::to_string(gr.grade) + " 品";
        it.creditCost = creditFromValue(it.value) * 2;   // 贴身之物，双倍人情
        it.affordable = credit >= it.creditCost;
        if (!it.affordable)
            it.reason = "交情不足（需 " + std::to_string(it.creditCost) +
                        "，有 " + std::to_string(credit) + "）";
        out.push_back(it);
    }

    std::sort(out.begin(), out.end(), [](const BagItem& a, const BagItem& b) {
        return a.creditCost < b.creditCost;
    });
    return out;
}

TradeResult takeFromNpc(Game& g, const string& npcId, const string& kind,
                        const string& id) {
    TradeResult r;
    r.error = interactionError(g, npcId);
    if (!r.error.empty()) return r;
    Npc* n = findNpc(g, npcId);
    if (!n) { r.error = "找不到这个人。"; return r; }
    if (!n->alive) { r.error = "斯人已逝。"; return r; }

    int value = 0;
    string name;

    if (kind == "gear") {
        auto it = std::find_if(n->gear.begin(), n->gear.end(),
                               [&](const Gear& x) { return x.id == id; });
        if (it == n->gear.end()) { r.error = "对方身上没有这件法器。"; return r; }
        // 正装备着的不给
        for (const auto& [slot, gid] : n->equipped)
            if (gid == id) { r.error = "对方正用着这件，不肯给。"; return r; }
        name = it->name;
        value = gearValue(*it);
        r.cost = creditFromValue(value) * 2;
        if (!spendCredit(g, npcId, r.cost)) {
            r.error = "交情不足（需 " + std::to_string(r.cost) + "）。";
            return r;
        }
        Gear got = *it;
        n->gear.erase(it);
        g.player.arts.gear.push_back(got);
    } else {
        const string key = kind + ":" + id;
        auto it = n->bag.find(key);
        if (it == n->bag.end() || it->second <= 0) {
            r.error = "对方没有这个。";
            return r;
        }
        if (kind == "potion") {
            const data::RecipeDef* rec = findRecipe(id);
            name = rec ? rec->name : id;
            value = rec ? rec->value : 40;
        } else if (kind == "herb") {
            name = id;
            const data::HerbDef* h = findHerbByName(id);
            value = h ? (h->price > 0 ? h->price : h->sourcePrice) : 20;
        } else if (kind == "material") {
            for (const auto& x : data::MaterialDef_TABLE())
                if (x.id == id) { name = x.name; value = x.price; }
        } else {
            r.error = "讨不了这种东西。";
            return r;
        }
        r.cost = tradeCost(value);
        if (!spendCredit(g, npcId, r.cost)) {
            r.error = "交情不足（需 " + std::to_string(r.cost) +
                      "，有 " + std::to_string(creditOf(g, npcId)) + "）。";
            return r;
        }
        it->second -= 1;
        if (it->second <= 0) n->bag.erase(it);

        if (kind == "potion") g.player.arts.potions[id] += 1;
        else if (kind == "herb") {
            g.player.arts.herbs[id] += 1;
            long long herbs = 0;
            for (const auto& [name, count] : g.player.arts.herbs) herbs += count;
            g.player.herbs = static_cast<int>(std::min(static_cast<long long>(std::numeric_limits<int>::max()), herbs));
        }
        else if (kind == "material") g.player.arts.materials[id] += 1;
    }

    r.ok = true;
    r.text = "你以人情向" + n->name + "讨来「" + name + "」。";
    logEvent(g, "npc",
             r.text + "（交情 −" + std::to_string(r.cost) + "）", false, npcId);
    return r;
}

// ============================================================
// NPC 用玩家送的装备
// ============================================================
std::string npcTryEquip(Game& g, Npc& npc, const Gear& gear) {
    (void)g;
    // 先复制输入，再动vector；调用者可能传入自身背包元素。
    const Gear incoming = gear;
    const string slot = incoming.slot.empty() ? string("weapon") : incoming.slot;
    auto received = std::find_if(npc.gear.begin(), npc.gear.end(), [&](const Gear& owned) { return owned.id == incoming.id; });
    if (received == npc.gear.end()) npc.gear.push_back(incoming);
    auto it = npc.equipped.find(slot);
    if (it == npc.equipped.end()) {
        // 该槽位空着，直接换上
        npc.equipped[slot] = incoming.id;
        return "";
    }

    // 找现在装备的那件，比一比。
    // 注意：先按值把旧装备拷出来，再动 npc.gear ——
    // 之前直接持有 gear 里的引用，push_back 触发重新分配后
    // 那个引用就成了野指针（会 bad_alloc 甚至更糟）。
    const string curId = it->second;
    Gear old;
    bool found = false;
    for (const auto& x : npc.gear) {
        if (x.id != curId) continue;
        old = x;
        found = true;
        break;
    }
    if (!found) {
        // 装备栏指向的东西不在列表里（旧档可能这样），直接补上
        npc.equipped[slot] = incoming.id;
        return "";
    }

    const bool better = incoming.grade > old.grade ||
                        (incoming.grade == old.grade && incoming.bonus > old.bonus);
    if (!better) return "";

    // 换上新的：旧的从装备栏摘下，但仍留在他的 gear 列表里（只是不戴了）
    npc.equipped[slot] = incoming.id;
    return old.name;
}

// ============================================================
// NPC 流派与神通
// ============================================================
namespace {

// 宗门 → 本命大道
string daoOfSect(const string& sect) {
    if (sect == "jinhong") return "剑";
    if (sect == "xinghe") return "水";
    if (sect == "zhushan") return "木";
    if (sect == "lihuo") return "火";
    if (sect == "huachen") return "土";
    return "";
}

} // namespace

void ensureNpcCultivation(Game& g, Npc& npc) {
    if (!npc.dao.empty() && !npc.spells.empty()) return;
    if (npc.beast) { npc.dao = "体"; return; }

    // 有宗门就按宗门定，散修按性格与随机
    string dao = daoOfSect(npc.sect);
    if (dao.empty()) {
        static const char* POOL[8] = {"剑", "火", "水", "木", "金", "土", "神", "气"};
        if (npc.personality == "潜心问道") dao = "神";
        else if (npc.personality == "好胜争先") dao = "剑";
        else if (npc.personality == "洒脱随性") dao = "水";
        else dao = POOL[randIntOf(g, 0, 7)];
    }
    npc.dao = dao;

    // 神通：按境界给 1~3 门本系神通，境界越高越多、越强
    vector<const data::SpellDef*> pool;
    for (const auto& sp : data::SpellDef_TABLE())
        if (string(sp.dao) == dao) pool.push_back(&sp);
    // 本系没有就退回通用
    if (pool.empty()) {
        for (const auto& sp : data::SpellDef_TABLE())
            if (string(sp.dao) == "气") pool.push_back(&sp);
    }
    const int want = clampInt(1 + npc.stage / 4, 1, 3);
    for (int i = 0; i < want && !pool.empty(); ++i) {
        const data::SpellDef* sp = pickOf(g, pool);
        if (std::find(npc.spells.begin(), npc.spells.end(), sp->id) ==
            npc.spells.end())
            npc.spells.push_back(sp->id);
    }
}

void ensureAllNpcData(Game& g) {
    for (auto& n : g.npcs) {
        ensureNpcCultivation(g, n);
        // 背包只给「活着且有过往来可能」的补，避免开局就给 720 人 roll
        if (n.alive && n.bag.empty()) ensureNpcBag(g, n);
    }
}

} // namespace nz::friendship
