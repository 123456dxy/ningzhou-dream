#include "harness.h"

#include <algorithm>
#include <stdexcept>

#include "core/api.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/cultivate.h"
#include "core/dungeon.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/roster.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/talents.h"
#include "core/travel.h"

namespace nztest {
using namespace nz;
namespace {

Game talentGame(const vector<string>& traits = {}) {
    CreateOptions options; options.name = "天赋实际触发回归"; options.seed = 734;
    options.traits = traits; options.traitsExplicit = true; options.budget = 40;
    Game game = createGame(options);
    game.npcs.clear(); game.job.reset(); game.pending.reset();
    game.player.stones = 5000; game.player.lifespan = 3000; game.plan = "rest";
    return game;
}

Job talentRest(const Game& game, int days) {
    Job job; job.actionType = "plan"; job.plan = "rest"; job.name = "天赋月历回归";
    job.started = game.clock.days; job.total = job.remaining = days; return job;
}

Game talentFight(const vector<string>& traits, const vector<string>& spells) {
    Game game = talentGame();
    game.player.traits = traits; game.player.body = 0; game.player.stage = 6;
    auto& arts = game.player.arts;
    arts.main.clear(); arts.manuals.clear(); arts.aux.clear(); arts.nodes.clear();
    arts.gear.clear(); arts.equipped.clear(); arts.spells = spells;
    EnemySpec enemy; enemy.name = "天赋触发试炼"; enemy.stage = 6; enemy.beast = true;
    battleStart(game, enemy);
    if (arts.battle) {
        arts.battle->max = 1000; arts.battle->hp = 500;
        arts.battle->enemyMax = arts.battle->enemyHp = 1000000;
        arts.battle->enemyShield = arts.battle->shield = 0;
    }
    return game;
}

void talentHand(Battle& battle, const vector<string>& spells) {
    battle.hand.clear(); battle.draw.clear(); battle.discard.clear(); battle.exhausted.clear();
    for (const auto& spell : spells)
        for (const auto& card : battle.deck)
            if (card.spell == spell && std::find(battle.hand.begin(), battle.hand.end(), card.uid) == battle.hand.end()) {
                battle.hand.push_back(card.uid); break;
            }
    for (const auto& card : battle.deck)
        if (std::find(battle.hand.begin(), battle.hand.end(), card.uid) == battle.hand.end())
            battle.draw.push_back(card.uid);
    battle.mana = 8;
}

http::Response talentRequest(const string& route, const json::Value& input = json::Object{}) {
    return api::handle({"POST", "/api/" + route, input.dump(), ""});
}

} // namespace

void runTalentTests() {
    std::printf("\n===== 天赋组合与真实战斗、生活触发 =====\n");
    {
        for (const string id : {"daoBody", "immortal", "blessing", "dragonBlood", "oddMeridian",
                               "greenBottle", "lightStep", "artisan", "family", "spiritEyes"}) {
            const auto* talent = findTalent(id);
            check(talent && *talent->desc, id + "天赋有可查询的真实效果说明");
        }
        const auto base = buildCharacter({}, {}, BASE_POINTS, 0);
        checkEq(buildCharacter({"immortal"}, {}, BASE_POINTS, 0).insight, base.insight + 10,
                "转世仙人的悟性在真正建号属性中生效");
        checkEq(buildCharacter({"dragonBlood"}, {}, BASE_POINTS, 0).body, base.body + 1,
                "龙族血脉的体魄在真正建号属性中生效");
        const auto odd = buildCharacter({"oddMeridian"}, {}, BASE_POINTS, 0);
        checkEq(odd.spent, -3, "先天异脉实际返还三点建号预算");
        for (const vector<string> traits : {vector<string>{"root", "mortal"},
                                           vector<string>{"daoBody", "weak"},
                                           vector<string>{"gold", "water"},
                                           vector<string>{"wood", "fire", "earth"}}) {
            bool rejected = false;
            try { buildCharacter(traits, {}, 40, 0); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected && !talentConflict(traits).empty(), "核心建号拒绝互斥天赋组合");
            check(!play::checkBuild(traits, {}, 40).ok, "前端建号预检与核心使用相同互斥规则");
        }
        check(talentConflict({"daoBody", "oddMeridian", "root"}).empty(),
              "天灵根、道体与异脉可以组成合法修炼路线");
        Game old = talentGame(); old.player.traits = {"gold", "water"};
        Game loaded; string error;
        check(loadFromJson(saveToJson(old), loaded, &error) && loaded.player.traits == old.player.traits,
              "旧档多灵根玩家可正常读取，不强制剥夺既有天赋");
    }
    {
        Game baseline = talentGame(), gifted = talentGame({"immortal", "family", "spiritEyes"});
        checkEq(gifted.player.arts.points, baseline.player.arts.points + 1, "真正新建角色发放转世悟道点");
        checkEq(gifted.player.arts.mind, baseline.player.arts.mind + 3, "真正新建角色发放异世神识");
        checkEq(gifted.player.arts.manuals["bull"], 1, "修真家族开局实际修得蛮牛护体功一层");
        check(std::find(gifted.player.arts.knownSpells.begin(), gifted.player.arts.knownSpells.end(), "gather") !=
              gifted.player.arts.knownSpells.end(), "修真家族开局实际领悟聚气神通");
        const string born = saveToJson(gifted); const auto rng = gifted.rng;
        talentBirth(gifted);
        check(saveToJson(gifted) == born && gifted.rng == rng, "开局传承防重复发放且不消耗随机数");
        Game loaded; string error;
        check(loadFromJson(born, loaded, &error), "带新天赋开局物品与标记的存档可实际载入");
        check(loaded.player.arts.points == gifted.player.arts.points && loaded.player.arts.mind == gifted.player.arts.mind,
              "读档不重复发放悟道点或神识");
        const int points = gifted.player.arts.points;
        gifted.player.stage = 1; talentBreakthrough(gifted, false);
        checkEq(gifted.player.arts.points, points, "小境界突破不会误发转世悟道点");
        gifted.player.stage = 3; talentBreakthrough(gifted, true);
        checkEq(gifted.player.arts.points, points + 1, "筑基大境界发放额外转世悟道点");
        talentBreakthrough(gifted, true);
        checkEq(gifted.player.arts.points, points + 1, "同一大境界不会重复发放转世悟道点");
        gifted.player.stage = 6; talentBreakthrough(gifted, true);
        checkEq(gifted.player.arts.points, points + 2, "下一大境界继续获得额外悟道点");
        Game breakthrough = talentGame({"immortal"}); breakthrough.player.stage = 2;
        breakthrough.player.xp = realmOf(2).need;
        Job job; job.actionType = "breakthrough"; job.total = job.remaining = 1;
        job.started = breakthrough.clock.days; job.breakthroughPrepared = true;
        job.breakthroughMajor = true; job.breakthroughChance = 1; job.breakthroughFailureLoss = 0.2;
        breakthrough.job = job; const int oldPoints = breakthrough.player.arts.points;
        const auto result = calendarRun(breakthrough, 1);
        check(result.ok && breakthrough.player.stage == 3 && !breakthrough.job,
              "转世仙人通过真实月历完成筑基大境界突破");
        checkEq(breakthrough.player.arts.points, oldPoints + 1, "真实突破结算接入转世额外悟道点");
    }
    {
        Player base = talentGame().player, blessed = base;
        base.arts.exp["水"] = blessed.arts.exp["水"] = 0;
        blessed.traits = {"blessing"};
        artGain(base, "水", 50); artGain(blessed, "水", 50);
        checkEq(blessed.arts.exp["水"] - base.arts.exp["水"], 10, "天道福泽真实获得大道经验额外20%");
        Player body = base; body.traits = {"daoBody"};
        checkNear(talentCultivationMul(body), 1.10, "先天道体的修炼倍率明确为1.10");
        checkNear(cultivate::rate(body), cultivate::rate(base) * 1.10, "真实修炼速度接入先天道体倍率");
        body.stage = 2; Player normal = body; normal.traits.clear();
        checkEq(battleManaCap(body), battleManaCap(normal), "炼气道体尚无额外灵气容量");
        body.stage = normal.stage = 3;
        checkEq(battleManaCap(body), battleManaCap(normal) + 2, "筑基道体真实灵气容量增加两点");
        checkEq(battleEnergy(body), battleEnergy(normal), "容量天赋不误增每回合自然回气");
        const auto* xp = findRecipe("5002");
        Player odd = base; odd.traits = {"oddMeridian"};
        check(xp && artPillEffect(base, *xp).value > 0 && artPillEffect(odd, *xp).value == 0,
              "先天异脉在真实丹药效果查询中阻止修为增益");
        Player scholar = base; scholar.traits = {"savvy"};
        check(cultivate::learnDays(scholar, 3) < cultivate::learnDays(base, 3),
              "相同悟性时过目不忘仍真实缩短功法研习时间");
        Game study = talentGame(); study.player = base;
        const auto usualStudy = ecoActionInfo(study, "learn", "mind");
        study.player = scholar;
        const auto quickStudy = ecoActionInfo(study, "learn", "mind");
        check(usualStudy.valid && quickStudy.valid && quickStudy.days < usualStudy.days,
              "功法真实行动报价接入过目不忘额外20%研习减时");
    }
    {
        Game game = talentGame({"greenBottle"}); game.clock.days = 29;
        game.player.arts.herbs = {{"银月花", 1}}; game.player.herbs = 1;
        game.job = talentRest(game, 1);
        const int stones = game.player.stones;
        const auto result = calendarRun(game, 1);
        check(result.ok && game.clock.days == 30 && !game.job, "绿瓶通过真实月历跨月并完成工作");
        checkEq(game.player.arts.herbs["银月花"], 2, "真实月历催熟一份已拥有低品草药");
        checkEq(game.player.herbs, 2, "绿瓶同步药材总量而不被分类同步删除");
        checkEq(game.player.stones, stones - 5, "真实月历只扣五灵石催熟费用");
        const string once = saveToJson(game); const auto rng = game.rng;
        talentMonthTick(game); talentMonthTick(game);
        check(saveToJson(game) == once && game.rng == rng, "同月反复触发不刷材料、扣钱或消耗随机数");
        Game loaded; string error;
        check(loadFromJson(once, loaded, &error), "绿瓶同月记录可真实保存读取");
        talentMonthTick(loaded);
        check(saveToJson(loaded) == once, "读档后同月不会重复催熟");
        game.clock.days = 60; game.job = talentRest(game, 1);
        const auto beforeRng = game.rng;
        talentMonthTick(game);
        checkEq(game.player.arts.herbs["银月花"], 3, "下个月绿瓶可再次催熟已有药材");
        checkEq(game.rng, beforeRng, "绿瓶挑选已有药材本身不掷随机数");
    }
    {
        for (const string unavailable : {"poor", "missing", "high", "beast", "dungeon"}) {
            Game game = talentGame({"greenBottle"}); game.clock.days = 29;
            game.player.arts.herbs = {{"银月花", 1}}; game.player.herbs = 1;
            if (unavailable == "poor") game.player.stones = 4;
            if (unavailable == "missing") { game.player.arts.herbs.clear(); game.player.herbs = 0; }
            if (unavailable == "high") {
                for (const auto& herb : data::HerbDef_TABLE()) if (herb.grade >= 3 && herb.name.find("妖丹") == string::npos) {
                    game.player.arts.herbs = {{herb.name, 1}}; break;
                }
            }
            if (unavailable == "beast") game.player.arts.herbs = {{"二阶妖丹·金", 1}};
            if (unavailable == "dungeon") game.dungeons.run = DungeonRun{};
            const auto herbs = game.player.arts.herbs;
            const int stones = game.player.stones, count = game.player.herbs;
            game.job = talentRest(game, 1); calendarRun(game, 1); talentMonthTick(game);
            check(game.player.arts.herbs == herbs && game.player.herbs == count && game.player.stones == stones,
                  unavailable + "情形真实月历不催熟、不扣费，不能绕过高阶材料来源");
            if (unavailable == "dungeon") {
                game.dungeons.run.reset(); const auto rng = game.rng;
                talentMonthTick(game);
                checkEq(game.player.arts.herbs["银月花"], 2, "离开副本后同月在安全时刻才催熟");
                checkEq(game.rng, rng, "副本外补催熟不消耗随机数");
            }
        }
    }
    {
        Player plain = talentGame().player; plain.arts.main.clear(); plain.arts.manuals.clear(); plain.arts.aux.clear();
        Player alchemist = plain; alchemist.traits = {"alchemy"};
        checkEq(artBrewInfo(alchemist, "5001").output, artBrewInfo(plain, "5001").output + 1,
                "丹火童子实际每炉多产一枚丹药");
        alchemist.arts.main = "pill"; alchemist.arts.manuals = {{"pill", 5}}; alchemist.arts.nodes = {"10-5"};
        checkEq(artBrewInfo(alchemist, "5001").output, 4, "天赋、功法与丹道叠加产丹仍以四枚为上限");
        Game brew = talentGame({"alchemy"}); const auto* recipe = findRecipe("5001");
        brew.player.arts.herbs.clear(); brew.player.herbs = 0;
        for (const auto& [name, count] : parseIngredients(recipe->ingredients)) {
            brew.player.arts.herbs[name] = count; brew.player.herbs += count;
        }
        ecoBegin(brew, "brew", "5001");
        check(brew.job && brew.job->craftOutput == 2, "实际开炉冻结丹火童子增产数量");
        if (brew.job) {
            brew.job->chance = 1;
            while (brew.job && !brew.ended && !brew.pending) calendarRun(brew, 30);
            checkEq(brew.player.arts.potions["5001"], 2, "丹火童子实际成功一炉产出两枚丹药");
        }
        Player artisan = plain; artisan.traits = {"artisan"};
        check(ecoCraftDays(artisan, "forge", 4) < ecoCraftDays(plain, "forge", 4), "百炼匠心真实缩短炼器时间");
        checkEq(ecoCraftDays(artisan, "brew", 4), ecoCraftDays(plain, "brew", 4), "器道天赋不误缩短炼丹时间");
        Game sale = talentGame(); sale.player = plain;
        Gear forged; forged.id = "talent-forge-sale"; forged.grade = 2; forged.craftCost = 100; forged.crafted = true;
        Gear bought = forged; bought.id = "talent-market-sale"; bought.crafted = false;
        sale.player.arts.gear = {forged, bought};
        const int madePrice = shop::salePrice(sale, "gear", forged.id), boughtPrice = shop::salePrice(sale, "gear", bought.id);
        sale.player.traits = {"artisan"};
        check(shop::salePrice(sale, "gear", forged.id) > madePrice, "百炼匠心提高真正自炼法器售值");
        checkEq(shop::salePrice(sale, "gear", bought.id), boughtPrice, "坊市买入法器不冒领自炼售值天赋");
        sale.player.arts.main = "forge"; sale.player.arts.manuals = {{"forge", 5}};
        sale.player.arts.nodes = {"11-3", "11-4", "11-5"};
        checkNear(talentMods(sale.player).forgeSpeed, std::min(0.6, artModsOf(sale.player.arts).forgeSpeed + 0.15),
                  "炼器天赋与道法叠加仍遵守60%减时上限");
        checkNear(talentMods(sale.player).craftedSale, 0.5, "自炼售值天赋与道法仍遵守50%上限");
    }
    {
        Player normal = talentGame().player, fast = normal; fast.traits = {"lightStep"};
        checkEq(artEscapeSpeed(fast), artEscapeSpeed(normal) + 6, "轻功天赋真实增加六点遁速");
        checkEq(battleSpeed(fast), battleSpeed(normal) + 6, "战斗先手使用同一轻功遁速");
        Game game = talentGame(); game.player = normal;
        const auto old = travelPlan(game, "qingshi"); game.player = fast;
        const auto quick = travelPlan(game, "qingshi");
        check(travelOption(quick, "walk")->days <= travelOption(old, "walk")->days && quick.escapeSpeed == old.escapeSpeed + 6,
              "轻功天赋进入实际赶路距离与耗时计算");
        Battle foe; foe.lethal = true; foe.enemySpeed = 50; foe.enemyStage = normal.stage;
        check(battleEscapeChance(fast, foe) > battleEscapeChance(normal, foe), "轻功天赋真实提高逃避战斗概率");
    }
    {
        Game game = talentFight({"gold"}, {"refresh"});
        check(game.player.arts.battle.has_value(), "金灵根实际战斗可建立");
        if (game.player.arts.battle) {
            auto& battle = *game.player.arts.battle;
            talentHand(battle, {"refresh", "strike", "defend", "strike"});
            const string before = saveToJson(game);
            const auto preview = battlePreview(game.player, battle, "refresh");
            check(preview.valid && preview.shield == 30, "金灵根弃三张牌的预览显示30护盾");
            check(saveToJson(game) == before, "天赋战斗预览不改变真实手牌或状态");
            check(battleTurn(game, "refresh").ok, "金灵根通过真实重抽神通出牌");
            checkEq(battle.shield, preview.shield, "金灵根真实弃牌凝盾与预览一致");
        }
    }
    for (const string root : {"water", "wood", "fire", "earth"}) {
        const string spell = root == "water" ? "wavehit" : root == "wood" ? "corrode" : root == "fire" ? "fireburst" : "goldslash";
        Game game = talentFight({root}, {spell});
        check(game.player.arts.battle.has_value(), root + "灵根实际战斗可建立");
        if (!game.player.arts.battle) continue;
        auto& battle = *game.player.arts.battle; talentHand(battle, {spell});
        battle.water = 3; battle.shield = 200; battle.burn = 10; battle.burnTurns = 3;
        Player ordinary = game.player; ordinary.traits.clear();
        const auto base = battlePreview(ordinary, battle, spell), special = battlePreview(game.player, battle, spell);
        check(base.valid && special.valid, root + "灵根神通可以实际预览");
        if (root == "water") checkEq(special.energy, base.energy + 3, "水灵根消费三层水势真实返还三点灵力");
        if (root == "wood") checkEq(special.heal, base.heal + 15, "木灵根施三层毒真实回复1.5%气血");
        if (root == "fire") check(special.damage > base.damage, "火灵根真实提高灼烧引爆伤害");
        if (root == "earth") check(special.damage > base.damage, "土灵根真实把当前护盾加入重击伤害");
        const int enemyHp = battle.enemyHp, hp = battle.hp, mana = battle.mana;
        check(battleTurn(game, spell).ok, root + "灵根真实神通出牌成功");
        checkEq(enemyHp - battle.enemyHp, special.damage, root + "灵根真实伤害与预览相同");
        checkEq(battle.hp - hp, special.heal, root + "灵根真实治疗与预览相同");
        checkEq(battle.mana - mana, special.energy, root + "灵根真实灵力变化与预览相同");
    }
    {
        Game game = talentFight({"dragonBlood"}, {"strike"});
        if (game.player.arts.battle) {
            auto& battle = *game.player.arts.battle; talentHand(battle, {"strike"});
            check(battleTurn(game, "strike").ok && battle.attacked, "龙血回血测试本回合确实打出攻击牌");
            battle.hp = 500; battle.intentKind = "guard"; battle.intentName = "凝盾";
            check(battleTurn(game, "end").ok, "龙血通过真实结束回合触发");
            checkEq(battle.hp, 530, "龙族血脉攻击后仍恢复3%气血，不要求静养");
            battle.hp = 999; battle.intentKind = "guard";
            battleTurn(game, "end");
            checkEq(battle.hp, 1000, "龙血回合治疗不能超过气血上限");
        } else check(false, "龙族血脉斗法可建立");
    }
    // 须由主测试入口放在原有 runApiTests 之后，保证冷启动回归不受影响。
    {
        Game& game = api::game(); const Game original = game;
        game = talentGame();
        for (const vector<string> traits : {vector<string>{"root", "mortal"}, vector<string>{"daoBody", "weak"},
                                           vector<string>{"gold", "water"}}) {
            json::Array selected; for (const auto& id : traits) selected.push_back(json::Value(id));
            json::Value input(json::Object{}); input.set("name", "非法天赋不得替换当前人生");
            input.set("traits", json::Value(selected));
            const string before = saveToJson(game);
            check(talentRequest("new", input).status >= 400, "真实new接口拒绝互斥天赋组合");
            check(saveToJson(game) == before, "非法天赋建号不会替换原角色、资源、随机序列或存档");
        }
        game = talentGame({"oddMeridian"}); game.player.arts.potions["5002"] = 1;
        const int xp = game.player.xp;
        json::Value pill(json::Object{}); pill.set("id", "5002");
        const auto result = talentRequest("use-pill", pill);
        check(result.status == 200 || result.status == 400, "真实服丹接口处理先天异脉");
        checkEq(game.player.xp, xp, "先天异脉实际服用修为丹也不能增长修为");
        game = original;
    }
}

} // namespace nztest
