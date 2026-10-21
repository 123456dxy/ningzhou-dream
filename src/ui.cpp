// 宁州一梦 · C++ 版 — 控制台界面实现
#include "ui.h"
#include "core/checkpoint.h"
#include "core/combat/battle_scaling.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>

#ifndef _WIN32
#include <unistd.h>
#endif
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#endif

#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/economy.h"
#include "core/legacy.h"
#include "core/save.h"
#include "core/society.h"
#include "core/travel.h"

namespace nz::ui {

// ============================================================
// 输出基础
// ============================================================
// 用途：设置控制台UTF8中文输入输出。
// 参与流程：本地控制台菜单与交互。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：无；完成说明所述写入或展示操作。
void setupConsole() {
#ifdef _WIN32
    // 切到 UTF-8 代码页，否则中文显示为乱码
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif
}

// 用途：输出菜单分隔线。
// 参与流程：本地控制台菜单与交互。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：无；完成说明所述写入或展示操作。
void line() { std::cout << "────────────────────────────────────────\n"; }

// 用途：打印当前菜单标题。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const std::string& text，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void title(const std::string& text) {
    std::cout << "\n";
    line();
    std::cout << "  " << text << "\n";
    line();
}

// 用途：清除当前控制台画面。
// 参与流程：本地控制台菜单与交互。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：无；完成说明所述写入或展示操作。
void clearScreen() {
    // 只在真正的交互式终端清屏。
    // 输入被重定向（管道/文件）时清屏会把输出搅乱，也让自动化测试看不全内容。
#ifdef _WIN32
    if (_isatty(_fileno(stdin))) system("cls");
#else
    if (isatty(fileno(stdin))) system("clear");
#endif
}

// 用途：读取一行输入并处理输入终止。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入std::string& out，按本模块入口调用。
// 返回值：成功读到一行为真；输入结束为假。
bool readLine(std::string& out) {
    if (!std::getline(std::cin, out)) return false;
    // 去掉行尾的 \r（Windows 换行）
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n'))
        out.pop_back();
    return true;
}

// 用途：读取范围内数字并使用默认值处理无效输入。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入int lo, int hi, int fallback，按本模块入口调用。
// 返回值：范围内输入值或fallback默认值。
int readInt(int lo, int hi, int fallback) {
    std::string s;
    if (!readLine(s)) return fallback;
    if (s.empty()) return fallback;
    try {
        const int v = std::stoi(s);
        if (v < lo || v > hi) return fallback;
        return v;
    } catch (...) {
        return fallback;
    }
}

// 中文按 2 列宽度计算，便于对齐
// 用途：按UTF8字符估算控制台显示宽度。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const std::string& s，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
size_t displayWidth(const std::string& s) {
    size_t w = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if (c >= 0xF0) len = 4;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        // 3 字节及以上基本是中文/全角，算 2 列
        w += (len >= 3) ? 2 : 1;
        i += len;
    }
    return w;
}

// 用途：补齐菜单文本的显示宽度。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const std::string& s, size_t width，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string pad(const std::string& s, size_t width) {
    const size_t w = displayWidth(s);
    if (w >= width) return s;
    return s + std::string(width - w, ' ');
}

// ============================================================
// 界面绘制
// ============================================================
// 用途：显示角色境界资源日期与状态。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void showHeader(const Game& g) {
    const Player& p = g.player;
    CalDate d = calendarDate(g);
    line();
    std::cout << "  " << pad(p.name, 10)
              << pad(realmName(p.stage), 10)
              << g.clock.days << "天（第" << d.year << "年" << d.month
              << "月" << d.day << "日）\n";
    std::cout << "  年龄 " << p.age << "/" << p.lifespan
              << "   气血 " << p.health << "/" << mfPlayerHealthMaximum(p)
              << "   修为 " << p.xp << "/" << realmOf(p.stage).need
              << "   灵石 " << p.stones
              << "   灵药 " << p.herbs << "\n";
    std::cout << "  遁速 " << artEscapeSpeed(p)
              << "   悟性 " << p.insight
              << "   资质 " << p.apt
              << "   福缘 " << p.luck
              << "   声望 " << p.renown << "\n";

    std::string where = findLoc(p.location) ? findLoc(p.location)->name : p.location;
    std::cout << "  所在 " << where;
    if (!p.sect.empty() && findSect(p.sect)) {
        std::cout << "   宗门 " << findSect(p.sect)->name
                  << "（" << socRank(p) << "）";
        if (g.bonds.payroll.balance > 0)
            std::cout << "   待领俸禄 " << g.bonds.payroll.balance;
    } else {
        std::cout << "   身份 散修";
    }
    if (g.bonds.notoriety > 0) std::cout << "   恶名 " << g.bonds.notoriety;
    line();
}

// 最近几条纪事
// 用途：显示最近指定数量的岁月纪事。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const Game& g, int count = 5，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void showLog(const Game& g, int count = 5) {
    if (g.logs.empty()) return;
    const int n = std::min<int>(count, static_cast<int>(g.logs.size()));
    std::cout << "\n  近况：\n";
    for (int i = static_cast<int>(g.logs.size()) - n;
         i < static_cast<int>(g.logs.size()); ++i) {
        const LogEntry& e = g.logs[static_cast<size_t>(i)];
        std::cout << "    " << (e.important ? "★ " : "· ") << e.text << "\n";
    }
}

// ============================================================
// 建号
// ============================================================
// 用途：显示可选初始天赋目录。
// 参与流程：本地控制台菜单与交互。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：无；完成说明所述写入或展示操作。
static void showTalentList() {
    std::cout << "\n  可用天赋（成本负数代表磨砺，会退还点数）：\n";
    const auto& t = TALENTS();
    for (size_t i = 0; i < t.size(); ++i) {
        std::printf("    %2zu. %s %s（%d点）%s\n", i + 1, t[i].id,
                    t[i].name, t[i].cost, t[i].desc);
    }
}

// 用途：交互选择初始天赋并校验预算。
// 参与流程：本地控制台菜单与交互。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
static std::string pickTraits() {
    std::cout << "\n  选择天赋：输入编号，最多 3 个，用空格分隔（直接回车用默认 sword）\n";
    std::cout << "  例：1 5 9\n> ";
    std::string s;
    if (!readLine(s) || s.empty()) return "sword";

    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < s.size() && out.size() < 3) {
        while (pos < s.size() && s[pos] == ' ') ++pos;
        const size_t start = pos;
        while (pos < s.size() && s[pos] != ' ') ++pos;
        if (pos == start) break;
        try {
            const int idx = std::stoi(s.substr(start, pos - start)) - 1;
            if (idx >= 0 && idx < static_cast<int>(TALENTS().size()))
                out.push_back(TALENTS()[static_cast<size_t>(idx)].id);
        } catch (...) {
            // 忽略非法输入
        }
    }
    if (out.empty()) return "sword";

    // 拼成空格分隔的串返回
    std::string joined;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i) joined += " ";
        joined += out[i];
    }
    return joined;
}

