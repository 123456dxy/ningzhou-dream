#include "harness.h"
#include <algorithm>
#include <set>
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/friendship.h"
#include "core/location_services.h"
#include "core/npc_learning.h"
#include "core/save.h"
#include "core/sim.h"
namespace nztest {
using namespace nz;
namespace {
Game learningGame() {
    Game g = createGame(CreateOptions{});
    g.pending.reset(); g.npcs.resize(1);
    auto& teacher = g.npcs.front();
    teacher.sect = "jinhong"; teacher.dao = "金"; teacher.stage = 6;
    teacher.age = 30; teacher.lifespan = realmOf(6).life; teacher.health = 100;
    teacher.alive = true; teacher.beast = false; teacher.location = g.player.location;
    g.player.stage = g.player.highestStage = 6;
    g.player.lifespan = realmOf(6).life + g.player.lifeBonus;
    g.player.stones = 10000; g.player.relations[teacher.id] = 100;
    g.player.npcBonds[teacher.id].credit = 10000;
    return g;
}
void completeLesson(Game& g, Job job) {
    job.remaining = 0;
    g.clock.days = job.started + job.total;
    g.job.reset(); npcLearnComplete(g, job);
}
}
void runNpcLearningLocationTests() {
    Game g = createGame(CreateOptions{}); g.npcs.resize(1);
    auto& teacher = g.npcs[0]; teacher.sect = "jinhong"; teacher.dao = "金"; teacher.stage = 3;
    teacher.location = g.player.location; teacher.alive = true; teacher.health = 100;
    g.player.stage = 3; g.player.stones = 10000; g.player.relations[teacher.id] = 80;
    g.player.npcBonds[teacher.id].credit = 10000;
    auto lessons = npcLessons(g, teacher.id);
    auto it = std::find_if(lessons.begin(), lessons.end(), [](const auto& l) { return l.kind == "spell" && !l.learned && l.enabled; });
    check(it != lessons.end(), "同地同境门派NPC有真实可学神通");
    if (it != lessons.end()) {
        const auto lesson = *it; const string id = teacher.id;
        const int credit = friendship::creditOf(g, id), stones = g.player.stones;
        check(npcLearnBegin(g, id, lesson.kind, lesson.id).empty(), "满足好感和交情后可开始授业");
        checkEq(friendship::creditOf(g, id), credit - lesson.creditCost, "授业只扣一次交情");
        checkEq(g.player.stones, stones - lesson.stoneCost, "授业只扣一次学费");
        const auto inProgress = saveToJson(g);
        check(!npcLearnBegin(g, id, lesson.kind, lesson.id).empty(), "已有授业任务时重复开始被拒绝");
        checkStr(saveToJson(g), inProgress, "重复开始不重复扣学费或交情");
        check(std::find(g.player.arts.knownSpells.begin(), g.player.arts.knownSpells.end(), lesson.id) == g.player.arts.knownSpells.end(), "开工未预发学习成果");
        const auto saved = saveToJson(g); Game loaded; string error;
        check(loadFromJson(saved, loaded, &error), "授业在途可保存并恢复：" + error);
        if (loaded.job) {
            calendarRun(loaded, loaded.job->remaining);
            check(std::find(loaded.player.arts.knownSpells.begin(), loaded.player.arts.knownSpells.end(), lesson.id) != loaded.player.arts.knownSpells.end(), "完成授业真实解锁神通");
            checkEq(friendship::creditOf(loaded, id), credit - lesson.creditCost, "读档完成不重复扣交情");
            loaded.pending.reset();
            const auto before = saveToJson(loaded);
            check(!npcLearnBegin(loaded, id, lesson.kind, lesson.id).empty(), "重复学习拒绝");
            checkStr(saveToJson(loaded), before, "失败请教不改变任何状态");
        }
        g.job.reset();
    }
    g.player.stage = 0; g.player.relations[teacher.id] = 0;
    for (const auto& l : npcLessons(g, teacher.id)) if (!l.learned) check(!l.enabled, "不足好感或境界不能越阶学术");
    const auto snapshot = saveToJson(g);
    check(!npcLearnBegin(g, teacher.id, "spell", "missing-spell").empty(), "NPC不能传授不存在神通");
    checkStr(saveToJson(g), snapshot, "伪造授业请求不扣款");
    checkEq(LOCATIONS().size(), 28, "宁州地图28处地点");
    std::set<string> ids;
    for (const auto& l : LOCATIONS()) {
        check(ids.insert(l.id).second, string(l.name) + "地点ID独立");
        check(!locationServices(g, l.id).empty(), string(l.name) + "有真实可用功能");
    }
    check(findLoc("xinghe")->x < findLoc("yunxi")->x && findLoc("jinhong")->x > findLoc("tianxing")->x, "地图星河在西北、金虹在东北");
    g.player.location = "fanghu"; g.pending.reset(); g.job.reset();
    string tab; const int herbs = g.player.herbs;
    check(locationServiceBegin(g, "gather", tab).empty(), "野外地点采集入口可执行");
    if (g.job) calendarRun(g, g.job->remaining);
    checkEq(g.player.herbs, herbs + 3, "地点采集真实产出并同步总量");
    g.pending.reset(); check(locationServiceBegin(g, "insight", tab).empty(), "本地感悟可开始");
    if (g.job) calendarRun(g, g.job->remaining);
    g.pending.reset(); check(!locationServiceBegin(g, "insight", tab).empty(), "地点感悟每月限一次");
    checkStr(beastPortrait(0), "/assets/portraits/beast-0.webp", "炼气妖兽使用低阶立绘");
    checkStr(beastPortrait(12), "/assets/portraits/beast-4.webp", "化神妖兽使用神兽立绘");
    check(personPortrait("男", "xinghe") != personPortrait("女", "xinghe"), "NPC门派立绘匹配性别");

    std::printf("\n-- 授业知识、失约退费和完成资格 --\n");
    {
        Game state = learningGame();
        const auto& teacher = state.npcs.front();
        const auto carried = artNpcLoadout(teacher);
        const string before = saveToJson(state);
        const auto knowledge = npcLessons(state, teacher.id);
        const auto again = npcLessons(state, teacher.id);
        int spellCount = 0;
        bool same = knowledge.size() == again.size(), matching = true;
        for (size_t i = 0; i < knowledge.size(); ++i) {
            const auto& lesson = knowledge[i];
            if (i < again.size()) same = same && lesson.id == again[i].id && lesson.kind == again[i].kind;
            matching = matching && lesson.id != "strike" && lesson.id != "defend" && lesson.stage <= teacher.stage;
            if (lesson.kind == "spell") {
                ++spellCount;
                matching = matching && (lesson.dao == carried.focus ||
                    std::find(carried.spells.begin(), carried.spells.end(), lesson.id) != carried.spells.end());
            }
        }
        check(spellCount > static_cast<int>(carried.spells.size()), "NPC可教神通知识多于当前战斗携带槽");
        check(same && matching, "授业列表稳定且只扩展本派大道、保留实际携带术法");
        checkStr(saveToJson(state), before, "查询NPC知识不修改随机序列或战斗构筑");
        for (const auto& id : carried.spells)
            check(std::any_of(knowledge.begin(), knowledge.end(), [&](const auto& l) { return l.kind == "spell" && l.id == id; }),
                  "实际携带神通仍可教：" + id);
    }
    {
        for (int scenario = 0; scenario < 7; ++scenario) {
            Game state = learningGame();
            const string teacher = state.npcs.front().id;
            const auto list = npcLessons(state, teacher);
            const auto selected = std::find_if(list.begin(), list.end(), [](const auto& lesson) {
                return lesson.kind == "spell" && lesson.stage >= 3 && lesson.enabled;
            });
            check(selected != list.end(), "完成校验测试有真实高阶神通可学");
            if (selected == list.end()) continue;
            const auto lesson = *selected;
            const int stones = state.player.stones, credit = friendship::creditOf(state, teacher);
            check(npcLearnBegin(state, teacher, lesson.kind, lesson.id).empty(), "校验场景先真实扣费开工");
            Job job = *state.job;
            if (scenario == 0) state.npcs.front().alive = false;
            if (scenario == 1) state.npcs.front().location = "yunxi";
            if (scenario == 2) state.npcs.front().beast = true;
            if (scenario == 3) job.kind = "unsupported-kind";
            if (scenario == 4) job.plan = "missing-technique";
            if (scenario == 5) state.player.stage = 0;
            if (scenario == 6) state.npcs.front().stage = 0;
            completeLesson(state, job);
            check(std::find(state.player.arts.knownSpells.begin(), state.player.arts.knownSpells.end(), lesson.id) == state.player.arts.knownSpells.end(),
                  "失约或伪造安排不发学习成果：" + std::to_string(scenario));
            checkEq(state.player.stones, stones, "失约或非法授业退还已付灵石");
            checkEq(friendship::creditOf(state, teacher), credit, "失约或非法授业退还实际消耗的交情");
            const string refunded = saveToJson(state);
            job.remaining = 0; npcLearnComplete(state, job);
            checkStr(saveToJson(state), refunded, "重复失败结算不会再次退费");
        }
    }
    {
        Game state = learningGame(); const string teacher = state.npcs.front().id;
        const auto list = npcLessons(state, teacher);
        const auto selected = std::find_if(list.begin(), list.end(), [](const auto& lesson) {
            return lesson.kind == "manual" && lesson.enabled;
        });
        check(selected != list.end(), "真实NPC也提供可请教功法");
        if (selected != list.end()) {
            const auto lesson = *selected;
            const int stones = state.player.stones, credit = friendship::creditOf(state, teacher);
            check(npcLearnBegin(state, teacher, lesson.kind, lesson.id).empty(), "开始请教真实功法");
            Job job = *state.job; job.name.clear();
            completeLesson(state, job);
            checkEq(state.player.arts.manuals[lesson.id], 1, "完成授业解锁功法第一层");
            check(std::find(state.economy.books.begin(), state.economy.books.end(), lesson.id) != state.economy.books.end() &&
                  std::find(state.player.arts.bookList.begin(), state.player.arts.bookList.end(), lesson.id) != state.player.arts.bookList.end(),
                  "授业功法同步到后续修习书单");
            check(state.logs.back().text.find(lesson.name) != string::npos, "空工作名仍记录真实功法名，不越界截取");
            checkEq(state.player.stones, stones - lesson.stoneCost, "完成功法授业不重复扣灵石");
            checkEq(friendship::creditOf(state, teacher), credit - lesson.creditCost, "完成功法授业不重复扣交情");
            const string completed = saveToJson(state);
            job.remaining = 0; npcLearnComplete(state, job);
            checkStr(saveToJson(state), completed, "重复完成也不会重复授业经验或好感");
        }
    }
    {
        Game state = learningGame();
        for (const char* dao : ART_DAOS) {
            state.npcs.front().sect.clear(); state.npcs.front().dao = dao; state.npcs.front().stage = 12;
            state.npcs.front().lifespan = realmOf(12).life;
            state.player.stage = 12;
            const auto knowledge = npcLessons(state, state.npcs.front().id);
            int highest = 0;
            bool hasOwnSpell = false;
            for (const auto& spell : allSpells()) if (spell.dao == dao) {
                hasOwnSpell = true; highest = std::max(highest, spell.stage);
            }
            bool teachable = false;
            for (const auto& lesson : knowledge)
                if (lesson.kind == "spell" && (!hasOwnSpell || (lesson.dao == dao && lesson.stage == highest)) && lesson.id != "strike") teachable = true;
            check(teachable, string(dao) + (hasOwnSpell ? "道最高阶神通存在本系NPC教法" : "道传授其实际组合中的辅系神通"));
        }
    }
}
}
