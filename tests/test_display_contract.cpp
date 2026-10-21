#include "harness.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/calendar.h"
#include "core/roster.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
namespace {
using namespace nz;
json::Value displayRequest(const string& path, const string& id = "") {
    json::Value body(json::Object{}); if (!id.empty()) body.set("id", id);
    return json::Value::parse(api::handle({"POST", "/api/" + path, body.dump(), ""}).body);
}
json::Value pillIn(const json::Value& state, const string& id) {
    for (const auto& pill : state["pillCatalog"].asArray()) if (pill["id"].asString() == id) return pill;
    return {};
}
Game displayGame() {
    CreateOptions opts; opts.traitsExplicit = true; opts.seed = 442211;
    Game g = createGame(opts); g.pending.reset(); g.job.reset(); g.npcs.clear();
    return g;
}
}
void runDisplayContractTests() {
    using namespace nz;
    std::printf("\n--- 玩家与丹药权威展示契约 ---\n");
    api::Session session; session.started = true; session.g = displayGame(); api::bindSession(&session);
    auto& g = session.g;
    g.player.health = 61; g.player.arts.mind = 12; g.player.arts.speed = 9;
    g.player.arts.hpBonus = 7; g.player.arts.repel = 2;
    g.player.arts.BaseHealthBonus = 7;
    g.player.arts.combatPrep = {{"mana", 2}, {"shield", .2}, {"reduction", .12}};
    g.player.arts.combatBuff = .1;
    const auto before = saveToJson(g);
    const auto state = displayRequest("state")["state"];
    const auto repeated = displayRequest("state")["state"];
    checkStr(saveToJson(g), before, "重复读取完整展示不改变角色、背包、准备、世界或RNG");
    checkEq(state["pillCatalog"].asArray().size(), data::RecipeDef_TABLE().size(), "丹药目录覆盖全部丹方，不受拥有数量或坊市品阶限制");
    bool complete = true;
    for (const auto& pill : state["pillCatalog"].asArray())
        complete = complete && !pill["effect"].asString().empty() && !pill["usage"].asString().empty() &&
            !pill["baseEffect"].asString().empty() && !pill["enabled"].asBool() && !pill["reason"].asString().empty();
    check(complete, "123味药均有真实药效、限制、拥有条件与拒绝原因");
    checkStr(state.dump(), repeated.dump(), "同一状态的展示确定一致");
    const Battle ref = battleReferenceState(g.player);
    const auto combat = state["player"]["combatReference"];
    check(combat["reference"].asBool() && !combat["context"].asString().empty(), "斗法参考有明确上下文标签");
    checkEq(state["player"]["health"].asInt(), g.player.health, "日常展示当前绝对气血点数");
    checkEq(state["player"]["healthMax"].asInt(), mfPlayerHealthMaximum(g.player), "日常最大气血复用权威生命公式");
    checkStr(state["player"]["healthUnit"].asString(), "点", "日常气血单位为绝对点数");
    checkEq(combat["hp"].asInt(), ref.hp, "参考气血直接使用当前绝对气血");
    checkEq(combat["hpMax"].asInt(), ref.max, "参考气血上限使用真实开局公式");
    checkEq(combat["mana"].asInt(), ref.mana, "参考开局灵力计入丹药准备");
    checkEq(combat["manaRegen"].asInt(), battleEnergy(g.player), "自然回复和灵力上限分开");
    checkEq(combat["manaCap"].asInt(), battleManaCap(g.player), "参考灵力上限来自真实上限函数");
    checkNear(combat["reduction"].asNumber(), battleReduction(g.player, ref), "参考减伤包含准备和真实上限");
    check(state["arts"]["mind"].asInt() == 12 && state["arts"]["speed"].asInt() == 9 &&
          state["arts"]["hpBonus"].asInt() == 7 && state["arts"]["baseHealthBonus"].asInt() == 7 &&
          state["arts"]["foundationHealthBonus"].isNumber() && state["arts"]["foundationHealthBonus"].asInt() == 0,
          "神识、遁速、旧气血倍率、丹药和筑基基础气血均可查看");
    check(state["arts"]["repel"].isNull(), "退休避险状态不再展示为有效效果");
    for (const auto& market : state["market"].asArray()) if (market["kind"].asString() == "potion")
        checkStr(market["desc"].asString(), pillIn(state, market["id"].asString())["effect"].asString(), "坊市丹药描述共用权威药效");
    for (const auto& brew : state["brewOptions"].asArray())
        checkStr(brew["desc"].asString(), pillIn(state, brew["id"].asString())["effect"].asString(), "炼丹描述共用权威药效");

    // Near remaining caps and past former caps, preview and application share one plan.
    for (const auto& recipe : data::RecipeDef_TABLE()) {
        g = displayGame(); auto& p = g.player; auto& a = p.arts;
        p.health = 99; p.xp = realmOf(p.stage).need - 1; p.apt = APT_MAX - 1;
        p.insight = INSIGHT_MAX - 1; p.lifeBonus = 249; p.lifespan = 349;
        a.hpBonus = 199; a.BaseHealthBonus = 199;
        a.heart = a.mind = a.speed = 499; a.poison = 7;
        a.potions[recipe.id] = 1; a.exp[recipe.dao] = 29;
        p.traits = {"blessing"};
        if (recipe.kind == "breakthrough") p.stage = artPillUseInfo(p, recipe).requiredStage;
        const auto info = artPillUseInfo(p, recipe);
        const auto unchanged = saveToJson(g);
        (void)artPillUseInfo(p, recipe);
        checkStr(saveToJson(g), unchanged, "服药预览无副作用：" + recipe.name);
        const Player previous = p;
        const string error = artUsePill(g, recipe.id);
        checkStr(error, info.reason, "预览与服药资格一致：" + recipe.name);
        if (!error.empty()) { checkStr(saveToJson(g), unchanged, "拒绝服药不扣资源"); continue; }
        check(a.potions.at(recipe.id) == 0 && a.used.at(recipe.id) == 1 && a.poison == info.poisonAfter,
              "用量、服用计数和丹毒与预览一致：" + recipe.name);
        int delta = 0; bool numeric = true;
        if (recipe.kind == "heal") delta = p.health - previous.health;
        else if (recipe.kind == "xp") delta = p.xp - previous.xp;
        else if (recipe.kind == "apt") delta = p.apt - previous.apt;
        else if (recipe.kind == "insight") delta = p.insight - previous.insight;
        else if (recipe.kind == "life") delta = p.lifespan - previous.lifespan;
        else if (recipe.kind == "body") {
            checkEq(a.BaseHealthBonus - previous.arts.BaseHealthBonus, recipe.value,
                    "体魄丹按标称值增加基础气血且可越过旧累计200限制：" + recipe.name);
            checkEq(a.hpBonus, previous.arts.hpBonus, "新体魄丹保留旧倍率字段：" + recipe.name);
            delta = mfPlayerHealthMaximum(p) - mfPlayerHealthMaximum(previous);
        }
        else if (recipe.kind == "heart") delta = a.heart - previous.arts.heart;
        else if (recipe.kind == "mind") delta = a.mind - previous.arts.mind;
        else if (recipe.kind == "speed") delta = a.speed - previous.arts.speed;
        else if (recipe.kind == "points") delta = a.points - previous.arts.points;
        else if (recipe.kind == "dao") delta = a.exp.at(recipe.dao) - previous.arts.exp.at(recipe.dao);
        else numeric = false;
        if (numeric) checkNear(info.effectValue, delta, "封顶与天赋后的实际变化量一致：" + recipe.name);
    }
    for (int blocker = 0; blocker < 8; ++blocker) {
        g = displayGame(); g.player.arts.potions["5001"] = 1;
        if (blocker == 0) g.player.arts.potions.clear();
        if (blocker == 1) g.player.arts.poison = 80;
        if (blocker == 2) g.player.arts.used["5001"] = findRecipe("5001")->tolerance;
        if (blocker == 3) g.job = Job{};
        if (blocker == 4) g.pending = Pending{};
        if (blocker == 5) g.player.arts.battle = Battle{};
        if (blocker == 6) g.ended = true;
        if (blocker == 7) g.dungeons.run = DungeonRun{};
        const auto snapshot = saveToJson(g);
        const auto view = pillIn(displayRequest("state")["state"], "5001");
        const auto result = displayRequest("use-pill", "5001");
        check(!view["enabled"].asBool() && !result["ok"].asBool(), "缺货、耐药、丹毒和全局阻塞正确禁止服用");
        checkStr(view["reason"].asString(), result["error"].asString(), "预览拒绝原因与真实API完全一致");
        checkStr(saveToJson(g), snapshot, "拒绝与预览不修改状态");
    }
    g = displayGame(); g.player.stage = g.player.highestStage = 5; g.player.xp = realmOf(5).need;
    g.player.lifespan = realmOf(5).life + g.player.lifeBonus;
    g.player.stones = 10000; g.player.pills = 2; g.player.cores = 3; g.player.arts.breakBuff = "stage";
    g.player.arts.BreakthroughPillStage = 5;
    const auto breakthrough = displayRequest("state")["state"]["breakthrough"];
    check(breakthrough["enabled"].asBool() && breakthrough["days"].asInt() == 90 &&
        breakthrough["pillsConsumed"].asInt() == 1 && breakthrough["coresConsumed"].asInt() == 1 &&
        breakthrough["breakBuffConsumed"].asBool(), "突破报价明确90天和实际消耗准备");
    for (int blocker = 0; blocker < 4; ++blocker) {
        g.player.xp = realmOf(5).need; g.player.stones = 10000; g.player.health = 100; g.job.reset();
        if (blocker == 0) g.player.xp = 0;
        if (blocker == 1) g.player.stones = 0;
        if (blocker == 2) g.player.health = HealthGateMinimum - 1;
        if (blocker == 3) g.job = Job{};
        const auto quoted = displayRequest("state")["state"]["breakthrough"];
        const auto action = displayRequest("breakthrough");
        check(!quoted["enabled"].asBool(), "突破阻塞可见");
        checkStr(quoted["reason"].asString(), action["error"].asString(), "突破拒绝文案与API一致");
    }
    // Live equipment and furnace quotes must allow all formerly available
    // actions and predict the same prices/errors as their actual API routes.
    const auto gearState = [](const json::Value& view) {
        for (const auto& item : view["gears"].asArray()) if (item["id"].asString() == "quote-gear") return item;
        return json::Value();
    };
    for (const string action : {"equip", "unequip", "inscribe", "salvage", "sell", "repair"}) {
        for (int scenario = 0; scenario < 7; ++scenario) {
            g = displayGame(); auto& p = g.player;
            p.stones = 1000; p.arts.nodes = {"11-1"}; p.arts.durability = 22;
            Gear gear; gear.id = "quote-gear"; gear.name = "报价法器"; gear.slot = "weapon";
            gear.shape = "sword"; gear.grade = 1; gear.bonus = .1; gear.craftCost = 100;
            p.arts.gear.push_back(gear); g.economy.marketCash = 10000;
            if (action == "unequip" || scenario == 1) p.arts.equipped[gear.slot] = gear.id;
            if (scenario == 2) p.stones = 0;
            if (scenario == 3) p.arts.nodes.clear();
            if (scenario == 4) g.job = Job{};
            if (scenario == 5) { p.arts.gear[0].inscribed = true; p.arts.durability = 40; }
            if (scenario == 6) { p.location = "fanghu"; g.economy.marketCash = 0; }
            const int startingStones = p.stones;
            const string snapshot = saveToJson(g);
            const auto view = displayRequest("state")["state"];
            const auto quote = action == "repair" ? view["furnaceRepair"] : gearState(view)["actions"][action];
            checkStr(saveToJson(g), snapshot, "装备和修炉报价不改资源、市场或RNG");
            json::Value body(json::Object{}); body.set("id", gear.id); body.set("kind", "gear");
            const string route = action == "equip" ? "equip" : action == "sell" ? "sell" :
                action == "repair" ? "furnace/repair" : "gear/" + action;
            const auto response = json::Value::parse(api::handle({"POST", "/api/" + route, body.dump(), ""}).body);
            check(quote["enabled"].asBool() == response["ok"].asBool(), "装备和修炉可用条件与实际动作一致：" + action);
            if (!response["ok"].asBool())
                checkStr(quote["reason"].asString(), response["error"].asString(), "装备和修炉禁用原因与API相同");
            else checkEq(p.stones - startingStones, quote["returnStones"].asInt() - quote["costStones"].asInt(),
                "装备和修炉实际收支与报价相同");
        }
    }
    g = displayGame(); g.player.arts.poison = 60;
    const auto poison = play::poisonInfo(g);
    check(poison.summary.find("×0.75") != string::npos, "丹毒效率小数不会错误截成零");
    checkNear(poison.detoxPerYear, 5, "自然化毒展示与普通角色每年5点结算一致");
    checkEq(poison.daysToClear, 60 * 360 / 5, "化毒预计天数使用真实年度5点速率");
    g.player.arts.nodes.push_back("10-2"); g.player.arts.poison = 2;
    g.clock.carry["detox"] = 0.75;
    const auto faster = play::poisonInfo(g);
    checkNear(faster.detoxPerYear, calendarDetoxPerYear(g.player), "丹道二层化毒展示复用真实速率");
    checkEq(faster.daysToClear, 90, "清空时间计入已累计的化毒零头");
    Game detox = g;
    const int days = faster.daysToClear;
    int cleared = 0;
    for (int day = 1; day <= days; ++day) {
        cleared += calendarTake(detox, "detox", calendarDetoxPerYear(detox.player) / 360.0);
        if (day == days - 1) check(cleared < 2, "预计清空前一天尚未累计足够化毒量");
    }
    checkEq(cleared, 2, "预计清空日与实际每日小数结算一致");
    api::bindSession(nullptr);
}
}