// 用途：执行控制台建号输入与确认流程。
// 参与流程：本地控制台菜单与交互。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：Game结果，包含说明所述状态或计算数据。
static Game newGameFlow() {
    title("创建新的一世");

    std::cout << "\n  姓名（直接回车用「沈知微」）：\n> ";
    std::string name;
    readLine(name);
    if (name.empty()) name = "沈知微";

    std::cout << "\n  性别 1.女 2.男（回车默认女）：\n> ";
    const int sex = readInt(1, 2, 1);
    const std::string gender = (sex == 2) ? "男" : "女";

    showTalentList();
    const std::string traitStr = pickTraits();

    std::vector<std::string> traits;
    size_t pos = 0;
    while (pos < traitStr.size()) {
        const size_t sp = traitStr.find(' ', pos);
        traits.push_back(traitStr.substr(
            pos, sp == std::string::npos ? std::string::npos : sp - pos));
        if (sp == std::string::npos) break;
        pos = sp + 1;
    }

    std::cout << "\n  随机种子（回车用 20261004，同种子结果可复现）：\n> ";
    std::string seedStr;
    readLine(seedStr);
    uint32_t seed = 20261004;
    if (!seedStr.empty()) {
        try {
            seed = static_cast<uint32_t>(std::stoul(seedStr));
        } catch (...) {
        }
    }

    CreateOptions opts;
    opts.name = name;
    opts.gender = gender;
    opts.seed = seed;
    opts.lifeId = legacy::newLifeId(seed);
    opts.traits = traits;
    // 属性点自动补到资质上，简化建号流程
    opts.allocation = {};

    Game g = createGame(opts);
    checkpointEnable(g);
    std::cout << "\n  已创建：" << g.player.name << "，"
              << realmName(g.player.stage) << "，灵石 " << g.player.stones
              << "\n";
    std::cout << "  （按回车继续）";
    std::string dummy;
    readLine(dummy);
    return g;
}

// ============================================================
// 行动菜单
// ============================================================
// 用途：执行控制台推进并显示事件或结局。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g, int days，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void doAdvance(Game& g, int days) {
    if (g.player.Zhuji && g.player.Zhuji->Status == ZhujiStatus::Active) {
        std::cout << "  筑基已入关，请在网页端继续凝练或冲穴。\n";
        return;
    }
    // 直接推进若干天：没有在途工作时先按当前计划建一个
    if (!g.job.has_value()) {
        Job j;
        j.actionType = "plan";
        j.plan = g.plan;
        j.name = findPlan(g.plan) ? findPlan(g.plan)->name : "计划";
        j.total = days;
        j.remaining = days;
        j.started = g.clock.days;
        const int cost = (g.plan == "cultivate")
                             ? calendarTake(g, "cultivationCost", cultivationCost(g.player) * days / 360.0)
                             : 0;
        j.cost = cost;
        const int reserve = (g.plan == "cultivate" && g.clock.carry["cultivationCost"] > 1e-9) ? 1 : 0;
        j.paid = g.player.stones >= cost + reserve;
        if (j.paid) g.player.stones -= cost;
        g.job = j;
    }
    // 反复运行直到这段工作结束或产生抉择
    int guard = 0;
    while (g.job.has_value() && guard++ < 200) {
        const CalRunResult r = calendarRun(g);
        if (!r.ok) {
            std::cout << "  " << r.error << "\n";
            return;
        }
        if (r.restored || r.ended || r.pending || g.player.arts.battle) break;
    }
}

// 处理待处理抉择（山门来信 / 药谷 / 游历遇事）
// 用途：展示并结算待处理事件选择。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void handlePending(Game& g) {
    if (!g.pending.has_value()) return;
    const Pending ev = *g.pending;

    title(ev.title + "  ——  " + ev.subtitle);
    std::cout << "\n  " << ev.text << "\n\n";
    for (size_t i = 0; i < ev.options.size(); ++i) {
        const ChoiceOption& o = ev.options[i];
        std::cout << "    " << (i + 1) << ". " << pad(o.name, 18) << o.desc
                  << (o.disabled ? "（当前不可选）" : "") << "\n";
    }
    std::cout << "\n  选择（回车默认 1）：\n> ";
    const int pick = readInt(1, static_cast<int>(ev.options.size()), 1);
    const std::string id = ev.options[static_cast<size_t>(pick - 1)].id;

    // 游历遇事有专门的处理函数
    if (ev.type == "adventure") {
        if (!adventureChoose(g, id)) g.pending.reset();
        return;
    }

    // 山门来信 / 药谷（对应 JS 的 choose）
    Player& p = g.player;
    if (ev.type == "sectInvitation") {
        if (id == "join") {
            if (p.sect.empty()) {
                logEvent(g, "sect",
                         "你接受金虹剑派的邀请。请前往山门，通过入门考核后正式拜师。",
                         true);
            }
        } else if (id == "study") {
            p.insight = std::min(500, p.insight + 8);
            logEvent(g, "self",
                     "你向五宗使者请教，记下各自的修炼门径，决定先看看这个世界。",
                     false, "", "悟道 +8");
        } else if (id == "free") {
            p.stones += 20;
            logEvent(g, "self", "你替使者捎带一封回信，获赠路资。暂不入宗，自寻道途。",
                     false, "", "灵石 +20");
        }
    } else if (ev.type == "valley") {
        Npc* npc = nullptr;
        if (!ev.npcId.empty()) {
            for (auto& n : g.npcs)
                if (n.id == ev.npcId && n.alive) npc = &n;
        }
        if (id == "help") {
            const int found = std::min(3, g.world.herbs);
            p.herbs += found;
            g.world.herbs -= found;
            if (npc) addRelation(g, npc->id, 20);
            const bool injured = randomOf(g) > 0.48 + p.luck * 0.003;
            if (injured) {
                applyDamage(g, 16);
                p.health = std::max(1, p.health);
            }
            logEvent(g, "self",
                     std::string(npc ? "你与" + npc->name + "合力破阵，救下对方"
                                     : "你循着阵纹找到生门，探得遗府一角") +
                         "。" + (injured ? "阵法反噬留下了伤势。" : "此行平安，机缘入袋。"),
                     true, npc ? npc->id : "");
        } else if (id == "take") {
            const int found = std::min(5, g.world.herbs);
            p.herbs += found;
            g.world.herbs -= found;
            if (npc) addRelation(g, npc->id, -15);
            logEvent(g, "self",
                     "你采走 " + std::to_string(found) + " 份灵药，避开了禁制。" +
                         (npc ? "对方对你的态度从此冷淡了一些。" : ""));
        } else if (id == "leave") {
            p.insight = std::min(500, p.insight + 5);
            logEvent(g, "self", "你记下方位与阵纹，留待日后再访。此刻保全自身也是一种选择。",
                     false, "", "悟道 +5");
        }
    }
    g.pending.reset();
}

