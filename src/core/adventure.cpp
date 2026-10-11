// 宁州一梦 · C++ 版 — 游历与后山实现（对应 Demo 06 的 adventure.mjs）
#include "core/adventure.h"

#include <algorithm>

#include "core/arts.h"
#include "core/battle.h"
#include "core/economy.h"
#include "core/sim.h"
#include "core/sword.h"
#include "data/reference_tables.h"

namespace nz {

// 六类遇事（顺序与 JS 的 ADV_KINDS 一致）
const vector<string>& advKinds() {
    static const vector<string> data = {"beast", "bandit", "justice",
                                        "backhill", "heritage", "caravan"};
    return data;
}

// 遇事标题
static string advTitle(const string& kind) {
    if (kind == "beast") return "药径妖影";
    if (kind == "bandit") return "山道劫修";
    if (kind == "justice") return "路见不平";
    if (kind == "backhill") return "后山遗藏";
    if (kind == "heritage") return "残碑问道";
    if (kind == "caravan") return "行商遇险";
    return "游历见闻";
}

// 对手战斗风格（与 JS 的映射一致）
static string advStyleOf(const string& kind) {
    if (kind == "beast") return "木";
    if (kind == "bandit") return "剑";
    if (kind == "justice") return "体";
    if (kind == "backhill") return "土";
    return "体";
}

// 按境界取矿材 id（对应 JS 的 ['iron','copper','gold','meteor','dragon']）
static string materialForStage(int stage) {
    static const char* table[5] = {"iron", "copper", "gold", "meteor",
                                   "dragon"};
    return table[clampInt(stage / 3, 0, 4)];
}

void ensureAdventure(Game& g) {
    // 默认构造即为初始状态，这里只保证 backhillMonth 已初始化
    if (g.adventure.backhillMonth == 0 && g.clock.days == 0)
        g.adventure.backhillMonth = -1;
}

// ------------------------------------------------------------
// 发放法器（对应 adventureGear）
// ------------------------------------------------------------
bool adventureGear(Game& g, const string& name, const string& slot, int grade,
                   double bonus, const string& element,
                   const string& source) {
    Arts& a = g.player.arts;
    if (a.gear.size() >= 40) {
        logEvent(g, "self",
                 source + "发现法器，但行囊已满，未能带走；请先出售或分解法器。");
        return false;
    }
    Gear gear;
    gear.id = "gear-" + std::to_string(g.clock.days) + "-" +
              std::to_string(g.economy.seq++);
    gear.name = name;
    gear.slot = slot;
    gear.grade = grade;
    gear.bonus = bonus;
    gear.element = element;
    gear.polarity = "天阳";
    // 五维基础 240/220/260/230/250，随品阶 +60，上限 1000
    static const int base[5] = {240, 220, 260, 230, 250};
    for (int i = 0; i < 5; ++i)
        gear.dimensions.push_back(std::min(1000, base[i] + grade * 60));
    gear.inscribed = false;
    gear.craftCost = 35 * grade * grade;
    a.gear.push_back(gear);

    // 百分比显示保留一位小数（与 JS 的 toFixed(1) 一致）
    const double pct = std::floor(bonus * 1000.0 + 0.5) / 10.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", pct);
    logEvent(g, "self",
             "从" + source + "获得" + std::to_string(grade) + "品「" + name +
                 "」，" + (slot == "weapon" ? "伤害" : "气血上限") + "+" +
                 string(buf) + "%；可在洞府炼器页装备。",
             true);
    return true;
}

// ------------------------------------------------------------
// 开局物品（对应 adventureBirth）
// ------------------------------------------------------------
void adventureBirth(Game& g) {
    Player& p = g.player;
    Arts& a = p.arts;
    auto has = [&](const char* id) {
        return std::find(p.traits.begin(), p.traits.end(), string(id)) !=
               p.traits.end();
    };

    // 铸器传人：器道经验 30 + 百炼真解抄本 + 锻剑铁 4
    if (has("forgeheir")) {
        artGain(p, "器", 30);
        a.materials["iron"] = a.materials["iron"] + 4;
        if (std::find(a.bookList.begin(), a.bookList.end(), "forge") ==
            a.bookList.end())
            a.bookList.push_back("forge");
    }

    // 丹鼎传家 / 丹宗遗脉：直接给高阶丹炉
    string furnace;
    if (has("furnancescion") || has("furnacescion")) furnace = "furnace-5";
    else if (has("furnaceheir")) furnace = "furnace-4";
    if (!furnace.empty()) {
        a.furnace = furnace;
        for (const auto& f : data::FurnaceDef_TABLE())
            if (f.id == furnace)
                logEvent(g, "self",
                         "随身丹炉：" + f.name +
                             "。高品炉不豁免境界与药材要求。",
                         true);
    }

    // 藏剑遗赠：一品灵剑，已装备
    if (has("heirblade")) {
        if (adventureGear(g, "祖传灵剑", "weapon", 1, 0.04, "剑", "藏剑遗赠"))
            a.equipped["weapon"] = a.gear.back().id;
    }
    // 护身旧甲：一品法衣，已装备
    if (has("artifactward")) {
        if (adventureGear(g, "护身法衣", "armor", 1, 0.06, "土", "护身旧甲"))
            a.equipped["armor"] = a.gear.back().id;
    }
}

// ------------------------------------------------------------
// 遇事选项（对应 adventureOptions）
// ------------------------------------------------------------
static vector<ChoiceOption> advOptions(const Player& p, const AdvContext& d) {
    vector<ChoiceOption> out;
    const int foe = d.stage;
    const string foeLabel = (foe == p.stage) ? "同境" : "高一小境";

    if (d.kind == "backhill") {
        out.push_back({"gear", "破阵取法器",
                       "迎战" + foeLabel + "守阵灵影；胜后获得" +
                           std::to_string(d.grade) + "品法器",
                       p.arts.gear.size() >= 40 || p.health < 35});
        out.push_back({"book", "寻取功法抄本",
                       "迎战守阵灵影；胜后获得未持有的适境功法",
                       p.health < 35});
        out.push_back({"leave", "记下阵势，暂且离开",
                       "阵道经验+4，不发生战斗", false});
        return out;
    }
    if (d.kind == "heritage") {
        out.push_back({"book", "修补碑文，抄录功法",
                       "消耗12灵石，获得一部未持有的适境功法；全有则剑道经验+12",
                       p.stones < 12});
        out.push_back({"study", "只参悟残存道韵",
                       "当前大道经验+8，剑意+1", false});
        out.push_back({"leave", "留下标记，不扰遗迹", "不取奖励", false});
        return out;
    }
    if (d.kind == "caravan") {
        out.push_back({"help", "帮助商旅修整车阵",
                       "消耗8灵石；领取16灵石工钱、器道经验+4",
                       p.stones < 8});
        out.push_back({"trade", "交换一批矿材",
                       "消耗12灵石，获得同阶矿材2份", p.stones < 12});
        out.push_back({"leave", "问路后继续赶路", "悟性+1", false});
        return out;
    }

    // 妖兽 / 劫修 / 行侠
    out.push_back({"fight", (d.kind == "justice") ? "出手相助" : "迎战",
                   foeLabel + "对手；胜后得灵石、材料或声望，败退会受伤",
                   p.health < 35});
    if (d.kind == "beast")
        out.push_back({"talk", "引开妖兽，绕路采药",
                       "未分类灵药+1，避开斗法", false});
    else if (d.kind == "justice")
        out.push_back({"talk", "掩护受困者撤离",
                       "声望+1，气血−3（不致死）", false});
    else
        out.push_back({"talk", "交出路资脱身",
                       "交出至多12灵石，平安脱身", false});
    out.push_back({"leave", "避开冲突", "安全离开，不得战利品", false});
    return out;
}

// ------------------------------------------------------------
// 触发遇事（对应 adventureEvent）
// ------------------------------------------------------------
void adventureEvent(Game& g, const string& kindIn) {
    Player& p = g.player;
    Adventure& d = g.adventure;
    const string kind = kindIn;
    const int group = p.stage / 3;

    // 附近一位真实人物（行侠时可用）
    const Npc* npc = nullptr;
    for (const auto& n : g.npcs)
        if (n.alive && n.location == p.location) { npc = &n; break; }

    // 对手境界：同境，18% 概率高一阶
    const int foe = std::min(12, p.stage + ((randomOf(g) < 0.18) ? 1 : 0));

    // 可能掉落的功法抄本（未持有且境界允许）
    vector<string> bookPool;
    for (const auto& m : data::BaseManualDef_TABLE())
        if (m.stage <= p.stage &&
            std::find(g.economy.books.begin(), g.economy.books.end(), m.id) ==
                g.economy.books.end())
            bookPool.push_back(m.id);
    for (const auto& m : data::ManualDef_TABLE())
        if (m.stage <= p.stage &&
            std::find(g.economy.books.begin(), g.economy.books.end(), m.id) ==
                g.economy.books.end())
            bookPool.push_back(m.id);

    // 地点描述
    string place;
    if (ecoIsCity(p.location)) place = "城外后山";
    else if (p.location == p.sect) place = "宗门后山";
    else place = "山野深处";

    AdvContext data;
    data.kind = kind;
    data.seq = ++d.seq;
    data.stage = foe;
    data.fee = 18 + group * 8;
    data.book = bookPool.empty() ? "" : pickOf(g, bookPool);
    data.npcId = npc ? npc->id : "";
    data.grade = std::min(5, 1 + group);
    data.style = advStyleOf(kind);

    // 正文
    string text;
    if (kind == "beast")
        text = "山道旁的药香引来一头妖兽。它守着药丛，尚未发现绕向山背的小径。";
    else if (kind == "bandit")
        text = "两名劫修拦住去路，索要买路钱。领头者按住剑柄，仍留着交涉的余地。";
    else if (kind == "justice")
        text = (npc ? npc->name : string("一名采药人")) +
               "被拦在断桥边。你可以正面击退恶徒，也可以掩护对方撤离。";
    else if (kind == "backhill")
        text = place +
               "的废弃洞府露出石门。守阵灵影未散，架上的法器与功法需要击破禁制才能取走。";
    else if (kind == "heritage")
        text = "雨水冲开残碑上的苔痕。碑文还留着一卷适合此境修习的功诀，修补与抄录需要耗费灵石。";
    else if (kind == "caravan")
        text = "商旅的车阵停在泥泞山路上。修整灵阵或交换矿料，都是这次游历能够亲手做的事。";
    else
        text = "旅途上遇到一段插曲。";

    Pending ev;
    ev.type = "adventure";
    ev.title = advTitle(kind);
    ev.subtitle = "游历机缘 · 可选择避让";
    ev.text = text;
    ev.options = advOptions(p, data);
    ev.adventure = data;

    g.pending = ev;
    d.lastKind = kind;
    d.since = 0;
    d.events++;

    logEvent(g, "self", "游历遇见「" + ev.title + "」，等待你的选择。", true);
}

// ------------------------------------------------------------
// 游历推进（对应 adventureExplore）
// ------------------------------------------------------------
void adventureExplore(Game& g, int days) {
    Adventure& d = g.adventure;
    d.since += days;
    d.total += days;

    if (g.pending.has_value() || g.ended || g.player.health <= 0) return;
    if (d.since < 20) return;

    // 满 20 天后有机会遇事；连续累计 60 天必遇
    const double chance = 1.0 - std::pow(0.45, days / 30.0);
    if (d.since < 60 && randomOf(g) >= chance) return;

    // 不与上一次同类
    vector<string> pool;
    for (const auto& k : advKinds())
        if (k != d.lastKind) pool.push_back(k);
    adventureEvent(g, pickOf(g, pool));
}

// ------------------------------------------------------------
// 后山探访
// ------------------------------------------------------------
string adventureActionError(Game& g, const string& id) {
    if (id != "backhill") return "未知探访。";
    const Player& p = g.player;
    if (!ecoIsCity(p.location) && p.location != p.sect)
        return "请在城池或本宗山门探访后山。";
    if (p.health < 35) return "请先休养至35气血。";
    if (p.stones < 8) return "探访需8灵石整备。";
    if (g.adventure.backhillMonth == g.clock.days / 30)
        return "本月已探访过后山。";
    return "";
}

void adventureBegin(Game& g, const string& id) {
    (void)id;
    g.player.stones -= 8;
    g.adventure.backhillMonth = g.clock.days / 30;

    Job job;
    job.actionType = "expedition";
    job.name = "探访后山";
    job.total = 7;
    job.remaining = 7;
    job.started = g.clock.days;
    job.cost = 8;
    job.paid = true;
    job.destination = "backhill";
    g.job = job;

    logEvent(g, "self", "整备后山探访，需7天、8灵石；遗藏要经过守阵斗法才能取得。");
}

void adventureComplete(Game& g) { adventureEvent(g, "backhill"); }

// 获得功法抄本（对应 adventureBook）
static void adventureBook(Game& g, const string& id) {
    if (!id.empty() &&
        std::find(g.economy.books.begin(), g.economy.books.end(), id) ==
            g.economy.books.end()) {
        g.economy.books.push_back(id);
        for (const auto& m : data::BaseManualDef_TABLE())
            if (m.id == id)
                logEvent(g, "self",
                         "获得功法「" + m.name + "」，来源：游历遗藏。", true);
        for (const auto& m : data::ManualDef_TABLE())
            if (m.id == id)
                logEvent(g, "self",
                         "获得功法「" + m.name + "」，来源：游历遗藏。", true);
    } else {
        artGain(g.player, "剑", 12);
        logEvent(g, "self", "已有此处传承，转为参悟剑道经验+12。");
    }
}

// ------------------------------------------------------------
// 玩家选择（对应 adventureChoose）
// ------------------------------------------------------------
bool adventureChoose(Game& g, const string& optionId) {
    if (!g.pending.has_value() || g.pending->type != "adventure") return false;
    Pending ev = *g.pending;
    if (!ev.adventure.has_value()) return false;
    const AdvContext data = *ev.adventure;

    Player& p = g.player;
    Arts& a = p.arts;

    // 校验选项存在且可用
    bool found = false;
    for (const auto& o : ev.options) {
        if (o.id == optionId) {
            if (o.disabled) return true;   // 不可选，视为已处理
            found = true;
        }
    }
    if (!found) return true;

    // ---- 需要打一场的选择 ----
    const bool needBattle =
        (optionId == "fight" || optionId == "gear") ||
        (optionId == "book" && data.kind == "backhill");
    if (needBattle) {
        if (p.health < 35) return true;
        if (optionId == "gear" && a.gear.size() >= 40) return true;

        g.pending.reset();
        EnemySpec enemy;
        if (data.kind == "beast") enemy.name = "守药妖兽";
        else if (data.kind == "bandit") enemy.name = "山道劫修";
        else if (data.kind == "justice") enemy.name = "拦路恶徒";
        else enemy.name = "守阵灵影";
        enemy.stage = data.stage;
        enemy.style = data.style;
        enemy.kind = "wild";
        enemy.beast = data.kind == "beast";
        enemy.adventure = data;
        enemy.adventureChoice = optionId;
        // 只有剑修风格的对手才给剑意
        enemy.sword = (data.style == "剑") ? 15 + data.stage * 20 : 0;
        battleStart(g, enemy);
        return true;
    }

    // ---- 非战斗选择 ----
    if (data.kind == "heritage" && optionId == "book") {
        if (p.stones < 12) return true;
        p.stones -= 12;
        adventureBook(g, data.book);
    } else if (data.kind == "heritage" && optionId == "study") {
        artGain(p, a.focus, 8);
        swordGain(g, 1);
    } else if (data.kind == "caravan" && optionId != "leave") {
        const int cost = (optionId == "help") ? 8 : 12;
        if (p.stones < cost) return true;
        p.stones -= cost;
        if (optionId == "help") {
            p.stones += 16;
            artGain(p, "器", 4);
        } else {
            const string mat = materialForStage(p.stage);
            a.materials[mat] = a.materials[mat] + 2;
        }
    } else if (optionId == "talk") {
        if (data.kind == "bandit") p.stones -= std::min(12, p.stones);
        if (data.kind == "beast") p.herbs++;
        if (data.kind == "justice") {
            p.renown++;
            p.health = std::max(1, p.health - 3);
        }
    } else if (optionId == "leave" && data.kind == "backhill") {
        artGain(p, "阵", 4);
    } else if (optionId == "leave" && data.kind == "caravan") {
        p.insight = std::min(INSIGHT_MAX, p.insight + 1);
    }

    // 找到所选选项名用于纪事
    string optName = optionId;
    for (const auto& o : ev.options)
        if (o.id == optionId) optName = o.name;

    logEvent(g, "self",
             "「" + ev.title + "」：" + optName + "。" +
                 (optionId == "leave" ? "你记下沿途见闻，继续前行。"
                                      : "所选报酬与代价已结算。"));
    g.pending.reset();
    return true;
}

// ------------------------------------------------------------
// 游历斗法结算（对应 adventureBattleEnd）
// ------------------------------------------------------------
void adventureBattleEnd(Game& g, const Battle& b) {
    if (b.advKind.empty()) return;
    Player& p = g.player;
    Arts& a = p.arts;

    if (!b.won) {
        if (!b.retreat) {
            const int lost = std::min(p.stones, 8 + (p.stage / 3) * 4);
            p.stones -= lost;
            logEvent(g, "self", "游历斗法败退，丢失" + std::to_string(lost) +
                                    "灵石行装；未取得战利品。");
        }
        return;
    }

    g.adventure.wins++;

    if (b.advKind == "backhill") {
        if (b.advChoice == "book") {
            adventureBook(g, b.advBook);
        } else {
            // 交替给剑/法衣
            const bool isSword = (b.advSeq % 2) != 0;
            const double bonus = std::floor(b.advGrade * 0.035 * 1000.0 + 0.5) /
                                 1000.0;
            adventureGear(g,
                          "遗府" + std::to_string(b.advGrade) + "品" +
                              (isSword ? "灵剑" : "法衣"),
                          isSword ? "weapon" : "armor", b.advGrade, bonus,
                          isSword ? "剑" : "土", "后山遗藏");
        }
        a.progress.quests++;
        return;
    }

    // 妖兽 / 劫修 / 行侠
    p.stones += b.advFee;
    const string mat = materialForStage(p.stage);
    string extra;
    if (b.advKind == "beast") {
        a.materials[mat] = a.materials[mat] + 2;
        p.herbs += 2;
        extra = "、矿材2份、未分类灵药2份";
    }
    if (b.advKind == "justice") {
        p.renown += 3;
        if (!b.advNpcId.empty()) {
            for (const auto& n : g.npcs) {
                if (n.id != b.advNpcId || !n.alive) continue;
                const int cur = relationOf(g, n.id);
                g.player.relations[n.id] = std::min(100, cur + 8);
            }
        }
        extra = "、声望3";
    }
    logEvent(g, "self",
             "游历斗法获胜，收获" + std::to_string(b.advFee) + "灵石" + extra +
                 "。",
             true);
}

} // namespace nz
