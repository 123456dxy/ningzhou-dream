// 宁州一梦 · C++ 版 — 游历与后山实现（对应 Demo 06 的 adventure.mjs）
#include "core/adventure.h"

#include <algorithm>

#include "core/arts.h"
#include "core/battle.h"
#include "core/economy.h"
#include "core/reward_receipt.h"
#include "core/sim.h"
#include "core/sword.h"
#include "data/reference_tables.h"

namespace nz {

// 用途：按境界可得药材兑现灵药奖励；参与流程：药谷抉择发奖。
// 使用方式：传最大采得份数，按当前事件/RNG值稳定选药但不再推进随机数；返回值：真实入袋数量并同步野外余量。
// 用途：把泛称灵药奖励兑现为适境分类药材。
// 参与流程：药谷事件救援等实际奖励。
// 使用方式：传最大份数，受世界库存和背包上限限制，使用当前 RNG 值选药。
// 返回值：实际数量；更新分类药材、总量和野外余量，不推进 RNG。
int adventureHarvestHerbs(Game& game, int maximum) {
    vector<const data::HerbDef*> candidates;
    const int gradeLimit = std::min(3, 1 + std::max(0, game.player.stage) / 3);
    for (const auto& herb : data::HerbDef_TABLE()) if (herb.grade <= gradeLimit) candidates.push_back(&herb);
    if (candidates.empty()) return 0;
    long long typed = 0;
    for (const auto& [name, amount] : game.player.arts.herbs) typed += std::max(0, amount);
    const int amount = static_cast<int>(std::max(0LL, std::min({static_cast<long long>(maximum),
        static_cast<long long>(game.world.herbs), 1000000000LL - typed,
        1000000000LL - game.player.herbs})));
    for (int index = 0; index < amount; ++index) {
        const size_t choice = (static_cast<uint64_t>(game.rng) + game.eventSeq + index) % candidates.size();
        ++game.player.arts.herbs[candidates[choice]->name];
    }
    game.player.herbs += amount;
    game.world.herbs -= amount;
    return amount;
}

// 六类遇事（顺序与 JS 的 ADV_KINDS 一致）
// 用途：列出六种游历遇事类型。
// 参与流程：游历事件随机选择。
// 使用方式：无参数，列表顺序保持既有事件协议。
// 返回值：遇事类型常量列表引用。
const vector<string>& advKinds() {
    static const vector<string> data = {"beast", "bandit", "justice",
                                        "backhill", "heritage", "caravan"};
    return data;
}

// 遇事标题
// 用途：按遇事类型生成事件标题。
// 参与流程：奇遇 Pending 生成。
// 使用方式：传 kind 旧值。
// 返回值：对应标题；未知类型为游历见闻。
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
// 用途：按事件种类指定对手流派。
// 参与流程：奇遇敌人上下文创建。
// 使用方式：传遇事类型。
// 返回值：战斗大道旧键，未列类型按体道。
static string advStyleOf(const string& kind) {
    if (kind == "beast") return "木";
    if (kind == "bandit") return "剑";
    if (kind == "justice") return "体";
    if (kind == "backhill") return "土";
    return "体";
}

// 按境界取矿材 id（对应 JS 的 ['iron','copper','gold','meteor','dragon']）
// 用途：按大境界选奇遇矿材 ID。
// 参与流程：行商交易及战斗奖励。
// 使用方式：传小境界，组序号夹在零至四。
// 返回值：铁至龙材的既有矿材 ID。
static string materialForStage(int stage) {
    static const char* table[5] = {"iron", "copper", "gold", "meteor",
                                   "dragon"};
    return table[clampInt(stage / 3, 0, 4)];
}

// 用途：保留奇遇状态初始化兼容入口。
// 参与流程：建号与读档初始化调用。
// 使用方式：默认构造/解码已用 -1 区分未探访；当前无需补值。
// 返回值：无；当前实现不修改游戏。
void ensureAdventure(Game& g) {
    // 默认构造与缺字段读档均使用 -1；0 是第一个月已探访的有效记录。
    (void)g;
}

// ------------------------------------------------------------
// 发放法器（对应 adventureGear）
// ------------------------------------------------------------
// 用途：在行囊有空间时发放一件奇遇法器。
// 参与流程：天赋初始装备和奇遇战利品。
// 使用方式：传名称、槽位、品阶、收益、元素和来源。
// 返回值：成功为真并更新 gear、序号和日志；袋满为假。
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
// 用途：发放本世建号的奇遇类天赋资源。
// 参与流程：创建新角色的初始传承。
// 使用方式：仅新建时调用，读档不再次补发。
// 返回值：无；更新材料、悟道、抄本、丹炉与初始装备。
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
        ecoSyncBooks(g);
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
// 用途：按事件上下文构建可选行动。
// 参与流程：奇遇 Pending 生成。
// 使用方式：传玩家与冻结事件，按健康、灵石和法器空间禁用选项。
// 返回值：ChoiceOption 列表，不执行奖励或战斗。
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
                       "迎战守阵灵影；胜后获得未持有的适境功法抄本（需修习）；全有则剑道经验+12",
                       p.health < 35});
        out.push_back({"leave", "记下阵势，暂且离开",
                       "阵道经验+4，不发生战斗", false});
        return out;
    }
    if (d.kind == "heritage") {
        out.push_back({"book", "修补碑文，抄录功法",
                       "消耗12灵石，获得一部未持有的适境功法抄本（需修习）；全有则剑道经验+12",
                       p.stones < 12});
        out.push_back({"study", "只参悟残存道韵",
                       "当前大道经验+8，剑道经验+1", false});
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
                   foeLabel + "对手；胜后得灵石、材料或声望；生死斗失败会死亡，可选择回档",
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
// 用途：生成冻结奖励上下文并提出游历抉择。
// 参与流程：游历触发及后山完成入口。
// 使用方式：传游戏与事件类型，按当前境界和已持抄本选奖励。
// 返回值：无；写 Pending、奇遇计数、抄本同步、RNG 和日志。
void adventureEvent(Game& g, const string& kindIn) {
    Player& p = g.player;
    Adventure& d = g.adventure;
    const string kind = kindIn;
    const int group = p.stage / 3;
    ecoSyncBooks(g);

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
// 用途：累积真实游历天数并检查遇事。
// 参与流程：游历日历推进。
// 使用方式：传实际完成天数；未满二十天不触发，六十天必遇。
// 返回值：无；更新游历进度，可能生成抉择并推进 RNG。
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
// 用途：检查主动后山探访的地点、伤势与月额。
// 参与流程：探访动作前置。
// 使用方式：传游戏和 backhill ID，只做校验。
// 返回值：拒绝原因；可探访时为空。
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

// 用途：收费并记录本月后山探访工作。
// 参与流程：已通过校验的主动探访。
// 使用方式：传游戏和探访 ID；调用前先校验，当前仅后山。
// 返回值：无；扣八灵石，记月额、建立七天 Job 并记日志。
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

// 用途：在探访完成后生成后山遇事。
// 参与流程：后山 Job 到期回调。
// 使用方式：传游戏，调用 adventureEvent 的 backhill 分支。
// 返回值：无；更新奇遇抉择、计数、RNG 和日志。
void adventureComplete(Game& g) { adventureEvent(g, "backhill"); }

// 获得功法抄本（对应 adventureBook）
// 用途：发放新的合法抄本或转为剑道经验。
// 参与流程：残碑和后山奖励。
// 使用方式：传游戏与选定抄本 ID，不自动修习。
// 返回值：无；更新抄本或统一剑道经验并记奖励日志。
static void adventureBook(Game& g, const string& id) {
    ManualInfo info;
    if (!id.empty() && findManualInfo(id, info) && ecoAcquireBook(g, id, "游历遗藏")) {
        // 抄录只新增抄本，修习仍需要时间、灵石与对应境界。
    } else {
        artGain(g.player, "剑", 12);
        logEvent(g, "self", "未找到可抄录的新传承，转为参悟剑道；实际经验见本次结算。");
    }
}

// ------------------------------------------------------------
// 玩家选择（对应 adventureChoose）
// ------------------------------------------------------------
// 用途：消费当前游历抉择并执行选项。
// 参与流程：Pending 选择动作。
// 使用方式：传游戏与选项 ID，战斗奖励上下文先登记再开战。
// 返回值：属于游历事件时为真（含无效选择），否则假；有效选择改状态。
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
    const RewardReceipt receipt(g);
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
                 receipt.changes(g) + "。");
    g.pending.reset();
    return true;
}

// ------------------------------------------------------------
// 游历斗法结算（对应 adventureBattleEnd）
// ------------------------------------------------------------
// 用途：按胜负兑现冻结的奇遇战利品。
// 参与流程：battleFinish 奇遇回调。
// 使用方式：传完成 Battle 快照，失败不发奖励，撤离另处理。
// 返回值：无；修改奇遇胜场、实际资源、抄本/法器及日志。
void adventureBattleEnd(Game& g, const Battle& b) {
    if (b.advKind.empty()) return;
    Player& p = g.player;
    Arts& a = p.arts;
    const RewardReceipt receipt(g);

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
        logEvent(g, "self", "后山遗藏结算：" + receipt.changes(g) + "。", true);
        return;
    }

    // 妖兽 / 劫修 / 行侠
    p.stones += b.advFee;
    const string mat = materialForStage(p.stage);
    if (b.advKind == "beast") {
        a.materials[mat] = a.materials[mat] + 2;
        p.herbs += 2;
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
    }
    logEvent(g, "self",
             "游历斗法获胜：" + receipt.changes(g) + "。",
             true);
}

} // namespace nz