// 洞府：道法节点与功法
// 用途：交互管理功法悟道神通与配装。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void doArtsMenu(Game& g) {
    Player& p = g.player;
    while (true) {
        title("洞府 · 道法");
        Arts& a = p.arts;
        std::cout << "\n  悟道点 " << a.points
                  << "   主修大道：" << a.focus
                  << "   主修功法：" << a.main << "\n\n";

        // 显示十二大道的经验与境界
        for (int i = 0; i < 12; ++i) {
            const char* dao = ART_DAOS[i];
            const int exp = a.exp.count(dao) ? a.exp.at(dao) : 0;
            std::cout << "    " << pad(dao, 4) << pad(ART_LEVELS[artLevel(exp)], 12)
                      << exp << " 经验";
            // 可点的节点
            for (int lv = 1; lv <= 5; ++lv) {
                const std::string nid = std::to_string(i) + "-" + std::to_string(lv);
                const bool has = std::find(a.nodes.begin(), a.nodes.end(), nid) != a.nodes.end();
                std::cout << (has ? "●" : "○") << lv;
            }
            std::cout << "\n";
        }

        std::cout << "\n  1.点亮道法节点  2.切换主修大道  3.修习功法"
                     "  4.学习功法（用抄本）  5.携带/卸下神通  6.返回\n> ";
        const int c = readInt(1, 6, 6);
        if (c == 6) return;

        if (c == 1) {
            std::cout << "\n  输入节点编号（如 8-1 表示气道一层）：\n> ";
            std::string nid;
            readLine(nid);
            // 找节点定义
            const int dash = static_cast<int>(nid.find('-'));
            if (dash <= 0) {
                std::cout << "  编号格式不对。\n";
                continue;
            }
            const int dao = std::stoi(nid.substr(0, dash));
            const int lv = std::stoi(nid.substr(dash + 1));
            if (dao < 0 || dao > 11 || lv < 1 || lv > 5) {
                std::cout << "  节点不存在。\n";
                continue;
            }
            const char* daoName = ART_DAOS[dao];
            const int cost = (lv == 1) ? 1 : (lv == 2) ? 2 : (lv == 3) ? 4
                             : (lv == 4) ? 6 : 8;
            if (std::find(a.nodes.begin(), a.nodes.end(), nid) != a.nodes.end()) {
                std::cout << "  这个节点已经点亮。\n";
            } else if (artLevel(a.exp[daoName]) < lv) {
                std::cout << "  道法境界不足，需要 " << daoName << "道达到 "
                          << ART_LEVELS[lv] << "。\n";
            } else if (a.points < cost) {
                std::cout << "  悟道点不足（需要 " << cost << "）。\n";
            } else {
                a.points -= cost;
                a.nodes.push_back(nid);
                std::cout << "  已点亮 " << daoName << "道第 " << lv << " 层。\n";
            }
        } else if (c == 2) {
            std::cout << "\n  输入大道名（金木水火土神体剑气阵丹器）：\n> ";
            std::string dao;
            readLine(dao);
            if (daoIndex(dao) >= 0) {
                a.focus = dao;
                std::cout << "  主修大道已切换为 " << dao << "。\n";
            } else {
                std::cout << "  没有这个大道。\n";
            }
        } else if (c == 3) {
            std::cout << "\n  输入功法 id（如 tuna 吐纳功、sword 剑典）：\n> ";
            std::string id;
            readLine(id);
            for (const auto& m : data::BaseManualDef_TABLE()) {
                if (m.id != id) continue;
                const int lv = a.manuals.count(m.id) ? a.manuals.at(m.id) : 0;
                if (p.stage < m.stage) {
                    std::cout << "  境界不足。\n";
                } else if (lv >= 5) {
                    std::cout << "  功法已满五层。\n";
                } else if (p.stones < m.price) {
                    std::cout << "  灵石不足（需要 " << m.price << "）。\n";
                } else {
                    p.stones -= m.price;
                    a.manuals[m.id] = lv + 1;
                    artGain(p, m.dao, 12);
                    std::cout << "  " << m.name << " 修至第 " << (lv + 1)
                              << " 层，花费 " << m.price << " 灵石。\n";
                }
                break;
            }
        } else if (c == 4) {
            std::cout << "\n  已获得的抄本：";
            for (const auto& b : a.bookList) std::cout << b << " ";
            std::cout << "\n  输入要挂为主修的功法 id：\n> ";
            std::string id;
            readLine(id);
            if (a.manuals.count(id)) {
                a.main = id;
                a.aux.erase(std::remove(a.aux.begin(), a.aux.end(), id),
                            a.aux.end());
                std::cout << "  主修功法已设为 " << id << "。\n";
            } else {
                std::cout << "  尚未修习这部功法。\n";
            }
        } else if (c == 5) {
            std::cout << "\n  普通攻击、普通防御固定携带，不占额外槽位。\n";
            std::cout << "  当前额外神通（" << a.spells.size() << "/" << battleSpellSlots(p) << "）：";
            for (const auto& s : a.spells) {
                const SpellDef* sp = battleSpell(s);
                std::cout << (sp ? sp->name : s) << " ";
            }
            std::cout << "\n\n  可携带的神通：\n";
            const auto& all = allSpells();
            for (size_t i = 0; i < all.size(); ++i) {
                const SpellDef& sp = all[i];
                const bool has = std::find(a.spells.begin(), a.spells.end(),
                                           sp.id) != a.spells.end();
                std::cout << "    " << pad(std::to_string(i + 1), 4)
                          << pad(sp.name, 12) << sp.dao << "道  灵力 "
                          << battleCost(p, sp) << "  " << (has ? "[已携带]" : "")
                          << "  需求：" << realmName(sp.stage)
                          << (p.stage < sp.stage ? "（境界不足）" : "") << "\n";
            }
            std::cout << "\n  输入编号切换携带（0 返回）：\n> ";
            const int pick =
                readInt(0, static_cast<int>(all.size()), 0);
            if (pick == 0) continue;
            const SpellDef& sp = all[static_cast<size_t>(pick - 1)];
            const ActionError r = artInstant(g, "spell", sp.id);
            if (r.ok)
                std::cout << "  已切换：" << sp.name << " 现在"
                          << (std::find(a.spells.begin(), a.spells.end(),
                                        sp.id) != a.spells.end()
                                  ? "已携带"
                                  : "已卸下")
                          << "。\n";
            else
                std::cout << "  " << r.error << "\n";
            std::cout << "  （按回车继续）";
            std::string d2;
            readLine(d2);
        }
    }
}

