#include "harness.h"
#include "core/arts.h"
#include "core/cultivate.h"
#include "core/json.h"
#include "core/interface/views.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/society.h"
#include <algorithm>
#include <limits>

namespace nztest {
namespace {
// 用途：构造不含运行服务或文件写入的修炼夹具；流程：单位缩放定向测试；用法：返回局部游戏；返回：固定种子的独立状态。
nz::Game mfCultivationGame() {
    nz::CreateOptions Options; Options.seed = 20261016;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear();
    State.player.apt = 90; State.player.insight = 37; State.player.traits.clear();
    State.player.arts.main.clear(); State.player.arts.aux.clear(); State.player.arts.manuals.clear();
    State.player.arts.nodes.clear(); State.player.arts.gear.clear(); State.player.arts.equipped.clear();
    State.player.arts.poison = 0; State.player.stage = 0; State.player.xp = 0;
    State.player.sect.clear(); State.player.health = 100; State.player.stones = 1000000;
    State.player.stoneSupply = State.player.qiSupply = 0; State.player.cave.clear();
    State.clock.carry.clear();
    return State;
}

// 用途：执行真实月度安排结算；流程：预览与日常修炼回归；用法：传局部游戏和计划；返回：本次实际修为变化。
int mfFinishCultivation(nz::Game& State, const std::string& Plan, int Days = 360) {
    nz::Job Work; Work.actionType = "plan"; Work.plan = Plan; Work.total = Days; Work.paid = true;
    const int Previous = State.player.xp;
    nz::monthComplete(State, Work);
    return State.player.xp - Previous;
}
} // namespace

// 用途：验证修为需求与正常修炼同步扩单位、丹药不扩药效；流程：成长平衡与旧档回归；用法：独立调用；返回：无，由测试框架记录。
void mfRunCultivationUnitTests() {
    using namespace nz;
    constexpr int OriginalNeeds[] = {110,190,320,900,1600,2800,3600,5400,8000,18000,30000,46000,0};
    for (int Stage = 0; Stage < 13; ++Stage) {
        checkEq(realmOf(Stage).need, OriginalNeeds[Stage] * 10, "各境界修为总量扩大十倍，化神仍无下阶");
    }
    auto State = mfCultivationGame();
    checkNear(cultivate::manualBase(State.player), 180, "无功法年度修炼基数由18变180");
    checkNear(cultivate::baseRate(State.player), 180, "资质悟性基准下真实基础年速扩大十倍");
    checkEq(mfFinishCultivation(State, "cultivate"), 180, "日常闭关真实结算扩大十倍");
    State = mfCultivationGame();
    const auto PureBefore = saveToJson(State);
    json::Value View(json::Object{}); api::view::appendPlayerState(State, View);
    checkEq(View["player"]["xpNeed"].asInt(), 1100, "人物预览需求与真实境界表一致");
    checkNear(View["player"]["cultivation"]["base"].asNumber(), 180, "人物修炼预览基础年速同步扩单位");
    checkStr(saveToJson(State), PureBefore, "修炼预览不改RNG、时钟或任何资源");
    checkEq(mfFinishCultivation(State, "cultivate", 2), 1, "按日修炼小数积累仍可达到新单位整数");
    for (const auto& [Plan, Fraction] : std::initializer_list<std::pair<const char*, double>>{
            {"gather",0.3},{"work",0.18},{"explore",0.4},{"rest",0.2}}) {
        State = mfCultivationGame();
        checkEq(mfFinishCultivation(State, Plan), jsFloor(180 * Fraction), std::string(Plan) + "零散正常修炼同步扩大");
    }
    State = mfCultivationGame();
    State.player.cave = State.player.location; State.player.caveTier = 2;
    State.player.stoneSupply = State.player.qiSupply = 3;
    const double Expected = 180 * 1.36 * 1.90 * 1.20;
    checkNear(cultivate::rate(State.player), Expected, "洞府、灵气和灵石倍率不重复扩大");
    checkNear(cultivate::breakdown(State.player).total, Expected, "修炼拆解预览和实际年速一致");
    checkEq(mfFinishCultivation(State, "cultivate"), jsFloor(Expected), "已付足配给时洞府真实结算与预览同源");
    checkEq(State.player.stones, 1000000 - 1080, "修为单位调整不提高灵石或灵气配给费用");
    State = mfCultivationGame();
    Npc Resting; Resting.id = "cultivation-resting-npc"; Resting.name = "修炼测试同道";
    Resting.age = 20; Resting.lifespan = 100; Resting.health = 30;
    Resting.stage = 0; Resting.location = State.player.location; Resting.xp = 0;
    // 此夹具只核对正常修炼单位；明确已有非丹药库存，避免真实年度补包/服丹混入收益。
    Resting.bag = {{"herb:恒心草", 1}};
    State.npcs = {Resting}; State.year = 1;
    worldYear(State);
    const auto Found = std::find_if(State.npcs.begin(), State.npcs.end(), [&](const Npc& Person) { return Person.id == Resting.id; });
    check(Found != State.npcs.end() && Found->xp == 40, "NPC真实年度休养的固定修炼量也扩大十倍");
    check(bondActionInfo(State, "teach").desc.find("+400") != string::npos, "NPC授业报价同步显示扩大后的400修为");
    for (int Stage : {0,3,6,9}) {
        State = mfCultivationGame(); State.year = 1;
        Resting.stage = Stage; Resting.health = 100; Resting.xp = 0; Resting.stones = 100000;
        Resting.apt = 90; Resting.personality = "潜心问道"; State.npcs = {Resting};
        worldYear(State);
        const auto Cultivating = std::find_if(State.npcs.begin(), State.npcs.end(), [&](const Npc& Person) { return Person.id == Resting.id; });
        const double Group[] = {1,1.38,3.2,4.5};
        check(Cultivating != State.npcs.end() && Cultivating->xp == jsRound(240 * Group[Stage / 3]), "NPC真实年度闭关在各境界同步扩大十倍");
    }
    State = mfCultivationGame();
    const auto Pill = std::find_if(data::RecipeDef_TABLE().begin(), data::RecipeDef_TABLE().end(),
        [](const data::RecipeDef& Entry) { return Entry.kind == "xp"; });
    check(Pill != data::RecipeDef_TABLE().end(), "存在可验证的修为丹方");
    if (Pill != data::RecipeDef_TABLE().end()) {
        State.player.arts.potions[Pill->id] = 1;
        check(artApplyPill(State.player, Pill->id).empty(), "真实服用修为丹成功");
        const int OriginalEffect = std::min(jsRound(jsSqrt(Pill->value) * 4), jsRound(110 * 0.25));
        checkEq(State.player.xp, OriginalEffect, "丹药原始药效与旧吸收上限均未乘十");
    }
    State = mfCultivationGame(); State.player.xp = 55;
    State.clock.carry["xp"] = 0.75;
    Resting.xp = 40; Resting.health = 100; State.npcs = {Resting};
    auto Legacy = json::Value::parse(saveToJson(State));
    checkEq(Legacy["cultivationUnitScale"].asInt(), 10, "现代存档明确记录修为单位，避免重复迁移");
    Legacy.asObject().erase("cultivationUnitScale");
    Game Loaded; std::string Error;
    check(loadDetachedFromJson(Legacy.dump(), Loaded, &Error), "缺标记旧档可迁移并保持世界纯载入：" + Error);
    checkEq(Loaded.player.xp, 557, "旧玩家整数与小数修炼进度一起保留比例");
    checkNear(Loaded.clock.carry["xp"], 0.5, "旧小数修为换算后的整数结入人物、余量仍可继续积累");
    check(!Loaded.npcs.empty() && Loaded.npcs.front().xp == 400, "旧NPC现有修为同步换算");
    Game Twice;
    check(loadDetachedFromJson(saveToJson(Loaded), Twice, &Error), "换算后的新存档可再次载入");
    checkEq(Twice.player.xp, Loaded.player.xp, "重新保存载入不二次放大玩家修为");
    checkNear(Twice.clock.carry["xp"], Loaded.clock.carry["xp"], "现代余数不二次换算");
    auto Terminal = json::Value::parse(saveToJson(Loaded));
    Terminal.asObject().erase("cultivationUnitScale");
    Terminal.asObject()["player"].set("stage", 12); Terminal.asObject()["player"].set("xp", 37);
    Terminal.asObject()["clock"].asObject()["carry"].set("xp", 0.75);
    check(loadDetachedFromJson(Terminal.dump(), Twice, &Error), "终境旧档没有下一阶需求也可载入");
    checkEq(Twice.player.xp, 37, "化神没有进度比例，合法历史修为不被清零");
    checkNear(Twice.clock.carry["xp"], 0.75, "终境旧余数记录保持原样");
    for (double InvalidCarry : {-1.0,1001.0,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto Invalid = Legacy;
        Invalid.asObject()["clock"].asObject()["carry"].set("xp", InvalidCarry);
        const std::string Before = saveToJson(Twice);
        check(!loadDetachedFromJson(Invalid.dump(), Twice, &Error), "非法修为余数在扩单位前拒绝");
        checkStr(saveToJson(Twice), Before, "非法余数拒绝保持当前游戏原样");
    }
    for (int InvalidScale : {0,2,11,std::numeric_limits<int>::max()}) {
        auto Invalid = json::Value::parse(saveToJson(Loaded)); Invalid.set("cultivationUnitScale", InvalidScale);
        const std::string Before = saveToJson(Twice);
        check(!loadDetachedFromJson(Invalid.dump(), Twice, &Error), "不支持的修为单位标记明确拒绝");
        checkStr(saveToJson(Twice), Before, "单位标记拒绝不污染当前游戏");
    }
}
} // namespace nztest
