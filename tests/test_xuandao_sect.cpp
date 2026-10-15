#include "harness.h"
#include <algorithm>
#include <set>
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/content/advanced_builds.h"
#include "core/content/game_ids.h"
#include "core/content/npc_combat_profiles.h"
#include "core/interface/views.h"
#include "core/location_services.h"
#include "core/npc_learning.h"
#include "core/save.h"
#include "core/sect_trial.h"
#include "core/shop.h"
#include "core/sim.h"

namespace nztest {
using namespace nz;
namespace {
// 用途：生成独立授业测试角色；流程：传承请求和真实完成；返回：只属于测试的合法游戏。
Game mfTransmissionGame() {
    CreateOptions Options; Options.name = "玄道传承回归"; Options.seed = 20261015;
    Game State = createGame(Options);
    State.pending.reset(); State.job.reset(); State.player.stage = State.player.highestStage = 9;
    State.player.lifespan = realmOf(9).life + State.player.lifeBonus;
    State.player.stones = 10000;
    State.player.sect = State.player.location = "xuandao";
    State.player.arts.knownSpells.clear(); State.player.arts.spells.clear();
    State.player.arts.manuals.clear();
    State.economy.contribution = 10000;
    return State;
}
// 用途：查测试中的玄道传承者；流程：授业夹具；返回：已有活跃人物指针或 null，不生成 NPC。
Npc* mfXuandaoTeacher(Game& State) {
    for (auto& Person : State.npcs)
        if (Person.alive && Person.sect == "xuandao" && content::isChampion(Person)) return &Person;
    return nullptr;
}
}

// 用途：覆盖第六宗注册、常态完整传承、请教真实结算和只读指引；返回：测试断言，无玩家文件或服务访问。
void mfRunXuandaoSectTests() {
    checkEq(SECTS().size(), 6, "六宗包含玄道宗");
    check(content::parseSectId("xuandao") == content::SectId::Xuandao &&
        content::sectDao(content::SectId::Xuandao) == content::DaoId::Array, "玄道宗有独立稳定 ID 与阵道主传承");
    check(findLoc("xuandao") && findSect("xuandao") && findSectTrial("xuandao"), "玄道宗地图、入门和宗门入口一致");
    checkEq(content::mfAdvancedBuilds().size(), 8, "八种初始流派各有一个进阶建议");
    check(content::mfFindAdvancedBuild("five-elements") && !content::mfFindAdvancedBuild("unknown"), "进阶目录拒绝未知ID");
    Game State = mfTransmissionGame();
    std::set<string> SectTeachers;
    for (const auto& Person : State.npcs) if (Person.alive && content::isChampion(Person)) SectTeachers.insert(Person.sect);
    checkEq(SectTeachers.size(), 6, "没有兽潮的常态新局也有六宗完整传承者");
    const auto* Teacher = mfXuandaoTeacher(State);
    check(Teacher != nullptr, "可找到玄道完整传承弟子");
    if (!Teacher) return;
    const string TeacherId = Teacher->id;
    State.player.location = Teacher->location;
    State.player.relations[TeacherId] = 100;
    State.player.npcBonds[TeacherId].credit = 10000;
    const auto BeforeQueries = saveToJson(State);
    const auto Lessons = npcLessons(State, TeacherId);
    const auto View = api::view::state(State);
    checkEq(View["advancedBuilds"].asArray().size(), 8, "状态提供进阶目录给桌面和测试页");
    checkEq(View["championTeachers"].asArray().size(), 6, "状态在非兽潮时提供全州传承者位置");
    bool HasFiveSwordName = false;
    for (const auto& PersonView : View["nearby"].asArray()) {
        if (PersonView["id"].asString() != TeacherId) continue;
        for (const auto& Component : PersonView["transmission"]["components"].asArray())
            if (Component["kind"].asString() == "manual" && Component["id"].asString() == "fiveSword")
                HasFiveSwordName = Component["name"].asString() == "五行剑诀" &&
                    Component["stage"].asInt() == 0 && Component["stageName"].asString() == realmName(0);
    }
    check(HasFiveSwordName, "完整传承目录纯编码新功法中文名及境界，不依赖可请教资格");
    checkStr(saveToJson(State), BeforeQueries, "学习与进阶查询不推进时间、RNG或修补人物");
    const auto* Profile = content::findNpcCombatProfile(*Teacher);
    for (const auto& ManualId : Profile->manuals)
        check(std::any_of(Lessons.begin(), Lessons.end(), [&](const NpcLesson& Lesson) {
            return Lesson.kind == "manual" && Lesson.id == ManualId;
        }), "完整传承者知识不受当前辅修槽位裁剪：" + ManualId);
    for (const auto& SpellId : Profile->spells)
        check(std::any_of(Lessons.begin(), Lessons.end(), [&](const NpcLesson& Lesson) {
            return Lesson.kind == "spell" && Lesson.id == SpellId;
        }), "完整传承者可逐项教授核心神通：" + SpellId);
    State.player.location = State.player.sect = "xuandao";
    const auto Services = locationServices(State, "xuandao");
    check(std::any_of(Services.begin(), Services.end(), [](const LocationService& Service) { return Service.id == "sect"; }), "地图玄道宗可直接打开差事与藏经阁");
    checkStr(shop::manualSource(State, "fiveSword").sect, "xuandao", "五行剑功法只属于玄道宗藏经阁");
    for (const char* SpellId : {"metalSword", "waterSword", "woodSword", "fireSword", "earthSword"})
        check(shop::librarySpellSource(State, SpellId).valid, string("玄道宗允许兑换五行剑：") + SpellId);
    auto OtherSect = State; OtherSect.player.sect = OtherSect.player.location = "jinhong";
    check(!shop::librarySpellSource(OtherSect, "metalSword").valid, "五行剑不因为金道分类泄露到金虹藏经阁");
    const auto InvalidBefore = saveToJson(OtherSect);
    check(!shop::libraryExchange(OtherSect, "fiveSword").ok && saveToJson(OtherSect) == InvalidBefore,
        "越宗换五行剑功法拒绝且不扣贡献");
    State.player.location = findLoc(Teacher->location) ? Teacher->location : "xuandao";
    for (const auto& Request : {std::pair<string, string>{"manual", "fiveSword"}, {"spell", "metalSword"}}) {
        Game LessonState = State;
        check(npcLearnBegin(LessonState, TeacherId, Request.first, Request.second).empty(), "完整传承仍通过真实逐项授业请求：" + Request.second);
        if (!LessonState.job) continue;
        const int PaidStones = LessonState.player.stones;
        const auto PendingBefore = saveToJson(LessonState);
        check(!npcLearnBegin(LessonState, TeacherId, Request.first, Request.second).empty() && saveToJson(LessonState) == PendingBefore,
            "重复授业请求不多扣费用");
        calendarRun(LessonState, LessonState.job->remaining);
        checkEq(LessonState.player.stones, PaidStones, "授业完成不再次扣费");
        if (Request.first == "manual") checkEq(LessonState.player.arts.manuals.at("fiveSword"), 1, "完整传承功法仍从第一层学习");
        else check(std::find(LessonState.player.arts.knownSpells.begin(), LessonState.player.arts.knownSpells.end(), "metalSword") != LessonState.player.arts.knownSpells.end(), "完成授业真实领悟金灵剑");
    }
    Game Migrated = State; Migrated.world.sects.erase("xuandao");
    Migrated.npcs.erase(std::remove_if(Migrated.npcs.begin(), Migrated.npcs.end(), [](const Npc& Person) { return Person.sect == "xuandao"; }), Migrated.npcs.end());
    // 旧档不应残留已删除老师的玩家引用。
    Migrated.player.npcBonds.erase(TeacherId); Migrated.player.relations.erase(TeacherId);
    Game Loaded; string Error;
    check(loadFromJson(saveToJson(Migrated), Loaded, &Error), "旧世界缺第六宗可在读档初始化补齐：" + Error);
    check(Loaded.world.sects.contains("xuandao") && mfXuandaoTeacher(Loaded), "旧档获得常态玄道宗老师，不依赖开潮");
    const auto RoundTrip = saveToJson(Loaded); Game Reloaded;
    check(loadFromJson(RoundTrip, Reloaded, &Error) && saveToJson(Reloaded) == RoundTrip, "完整传承ID与第六宗账簿往返保持一致");
}
} // namespace nztest