// 斗法界面：卡牌交互
// 用途：显示斗法状态并接收出牌与结束回合。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void doBattleLoop(Game& g) {
    if (!g.player.arts.battle.has_value()) return;
    Battle& b = *g.player.arts.battle;

    while (g.player.arts.battle.has_value()) {
        Battle& bb = *g.player.arts.battle;
        title("斗法 · " + bb.enemy);
        std::cout << "\n  我方 气血 " << bb.hp << "/" << bb.max
                  << "  护盾 " << bb.shield
                  << "  灵力 " << bb.mana << "/" << bb.maxMana
                  << "  剑气 " << bb.swordQi << "\n";
        std::cout << "  敌方 气血 " << bb.enemyHp << "/" << bb.enemyMax
                  << "  护盾 " << bb.enemyShield;
        if (bb.swordQi > 0)
            std::cout << "  （剑气每层 +7% 剑系伤害）";
        std::cout << "\n";
        std::cout << "  第 " << bb.turn << " 回合   敌方意图："
                  << bb.intentName << "（原始伤害 " << bb.intentRaw << "）\n";
        std::cout << "  下回合回复灵力 " << battleNextMana(g.player, bb)
                  << "  总减伤 " << jsRound(battleReduction(g.player, bb) * 100) << "%"
                  << "  增伤 " << jsRound(bb.buff * 100) << "%"
                  << "  本场移除 " << bb.exhausted.size() << " 张\n";
        if (bb.manaBonus || bb.manaPenalty)
            std::cout << "  本场灵力回复：+" << bb.manaBonus << " / -" << bb.manaPenalty << "\n";

        // 手牌带即时预览
        std::cout << "\n  手牌：\n";
        const vector<string> spells = handSpells(bb);
        for (size_t i = 0; i < bb.hand.size(); ++i) {
            const SpellDef* sp = battleSpell(spells[i]);
            if (!sp) continue;
            const Preview pv = battlePreview(g.player, bb, spells[i]);
            std::cout << "    " << (i + 1) << ". " << pad(sp->name, 12)
                      << "费" << pv.cost;
            if (pv.damage > 0) std::cout << " 伤害" << pv.damage;
            if (pv.shield > 0) std::cout << " 护盾+" << pv.shield;
            if (pv.heal > 0) std::cout << " 治疗+" << pv.heal;
            if (pv.qi != 0) std::cout << " 剑气" << (pv.qi > 0 ? "+" : "") << pv.qi;
            std::cout << "  " << battleSpellDescription(*sp) << "\n";
        }
        std::cout << "\n  0.结束回合   9.撤离\n";
        std::cout << "  选择要打出的牌：\n> ";
        const int pick = readInt(0, 9, -1);

        if (pick == -1 || (pick != 9 && pick > static_cast<int>(bb.hand.size()))) {
            std::cout << "  输入无效。\n";
            continue;
        }
        std::string action;
        if (pick == 0) action = "end";
        else if (pick == 9 && pick > static_cast<int>(bb.hand.size()))
            action = "escape";
        else action = bb.hand[static_cast<size_t>(pick - 1)];

        std::string target;
        if (pick > 0 && pick != 9) {
            const SpellDef* selected = battleSpell(spells[static_cast<size_t>(pick - 1)]);
            if (selected && battleNeedsTarget(*selected)) {
                vector<string> targets;
                for (const auto& c : bb.deck) {
                    if (c.spell == selected->id ||
                        std::find(bb.exhausted.begin(), bb.exhausted.end(), c.uid) != bb.exhausted.end() ||
                        std::find(targets.begin(), targets.end(), c.spell) != targets.end()) continue;
                    targets.push_back(c.spell);
                }
                std::cout << "\n  选择本场移除的神通（所有副本，0取消）：\n";
                for (size_t i = 0; i < targets.size(); ++i) {
                    const SpellDef* candidate = battleSpell(targets[i]);
                    std::cout << "    " << i + 1 << ". " << (candidate ? candidate->name : targets[i]) << "\n";
                }
                const int chosen = readInt(0, static_cast<int>(targets.size()), 0);
                if (chosen == 0) continue;
                target = targets[static_cast<size_t>(chosen - 1)];
            }
        }
        const TurnResult r = battleTurn(g, action, target);
        if (!r.ok) {
            std::cout << "  " << r.error << "\n";
            std::cout << "  （按回车继续）";
            std::string dummy;
            readLine(dummy);
        }
    }

    // 斗法结束
    std::cout << "\n  斗法结束。当前气血 " << g.player.health << "\n";
    std::cout << "  （按回车继续）";
    std::string dummy;
    readLine(dummy);
}



// 存档菜单
// 用途：交互选择存档位进行保存读取。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void doSaveMenu(Game& g) {
    title("存档");
    const auto slots = listSaveSlots();
    for (const auto& s : slots) {
        if (s.exists) {
            std::cout << "    " << s.slot << ". " << pad(s.name, 10)
                      << pad(s.realm, 10) << "第" << s.year << "年 "
                      << s.age << "岁   " << s.bytes << " 字节\n";
        } else {
            std::cout << "    " << s.slot << ". （空）\n";
        }
    }
    std::cout << "\n  1-3.保存到该位   4-6.读取手动档   7.读取十年自动档   0.返回\n> ";
    const int c = readInt(0, 7, 0);
    if (c == 0) return;

    if (c >= 1 && c <= 3) {
        std::string err;
        const auto previous = g.checkpoint;
        checkpointManual(g,c);
        if (saveToFile(g, savePathFor(c), &err))
            std::cout << "  已保存到存档位 " << c << "。\n";
        else {
            g.checkpoint = previous;
            std::cout << "  保存失败：" << err << "\n";
        }
    } else if (c >= 4 && c <= 7) {
        const int slot = c - 3;
        Game loaded;
        std::string err;
        if (loadFromFile(savePathFor(slot), loaded, &err)) {
            g = loaded;
            checkpointEnable(g);
            std::cout << "  已读取存档位 " << slot << "。\n";
        } else {
            std::cout << "  读取失败：" << err << "\n";
        }
    }
    std::cout << "  （按回车继续）";
    std::string dummy;
    readLine(dummy);
}

// 调试信息：把关键内部状态摊开，方便核对
// 用途：显示调试数值与世界内部状态。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void showDebug(const Game& g) {
    title("内部状态（调试）");
    const Player& p = g.player;
    const Arts& a = p.arts;

    std::cout << "\n  【种子与随机数】\n";
    std::cout << "    seed=" << g.seed << "  rng=" << g.rng
              << "（同种子可复现）\n";
    std::cout << "    version=" << g.version
              << "  eventSeq=" << g.eventSeq
              << "  npcSeq=" << g.npcSeq << "\n";

    std::cout << "\n  【时间】\n";
    std::cout << "    days=" << g.clock.days
              << "  year=" << g.year
              << "  ageBase=" << g.clock.ageBase << "\n";
    std::cout << "    carry: ";
    for (const auto& [k, v] : g.clock.carry)
        std::cout << k << "=" << v << " ";
    std::cout << "\n";

    std::cout << "\n  【玩家】\n";
    std::cout << "    stage=" << p.stage << "  xp=" << p.xp << "/"
              << realmOf(p.stage).need << "  health=" << p.health << "\n";
    std::cout << "    sword=" << p.sword << "  apt=" << p.apt
              << "  luck=" << p.luck << "  insight=" << p.insight
              << "  body=" << p.body << "\n";
    std::cout << "    traits=";
    for (const auto& t : p.traits) std::cout << t << " ";
    std::cout << "\n";

    std::cout << "\n  【洞府】\n";
    const ArtMods& m = artModsOf(a);
    std::cout << "    focus=" << a.focus << "  main=" << a.main
              << "  points=" << a.points << "\n";
    std::cout << "    加成 cult=" << m.cult << " attack=" << m.attack
              << " hp=" << m.hp << " mana=" << m.mana
              << " sword=" << m.sword << " shield=" << m.shield << "\n";
    std::cout << "    nodes=";
    for (const auto& n : a.nodes) std::cout << n << " ";
    std::cout << "\n    spells=";
    for (const auto& s : a.spells) std::cout << s << " ";
    std::cout << "\n";
    std::cout << "    poison=" << a.poison << "  furnace=" << a.furnace
              << "  durability=" << a.durability << "\n";

    std::cout << "\n  【战斗公式】\n";
    std::cout << "    battleEnergy(灵力上限)=" << battleEnergy(p)
              << "  battleSwordBonus=" << battleSwordBonus(p) << "\n";
    std::cout << "    cultivationRate(年修为)=" << cultivationRate(p)
              << "  breakthroughChance=" << breakthroughChance(g) << "\n";

    std::cout << "\n  【世界】\n";
    std::cout << "    market=" << g.world.market
              << "  herbs=" << g.world.herbs
              << "  coreStock=" << g.world.coreStock
              << "  pillStock=" << g.world.pillStock << "\n";
    std::cout << "    mineOwner=" << g.world.mineOwner
              << "  prosperity=" << g.world.prosperity << "\n";
    std::cout << "    NPC 数量=" << g.npcs.size() << "  纪事条数="
              << g.logs.size() << "\n";

    std::cout << "\n  【游历】\n";
    std::cout << "    since=" << g.adventure.since
              << "  total=" << g.adventure.total
              << "  events=" << g.adventure.events
              << "  wins=" << g.adventure.wins
              << "  lastKind=" << g.adventure.lastKind << "\n";

    std::cout << "\n  （按回车返回）";
    std::string dummy;
    readLine(dummy);
}

