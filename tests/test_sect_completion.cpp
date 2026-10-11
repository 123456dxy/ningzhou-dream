#include "harness.h"

#include <algorithm>

#include "core/economy.h"
#include "core/play.h"
#include "core/save.h"
#include "core/sect_trial.h"
#include "core/sim.h"

namespace nztest {
using namespace nz;
namespace {
Game sectGame() {
    Game g = createGame();
    g.pending.reset(); g.job.reset(); g.npcs.clear();
    g.player.arts.battle.reset();
    g.player.sect = "jinhong"; g.player.location = "jinhong";
    g.player.health = 100; g.player.stones = 500;
    g.player.stage = 0; g.player.renown = 10;
    g.economy.month = g.clock.days / 30;
    g.economy.taskCounts.clear();
    g.world.sects["jinhong"].treasury = 10000;
    Npc elder;
    elder.id = "sect-elder"; elder.name = "试剑师长"; elder.gender = "男";
    elder.sect = "jinhong"; elder.location = "jinhong";
    elder.stage = 3; elder.age = 40; elder.lifespan = 200;
    elder.alive = true; elder.health = 100;
    g.npcs.push_back(elder);
    g.player.relations[elder.id] = 40;
    return g;
}
const play::SocialAction* masterAction(const vector<play::SocialAction>& actions) {
    for (const auto& action : actions) if (action.id == "master") return &action;
    return nullptr;
}
}

void runSectCompletionTests() {
    std::printf("\n=== 宗门师长入口、拜师约束与差事额度 ===\n");
    {
        Game g = sectGame(); g.player.relations["sect-elder"] = 0;
        const auto actions = play::socialActions(g, "sect-elder");
        const auto* master = masterAction(actions);
        check(master && !master->enabled && master->reason.find("好感不足40") != string::npos,
              "好感不足仍显示拜师选项与明确所缺条件");
        const auto before = saveToJson(g);
        check(!play::socialDo(g, "sect-elder", "master").ok, "不足好感不能直接调用拜师");
        checkStr(saveToJson(g), before, "被拒拜师不扣钱、不建立关系、不耗时");
        g.player.relations["sect-elder"] = 40; g.player.renown = 9;
        check(sectMasterError(g, "sect-elder").find("声望10") != string::npos, "拜师需要声望十");
        g.player.renown = 10; g.player.stones = 19;
        check(sectMasterError(g, "sect-elder").find("20灵石") != string::npos, "拜师显示准确礼金门槛");
        g.player.stones = 20;
        checkStr(sectMasterError(g, "sect-elder"), "", "完整达到条件可拜师");
        const auto result = play::socialDo(g, "sect-elder", "master");
        check(result.ok, "宗门拜师复用真实道友往来执行");
        checkStr(g.bonds.master, "sect-elder", "拜师记录真实 NPC 师尊");
        checkEq(g.player.stones, 0, "拜师消耗二十灵石");
        checkEq(relationOf(g, "sect-elder"), 50, "拜师建立关系并增加好感十");
        const auto after = saveToJson(g);
        check(!play::socialDo(g, "sect-elder", "master").ok, "不得重复拜同一师尊");
        checkStr(saveToJson(g), after, "重复拜师不再支付或刷好感");
    }
    {
        Game g = sectGame(); g.player.sect.clear();
        check(sectMasterError(g, "sect-elder").find("入门考核") != string::npos, "散修必须先通过入门考核");
        g.player.sect = "lihuo";
        check(sectMasterError(g, "sect-elder").find("本门") != string::npos, "不能拜异宗师长");
        g.player.sect = "jinhong"; g.player.stage = 3;
        check(sectMasterError(g, "sect-elder").find("高于") != string::npos, "同境界不可拜为师长");
        g.player.stage = 0; g.bonds.partner = "sect-elder";
        check(sectMasterError(g, "sect-elder").find("道侣") != string::npos, "修复道侣又拜为师尊的冲突");
        g.bonds.partner.clear(); g.bonds.disciples = {"sect-elder"};
        check(sectMasterError(g, "sect-elder").find("弟子") != string::npos, "修复反拜自己弟子为师尊的冲突");
    }
    {
        Game g = sectGame();
        g.player.stage = 6; g.npcs.front().stage = 3;
        g.bonds.master = "sect-elder";
        const auto withMaster = saveToJson(g);
        const auto masterResult = play::socialDo(g, "sect-elder", "disciple");
        check(!masterResult.ok && masterResult.error.find("现任师尊") != string::npos,
              "玩家超过师尊境界后仍不能将师尊收为弟子");
        checkStr(saveToJson(g), withMaster, "拒收师尊为徒不修改游戏");
        g.bonds.master.clear(); g.bonds.partner = "sect-elder";
        const auto withPartner = saveToJson(g);
        const auto partnerResult = play::socialDo(g, "sect-elder", "disciple");
        check(!partnerResult.ok && partnerResult.error.find("道侣") != string::npos,
              "道侣不能被重复收为弟子");
        checkStr(saveToJson(g), withPartner, "拒收道侣为徒不修改游戏");
    }
    {
        Game g = sectGame();
        Npc traveler = g.npcs.front(); traveler.id = "sect-traveling-elder"; traveler.location = "yunxi";
        traveler.stage = 6; g.npcs.push_back(traveler);
        Npc dead = traveler; dead.id = "sect-dead-elder"; dead.alive = false; g.npcs.push_back(dead);
        Npc beast = traveler; beast.id = "sect-beast"; beast.beast = true; g.npcs.push_back(beast);
        Npc outsider = traveler; outsider.id = "sect-other-elder"; outsider.sect = "lihuo"; g.npcs.push_back(outsider);
        const auto before = saveToJson(g);
        const auto mentors = sectMentorCandidates(g);
        checkEq(mentors.size(), 2, "师长列表只包含在世高境同门，仍列出外出师长");
        checkStr(mentors.front()->id, "sect-elder", "同地师长优先展示");
        check(!sectMasterError(g, traveler.id).empty(), "异地师长必须先赶路相见");
        checkStr(saveToJson(g), before, "查看师长列表和不可用原因不改游戏");
        g.bonds.master = "sect-elder"; g.player.stage = 6;
        const auto retained = sectMentorCandidates(g);
        checkEq(retained.size(), 1, "玩家超过师尊境界后仍保留现任师尊入口");
        checkStr(retained.front()->id, "sect-elder", "现任师尊优先且不因境界变化失去名分");
    }
    {
        Game g = sectGame();
        checkEq(sectTaskDefs().size(), 4, "四种差事包含原山门差事与三种新差事");
        check(!ecoActionError(g, "task", "sect-array").empty(), "护宗灵阵差事要求筑基期");
        checkStr(ecoActionError(g, "task", "sect-patrol"), "", "炼气修士可以接巡守差事");
        const auto offer = ecoActionInfo(g, "task", "sect-patrol");
        const int stones = g.player.stones, treasury = g.world.sects["jinhong"].treasury;
        const int contribution = g.economy.contribution;
        const double exp = g.player.arts.exp["金"];
        ecoBegin(g, "task", "sect-patrol");
        check(g.job.has_value(), "宗门差事真正创建耗时工作");
        checkEq(sectTasksTakenThisMonth(g), 1, "接单立即占本月共享额度");
        checkEq(g.player.stones, stones, "未完成差事不提前领取工资");
        checkEq(g.world.sects["jinhong"].treasury, treasury - offer.fee, "差事工资从宗门府库预留");
        if (g.job) {
            const Job job = *g.job; g.job.reset();
            ecoComplete(g, job);
            checkEq(g.player.stones, stones + offer.fee, "巡守完成发放预定工资");
            checkEq(g.economy.contribution, contribution + 10, "巡守完成发放贡献十");
            checkNear(g.player.arts.exp["金"], exp + 4, "巡守增长本宗金道经验四");
        }
        g.economy.taskCounts["sect"] = 1; g.economy.taskCounts["sect-records"] = 1;
        checkEq(sectTasksTakenThisMonth(g), 3, "新旧宗门任务累计同一个月限");
        check(!ecoActionError(g, "task", "sect-patrol").empty(), "满额不得重复新差事");
        check(!ecoActionError(g, "task", "sect").empty(), "满额不得通过旧差事绕过月限");
        g.economy.month--;
        checkEq(sectTasksTakenThisMonth(g), 0, "上月任务记录不占当前月额度");
    }
    {
        Game g = sectGame(); g.player.location = "wuling";
        for (const auto& task : sectTaskDefs()) {
            const auto before = saveToJson(g);
            check(!ecoActionError(g, "task", task.id).empty(), "异地不能远程接" + task.name);
            ecoBegin(g, "task", task.id);
            checkStr(saveToJson(g), before, "异地拒绝" + task.name + "不传送、不扣府库");
        }
    }
}
} // namespace nztest
