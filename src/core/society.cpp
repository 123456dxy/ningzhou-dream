// 宁州一梦 · C++ 版 — 宗门与知交实现
// 覆盖 society.mjs 的：姓名、身份、俸禄账簿、师徒道侣、原创支线、野外冲突。
#include "core/society.h"
#include "core/source_limits.h"

#include <algorithm>

#include "core/arts.h"
#include "core/battle.h"
#include "core/economy.h"
#include "core/reward_receipt.h"
#include "core/sim.h"
#include "data/reference_tables.h"

namespace nz {

// ============================================================
// NPC 姓名（对应 JS 版 society.mjs 的 socNPCName）
// ============================================================
// JS 里 first / second 是 [...'字符串']，按字符拆成数组。
// 这些是 UTF-8 汉字，不能按字节拆，所以这里直接写成字符串数组。
string socNPCName(int i) {
    static const char* surnames[] = {
        "苏", "谢", "陆", "顾", "柳", "沈", "江", "林", "孟", "裴",
        "温", "许", "周", "秦", "楚", "叶", "方", "宋", "韩", "洛",
        "唐", "萧", "尹", "白", "程", "纪", "齐", "薛", "莫", "季",
        "徐", "段", "颜", "凌", "夏", "穆", "傅", "燕", "阮", "宁",
        "司马", "上官", "欧阳", "慕容", "司徒", "南宫", "闻人", "公孙",
        "东方", "独孤",
    };
    static const char* first[] = {
        "清", "云", "怀", "映", "照", "闻", "长", "明", "玉", "青",
        "行", "若", "观", "寄", "疏", "望", "修", "静", "昭", "玄",
        "松", "灵", "听", "归", "寒", "秋", "远", "鹤", "临", "知",
        "墨", "雨", "雪", "月", "星", "风", "竹", "苍", "锦", "南",
        "丹", "安", "景",
    };
    static const char* second[] = {
        "川", "山", "宁", "和", "秋", "桐", "澜", "溪", "远", "衡",
        "舟", "岚", "之", "水", "南", "风", "寒", "月", "影", "微",
        "河", "舒", "阳", "尘", "玉", "声", "明", "迟", "晏", "枝",
        "安", "照", "均", "生", "雨", "鸿", "初", "白", "庭", "渊",
        "容", "书", "心", "歌", "尧", "真", "辞", "归", "溪", "雪",
        "华",
    };
    constexpr int kSurnameCount = 50;
    constexpr int kFirstCount = 43;
    constexpr int kSecondCount = 51;
    constexpr int kSpace = kFirstCount * kSecondCount;

    const int index = (i * 37) % kSpace;
    string out = surnames[(i * 13) % kSurnameCount];
    out += first[index / kSecondCount];
    out += second[index % kSecondCount];
    if (i >= kSpace) out += "·" + std::to_string(i / kSpace);
    return out;
}

// ============================================================
// 身份与俸禄
// ============================================================
string socRank(const Person& p) {
    static const char* ranks[5] = {"外门弟子", "内门弟子", "亲传执事",
                                   "长老", "太上长老"};
    return ranks[std::min(4, p.stage / 3)];
}

int socStipend(const Person& p) {
    static const int table[5] = {8, 16, 28, 45, 65};
    return table[std::min(4, p.stage / 3)];
}

// ------------------------------------------------------------
// 初始化（对应 ensureSociety）
// ------------------------------------------------------------
void ensureSociety(Game& g) {
    Bonds& b = g.bonds;

    // 天赋起源文本（保证条目存在）
    if (b.origins.empty()) {
        for (const auto& t : g.player.traits)
            b.origins.emplace_back(t, "");
    }

    // 资质丹已用额度按服用记录折算
    if (g.player.arts.aptPills == 0) {
        int used = 0;
        for (const auto& [rid, n] : g.player.arts.used) {
            for (const auto& r : data::RecipeDef_TABLE()) {
                if (r.id != rid) continue;
                if (r.kind == "apt") used += r.value * n;
            }
        }
        g.player.arts.aptPills = std::min(10, used);
    }
}

// 重新建立俸禄账簿（对应 socInitPayroll）
void socInitPayroll(Game& g, bool legacy) {
    Player& p = g.player;
    Bonds& b = g.bonds;
    const int month = g.clock.days / 30;
    b.payroll = Payroll{};
    b.payroll.sect = p.sect;
    b.payroll.lastMonth = month;
    if (p.sect.empty()) return;

    int start = month;
    if (legacy) {
        // 从最后一次「拜入」的月份开始补记
        for (const auto& e : g.logs) {
            if (e.category != "sect") continue;
            if (e.text.find("拜入") == string::npos) continue;
            const int days = e.days.value_or(e.year * 360);
            start = std::max(0, days / 30);
        }
    }

    for (int m = start; m <= month; ++m) {
        Person tmp;
        tmp.stage = (start == month) ? p.stage : (legacy ? 0 : p.stage);
        b.payroll.balance += socStipend(tmp);
        b.payroll.credited++;
    }
}

// 每月记账（对应 socAccrue）
void socAccrue(Game& g) {
    Player& p = g.player;
    Bonds& b = g.bonds;
    const int month = g.clock.days / 30;
    Payroll& pay = b.payroll;

    if (pay.sect != p.sect) {
        socInitPayroll(g);
        return;
    }
    if (month > pay.lastMonth) {
        // 恶名 40 以上暂停俸禄
        if (!p.sect.empty() && b.notoriety < 40) {
            const int count = month - pay.lastMonth;
            pay.balance += count * socStipend(p);
            pay.credited += count;
        }
        pay.lastMonth = month;
    }
}

// ============================================================
// 原创支线（对应 SOC_STORIES）
// ============================================================
bool storyRequiresMet(const Game& g, const data::StoryDef& s) {
    const Player& p = g.player;
    const Bonds& b = g.bonds;
    switch (s.conditionId) {
        case 1:   // 未拆的家书：成年
            return p.age >= 18;
        case 2:   // 石脉洗灵：筑基以上且土道经验 >= 30
            return p.stage >= 3 && (p.arts.exp.count("土") ? p.arts.exp.at("土") : 0) >= 30;
        case 3: { // 灵根的回声：带特定灵根天赋
            if (p.stage < 3) return false;
            static const char* roots[6] = {"root", "gold", "wood",
                                           "water", "fire", "earth"};
            for (const char* r : roots)
                if (std::find(p.traits.begin(), p.traits.end(), string(r)) !=
                    p.traits.end())
                    return true;
            return false;
        }
        case 4:   // 旧剑归途：斗法胜场 >= 3
            return p.arts.progress.wins >= 3;
        case 5:   // 师门灯火：有师尊且授业 >= 3 次
            return !b.master.empty() && b.lessons >= 3;
        case 6:   // 同看一轮月：有道侣且相伴 >= 90 天
            return !b.partner.empty() && b.bondDays >= 90;
        case 7:   // 药圃无人：炼丹成功 >= 5 且属竹山宗
            return p.arts.progress.brew >= 5 && p.sect == "zhushan";
        case 8:   // 海灯不灭：金丹以上且水道、阵道经验各 >= 100
            return p.stage >= 6 && (p.arts.exp.count("水") ? p.arts.exp.at("水") : 0) >= 100 &&
                   (p.arts.exp.count("阵") ? p.arts.exp.at("阵") : 0) >= 100;
        default:
            return false;
    }
}

string storyError(const Game& g, const data::StoryDef* s) {
    if (!s) return "没有这段机缘。";
    if (g.bonds.stories.count(s->id)) return "这段因缘已落笔。";
    if (!storyRequiresMet(g, *s)) return "缘分尚未成熟。";
    if (!s->location.empty() && g.player.location != s->location)
        return "请先前往机缘地点。";
    return "";
}

string storyChoiceError(const Game& g, const data::StoryDef& s, int optionIndex) {
    if (optionIndex != 0 && optionIndex != 1) return "请选择有效的因缘选项。";
    if (g.bonds.stories.count(s.id)) return "这段因缘已落笔。";
    if (s.id == "rootecho" && optionIndex == 0 && g.player.stones < 40)
        return "承接残碑需40灵石（当前" + std::to_string(g.player.stones) + "）。";
    return "";
}

string storyBegin(Game& g, const data::StoryDef& s, int optionIndex) {
    if (g.ended || g.player.health <= 0 || g.player.age >= g.player.lifespan)
        return "此世已结束。";
    if (g.job || g.pending || g.player.arts.battle)
        return "请先处理当前安排、抉择或斗法。";
    if (const auto error = storyError(g, &s); !error.empty()) return error;
    if (const auto error = storyChoiceError(g, s, optionIndex); !error.empty()) return error;
    Job job;
    job.actionType = "story"; job.name = s.name; job.plan = s.id;
    job.kind = optionIndex == 0 ? "0" : "1"; job.origin = g.player.location;
    job.total = job.remaining = s.days; job.started = g.clock.days; job.paid = true;
    g.job = job;
    return "";
}

void storyComplete(Game& g, const data::StoryDef& s, int optionIndex) {
    if (!storyChoiceError(g, s, optionIndex).empty()) return;
    const RewardReceipt receipt(g);
    Player& p = g.player;
    Bonds& b = g.bonds;
    Arts& a = p.arts;
    b.stories[s.id] = g.year;

    // 奖励为抄本，修习仍遵循境界、费用和用时。
    auto grantBook = [&](const char* id) {
        ecoAcquireBook(g, id, "因缘「" + s.name + "」");
    };


    // 与 JS 中各支线的选项效果逐条对应
    if (s.id == "letter") {
        if (optionIndex == 0) {          // 回信报平安：心境+4，福缘+2
            a.heart += 4;
            p.luck = std::min(150, p.luck + 2);
        } else {                          // 将信收入旧卷：悟性+2
            p.insight = std::min(INSIGHT_MAX, p.insight + 2);
        }
    } else if (s.id == "rootpool") {
        if (optionIndex == 0) {          // 以身引泉：资质+3，气血−12
            p.apt = std::min(APT_MAX, p.apt + 3);
            p.health = std::max(1, p.health - 12);
        } else {                          // 只取一缕灵息：土道经验+40
            artGain(p, "土", 40);
        }
    } else if (s.id == "rootecho") {
        if (optionIndex == 0) {          // 承接残碑：资质+3，消耗 40 灵石
            p.apt = std::min(APT_MAX, p.apt + 3);
            p.stones -= 40;
        } else {                          // 悟其得失：气道经验+50
            artGain(p, "气", 50);
        }
    } else if (s.id == "oldblade") {
        if (optionIndex == 0) {          // 送剑穗归乡：重剑诀抄本，声望+3
            grantBook("heavy");
            p.renown += 3;
        } else {                          // 帮他补好断剑：养刀术抄本，器道+30
            grantBook("bladecare");
            artGain(p, "器", 30);
        }
    } else if (s.id == "mentorwill") {
        if (optionIndex == 0) {          // 立下授业之愿：胎息经抄本，贡献+30
            grantBook("fetal");        // 胎息经
            g.economy.contribution += 30;
        } else {                          // 修订授业手札：五内观想法，神道+40
            grantBook("visual");       // 五内观想法
            artGain(p, "神", 40);
        }
    } else if (s.id == "sharedmoon") {
        if (optionIndex == 0) {          // 停下来陪伴：心境+6，交情+8
            a.heart += 6;
            if (!b.partner.empty()) addRelation(g, b.partner, 8);
        } else {                          // 留下共修心得：水道+35，交情+4
            artGain(p, "水", 35);
            if (!b.partner.empty()) addRelation(g, b.partner, 4);
        }
    } else if (s.id == "toxinMercy") {
        if (optionIndex == 0) {          // 送药而不追究：龟息术抄本，声望+4
            grantBook("turtle");
            p.renown += 4;
        } else {                          // 告知执事并救治：鸩羽诀抄本，贡献+25
            grantBook("venom");
            g.economy.contribution += 25;
        }
    } else if (s.id == "seaLantern") {
        if (optionIndex == 0) {          // 抢救水剑经
            grantBook("deepwater");    // 沧溟水剑经
        } else {                          // 引灯火入道：悟道点+2，心境+5
            a.points += 2;
            a.heart += 5;
        }
    }

    const string optName = (optionIndex == 0) ? s.opt1Name : s.opt2Name;
    logEvent(g, "self",
             "「" + s.name + "」：" + optName + "。" + receipt.changes(g) + "。这段因缘已落笔。", true);
}

// ============================================================
// 野外悬赏
// ============================================================
const data::WildTaskDef* findWildTask(const string& id) {
    for (const auto& t : data::WildTaskDef_TABLE())
        if (t.id == id) return &t;
    return nullptr;
}

string wildActionError(const Game& g, const string& id) {
    if (!findWildTask(id)) return "没有这类悬赏。";
    if (g.ended) return "这一世已经结束。";
    if (g.pending.has_value()) return "请先处理当前事件。";
    if (g.player.arts.battle.has_value()) return "请先完成斗法。";
    if (g.job.has_value()) return "请先完成或中止当前安排。";
    const auto* loc = findLoc(g.player.location);
    if (!loc || string(loc->type) != "wild") return "请前往方壶山、青石灵脉或无尽海接野外悬赏。";
    if (g.player.location == "sea" && g.player.stage < 3) return "无尽海悬赏需筑基境界。";
    if (g.player.health < 50) return "气血不足 50，请先休养。";
    if (g.player.stones < 3) return "接悬赏需 3 灵石。";
    if (g.bonds.wildMonth == g.clock.days / 30 && g.bonds.wildCount >= 3)
        return "本月野外悬赏已满三次。";
    return "";
}

void societyWildBegin(Game& g, const string& id) {
    if (!wildActionError(g, id).empty()) return;
    const auto* t = findWildTask(id);
    const int month = g.clock.days / 30;
    if (g.bonds.wildMonth != month) {
        g.bonds.wildMonth = month;
        g.bonds.wildCount = 0;
    }
    g.bonds.wildCount++;
    g.player.stones -= 3;
    Job job;
    job.actionType = "wild";
    job.kind = t->id;
    job.name = t->name;
    job.total = job.remaining = t->days;
    job.started = g.clock.days;
    job.cost = 3;
    job.paid = true;
    job.fee = t->fee + (g.player.stage / 3) * 25;
    job.sect = t->style;
    g.job = job;
    logEvent(g, "self", "接下悬赏「" + t->name + "」，需 " + std::to_string(t->days) + " 天。");
}

// ============================================================
// 师徒 / 道侣
// ============================================================
bool bondCanTakeDisciple(const Game& g) {
    return g.bonds.disciples.size() < 2;   // 最多两名弟子
}

string bondMonthlyError(const Game& g, const string& kind) {
    const int month = g.clock.days / 30;
    const Bonds& b = g.bonds;

    if (kind == "lesson") {          // 师尊授业：每月一次
        if (b.master.empty()) return "尚未拜师。";
        if (b.mentorMonth == month) return "本月已受业一次。";
        return "";
    }
    if (kind == "teach") {           // 培养弟子：每月一次
        if (b.disciples.empty()) return "门下尚无弟子。";
        if (b.discipleMonth == month) return "本月已授业一次。";
        return "";
    }
    if (kind == "together") {        // 相伴共修：每月一次
        if (b.partner.empty()) return "尚无道侣。";
        if (b.partnerMonth == month) return "本月已相伴一次。";
        return "";
    }
    return "未知往来。";
}

BondActionInfo bondActionInfo(const Game& g, const string& kind) {
    BondActionInfo info;
    info.id = kind;
    info.valid = true;
    if (kind == "lesson") {
        info.name = "师尊授业";
        info.desc = "大道经验 +10、心境 +1；第二次授业另传胎息经抄本，每月一次。";
        info.days = 10;
        info.cost = 12;
        info.npcId = g.bonds.master;
    } else if (kind == "teach") {
        info.name = "传道授业";
        info.desc = "指点同地弟子，弟子修为 +40、悟性 +3，每月一次。";
        info.days = 10;
        info.cost = 10;
    } else if (kind == "together") {
        info.name = "相伴共修";
        info.desc = "与道侣相伴十日，大道经验 +6、心境 +2、气血 +4，每月一次。";
        info.days = 10;
        info.npcId = g.bonds.partner;
    } else if (kind == "part") {
        info.name = "解除道侣之约";
        info.desc = "解除道侣名分，对方好感 −20。";
        info.days = 1;
    } else if (kind == "dismiss") {
        info.name = "辞别师门";
        info.desc = "解除师尊名分，对方好感 −15。";
        info.days = 1;
    } else {
        info.valid = false;
    }
    return info;
}

string bondActionError(const Game& g, const string& kind) {
    const auto info = bondActionInfo(g, kind);
    if (!info.valid) return "未知师门往来。";
    if (g.ended) return "这一世已经结束。";
    if (g.pending.has_value()) return "请先处理当前事件。";
    if (g.player.arts.battle.has_value()) return "请先完成斗法。";
    if (g.job.has_value()) return "请先完成或中止当前安排。";
    if (g.player.stones < info.cost) return "本次往来灵石不足，需 " + std::to_string(info.cost) + " 灵石。";
    if (kind == "part") return g.bonds.partner.empty() ? "尚无道侣。" : "";
    if (kind == "dismiss") return g.bonds.master.empty() ? "尚未拜师。" : "";
    const string monthly = bondMonthlyError(g, kind);
    if (!monthly.empty()) return monthly;
    if ((kind == "lesson" || kind == "teach") &&
        (g.player.sect.empty() || g.player.location != g.player.sect))
        return "请回所属山门授业。";
    if (kind == "teach") {
        for (const auto& npc : g.npcs) {
            if (!npc.alive || npc.location != g.player.location) continue;
            if (std::find(g.bonds.disciples.begin(), g.bonds.disciples.end(), npc.id) != g.bonds.disciples.end())
                return "";
        }
        return "请先与在世弟子在山门相见。";
    }
    for (const auto& npc : g.npcs) {
        if (npc.id != info.npcId) continue;
        if (!npc.alive) return "对方已离世。";
        return npc.location == g.player.location ? "" : "请先与对方在同一地点相见。";
    }
    return "未找到这位故人。";
}

void societyBondBegin(Game& g, const string& kind) {
    if (!bondActionError(g, kind).empty()) return;
    const auto info = bondActionInfo(g, kind);
    Job job;
    job.actionType = "bond";
    job.kind = kind;
    job.name = info.name;
    job.npcId = info.npcId;
    job.total = job.remaining = info.days;
    job.started = g.clock.days;
    job.cost = info.cost;
    job.paid = true;
    g.player.stones -= info.cost;
    g.job = job;
    logEvent(g, "self", "安排" + info.name + "，需 " + std::to_string(info.days) +
             " 天，投入 " + std::to_string(info.cost) + " 灵石。");
}

// ============================================================
// 领取俸禄（对应 socInstant 的 stipend 分支）
// ============================================================
StipendResult socCollectStipend(Game& g) {
    StipendResult out;
    Player& p = g.player;
    Bonds& b = g.bonds;
    const int month = g.clock.days / 30;

    if (g.ended || g.pending.has_value())
        return {false, "请先处理当前事件。", 0, 0};
    if (p.sect.empty()) return {false, "散修没有俸禄。", 0, 0};
    if (p.location != p.sect) return {false, "请回所属山门领取。", 0, 0};
    if (b.stipend == month) return {false, "本月已经领过俸禄。", 0, 0};
    // 恶名 40 以上暂停领取（旧欠不消失）
    if (b.notoriety >= 40)
        return {false, "恶名过高，宗门暂停发放；旧欠保留。", 0, 0};

    Payroll& pay = b.payroll;
    if (pay.balance <= 0) return {false, "暂无可领俸禄。", 0, 0};

    // 府库不足时部分支付，欠额保留
    const int treasury = g.world.sects[p.sect].treasury;
    const int paid = std::min(pay.balance, std::max(0, treasury));
    g.world.sects[p.sect].treasury -= paid;
    p.stones += paid;
    pay.balance -= paid;
    b.stipend = month;

    out.paid = paid;
    out.owed = pay.balance;
    logEvent(g, "self",
             "领取俸禄 " + std::to_string(paid) + " 灵石。" +
                 (out.owed > 0 ? "府库不足，尚欠 " + std::to_string(out.owed) +
                                     " 灵石，欠额保留。"
                               : ""),
             out.owed > 0);
    return out;
}


// ============================================================
// 关系往来的结算（对应 JS socComplete 的 bond 分支）
// ============================================================
void societyBondComplete(Game& g, const string& kind, const Npc* npc) {
    Player& p = g.player;
    Bonds& b = g.bonds;
    Arts& a = p.arts;
    const int month = g.clock.days / 30;
    const RewardReceipt receipt(g);

    if (kind == "master" && npc) {
        b.master = npc->id;
        addRelation(g, npc->id, 10);
        logEvent(g, "sect",
                 "你行过拜师礼，" + npc->name + "收下你的拜帖。往后可在山门受业。",
                 true);
    } else if (kind == "lesson") {
        b.lessons++;
        b.mentorMonth = month;
        artGain(p, a.focus, 10);
        a.heart += 1;
        // 第二次授业另传《胎息经》抄本
        if (b.lessons == 2) ecoAcquireBook(g, "fetal", "师尊第二次授业");
        logEvent(g, "self", "受师尊指点：" + receipt.changes(g) + "。");
    } else if (kind == "disciple" && npc) {
        b.disciples.push_back(npc->id);
        addRelation(g, npc->id, 10);
        logEvent(g, "sect",
                 npc->name + "拜入门下。可在山门授业，培养其修为与悟性。", true);
    } else if (kind == "teach") {
        b.discipleMonth = month;
        // 培养附近真实弟子的修为与悟性，不增加资质
        for (const auto& id : b.disciples) {
            for (auto& n : g.npcs) {
                if (n.id != id || !n.alive || n.location != p.location) continue;
                const int need = realmOf(n.stage).need;
                n.xp = std::min(need, n.xp + 40);
                n.insight = std::min(INSIGHT_MAX, n.insight + 3);
                npcHistory(g, n, "受师尊" + p.name + "指点，修为与悟性有所增长。");
            }
        }
        logEvent(g, "self", "为门下弟子授业，弟子修为与悟性有所增长。");
    } else if (kind == "partner" && npc) {
        b.partner = npc->id;
        b.bondDays = 0;
        addRelation(g, npc->id, 20);
        logEvent(g, "self", "你与" + npc->name + "结为道侣。往后可相伴共修。", true);
    } else if (kind == "together") {
        b.partnerMonth = month;
        b.bondDays += 10;
        artGain(p, a.focus, 6);
        a.heart += 2;
        p.health = std::min(100, p.health + 4);
        if (!b.partner.empty()) addRelation(g, b.partner, 3);
        logEvent(g, "self",
                 "与道侣相伴共修10天：" + receipt.changes(g) + "。");
    } else if (kind == "part") {
        if (!b.partner.empty()) addRelation(g, b.partner, -20);
        logEvent(g, "self", "你与道侣解除了约定。", true);
        b.partner.clear();
        b.bondDays = 0;
    } else if (kind == "dismiss") {
        if (!b.master.empty()) addRelation(g, b.master, -15);
        logEvent(g, "sect", "你辞别师门。旧日授业之恩仍在，名分已解。", true);
        b.master.clear();
        b.lessons = 0;
    }
}

void societyWildStart(Game& g, const Job& job) {
    EnemySpec e;
    e.name = job.name;
    e.stage = g.player.stage;
    e.style = job.sect;    // 建工作时借用 sect 字段携带对手风格
    e.kind = "wild";
    e.beast = job.kind == "beast";
    e.sword = (job.sect == "剑") ? 15 + g.player.stage * 20 : 0;
    e.wildTask = job.kind;
    e.wildFee = job.fee;
    battleStart(g, e);
}

void societyWildEnd(Game& g, const Battle& b) {
    if (!b.won || b.kind != "wild" || !b.advKind.empty() || b.wildTask.empty()) return;
    g.player.stones += b.advFee;
    g.player.renown += 2;
    g.bonds.wildWins++;
    if (b.wildTask == "beast") {
        vector<const data::MaterialDef*> pool;
        const int tier = ordinaryMaterialGradeLimit(g.player);
        for (const auto& material : data::MaterialDef_TABLE())
            if (material.grade == tier) pool.push_back(&material);
        if (!pool.empty()) {
            const auto* material = pickOf(g, pool);
            g.player.arts.materials[material->id] += 2;
            logEvent(g, "self", "驱妖获得" + material->name + " ×2。");
        }
    }
    logEvent(g, "self",
             "悬赏「" + b.enemy + "」完成，领取赏金 " +
                 std::to_string(b.advFee) + " 灵石，声望 +2。",
             true);
}

} // namespace nz