// ============================================================
// 主循环
// ============================================================
// 宗门与知交：拜师 / 授业 / 收徒 / 道侣 / 原创支线 / 野外悬赏
// ============================================================

namespace {

struct BondKind {
    const char* id;
    const char* name;
    int days;
    int cost;
};

// 对应 JS 的 socActionInfo 里 bond 分支的 days/cost
const BondKind kBondKinds[] = {
    {"master", "拜师礼", 5, 20},
    {"lesson", "师尊授业", 10, 12},
    {"disciple", "收徒考察", 5, 0},
    {"teach", "传道授业", 10, 10},
    {"partner", "结为道侣", 7, 0},
    {"together", "相伴共修", 10, 0},
    {"part", "解除道侣之约", 1, 0},
    {"dismiss", "辞别师门", 1, 0},
};

// 用途：按ID查询控制台NPC往来类别。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const std::string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const BondKind* findBondKind(const std::string& id) {
    for (const auto& k : kBondKinds)
        if (id == k.id) return &k;
    return nullptr;
}

// 用途：按可见序号查询附近道友。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g, int index，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
Npc* nearbyNpc(Game& g, int index) {
    int seen = 0;
    for (auto& n : g.npcs) {
        if (!n.alive || n.location != g.player.location) continue;
        if (seen == index) return &n;
        ++seen;
    }
    return nullptr;
}

// 用途：统计当前地点可见NPC。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int nearbyCount(const Game& g) {
    int n = 0;
    for (const auto& x : g.npcs)
        if (x.alive && x.location == g.player.location) ++n;
    return n;
}

// 校验（对应 socActionError 的 bond 分支）
// 用途：检查控制台NPC往来门槛与资源。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入const Game& g, const std::string& kind, const Npc* npc，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string bondError(const Game& g, const std::string& kind, const Npc* npc) {
    const Player& p = g.player;
    const Bonds& b = g.bonds;

    if (kind == "master") {
        if (!npc) return "附近没有可拜的修士。";
        if (p.sect.empty()) return "散修需先入宗才能拜师。";
        if (npc->sect != p.sect) return "只能拜本宗前辈为师。";
        if (!b.master.empty()) return "已有师尊，需先辞别师门。";
        if (npc->stage <= p.stage) return "对方境界不高于你，不足以拜师。";
        if (relationOf(g, npc->id) < 15) return "交情不足 15，先多来往。";
        if (p.stones < 20) return "拜师礼需 20 灵石。";
        if (b.partner == npc->id) return "此人已是道侣。";
        return "";
    }
    if (kind == "lesson") {
        if (b.master.empty()) return "尚未拜师。";
        if (!npc || npc->id != b.master) return "需在山门与师尊相见。";
        return bondMonthlyError(g, "lesson");
    }
    if (kind == "disciple") {
        if (!npc) return "附近没有合适的修士。";
        if (!bondCanTakeDisciple(g)) return "门下已满两人。";
        if (p.age < 18) return "本人尚未成年。";
        if (p.stage < 3) return "本人需筑基以上才能收徒。";
        if (npc->age < 18) return "对方尚未成年。";
        if (npc->stage >= p.stage) return "对方境界不低于你，不宜为徒。";
        if (relationOf(g, npc->id) < 30) return "交情不足 30。";
        return "";
    }
    if (kind == "teach") {
        if (b.disciples.empty()) return "门下尚无弟子。";
        return bondMonthlyError(g, "teach");
    }
    if (kind == "partner") {
        if (!npc) return "附近没有合适的修士。";
        if (!b.partner.empty()) return "已有道侣。";
        if (b.master == npc->id) return "师徒不可结为道侣。";
        if (p.age < 18 || npc->age < 18) return "双方须成年。";
        if (std::abs(p.stage - npc->stage) > 3) return "境界相差超过 3 小境。";
        // 性格决定交情门槛：70 / 80 / 85
        const int need = (npc->personality == "重情重义") ? 70
                         : (npc->personality == "洒脱随性") ? 80
                                                            : 85;
        if (relationOf(g, npc->id) < need)
            return "交情不足 " + std::to_string(need) + "。";
        return "";
    }
    if (kind == "together") {
        if (b.partner.empty()) return "尚无道侣。";
        return bondMonthlyError(g, "together");
    }
    if (kind == "part") return b.partner.empty() ? "尚无道侣。" : "";
    if (kind == "dismiss") return b.master.empty() ? "尚未拜师。" : "";
    return "未知往来。";
}

