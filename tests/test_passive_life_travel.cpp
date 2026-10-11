#include "harness.h"

#include <algorithm>
#include <set>

#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/travel.h"

using namespace nz;

namespace nztest {
namespace {
Game lifeGame() {
    CreateOptions options; options.name = "被动组合回归"; options.seed = 734;
    Game g = createGame(options);
    g.npcs.clear(); g.job.reset(); g.pending.reset();
    g.player.location = "wuling"; g.player.stones = 5000;
    g.player.alchemy = 500;
    return g;
}

void rejectLifeSave(const json::Value& invalid, const string& label) {
    Game current = lifeGame(); current.player.name = "保留的当前生活角色";
    current.player.stones = 321;
    const string before = saveToJson(current);
    string error;
    check(!loadFromJson(invalid.dump(), current, &error) && !error.empty(), label + "拒绝读取并说明原因");
    checkStr(saveToJson(current), before, label + "不替换当前角色或状态");
}
}

void runPassiveLifeTravelTests() {
    std::printf("\n-- 生活被动：成丹、铸造时间与来源售价 --\n");
    {
        Player p;
        p.arts.main = "pill"; p.arts.manuals = {{"pill", 5}};
        checkEq(artBrewInfo(p, "5001").output, 2, "丹火诀主修满层实际每炉产出两枚");
        p.arts.main.clear(); p.arts.aux = {"pill"};
        checkEq(artBrewInfo(p, "5001").output, 1, "丹火诀辅修半效未到整数增产阈值");
        p.arts.nodes = {"10-5"};
        checkEq(artBrewInfo(p, "5001").output, 2, "丹道大成独立增加一枚成功产出");
        p.arts.main = "pill"; p.arts.aux.clear();
        checkEq(artBrewInfo(p, "5001").output, 3, "丹道大成与丹火诀主修组合可叠加增产");
        ManualInfo info;
        check(findManualInfo("pill", info) && info.effect.find("额外成丹") != string::npos,
              "功法详情显示实际生活触发而非仅成丹概率");
    }
    {
        Game g = lifeGame();
        g.player.arts.main = "pill"; g.player.arts.manuals["pill"] = 5;
        g.player.arts.nodes = {"10-5"}; g.player.arts.exp["丹"] = 2000;
        const auto* recipe = findRecipe("5001");
        for (const auto& [name, count] : parseIngredients(recipe->ingredients))
            g.player.arts.herbs[name] = count * 3;
        g.player.herbs = artBrewInfo(g.player, "5001").count * 3;
        g.player.arts.potions.clear();
        const auto quote = ecoActionInfo(g, "brew", "5001", 3);
        checkEq(quote.output, 3, "批量开炉报价明确每炉三枚");
        const int herbs = g.player.herbs, stones = g.player.stones;
        ecoBegin(g, "brew", "5001", 3);
        check(g.job && g.job->craftOutput == 3, "开炉冻结产量进入任务");
        if (g.job) {
            checkEq(g.player.herbs, 0, "增产不额外消耗已约定的三炉材料");
            checkEq(g.player.stones, stones - quote.cost, "批量开炉只扣原材料费用");
            g.job->chance = 1;
            g.player.arts.main = "tuna"; g.player.arts.nodes.clear();
            Game loaded; string error;
            check(loadFromJson(saveToJson(g), loaded, &error), "冻结的增产任务可保存读取");
            if (loaded.job) {
                checkEq(loaded.job->craftOutput, 3, "读档保留开工时每炉产出");
                while (loaded.job) calendarRun(loaded, 30);
                checkEq(loaded.player.arts.potions["5001"], 9, "移除被动后完成仍按三炉九枚结算");
                checkEq(loaded.player.arts.progress.brew, 3, "炼丹成就仍按成功炉数计数");
                checkEq(loaded.player.herbs, herbs - quote.batch * artBrewInfo(g.player, "5001").count,
                        "完成时不重复扣材料");
            }
        }
    }
    {
        Player p;
        const int oldForge = ecoCraftDays(p, "forge", 3);
        const int oldBrew = ecoCraftDays(p, "brew", 3);
        p.arts.nodes = {"11-3"};
        check(ecoCraftDays(p, "forge", 3) < oldForge, "器道贯通真实缩短铸造耗时");
        checkEq(ecoCraftDays(p, "brew", 3), oldBrew, "器道增速不误缩短炼丹耗时");
        p.arts.main = "forge"; p.arts.manuals = {{"forge", 5}};
        const ArtMods full = artModsOf(p.arts);
        p.arts.main.clear(); p.arts.aux = {"forge"};
        const ArtMods half = artModsOf(p.arts);
        checkNear(full.forgeSpeed - 0.3, (half.forgeSpeed - 0.3) * 2,
                  "百炼真解生活减时遵守主满辅半");
        check(ecoCraftDays(p, "forge", 3) >= 2, "炼器叠加减时保留最短实际工作天数");

        Game g = lifeGame();
        Gear made; made.id = "forge-sale"; made.grade = 2; made.craftCost = 100; made.crafted = true;
        Gear bought = made; bought.id = "market-sale"; bought.crafted = false;
        g.player.arts.gear = {made, bought};
        const int baseMade = ecoSalePrice(g, "gear", made.id);
        const int baseBought = ecoSalePrice(g, "gear", bought.id);
        g.player.arts.main = "forge"; g.player.arts.manuals["forge"] = 5;
        g.player.arts.nodes = {"11-4", "11-5"};
        checkNear(artModsOf(g.player.arts).craftedSale, 0.5, "器道与百炼售值组合有50%明确上限");
        check(ecoSalePrice(g, "gear", made.id) > baseMade, "真正自炼法宝售价提高");
        checkEq(ecoSalePrice(g, "gear", bought.id), baseBought,
                "购入与NPC赠送法器即使有craftCost也不享自炼售值");
        ecoBegin(g, "forge", "iron|jade|wood|sword");
        check(g.job.has_value(), "自炼来源测试实际开工");
        if (g.job) {
            g.job->chance = 1;
            while (g.job) calendarRun(g, 30);
            check(g.player.arts.gear.back().crafted && g.player.arts.gear.back().id.rfind("forge-", 0) == 0,
                  "实际成功铸造法器标记自炼来源");
        }
    }
    std::printf("\n-- 制造与旅行存档：单字段损坏拒绝 --\n");
    {
        Game g = lifeGame();
        const auto* recipe = findRecipe("5001");
        g.player.arts.herbs.clear(); g.player.herbs = 0;
        for (const auto& [name, count] : parseIngredients(recipe->ingredients)) {
            g.player.arts.herbs[name] = count;
            g.player.herbs += count;
        }
        ecoBegin(g, "brew", "5001");
        check(g.job && g.job->actionType == "brew", "产出损坏回归从真实扣料开炉开始");
        if (g.job) {
            const auto valid = json::Value::parse(saveToJson(g));
            Game restored; string error;
            check(loadFromJson(valid.dump(), restored, &error), "真实开炉在途存档可正常读取");
            auto badOutput = valid;
            badOutput.asObject()["job"].set("craftOutput", json::Value(5));
            rejectLifeSave(badOutput, "超过四枚上限的炼丹产出");
        }
    }
    {
        Game g = lifeGame();
        check(travelBegin(g, "qingshi", "boat").empty() && g.job,
              "赶路损坏回归从真实付费行程开始");
        if (g.job) {
            const auto valid = json::Value::parse(saveToJson(g));
            Game restored; string error;
            check(loadFromJson(valid.dump(), restored, &error), "真实未推进行程存档可正常读取");
            auto futureCheck = valid;
            futureCheck.asObject()["job"].set("travelChecks", json::Value(1));
            rejectLifeSave(futureCheck, "超过实际进度的旅途检查天数");
            auto multipleAmbushes = valid;
            multipleAmbushes.asObject()["job"].set("travelAmbushes", json::Value(2));
            rejectLifeSave(multipleAmbushes, "超过单段上限的截杀记录");
        }
    }
    {
        Game g = lifeGame();
        ecoBegin(g, "forge", "iron|jade|wood|sword");
        check(g.job && g.job->actionType == "forge", "来源损坏回归从真实炼器开工开始");
        if (g.job) {
            g.job->chance = 1;
            while (g.job) calendarRun(g, 30);
            check(!g.player.arts.gear.empty() && g.player.arts.gear.back().crafted,
                  "来源损坏回归实际完成并生成自炼法器");
            if (!g.player.arts.gear.empty()) {
                const auto valid = json::Value::parse(saveToJson(g));
                Game restored; string error;
                check(loadFromJson(valid.dump(), restored, &error), "真实自炼法器存档可正常读取");
                auto badSource = valid;
                badSource.asObject()["player"].asObject()["arts"].asObject()["gear"].asArray().back()
                    .set("crafted", json::Value(string("true")));
                rejectLifeSave(badSource, "非布尔自炼来源标记");
            }
        }
    }
    std::printf("\n-- 遁速、截杀与NPC实际被动构筑 --\n");
    {
        Game g = lifeGame(); g.player.stage = 3;
        const auto base = travelPlan(g, "xinghe");
        g.player.arts.nodes = {"6-2", "6-4"};
        g.player.arts.main = "cloud"; g.player.arts.manuals["cloud"] = 5;
        g.player.arts.manuals["rainform"] = 5; g.player.arts.aux = {"rainform"};
        const auto fast = travelPlan(g, "xinghe");
        check(fast.escapeSpeed > base.escapeSpeed && battleSpeed(g.player) == artEscapeSpeed(g.player),
              "赶路与斗法使用相同功法、悟道遁速");
        check(travelOption(fast, "walk")->days < travelOption(base, "walk")->days &&
              travelOption(fast, "flight")->days < travelOption(base, "flight")->days,
              "遁速被动真实缩短步行与御器时间");
        checkEq(travelOption(fast, "boat")->days, travelOption(base, "boat")->days,
                "个人遁速不改变公共灵舟航速");
        checkEq(travelOption(fast, "flight")->cost, travelOption(base, "flight")->cost,
                "遁速缩短时间不会改变按地图距离收取的费用");
        check(travelOption(fast, "walk")->ambushChance < travelOption(base, "walk")->ambushChance &&
              travelOption(base, "boat")->ambushChance < travelOption(base, "walk")->ambushChance,
              "遁速与灵舟护送分别降低实际截杀风险");
    }
    {
        Game road = lifeGame();
        check(travelBegin(road, "xinghe", "walk").empty(), "建立真实长途行程用于截杀回归");
        bool encountered = false;
        Game stopped;
        // 通过真实日历寻找固定遭遇样本，避免在测试内复刻随机算法。
        for (uint32_t seed = 0; seed < 10000 && !encountered; ++seed) {
            Game candidate = road; candidate.rng = seed;
            calendarRun(candidate, 1);
            if (candidate.player.arts.battleActive()) { stopped = std::move(candidate); encountered = true; }
        }
        check(encountered, "日历逐日推进实际触发途中截杀");
        if (encountered) {
            checkStr(stopped.player.location, road.player.location, "遭遇截杀时地点仍在出发地");
            checkEq(stopped.player.stones, road.player.stones, "截杀不重复扣已经支付的行程费用");
            check(stopped.job && stopped.job->remaining == road.job->total - 1 &&
                  stopped.job->travelChecks == 1 && stopped.job->travelAmbushes == 1,
                  "遭遇记录实际一天进度并保存一次截杀计数");
            check(stopped.player.arts.battle->kind == "ambush" && stopped.player.arts.battle->lethal,
                  "截杀进入真正可撤离的生死斗法");
            const int day = stopped.clock.days, remaining = stopped.job->remaining;
            check(!calendarRun(stopped, 1).ok && stopped.clock.days == day && stopped.job->remaining == remaining,
                  "截杀斗法未处理不能继续消耗赶路天数");
            Game loaded; string error;
            check(loadFromJson(saveToJson(stopped), loaded, &error), "途中截杀与行程共同存档可恢复");
            if (loaded.job && loaded.player.arts.battleActive()) {
                battleFinish(loaded, true, false);
                const auto rng = loaded.rng;
                check(!travelAmbushTick(loaded) && loaded.rng == rng,
                      "处理斗法后同一天继续不重掷随机截杀");
                checkEq(loaded.job->travelAmbushes, 1, "胜后仍保留本段已截杀记录");
                while (loaded.job && !loaded.pending && !loaded.player.arts.battleActive()) calendarRun(loaded, 30);
                checkStr(loaded.player.location, "xinghe", "脱身后继续剩余路程并真实抵达");
                checkEq(loaded.clock.days, road.clock.days + road.job->total,
                        "斗法处理不额外或重复消耗行程天数");
            }
        }
    }
    {
        Npc scatter; scatter.stage = 0; scatter.dao = "剑";
        Npc disciple = scatter; disciple.sect = "jinhong";
        const auto weak = artNpcLoadout(scatter), rich = artNpcLoadout(disciple);
        check(weak.aux.empty() && weak.nodes.empty() && weak.spells.size() <= 2,
              "初期散修被动与额外神通较弱");
        check(rich.manuals.at(rich.main) > weak.manuals.at(weak.main) && !rich.aux.empty() && !rich.nodes.empty(),
              "同境界宗门修士拥有实际功法和悟道优势");
        bool legal = true;
        for (int stage : {0, 3, 6, 9, 12}) for (const char* sect : {"", "jinhong", "xinghe", "zhushan", "lihuo", "huachen"}) {
            disciple.stage = stage; disciple.sect = sect;
            const auto a = artNpcLoadout(disciple);
            ManualInfo info;
            legal = legal && findManualInfo(a.main, info) && info.stage <= stage;
            for (const auto& id : a.aux) legal = legal && findManualInfo(id, info) && info.stage <= stage;
            for (const auto& id : a.nodes) legal = legal && findNode(id);
            for (const auto& id : a.spells) legal = legal && battleSpell(id) && battleSpell(id)->stage <= stage;
            legal = legal && std::set<string>(a.spells.begin(), a.spells.end()).size() == a.spells.size();
        }
        check(legal, "宗门/境界推荐构筑全用合法且可施展的真实功法神通节点");
        scatter.gear.push_back(Gear{}); scatter.gear.back().id = "npc-real-weapon";
        scatter.equipped["weapon"] = "npc-real-weapon";
        const auto worn = artNpcLoadout(scatter);
        check(worn.gear.size() == 1 && worn.equipped.at("weapon") == "npc-real-weapon",
              "NPC构筑保留其真实携带装备");
    }
}
} // namespace nztest
