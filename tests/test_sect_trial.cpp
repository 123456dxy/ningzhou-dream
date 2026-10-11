#include "harness.h"

#include <algorithm>
#include <set>

#include "core/battle.h"
#include "core/calendar.h"
#include "core/save.h"
#include "core/sect_trial.h"
#include "core/shop.h"
#include "core/sim.h"
#include "data/reference_tables.h"

namespace nztest {
using namespace nz;
namespace {
Game trialGame(const string& sect = "jinhong") {
    Game g = createGame();
    g.pending.reset(); g.job.reset(); g.npcs.clear();
    g.player.sect.clear(); g.player.location = sect;
    g.player.renown = 20; g.player.stones = 10000; g.player.health = 100;
    g.flags["choiceYear"] = g.year;
    return g;
}
bool includes(const vector<string>& entries, const string& id) {
    return std::find(entries.begin(), entries.end(), id) != entries.end();
}
void winTrial(Game& g) {
    if (!g.player.arts.battle) return;
    g.player.arts.battle->enemyHp = 0;
    battleFinish(g, true);
}
bool offered(const vector<shop::LibrarySpellSource>& entries, const string& id) {
    return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) { return entry.id == id; });
}
}

void runSectTrialTests() {
    std::printf("\n=== 宗门实战考核与藏经阁传承 ===\n");
    {
        Game g = trialGame(); g.player.location = "wuling";
        const auto before = saveToJson(g);
        check(!sectTrialBegin(g, "jinhong").empty(), "入门报名必须先赶往实际山门");
        check(saveToJson(g) == before, "远程报名拒绝不扣钱不传送不入宗");
        g.player.location = "jinhong"; g.player.stones = 9;
        const auto poor = saveToJson(g);
        check(!sectTrialBegin(g, "jinhong").empty(), "不足十灵石不能开始考核");
        check(saveToJson(g) == poor, "报名费不足不产生考核或耗时工作");
        g.player.stones = 100; Job job; job.actionType = "plan"; job.total = job.remaining = 2; g.job = job;
        const auto busy = saveToJson(g);
        check(!sectTrialBegin(g, "jinhong").empty(), "正在安排中不能额外报名考核");
        check(saveToJson(g) == busy, "忙碌时报名保持状态不变");
    }
    {
        Game g = trialGame("xinghe"); g.player.gender = "男";
        check(!sectTrialError(g, "xinghe").empty(), "星河考核保留招收女修要求");
        g.player.gender = "女"; g.player.renown = 7;
        check(!sectTrialError(g, "xinghe").empty(), "星河考核保留声望八点门槛");
        g.player.renown = 8;
        check(sectTrialError(g, "xinghe").empty(), "女修满足条件可报名水月试剑");
        g.player.location = "tianji";
        check(!sectTrialBegin(g, "tianji").empty(), "天机阁不被错误当成可拜入的五宗");
    }
    std::set<string> trialNames;
    for (const auto& def : sectTrialDefs()) {
        Game g = trialGame(def.sect);
        trialNames.insert(def.name);
        const int stones = g.player.stones, renown = g.player.renown;
        const auto spells = g.player.arts.spells;
        check(sectTrialBegin(g, def.sect).empty(), def.sect + "开始真实本门试炼");
        check(g.player.sect.empty(), def.sect + "报名与开战尚未入宗");
        checkEq(g.player.stones, stones - def.fee, def.sect + "只扣一次十灵石报名费");
        check(g.job && g.job->kind == "sectTrial" && g.job->remaining == 3, def.sect + "试炼冻结真实三天耗时");
        check(g.player.arts.battle && g.player.arts.battle->kind == "sectTrial" && !g.player.arts.battle->lethal, def.sect + "入门采用非致命卡牌战斗");
        if (!g.player.arts.battle) continue;
        const Battle battle = *g.player.arts.battle;
        check(battle.enemySect == def.sect && battle.enemyCards.enabled && battle.enemySpells.size() >= 2, def.sect + "敌人实际携带本宗功法与多种神通");
        check(!battle.enemyCards.main.empty() && battle.enemyCards.manuals.count(battle.enemyCards.main) > 0, def.sect + "考核弟子实际装配主修功法");
        checkStr(shop::manualSource(g, battle.enemyCards.main).sect, def.sect, def.sect + "考核主修属于本门流派");
        check(sectTrialStateError(g).empty(), def.sect + "在途考核通过核心状态校验");
        Game loaded; string error;
        check(loadFromJson(saveToJson(g), loaded, &error), def.sect + "在途试炼可读写存档：" + error);
        check(loaded.player.arts.battle && loaded.player.arts.battle->quest == battle.quest, def.sect + "读档保留真实报名序号上下文");
        const auto fighting = saveToJson(g);
        check(!sectTrialBegin(g, def.sect).empty(), def.sect + "战斗中禁止重复报名");
        check(saveToJson(g) == fighting, def.sect + "重复报名不会第二次扣钱");
        winTrial(g);
        checkStr(g.player.sect, def.sect, def.sect + "实际赢得试炼后正式入门");
        checkEq(g.player.renown, renown + 3, def.sect + "入门声望只发一次");
        check(includes(g.player.arts.bookList, def.manual) && includes(g.economy.books, def.manual), def.sect + "入门发放本门功法抄本");
        check(includes(g.player.arts.knownSpells, def.spell), def.sect + "入门领悟本门神通");
        check(g.player.arts.spells == spells, def.sect + "发放神通不挤占或自动改变携带槽");
        check(g.bonds.payroll.sect == def.sect, def.sect + "入门后初始化本门俸禄账簿");
        const auto won = saveToJson(g);
        sectTrialBattleComplete(g, battle, true);
        check(saveToJson(g) == won, def.sect + "重放战斗结算不再次发奖励");
        check(!sectTrialBegin(g, def.sect).empty(), def.sect + "已入门不能刷考核奖励");
        if (g.job) calendarRun(g, 3);
        checkEq(g.clock.days, 3, def.sect + "入门试炼真实消耗三天日历");
        check(sectTrialStateError(g).empty(), def.sect + "试炼结算后没有残留活跃报名");
    }
    checkEq(trialNames.size(), 5, "五宗拥有五种独立特色考核");
    {
        Game g = trialGame(); sectTrialBegin(g, "jinhong");
        if (!g.player.arts.battle) return;
        const Battle old = *g.player.arts.battle;
        g.player.arts.battle->hp = 1;
        battleFinish(g, false);
        check(g.player.sect.empty() && !g.ended && g.player.health > 0, "未通过非致命考核保留散修身份和生命");
        if (g.job) calendarRun(g, 3);
        g.player.health = 100;
        const int stones = g.player.stones;
        check(sectTrialBegin(g, "jinhong").empty(), "未通过考核休养后可再次应试");
        checkEq(g.player.stones, stones - 10, "重试需要新的小额报名费");
        const auto second = saveToJson(g);
        sectTrialBattleComplete(g, old, true);
        check(saveToJson(g) == second && g.player.sect.empty(), "旧失败考核不能重放为新报名的胜利");
        Game corrupt = g; corrupt.player.arts.battle->quest = "sect-trial:zhushan:2";
        check(!sectTrialStateError(corrupt).empty(), "伪造异宗试炼上下文无法通过校验");
        corrupt = g; corrupt.job->cost = 0;
        check(!sectTrialStateError(corrupt).empty(), "试炼费用记录损坏无法通过校验");
    }
    {
        Game g = trialGame(); g.player.location = "wuling";
        const int stones = g.player.stones;
        Job old; old.actionType = "join"; old.sect = "jinhong"; old.paid = true; old.cost = 30; old.total = 30;
        monthComplete(g, old);
        check(g.player.sect.empty() && g.player.location == "wuling" && !g.player.arts.battle, "旧档入门工作不得绕过考核或自动传送");
        checkEq(g.player.stones, stones + 30, "旧报名安排不能转入试炼时退回已付费用");
        g = trialGame(); const int original = g.player.stones;
        monthComplete(g, old);
        check(g.player.sect.empty() && g.player.arts.battle && g.player.arts.battle->kind == "sectTrial", "旧档在实际山门完成入门安排后转入真实考核");
        checkEq(g.player.stones, original + 20, "旧三十灵石费用退差额后仅保留十灵石试炼费");
    }
    {
        Game g = trialGame("xinghe"); g.player.sect = "xinghe"; g.player.stage = 6; g.economy.contribution = 1000;
        const auto manuals = shop::availableManuals(g);
        check(std::none_of(manuals.begin(), manuals.end(), [](const auto& m) { return m.sect != "xinghe"; }), "星河藏经阁仅返回本门与共享功法");
        check(std::none_of(manuals.begin(), manuals.end(), [](const auto& m) { return m.id == "fire" || m.id == "flame" || m.id == "heavy"; }), "星河不出售火主修或金虹私传功法");
        checkStr(shop::manualSource(g, "cloud").sect, "xinghe", "无宗门source的通用凌云诀可由星河共享");
        checkStr(shop::manualSource(g, "fetal").sect, "huachen", "无source体修功法归化尘而非随意分给金虹");
        checkStr(shop::manualSource(g, "fire").sect, "lihuo", "基础火功法明确归属离火门");
        for (const auto& m : data::ManualDef_TABLE()) if (!m.source.empty())
            checkStr(shop::manualSource(g, m.id).sect, m.source, m.name + "严格保留数据表所属宗门");
        const auto before = saveToJson(g);
        check(!shop::libraryExchange(g, "flame").ok, "跨宗直接提交火功法兑换请求被拒绝");
        check(saveToJson(g) == before, "跨宗功法兑换不扣贡献不发抄本");
        const auto spells = shop::availableLibrarySpells(g);
        check(offered(spells, "waterSeal") && offered(spells, "wavehit") && offered(spells, "sword"), "星河可兑换蓄水与水剑神通构筑");
        check(offered(spells, "skim") && offered(spells, "purge") && offered(spells, "qiFlow"), "各宗共享抽牌、移牌、聚气通用术法");
        check(!offered(spells, "fireburst") && !offered(spells, "goldslash") && !offered(spells, "body"), "星河神通目录排除火金体特色术法");
        const int points = g.economy.contribution;
        const auto source = shop::librarySpellSource(g, "waterSeal");
        const auto carried = g.player.arts.spells;
        check(source.enabled && shop::librarySpellExchange(g, "waterSeal").ok, "本门神通花贡献真实学会");
        checkEq(g.economy.contribution, points - source.points, "神通兑换只扣真实贡献成本");
        check(includes(g.player.arts.knownSpells, "waterSeal") && g.player.arts.spells == carried, "领悟进入已知神通列表而不自动装备");
        const auto learned = saveToJson(g);
        check(!shop::librarySpellExchange(g, "waterSeal").ok, "重复兑换已知神通被拒绝");
        check(saveToJson(g) == learned, "重复神通兑换保持贡献与状态不变");
        check(!shop::librarySpellExchange(g, "fireburst").ok, "直接请求异宗神通不能绕过过滤");
        check(!shop::librarySpellExchange(g, "strike").ok && !shop::librarySpellExchange(g, "defend").ok, "必带普通攻防不被错误当成贡献商品");
        g.player.stage = 0;
        check(!shop::librarySpellExchange(g, "qiFlow").ok, "高境界神通依旧需要实际修为门槛");
        g.player.stage = 6; g.player.location = "wuling";
        check(!shop::librarySpellExchange(g, "wavehit").ok, "兑换神通必须回实际山门");
        g.player.location = "xinghe"; Job job; job.actionType = "plan"; job.total = job.remaining = 3; g.job = job;
        const auto busy = saveToJson(g);
        check(!shop::librarySpellExchange(g, "wavehit").ok && !shop::libraryExchange(g, "ocean").ok, "忙碌中不可买神通或抄本影响战斗构筑");
        check(saveToJson(g) == busy, "忙碌兑换不改变贡献和传承");
    }
}

} // namespace nztest