// 用途：执行选定道友的关系往来。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g, const std::string& kind，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void doBondAction(Game& g, const std::string& kind) {
    const BondKind* def = findBondKind(kind);
    if (!def) return;

    const bool needsNpc = (kind == "master" || kind == "disciple" ||
                           kind == "partner" || kind == "lesson");
    const Npc* npc = nullptr;

    if (needsNpc) {
        const int count = nearbyCount(g);
        if (count == 0) {
            std::cout << "\n  此处没有别的修士。\n  （按回车继续）";
            std::string d;
            readLine(d);
            return;
        }
        std::cout << "\n  附近的修士：\n";
        for (int i = 0; i < count; ++i) {
            const Npc* n = nearbyNpc(g, i);
            if (!n) break;
            std::cout << "    " << (i + 1) << ". " << pad(n->name, 12)
                      << pad(realmName(n->stage), 10)
                      << pad(n->personality, 12)
                      << "交情 " << relationOf(g, n->id) << "\n";
        }
        std::cout << "\n  选择对象（0 取消）：\n> ";
        const int pick = readInt(0, count, 0);
        if (pick == 0) return;
        npc = nearbyNpc(g, pick - 1);
    } else if (kind == "lesson" && !g.bonds.master.empty()) {
        for (const auto& n : g.npcs)
            if (n.id == g.bonds.master && n.alive &&
                n.location == g.player.location)
                npc = &n;
    }

    const std::string err = bondError(g, kind, npc);
    if (!err.empty()) {
        std::cout << "\n  " << err << "\n  （按回车继续）";
        std::string d;
        readLine(d);
        return;
    }

    std::cout << "\n  " << def->name << "：需 " << def->days << " 天"
              << (def->cost > 0 ? "，" + std::to_string(def->cost) + " 灵石" : "")
              << "\n  确认进行？1.是 0.否\n> ";
    if (readInt(0, 1, 0) != 1) return;

    // 解除类动作立即生效
    if (kind == "part" || kind == "dismiss") {
        societyBondComplete(g, kind, npc);
        std::cout << "\n  已处理。\n  （按回车继续）";
        std::string d;
        readLine(d);
        return;
    }

    g.player.stones -= def->cost;

    Job j;
    j.actionType = "bond";
    j.kind = kind;
    j.name = def->name;
    j.total = def->days;
    j.remaining = def->days;
    j.started = g.clock.days;
    j.cost = def->cost;
    j.paid = true;
    if (npc) j.npcId = npc->id;
    g.job = j;

    logEvent(g, "self",
             "安排" + std::string(def->name) + "，需 " +
                 std::to_string(def->days) + " 天。");
}

// 用途：展示支线并提交分支选择。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void doStoryMenu(Game& g) {
    title("原创支线");
    std::vector<const data::StoryDef*> available;
    for (const auto& s : data::StoryDef_TABLE())
        if (storyError(g, &s).empty()) available.push_back(&s);

    if (available.empty()) {
        std::cout << "\n  当前没有可触发的支线（条件未满足或已完成）。\n";
        std::cout << "  已落笔：";
        if (g.bonds.stories.empty()) std::cout << "（无）";
        for (const auto& [id, year] : g.bonds.stories)
            std::cout << id << "(" << year << ") ";
        std::cout << "\n\n  共 8 段支线，其中 5 段为隐藏（需满足特定条件）。\n";
        std::cout << "\n  （按回车继续）";
        std::string d;
        readLine(d);
        return;
    }

    std::cout << "\n";
    for (size_t i = 0; i < available.size(); ++i) {
        const auto* s = available[i];
        std::cout << "    " << (i + 1) << ". " << pad(s->name, 14)
                  << "（" << s->days << "天） " << s->desc << "\n";
    }
    std::cout << "\n  选择（0 返回）：\n> ";
    const int pick = readInt(0, static_cast<int>(available.size()), 0);
    if (pick == 0) return;

    const data::StoryDef* s = available[static_cast<size_t>(pick - 1)];
    title(s->name);
    std::cout << "\n  " << s->opening << "\n\n";
    std::cout << "    1. " << pad(s->opt1Name, 20) << s->opt1Effect << "\n";
    std::cout << "    2. " << pad(s->opt2Name, 20) << s->opt2Effect << "\n";
    std::cout << "\n  选择（0 放弃）：\n> ";
    const int choice = readInt(0, 2, 0);
    if (choice == 0) return;

    const auto error = storyBegin(g, *s, choice - 1);
    if (!error.empty()) std::cout << "\n  " << error << "\n";
    else {
        doAdvance(g, s->days);
        std::cout << (g.bonds.stories.count(s->id) ? "\n  支线已完成并结算。\n" :
                      g.ended ? "\n  此世已结束，未完成支线不发放奖励。\n" :
                      "\n  支线尚未完成，请继续当前安排。\n");
    }
    std::cout << "  （按回车继续）";
    std::string d;
    readLine(d);
}

// 用途：展示野外探索与悬赏选择。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void doWildMenu(Game& g) {
    title("野外悬赏");
    const auto& tasks = data::WildTaskDef_TABLE();
    std::cout << "\n";
    for (size_t i = 0; i < tasks.size(); ++i) {
        const auto& t = tasks[i];
        const int fee = t.fee + (g.player.stage / 3) * 25;
        std::cout << "    " << (i + 1) << ". " << pad(t.name, 14)
                  << "（" << t.days << "天，3灵石）赏金 " << fee << "  "
                  << t.desc << "\n";
    }
    std::cout << "\n  选择（0 返回）：\n> ";
    const int pick = readInt(0, static_cast<int>(tasks.size()), 0);
    if (pick == 0) return;

    const auto& t = tasks[static_cast<size_t>(pick - 1)];
    const std::string error = wildActionError(g, t.id);
    if (!error.empty()) {
        std::cout << "\n  " << error << "\n  （按回车继续）";
        std::string d;
        readLine(d);
        return;
    }
    societyWildBegin(g, t.id);
}

// 用途：展示名册宗门与好友通讯入口。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void doSocietyMenu(Game& g) {
    while (true) {
        title("宗门与知交");
        const Bonds& b = g.bonds;

        std::cout << "\n  师尊：";
        if (b.master.empty()) std::cout << "（无）";
        else
            for (const auto& n : g.npcs)
                if (n.id == b.master) std::cout << n.name;
        std::cout << "    授业 " << b.lessons << " 次\n";

        std::cout << "  弟子：";
        if (b.disciples.empty()) std::cout << "（无）";
        for (const auto& id : b.disciples)
            for (const auto& n : g.npcs)
                if (n.id == id) std::cout << n.name << " ";
        std::cout << "\n";

        std::cout << "  道侣：";
        if (b.partner.empty()) std::cout << "（无）";
        else {
            for (const auto& n : g.npcs)
                if (n.id == b.partner) std::cout << n.name;
            std::cout << "    相伴 " << b.bondDays << " 天";
        }
        std::cout << "\n";
        std::cout << "  待领俸禄 " << b.payroll.balance
                  << "    恶名 " << b.notoriety
                  << "    已完成支线 " << b.stories.size() << "/8\n";

        std::cout << "\n   1.拜师礼         2.师尊授业       3.收徒考察\n";
        std::cout << "   4.传道授业       5.结为道侣       6.相伴共修\n";
        std::cout << "   7.解除道侣之约   8.辞别师门\n";
        std::cout << "   9.原创支线      10.野外悬赏      0.返回\n> ";
        const int c = readInt(0, 10, 0);
        if (c == 0) return;

        switch (c) {
            case 1: doBondAction(g, "master"); break;
            case 2: doBondAction(g, "lesson"); break;
            case 3: doBondAction(g, "disciple"); break;
            case 4: doBondAction(g, "teach"); break;
            case 5: doBondAction(g, "partner"); break;
            case 6: doBondAction(g, "together"); break;
            case 7: doBondAction(g, "part"); break;
            case 8: doBondAction(g, "dismiss"); break;
            case 9: doStoryMenu(g); break;
            case 10: doWildMenu(g); break;
            default: break;
        }
    }
}

} // namespace

