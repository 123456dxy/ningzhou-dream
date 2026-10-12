// 宁州一梦 · C++ 版 — 声望系统（实现）
#include "core/renown.h"

#include <algorithm>
#include <cmath>

#include "core/base.h"
#include "core/sect_trial.h"
#include "core/sim.h"
#include "core/society.h"

namespace nz::renown {

using std::string;
using std::vector;

// 五档名望。数值刻意做得「前几档容易到、后两档要攒一辈子」，
// 让声望在整局里都值得追，又不会早早封顶。
// 用途：读取五档声望阈值与说明。
// 参与流程：声望分档及进度查询。
// 使用方式：无参数，使用固定静态表。
// 返回值：Tier 列表常量引用。
const vector<Tier>& tiers() {
    static const vector<Tier> data = {
        {"unknown", "无名之辈", "宁州没人听说过你。", 0},
        {"known", "小有名气", "同城修士多半听过你的名字。", 20},
        {"notable", "一方名士", "五城坊市都愿意给你让利。", 60},
        {"famous", "名动宁州", "大宗掌门也知道有你这个人。", 130},
        {"legend", "天下皆知", "你的名字本身就是一种分量。", 220},
    };
    return data;
}

// 用途：按声望查当前身份档位。
// 参与流程：声望页面和描述。
// 使用方式：传声望，未达下一阈值保持当前档。
// 返回值：静态表中当前 Tier 的引用。
const Tier& tierOf(int renown) {
    const auto& t = tiers();
    const Tier* cur = &t[0];
    for (const auto& x : t)
        if (renown >= x.at) cur = &x;
    return *cur;
}

// 用途：计算晋升下一声望档还需的点数。
// 参与流程：声望进度展示。
// 使用方式：传当前声望。
// 返回值：未达下一档的差额；满档为 -1。
int toNextTier(int renown) {
    for (const auto& x : tiers())
        if (renown < x.at) return x.at - renown;
    return -1;
}

// 用途：计算当前声望档内进度。
// 参与流程：声望进度条。
// 使用方式：传当前声望，按相邻档阈值归一化。
// 返回值：0..1 的进度，最高档为一。
double tierProgress(int renown) {
    const auto& t = tiers();
    int lo = t[0].at, hi = t.back().at;
    for (size_t i = 0; i < t.size(); ++i) {
        if (renown < t[i].at) {
            hi = t[i].at;
            lo = (i == 0) ? 0 : t[i - 1].at;
            break;
        }
        if (i + 1 == t.size()) return 1.0;
    }
    if (hi <= lo) return 1.0;
    return clamp(static_cast<double>(renown - lo) / (hi - lo), 0.0, 1.0);
}

// ------------------------------------------------------------
// 加成
// ------------------------------------------------------------
// 每点声望 0.25% 折扣，上限 15%
// 用途：计算声望带来的坊市买入折扣。
// 参与流程：商品购买报价。
// 使用方式：传非负口径声望，负值按零处理。
// 返回值：不超过百分之十五的折扣比例。
double buyDiscount(int renown) {
    return std::min(0.15, std::max(0, renown) * 0.0025);
}

// 每点 0.3%，上限 20%
// 用途：计算声望带来的出售溢价。
// 参与流程：坊市收购报价。
// 使用方式：传声望，负值不提供加成。
// 返回值：不超过百分之二十的溢价比例。
double sellBonus(int renown) {
    return std::min(0.20, std::max(0, renown) * 0.0030);
}

// 基础 8 件，每 20 点声望 +1，上限 14
// 用途：计算同类货品月度出售上限。
// 参与流程：坊市需求量校验。
// 使用方式：传当前声望，每二十点增加一件。
// 返回值：八至十四件的月度上限。
int sellLimit(int renown) {
    return std::min(14, 8 + std::max(0, renown) / 20);
}

// 基础 400，每点 +8，上限 2400
// 用途：计算坊市当前角色可享的收购资金额度。
// 参与流程：坊市出售资金限制。
// 使用方式：传声望，每点增加八灵石额度。
// 返回值：四百至两千四百的资金上限。
int marketCashCap(int renown) {
    return std::min(2400, 400 + std::max(0, renown) * 8);
}

// 每 4 点声望给 1 点初始交情，上限 25
// 用途：计算声望影响的初见好感系数。
// 参与流程：首次见礼和 NPC 初始关系。
// 使用方式：传当前声望，每四点折算一点。
// 返回值：零至二十五的初见印象值。
int firstImpression(int renown) {
    return std::min(25, std::max(0, renown) / 4);
}

// 每点 0.4%，上限 25%
// 用途：计算声望带来的藏经阁兑换折扣。
// 参与流程：宗门传承报价。
// 使用方式：传当前声望，负值按零。
// 返回值：最高百分之二十五的兑换折扣。
double libraryDiscount(int renown) {
    return std::min(0.25, std::max(0, renown) * 0.0040);
}

// 每点 0.5%，上限 30%
// 用途：计算声望对山门差事工钱的加成。
// 参与流程：宗门差事收益报价。
// 使用方式：传声望，不修改俸禄账。
// 返回值：最高百分之三十的工钱比例加成。
double stipendBonus(int renown) {
    return std::min(0.30, std::max(0, renown) * 0.0050);
}

// 大派看名声：星河、离火 要 8；其余不设门槛（与原版一致）
// 用途：查询特定宗门的入门声望要求。
// 参与流程：宗门考核前置。
// 使用方式：传宗门 ID，星河和离火有额外要求。
// 返回值：两宗为八点，其余为零。
int sectRequirement(const string& sectId) {
    if (sectId == "xinghe" || sectId == "lihuo") return 8;
    return 0;
}

// 高境修士收徒会看你的名声
// 用途：提供拜师统一声望门槛。
// 参与流程：当面拜师校验。
// 使用方式：无参数。
// 返回值：十点声望。
int masterRequirement() { return 10; }

// ------------------------------------------------------------
// 文案
// ------------------------------------------------------------
// 用途：生成当前声望身份与晋档说明。
// 参与流程：声望页面说明。
// 使用方式：传当前声望。
// 返回值：档位描述及距下一档点数的文案。
string describe(int renown) {
    const Tier& t = tierOf(renown);
    const int next = toNextTier(renown);
    string s = string("【") + t.name + "】" + t.desc;
    if (next > 0)
        s += " 距下一档还差 " + std::to_string(next) + " 点声望。";
    else
        s += " 已至顶峰。";
    return s;
}

// 用途：列出声望来源及当前可行动资格。
// 参与流程：声望导航目录。
// 使用方式：有 Game 时逐项检查真实条件；无参数时读简明说明。
// 返回值：结构化来源或旧说明列表，不推进时间与 RNG。
vector<string> sources() {
    return {
        "游历宁州：每累计900天 +1（0.4点/年，零数保留）",
        "突破境界：大境界 +12，小境界 +3",
        "通过入门考核并拜入宗门 +3（每世一次）",
        "路见不平：出手相助胜利 +3；掩护撤离 +1",
        "野外悬赏得胜 +2（每月最多三次）",
        "旧剑归途：送剑穗归乡 +3（每世一次）",
        "药圃无人：送药而不追究 +4（每世一次）",
        "迎战兽潮得胜：初起 +5、猖獗 +7、滔天 +9",
    };
}

// 用途：列出声望来源及当前可行动资格。
// 参与流程：声望导航目录。
// 使用方式：有 Game 时逐项检查真实条件；无参数时读简明说明。
// 返回值：结构化来源或旧说明列表，不推进时间与 RNG。
vector<Source> sources(const Game& g) {
    const auto blocked = [&]() -> string {
        if (g.ended || g.player.health <= 0) return "这一世已结束或伤势过重。";
        if (g.dungeons.run) return "请先离开当前秘境。";
        if (g.player.arts.battle) return "请先完成当前斗法。";
        if (g.pending) return "请先处理眼前抉择。";
        if (g.job) return "请先完成或中止当前安排。";
        return "";
    };
    const string busy = blocked();
    vector<Source> result;
    auto add = [&](Source source, const string& error) {
        source.enabled = error.empty();
        source.reason = error.empty() ? "当前满足条件，可前往入口安排。" : error;
        result.push_back(std::move(source));
    };
    const auto carried = g.clock.carry.find("renown");
    const double progress = carried == g.clock.carry.end() ? 0 : carried->second;
    add({"explore", "游历宁州", "每累计900天 +1",
         {"安排「游历寻缘」，按实际完成的游历天数累计；赶路与其他安排不计入。",
          "每360天累积0.4点，下一点进度 " + std::to_string(std::max(0, std::min(99, static_cast<int>(std::floor(progress * 100 + 1e-7))))) + "%（累计游历 " + std::to_string(g.adventure.total) + " 天）。"},
         "可反复游历，未满1点的声望进度保留。", false, "", {"chronicle", "", "", "安排游历寻缘"}}, busy);

    const auto breakthrough = breakthroughInfo(g);
    string breakthroughError = busy;
    if (breakthroughError.empty() && !breakthrough.ready) breakthroughError = "修为尚未圆满，或已达到最高境界。";
    if (breakthroughError.empty() && g.player.health < 35) breakthroughError = "闭关需气血至少35。";
    if (breakthroughError.empty() && g.player.stones < breakthrough.cost) breakthroughError = "闭关所需灵石不足。";
    add({"breakthrough", "成功突破", "小境界 +3 / 大境界 +12",
         {"修为圆满、气血至少35，并支付本次闭关的灵石费用。",
          "当前目标 " + breakthrough.next + "；所需 " + std::to_string(breakthrough.cost) + " 灵石；本次成功后声望 +" + std::to_string(breakthrough.major ? 12 : 3) + "。"},
         "仅成功晋升时获得；失败不增加声望。", false, "", {"chronicle", "", "", "查看闭关突破"}}, breakthroughError);

    string trialError = "需抵达尚未拜入的宗门山门报名。";
    if (!busy.empty()) trialError = busy;
    else if (!g.player.sect.empty()) trialError = "已拜入宗门，本世无法重复获取入门声望。";
    else for (const auto& trial : sectTrialDefs()) {
        if (g.player.location == trial.sect) { trialError = sectTrialError(g, trial.sect); break; }
    }
    add({"sectTrial", "拜入宗门", "+3",
         {"亲赴山门，支付10灵石，气血至少30；通过3天入门斗法后才获得声望。",
          "星河剑派与离火门需声望8，星河仅收女修；金虹、竹山、化尘不设声望门槛。"},
         "每世首次入门一次；考核失败无声望，可重试。", false, "", {"society", "sect", "", "查看山门考核"}}, trialError);

    const bool justice = g.pending && g.pending->type == "adventure" && g.pending->adventure && g.pending->adventure->kind == "justice";
    const string justiceError = justice ? (g.ended ? "这一世已结束。" : "") : "游历寻缘时随机触发「路见不平」，需等待对应机缘。";
    add({"justiceFight", "行侠·出手相助", "+3",
         {"遇到「路见不平」时选择出手相助，并击败拦路恶徒。", "出手需要气血至少35；失败或避开冲突无声望。"},
         "每次触发机缘可选一次，与掩护撤离互斥。", false, "", {"quests", "travel", "", "查看游历机缘"}},
        justiceError.empty() && g.player.health < 35 ? "气血不足35，无法出手相助。" : justiceError);
    add({"justiceRescue", "行侠·掩护撤离", "+1",
         {"遇到「路见不平」时选择掩护受困者撤离。", "无需斗法，气血损失3点，最低保留1点。"},
         "每次触发机缘可选一次，与出手相助互斥。", false, "", {"quests", "travel", "", "查看游历机缘"}}, justiceError);

    const int used = g.bonds.wildMonth == g.clock.days / 30 ? g.bonds.wildCount : 0;
    string wildError = busy.empty() ? wildActionError(g, "bandit") : busy;
    if (wildError == "请前往方壶山、青石灵脉或无尽海接野外悬赏。") wildError = "需抵达野外地点接悬赏。";
    const auto* location = findLoc(g.player.location);
    add({"wildTask", "野外悬赏胜利", "每胜 +2",
         {"在野外地点接清剿山匪、驱除药田妖兽或截回商路货箱；气血至少50，报名3灵石。",
          "分别准备6 / 7 / 9天后进行非致命斗法；无尽海悬赏需筑基。"},
         "每月最多3次，本月已接 " + std::to_string(used) + " / 3；失败也占报名次数。", false, "",
         {"quests", "wild", location && string(location->type) == "wild" ? "" : "fanghu", "查看野外悬赏"}}, wildError);

    for (const auto& story : data::StoryDef_TABLE()) {
        if (story.id != "oldblade" && story.id != "toxinMercy") continue;
        const bool blade = story.id == "oldblade";
        const string error = g.bonds.stories.count(story.id) ? "这段因缘本世已落笔，不能再次领取。" : !busy.empty() ? busy : storyError(g, &story);
        add({story.id, story.name, blade ? "+3" : "+4",
             {blade ? "斗法胜场达到3次（当前 " + std::to_string(g.player.arts.progress.wins) + " 次），前往逸风城。"
                    : "拜入竹山宗并成功炼丹5次（当前 " + std::to_string(g.player.arts.progress.brew) + " 次），前往竹山宗。",
              string("安排8天支线，选择「") + (blade ? "送剑穗归乡" : "送药而不追究") + "」；另一选择不加声望。"},
             "本世仅一次，选择后无法重复支线。", false, "", {"quests", "wild", story.location, "定位支线地点"}}, error);
    }
    add({"beastTide", "迎战兽潮胜利", g.tide.active ? "本期每胜 +" + std::to_string(3 + g.tide.severity * 2) : "初起 +5 / 猖獗 +7 / 滔天 +9",
         {"兽潮出现期间在岁月纪事选择迎战并获胜；全宁州可迎战。",
          g.tide.active ? "本期尚余 " + std::to_string(g.tide.monthsLeft) + " 个月，已击退 " + std::to_string(g.tide.wavesWon) + " 场。" : "需等待随机兽潮事件；固守不出不增加声望。"},
         "兽潮持续期间可重复迎战，没有每月次数限制。", false, "", {"chronicle", "", "", "查看兽潮纪事"}},
        !busy.empty() ? busy : g.tide.active ? "" : "当前宁州没有兽潮。" );
    return result;
}

} // namespace nz::renown
