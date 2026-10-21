#include "harness.h"
#include "core/api.h"
#include "core/artifact.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <set>

namespace nztest {
using namespace nz;
namespace {
Game fresh() {
    CreateOptions opts; opts.seed = 121023;
    Game g = createGame(opts); g.pending.reset(); g.job.reset(); g.npcs.clear();
    return g;
}
json::Value request(const string& route, const json::Value& body = json::Object{}) {
    return json::Value::parse(api::handle({"POST", "/api/" + route, body.dump(), ""}).body);
}
json::Value findItem(const json::Value& list, const string& id) {
    for (const auto& item : list.asArray()) if (item["id"].asString() == id) return item;
    return {};
}
bool sameCoreNumbers(const Preview& a, const Preview& b) {
    return a.valid == b.valid && a.cost == b.cost && a.damage == b.damage &&
        a.shield == b.shield && a.heal == b.heal && a.energy == b.energy && a.qi == b.qi;
}
Gear sword() {
    Gear gear; gear.id = "numeric-sword"; gear.name = "锻剑铁剑";
    gear.slot = "weapon"; gear.shape = "sword"; gear.element = "金";
    gear.polarity = "天阳"; gear.grade = 1; gear.bonus = 0.12; gear.crafted = true;
    return gear;
}

// 用途：按稳定ID核对接口下发的知识集合；流程：已学神通与重复抄本回归；用法：传字符串数组；返回：去重后的集合，数组长度另验重复。
std::set<string> mfKnowledgeIds(const json::Value& Entries) {
    std::set<string> Result;
    for (const auto& Entry : Entries.asArray()) Result.insert(Entry.asString());
    return Result;
}

// 用途：隔离数值预览的API会话并明确标记已开局；流程：目录与装备视图回归；用法：创建局部对象；返回：无，析构解除绑定，不替换默认玩家。
struct NumericPreviewSession {
    api::Session State;

    NumericPreviewSession() {
        State.g = fresh();
        State.started = true;
        api::bindSession(&State);
    }