// ============================================================
// 用途：驱动控制台主菜单与子流程。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
static void mainMenu(Game& g) {
    string lastAutomatic;
    while (true) {
        checkpointTick(g);
        if (g.checkpoint.enabled && g.checkpoint.source == "auto" &&
            !g.checkpoint.snapshot.empty() && lastAutomatic != g.checkpoint.snapshot) {
            Game automatic; string error;
            if (loadFromJson(g.checkpoint.snapshot,automatic,&error)) {
                automatic.checkpoint = g.checkpoint;
                if (saveToFile(automatic,savePathFor(4),&error)) lastAutomatic=g.checkpoint.snapshot;
            }
            if (!error.empty()) std::cout << "  自动存档写入失败：" << error << "\n";
        }
        // 有抉择先处理，有斗法先打完
        if (g.pending.has_value()) {
            handlePending(g);
            if (g.player.arts.battle.has_value()) doBattleLoop(g);
            continue;
        }
        if (g.player.arts.battle.has_value()) {
            doBattleLoop(g);
            continue;
        }
        if (g.ended) {
            title("这一世已经结束");
            const Player& p = g.player;
            std::cout << "\n  " << p.name << "，" << p.age << " 岁。\n";
            if (g.ending.has_value())
                std::cout << "  " << g.ending->cause << "，最终境界 "
                          << g.ending->realm << "，评分 "
                          << g.ending->score << "\n";
            const bool canRestore = g.checkpoint.enabled && g.ending && g.ending->cause != "寿终正寝";
            std::cout << "\n  1.接受死亡，开始新的一世";
            if (canRestore) std::cout << "   2.回到最近存档";
            std::cout << "   0.退出\n> ";
            const int action=readInt(0, canRestore ? 2 : 1, 0);
            if (action==1) g=newGameFlow();
            else if (action==2) {
                const string cause=g.ending->cause;
                if (!checkpointRestore(g,cause))
                    std::cout << "最近存档不存在或损坏，无法恢复。\n";
            } else return;
            continue;
        }

        clearScreen();
        showHeader(g);
        showLog(g);

        std::cout << "\n  当前计划："
                  << (findPlan(g.plan) ? findPlan(g.plan)->name : g.plan)
                  << "（" << (findPlan(g.plan) ? findPlan(g.plan)->desc : "")
                  << "）\n";
        if (g.job.has_value()) {
            std::cout << "  在途：" << g.job->name << " 剩余 "
                      << g.job->remaining << "/" << g.job->total << " 天\n";
        }

        std::cout << "\n  1.推进行动（到下月/工作完成）\n";
        std::cout << "  2.度过一整年\n";
        std::cout << "  3.切换月度计划\n";
        std::cout << "  4.洞府 · 道法\n";
        std::cout << "  5.尝试突破\n";
        std::cout << "  6.领取俸禄\n";
        std::cout << "  7.城市与宗门事务\n";
        std::cout << "  8.宗门与知交（拜师/授业/道侣/支线/悬赏）\n";
        std::cout << "  9.查看内部状态（调试）\n";
        std::cout << "  10.存档 / 读档\n";
        std::cout << "  0.退出\n> ";
        const int c = readInt(0, 10, -1);

        if (g.player.Zhuji && g.player.Zhuji->Status == ZhujiStatus::Active && c >= 1 && c <= 8) {
            std::cout << "  筑基正在运转，请存档后在网页端继续；当前可查看状态、存读档或退出。\n";
            std::string Input;
            readLine(Input);
            continue;
        }
        if (g.job.has_value() && c >= 3 && c <= 8) {
            std::cout << "  当前在途安排尚未完成，请先推进行动。\n";
            std::string dummy;
            readLine(dummy);
            continue;
        }

        if (c == 0) {
            std::cout << "\n  退出前要存档吗？1.存到存档位1  0.直接退出\n> ";
            if (readInt(0, 1, 0) == 1) {
                std::string err;
                const auto previous=g.checkpoint;
                checkpointManual(g,1);
                if (saveToFile(g, savePathFor(1), &err))
                    std::cout << "  已保存到存档位 1。\n";
                else {
                    g.checkpoint=previous;
                    std::cout << "  保存失败：" << err << "\n";
                }
            }
            return;
        }
        if (c == 1) {
            doAdvance(g, calendarDate(g).left);
        } else if (c == 2) {
            // 一年 = 12 个月，逐步推进以便及时处理事件
            const int restores=g.checkpoint.restores;
            for (int i = 0; i < 12; ++i) {
                if (g.checkpoint.restores!=restores || g.pending.has_value() || g.ended ||
                    g.player.arts.battle.has_value() ||
                    (g.player.Zhuji && g.player.Zhuji->Status == ZhujiStatus::Active))
                    break;
                doAdvance(g, calendarDate(g).left);
            }
        } else if (c == 3) {
            std::cout << "\n";
            for (size_t i = 0; i < PLANS().size(); ++i)
                std::cout << "    " << (i + 1) << ". " << PLANS()[i].name
                          << "  " << PLANS()[i].desc << "\n";
            std::cout << "> ";
            const int pk = readInt(1, static_cast<int>(PLANS().size()), 1);
            g.plan = PLANS()[static_cast<size_t>(pk - 1)].id;
        } else if (c == 4) {
            doArtsMenu(g);
        } else if (c == 5) {
            if (g.player.stage == 2) {
                std::cout << "  筑基需在网页端自选五行凝练或周天冲穴，请存档后继续。\n";
                std::string Input;
                readLine(Input);
                continue;
            }
            const BreakthroughInfo info = breakthroughInfo(g);
            std::cout << "\n  目标：" << info.next
                      << "   成功率 " << jsRound(info.chance * 100) << "%"
                      << "   需灵石 " << info.cost << "\n";
            if (!info.ready) {
                std::cout << "  修为尚未圆满（" << g.player.xp << "/"
                          << realmOf(g.player.stage).need << "）。\n";
            } else if (g.player.stones < info.cost) {
                std::cout << "  灵石不足。\n";
            } else if (g.player.health < HealthGateMinimum) {
                std::cout << "  伤势过重，请先休养至 30 气血。\n";
            } else {
                std::cout << "  开始闭关？1.是 0.否\n> ";
                if (readInt(0, 1, 0) == 1) {
                    Job j;
                    j.actionType = "breakthrough";
                    j.name = "闭关叩关";
                    j.total = info.major ? 90 : 30;
                    j.remaining = j.total;
                    j.started = g.clock.days;
                    j.cost = info.cost;
                    j.paid = true;
                    j.breakthroughPrepared = true;
                    j.breakthroughMajor = info.major;
                    j.breakthroughChance = info.chance;
                    j.breakthroughFailureLoss = info.failureLoss;
                    g.player.stones -= info.cost;
                    if (info.major) {
                        if (g.player.pills > 0) --g.player.pills;
                        if (g.player.cores > 0) --g.player.cores;
                    }
                    if ((!info.major && g.player.arts.breakBuff == "step") ||
                        (info.major && g.player.arts.breakBuff == "stage" &&
                         g.player.arts.BreakthroughPillStage == g.player.stage)) {
                        g.player.arts.breakBuff.clear();
                        g.player.arts.BreakthroughPillStage = -1;
                    }
                    g.job = j;
                    doAdvance(g, j.total);
                }
            }
            std::cout << "  （按回车继续）";
            std::string dummy;
            readLine(dummy);
        } else if (c == 6) {
            const StipendResult r = socCollectStipend(g);
            std::cout << "\n";
            if (r.ok)
                std::cout << "  领取 " << r.paid << " 灵石。"
                          << (r.owed > 0 ? "尚欠 " + std::to_string(r.owed) : "")
                          << "\n";
            else
                std::cout << "  " << r.error << "\n";
            std::cout << "  （按回车继续）";
            std::string dummy;
            readLine(dummy);
        } else if (c == 7) {
            title("城市与宗门事务");
            std::cout << "\n  1.探访后山（7天，8灵石）\n";
            std::cout << "  2.接取任务（城中跑腿）\n";
            std::cout << "  3.出售一件丹药\n";
            std::cout << "  4.前往他处（选择路线与赶路方式）\n";
            std::cout << "  0.返回\n> ";
            const int cc = readInt(0, 4, 0);
            if (cc == 4) {
                // 移动是触发地点限定事件（支线、机缘、悬赏）的前提
                std::cout << "\n  可去之处：\n";
                for (size_t i = 0; i < LOCATIONS().size(); ++i) {
                    const auto& l = LOCATIONS()[i];
                    const bool here = (g.player.location == l.id);
                    std::cout << "    " << (i + 1) << ". " << pad(l.name, 12)
                              << pad(l.type, 8) << l.description
                              << (here ? "  ← 当前所在" : "") << "\n";
                }
                std::cout << "\n  选择（0 取消）：\n> ";
                const int pk = readInt(0, static_cast<int>(LOCATIONS().size()), 0);
                if (pk > 0) {
                    const std::string dest = LOCATIONS()[static_cast<size_t>(pk - 1)].id;
                    const TravelPlan route = travelPlan(g, dest);
                    std::cout << "\n  选择赶路方式：\n";
                    for (size_t i = 0; i < route.options.size(); ++i) {
                        const auto& option = route.options[i];
                        std::cout << "    " << i + 1 << ". " << option.name
                                  << "  " << option.days << "天 · " << option.cost << "灵石";
                        if (!option.enabled) std::cout << "（" << option.reason << "）";
                        std::cout << "\n";
                    }
                    std::cout << "  0.取消\n> ";
                    const int modePick = readInt(0, static_cast<int>(route.options.size()), 0);
                    if (modePick > 0) {
                        const auto& option = route.options[static_cast<size_t>(modePick - 1)];
                        const std::string err = travelBegin(g, dest, option.id);
                        if (!err.empty()) {
                            std::cout << "  " << err << "\n";
                        } else {
                            doAdvance(g, option.days);
                            if (!g.ended && !g.job && g.player.location == dest)
                                std::cout << "  已抵达 " << findLoc(dest)->name << "。\n";
                            else if (g.job && g.job->actionType == "travel")
                                std::cout << "  赶路暂停，距离抵达还有 " << g.job->remaining
                                          << " 天，请先处理当前遇事。\n";
                        }
                    }
                }
                std::cout << "  （按回车继续）";
                std::string d;
                readLine(d);
                continue;
            }
            if (cc == 1) {
                const std::string err = adventureActionError(g, "backhill");
                if (!err.empty()) {
                    std::cout << "  " << err << "\n";
                } else {
                    adventureBegin(g, "backhill");
                    doAdvance(g, 7);
                }
                std::cout << "  （按回车继续）";
                std::string dummy;
                readLine(dummy);
            } else if (cc == 2) {
                const std::string err = ecoActionError(g, "task", "errand");
                if (!err.empty()) {
                    std::cout << "  " << err << "\n";
                } else {
                    ecoBegin(g, "task", "errand");
                    doAdvance(g, 10);
                }
                std::cout << "  （按回车继续）";
                std::string dummy;
                readLine(dummy);
            } else if (cc == 3) {
                bool sold = false;
                for (const auto& [id, n] : g.player.arts.potions) {
                    if (n <= 0) continue;
                    const std::string err = ecoActionError(g, "task", "errand");
                    (void)err;
                    const int price = ecoSalePrice(g, "potion", id);
                    std::cout << "    丹药 " << id << " 售价 " << price << "\n";
                    sold = true;
                }
                if (!sold) std::cout << "    没有可出售的丹药。\n";
                std::cout << "  （按回车继续）";
                std::string dummy;
                readLine(dummy);
            }
        } else if (c == 8) {
            doSocietyMenu(g);
        } else if (c == 9) {
            showDebug(g);
        } else if (c == 10) {
            doSaveMenu(g);
        }
    }
}

// ============================================================
// 入口
// ============================================================
// 用途：初始化控制台并处理启动参数和主循环。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入int argc, char** argv，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int run(int argc, char** argv) {
    setupConsole();

    // 命令行：--debug 直接打印内部状态后退出；--new 直接开新档
    bool debugOnly = false;
    bool forceNew = false;
    int loadSlot = -1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--debug") debugOnly = true;
        else if (a == "--new") forceNew = true;
        else if (a == "--load" && i + 1 < argc) loadSlot = std::atoi(argv[++i]);
        else if (a == "--help" || a == "-h") {
            std::cout << "宁州一梦 · C++ 版\n\n"
                         "用法：ningzhou [选项]\n"
                         "  （无参数）   读存档位 1，没有则新建\n"
                         "  --new        直接新建一世\n"
                         "  --load N     读取存档位 N（1-4，4为自动档）\n"
                         "  --debug      打印内部状态后退出\n"
                         "  --help       显示本帮助\n";
            return 0;
        }
    }

    Game g;
    bool haveGame = false;

    if (!forceNew) {
        const int slot = (loadSlot >= 1 && loadSlot <= 4) ? loadSlot : 1;
        std::string err;
        if (loadFromFile(savePathFor(slot), g, &err)) {
            haveGame = true;
            std::cout << "已读取存档位 " << slot << "：" << g.player.name
                      << "，" << realmName(g.player.stage) << "\n";
        } else if (loadSlot >= 1) {
            std::cout << "读取存档失败：" << err << "\n";
        }
    }

    if (!haveGame) {
        if (debugOnly) {
            // 调试模式没有存档时，造一个临时局面
            CreateOptions opts;
            g = createGame(opts);
        } else {
            g = newGameFlow();
        }
    }

    if (debugOnly) {
        showDebug(g);
        return 0;
    }

    checkpointEnable(g);
    mainMenu(g);

    std::cout << "\n  后会有期。\n";
    return 0;
}

} // namespace nz::ui

// main.cpp 调用这个入口
// 用途：进入控制台程序入口。
// 参与流程：本地控制台菜单与交互。
// 使用方式：传入int argc, char** argv，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int main(int argc, char** argv) { return nz::ui::run(argc, argv); }