    ~NumericPreviewSession() { api::bindSession(nullptr); }
};
}
void runNumericPreviewTests() {
    std::printf("\n--- 非战斗神通与炼器数值 ---\n");
    Game g = fresh(); g.player.arts.main = "hidden"; g.player.arts.manuals["hidden"] = 3;
    g.player.arts.nodes = {"7-2", "9-1", "3-2"};
    g.player.arts.combatPrep = {{"mana", 2}, {"qi", 1}, {"shield", 0.05}};
    g.player.arts.combatBuff = 0.1;
    const string before = saveToJson(g);
    for (int stage : {0, 3, 6, 9, 12}) {
        g.player.stage = stage;
        const string unchanged = saveToJson(g);
        for (const auto& sp : allSpells()) {
            const auto reference = battleReferencePreview(g.player, sp);
            check(reference.valid && !reference.text.empty(), "所有境界的目录参考效果可查看：" + sp.id);
            if (stage >= sp.stage && sp.kind != "redraw")
                check(sameCoreNumbers(reference, battlePreview(g.player, battleReferenceState(g.player), sp.id)),
                      "非战斗与战斗共用结算值：" + sp.id);
        }
        checkStr(saveToJson(g), unchanged, "多次目录试算不改变角色、世界、RNG或开局准备");
    }
    g = fresh();
    const auto healing = battleReferencePreview(g.player, *battleSpell("wood"));
    check(healing.heal == 0 && healing.healPotential > 0 && healing.text.find("单次治疗量") != string::npos,
          "满血仍显示实际治疗能力并区分当前可回复量");
    const auto burning = battleReferencePreview(g.player, *battleSpell("fire"));
    check(burning.damage > 0 && burning.statuses.size() == 1 && burning.statuses[0].id == "burn" &&
        burning.statuses[0].amount > 0 && burning.statuses[0].turns == 3 && burning.statuses[0].perTurn > 0,
        "焰火诀显示直接伤害、叠加灼烧量、每回合伤害与三回合持续");
    const auto poison = battleReferencePreview(g.player, *battleSpell("corrode"));
    check(poison.statuses.size() == 1 && poison.statuses[0].id == "poison" &&
        poison.statuses[0].turns == 1 && poison.statuses[0].perTurn > 0 && poison.statuses[0].cap == BATTLE_POISON_CAP &&
        poison.text.find("本场持续") != string::npos,
        "施毒显示真实层数、伤害、本场持续及上限");
    const auto redraw = battleReferencePreview(g.player, *battleSpell("refresh"));
    check(redraw.draw >= 5 && battlePreviewConditions(g.player, *battleSpell("refresh")).find("4张") != string::npos,
          "换念预览明确以其余四张手牌为参考，不假定真实抽牌结果");
    check(battlePreviewConditions(g.player, *battleSpell("fireburst")).find("剩余回合") != string::npos,
          "引爆预览提示依赖已有灼烧的条件公式");

    g.player.arts.combatPrep = {{"mana", 2}, {"qi", 1}, {"shield", 0.05}};
    g.player.arts.nodes = {"7-2", "9-1"};
    const Battle opening = battleReferenceState(g.player);
    EnemySpec foe; foe.name = "参考目标"; foe.speed = 1;
    battleStart(g, foe);
    check(g.player.arts.battle && opening.max == g.player.arts.battle->max &&
        opening.hp == g.player.arts.battle->hp && opening.mana == g.player.arts.battle->mana &&
        opening.swordQi == g.player.arts.battle->swordQi && opening.shield == g.player.arts.battle->shield,
        "参考环境与真实开局共用气血、灵力、剑气、护盾及准备状态");

    NumericPreviewSession PreviewSession;
    auto& current = PreviewSession.State.g;
    current.player.arts.spells = {"sword"}; current.player.arts.knownSpells = {"sword", "wood"};
    current.player.arts.bookList = {"wheel"}; current.economy.books = {"wheel"};
    const string queryBefore = saveToJson(current);
    const auto catalog = request("arts/spells")["spells"];
    const auto unknown = findItem(catalog, "gold"), known = findItem(catalog, "wood"), equipped = findItem(catalog, "sword");
    check(!unknown["learned"].asBool() && unknown["unlocked"].asBool() && !unknown["enabled"].asBool() &&
        !unknown["canEquip"].asBool(), "未学习和境界已满足分离，不能因为够境界亮成已学");
    check(known["learned"].asBool() && !known["equipped"].asBool() && known["canEquip"].asBool(),
          "已学未携带明确可装备");
    check(equipped["learned"].asBool() && equipped["equipped"].asBool() && equipped["canUnequip"].asBool(),
          "已携带神通明确可卸下");
    check(findItem(catalog, "rain")["preview"]["valid"].asBool() && !findItem(catalog, "rain")["unlocked"].asBool(),
          "境界锁定的神通可阅读参考值但仍不能携带");
    check(unknown["preview"]["reference"].asBool() && unknown["preview"]["damage"].asInt() > 0 &&
          !unknown["preview"]["context"].asString().empty(), "目录 API 给出带参考环境的实际数值");
    const auto state = request("state")["state"];
    check(state["manualCatalog"].asArray().size() == mfManualCatalog().size() &&
          std::all_of(mfManualCatalog().begin(), mfManualCatalog().end(), [&](const auto& Entry) {
              const auto Item = findItem(state["manualCatalog"], Entry.first);
              return !Entry.second.name.empty() && Item["name"].asString() == Entry.second.name;
          }) && findItem(state["manualCatalog"], "gold")["name"].asString() == "金虹诀",
          "全部流派功法都有中文目录，不再以英文 ID 代替真名");
    const std::set<string> ExpectedKnowledge(current.player.arts.knownSpells.begin(), current.player.arts.knownSpells.end());
    std::set<string> ExpectedBooks(current.player.arts.bookList.begin(), current.player.arts.bookList.end());
    ExpectedBooks.insert(current.economy.books.begin(), current.economy.books.end());
    check(mfKnowledgeIds(state["arts"]["knownSpells"]) == ExpectedKnowledge &&
          state["arts"]["knownSpells"].asArray().size() == ExpectedKnowledge.size() &&
          mfKnowledgeIds(state["arts"]["bookList"]) == ExpectedBooks &&
          state["arts"]["bookList"].asArray().size() == ExpectedBooks.size(),
          "状态下发已学神通和已得抄本，重复库存条目不重复显示");
    checkStr(saveToJson(current), queryBefore, "全量目录状态查询不改变存档或 RNG");

    json::Value input(json::Object{});
    input.set("main", "iron"); input.set("aux", "jade"); input.set("core", "wood"); input.set("shape", "sword");
    const auto forge = request("forge-preview", input)["info"];
    check(forge["ability"]["preview"]["damage"].asInt() > 0 && forge["passive"]["primaryValue"].asNumber() > 0 &&
        forge["passive"]["delta"].isObject(), "炼器预览显示装备后的真实主动伤害和被动属性");
    check(forge["inscribedPassive"]["primaryValue"].asNumber() > forge["passive"]["primaryValue"].asNumber() &&
        forge["inscribedAbility"]["preview"]["damage"].asInt() >= forge["ability"]["preview"]["damage"].asInt() &&
        forge["inscribeCost"].asInt() == 30 * forge["grade"].asInt(), "铭刻前后属性与主动威力对照来自真实加成，费用严格30乘品阶");
    checkStr(saveToJson(current), queryBefore, "炼器及铭刻预览不实际装备、造器、付费或消耗随机数");

    const Gear PreviewSword = sword();
    current.player.arts.gear.push_back(PreviewSword); current.player.arts.equipped["weapon"] = PreviewSword.id;
    const auto GearState = findItem(request("state")["state"]["gears"], PreviewSword.id);
    const auto ability = artifactAbility(PreviewSword);
    const auto real = battlePreview(current.player, battleReferenceState(current.player), ability.spell.id);
    check(GearState["id"].asString() == PreviewSword.id && ability.valid && real.valid && real.damage > 0 &&
          GearState["ability"]["preview"]["damage"].asInt() == real.damage,
          "拥有法器的非战斗参考伤害与装备后的真实战斗试算一致");
    current.player.sect = "jinhong"; current.player.location = "jinhong";
    current.player.traits = {"blessing"}; current.player.arts.exp["金"] = 28;
    const string taskBefore = saveToJson(current);
    const auto blessedTask = findItem(request("eco-options")["options"], "sect-patrol");
    check(blessedTask["daoExp"].asInt() == 5 && blessedTask["daoPoints"].asInt() == 1,
          "宗门差事预览包含福缘天赋倍率与跨悟道阈值的真实收益");
    checkStr(saveToJson(current), taskBefore, "查看任务奖励不提前增加经验、悟道点或改变 RNG");
    current.player.arts.exp["金"] = 9999;
    const auto cappedTask = findItem(request("eco-options")["options"], "sect-patrol");
    checkEq(cappedTask["daoExp"].asInt(), 1, "接近经验封顶时任务板只显示仍可增加的一点经验");
    (void)before;
}
} // namespace nztest
