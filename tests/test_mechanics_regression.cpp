#include "harness.h"

#include <algorithm>
#include <climits>

#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/conflict.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/society.h"

using namespace nz;

namespace nztest {
namespace {
Game richGame() {
    Game g = createGame(CreateOptions{});
    g.pending.reset();
    g.player.stones = 10000;
    g.player.health = mfPlayerHealthMaximum(g.player);
    return g;
}
int materialCount(const Game& g) {
    int total = 0;
    for (const auto& [id, count] : g.player.arts.materials) total += count;
    return total;
}
void startTestBattle(Game& g, const string& kind = "spar") {
    EnemySpec enemy;
    enemy.name = "回归对手";
    enemy.kind = kind;
    enemy.beast = true;
    battleStart(g, enemy);
}
}

void runMechanicsRegressionTests() {
    std::printf("\n===== 机制漏洞回归 =====\n");
    {
        Game g = richGame();
        const int materials = materialCount(g);
        const int stones = g.player.stones;
        startTestBattle(g);
        battleFinish(g, true);
        checkEq(g.player.stones, stones + 20, "普通切磋只给切磋灵石");
        checkEq(g.bonds.wildWins, 0, "普通切磋不计野外悬赏胜场");
        checkEq(g.player.renown, 0, "普通切磋不发悬赏声望");
        checkEq(g.economy.huntCounts, 0, "普通切磋不计猎妖委托");
        checkEq(materialCount(g), materials, "普通切磋不凭空发猎妖矿材");
    }
    {
        Game g = richGame();
        const int stones = g.player.stones;
        startTestBattle(g, "wild");
        g.player.arts.battle->advKind = "justice";
        g.player.arts.battle->advFee = 40;
        battleFinish(g, true);
        checkEq(g.player.stones, stones + 40, "游历赏金不与悬赏重复发放");
        checkEq(g.bonds.wildWins, 0, "游历遭遇不计悬赏");
        checkEq(g.player.renown, 3, "行侠只发自己的声望");
    }
    {
        Game a = richGame(), b = richGame();
        Job hunt;
        hunt.actionType = "hunt";
        hunt.fee = 32;
        ecoComplete(a, hunt);
        hunt.fee = 77;
        ecoComplete(b, hunt);
        const int aStones = a.player.stones, bStones = b.player.stones;
        Game loaded;
        string error;
        check(loadFromJson(saveToJson(a), loaded, &error), "在途猎妖可以存读档");
        if (loaded.player.arts.battle.has_value()) {
            checkEq(loaded.player.arts.battle->huntFee, 32, "猎妖赏金存档保留");
            battleFinish(loaded, true);
            checkEq(loaded.player.stones, aStones + 32, "另一档猎妖不覆盖本档赏金");
        }
        battleFinish(b, true);
        checkEq(b.player.stones, bStones + 77, "第二档发放自己的赏金");
    }
    {
        Game g = richGame();
        startTestBattle(g);
        Battle& b = *g.player.arts.battle;
        b.BeastActions.clear();
        b.intentKind = "poison";
        b.intentRaw = 0;
        const int enemyHp = b.enemyHp;
        battleTurn(g, "end");
        checkEq(b.poison, 0, "敌方施毒不增加敌方受到的毒层");
        check(b.playerPoison > 0, "玩家记录敌方毒伤");
        const int hp = b.hp, tick = b.playerPoison;
        b.BeastActions.clear();
        b.intentKind = "guard";
        b.intentRaw = 0;
        battleTurn(g, "end");
        checkEq(b.hp, hp - tick, "敌方毒伤确实扣玩家气血");
        checkEq(b.enemyHp, enemyHp, "敌方不会被自己的毒伤打中");
    }
    {
        Game g = richGame();
        startTestBattle(g);
        Battle& b = *g.player.arts.battle;
        b.enemyWeaken = 1;
        const auto weak = battlePreview(g.player, b, "strike");
        Battle clean = b;
        clean.enemyWeaken = 0;
        check(weak.damage < battlePreview(g.player, clean, "strike").damage,
              "敌方扰灵削弱下一次直接攻击");
        const auto heal = battlePreview(g.player, b, "wood");
        check(heal.valid && b.enemyWeaken == 1, "治疗预览不会消耗扰灵");
        b.hand.clear();
        for (const auto& card : b.deck)
            if (card.spell == "strike") { b.hand.push_back(card.uid); break; }
        b.mana = 8;
        battleTurn(g, "strike");
        checkEq(b.enemyWeaken, 0, "扰灵仅影响一手直接攻击");
    }
    {
        Game base = richGame(), boosted = base;
        boosted.player.arts.combatBuff = 0.1;
        boosted.player.arts.combatPrep["reduction"] = 0.12;
        startTestBattle(base);
        startTestBattle(boosted);
        check(battlePreview(boosted.player, *boosted.player.arts.battle, "strike").damage >
              battlePreview(base.player, *base.player.arts.battle, "strike").damage,
              "百分比攻击丹药确实提高伤害");
        checkNear(boosted.player.arts.battle->reduction, 0.12, "减伤丹保留小数效果");
        base.player.arts.battle->BeastActions.clear();
        boosted.player.arts.battle->BeastActions.clear();
        base.player.arts.battle->intentKind = "attack";
        boosted.player.arts.battle->intentKind = "attack";
        base.player.arts.battle->intentRaw = boosted.player.arts.battle->intentRaw = 100;
        const int baseHp = base.player.arts.battle->hp;
        const int boostHp = boosted.player.arts.battle->hp;
        battleTurn(base, "end");
        battleTurn(boosted, "end");
        checkEq(baseHp - base.player.arts.battle->hp, 100, "未服减伤丹承受完整伤害");
        checkEq(boostHp - boosted.player.arts.battle->hp, 88, "减伤丹实际减免 12% 来伤");
    }
    {
        Game g = richGame();
        g.player.sword = 0;  // 旧字段兼容；实际增伤由本系节点决定。
        startTestBattle(g);
        const Battle battle = *g.player.arts.battle;
        const int gold = battlePreview(g.player, battle, "gold").damage;
        const int fire = battlePreview(g.player, battle, "fire").damage;
        const int sword = battlePreview(g.player, battle, "sword").damage;
        g.player.arts.nodes = {"0-3", "3-3", "7-1"};
        check(battlePreview(g.player, battle, "gold").damage > gold, "金道贯通增加本系伤害");
        check(battlePreview(g.player, battle, "fire").damage > fire, "火道贯通增加本系伤害");
        check(std::abs(battlePreview(g.player, battle, "sword").damage - sword * 1.12) <= 1.0,
              "剑道入门兑现 12% 伤害加成（允许逐步取整）");
    }
    {
        Game g = richGame();
        const auto* recipe = findRecipe("5001");
        for (const auto& [name, count] : parseIngredients(recipe->ingredients))
            g.player.arts.herbs[name] = count * 10;
        g.player.herbs = 30;
        g.player.arts.durability = 2;
        check(!ecoActionError(g, "brew", "5001", 3).empty(), "整批炼丹要求足够耐久");
        const int stones = g.player.stones;
        ecoBegin(g, "brew", "5001", 3);
        check(!g.job.has_value() && g.player.stones == stones, "批量不足时不扣钱不创建工作");
        g.player.arts.durability = 40;
        checkStr(ecoActionError(g, "brew", "5001", 3), "", "三炉炼丹通过校验");
        const auto info = ecoActionInfo(g, "brew", "5001", 3);
        ecoBegin(g, "brew", "5001", 3);
        checkEq(g.player.stones, stones - 42, "三炉按三份开炉灵石收费");
        checkEq(g.player.arts.durability, 37, "三炉消耗三点耐久");
        checkEq(g.player.herbs, 21, "中文丹方扣除九份药材总数");
        for (const auto& [name, count] : parseIngredients(recipe->ingredients))
            checkEq(g.player.arts.herbs[name], count * 7, "定时炼丹真正扣除指定药材：" + name);
        checkEq(g.job->gift, 3, "工作保存批次数量");
        checkEq(g.job->total, info.days, "批量耗时与界面查询一致");
    }
    {
        Game g = richGame();
        g.player.arts.materials["iron"] = 4;
        check(!ecoActionError(g, "forge", "iron|iron|iron|sword").empty(),
              "重复使用同种材料必须合并到十份需求");
        g.player.arts.materials["iron"] = 10;
        checkStr(ecoActionError(g, "forge", "iron|iron|iron|sword"), "", "十份材料可以炼同材法器");
        g.player.arts.materials["gold"] = 100;
        g.player.arts.materials["crystal"] = 100;
        g.player.arts.materials["soul"] = 100;
        check(!ecoActionError(g, "forge", "gold|crystal|soul|armor").empty(), "炼气不能越阶铸三阶法器");
    }
    {
        Game g = richGame();
        const int stones = g.player.stones;
        check(!play::marketBuy(g, "potion", "5005", INT_MAX).empty(), "巨量购买被拒绝");
        check(!shop::tradeStock(g, "cores", true, INT_MAX).ok, "巨量灵核交易被拒绝");
        checkEq(g.player.stones, stones, "非法数量不改变灵石");
        check(!play::marketBuy(g, "gear", "sword-0", 1).empty(), "零品阶法器不能购买");
        check(!play::marketBuy(g, "gear", "sword-4", 1).empty(), "法器商店不能越境购买");
        check(!play::marketBuy(g, "gear", "sword-1junk", 1).empty(), "畸形法器参数不能购买");
        const auto BeforeGearPurchase = saveToJson(g);
        checkStr(play::marketBuy(g, "gear", "sword-1", 1), "成品法器未上架，可在工坊自行炼制。", "标准法器旧购买入口明确下架");
        check(saveToJson(g) == BeforeGearPurchase, "下架商品购买不扣费不造法器也不推进标识序列");
        for (const auto& item : play::marketList(g))
            if (item.kind == "potion") check(item.price > item.sellPrice, "丹药买价高于售价：" + item.name);
    }
    {
        Game g = richGame();
        check(!wildActionError(g, "beast").empty(), "城中不能接野外悬赏");
        g.player.location = "sea";
        check(!wildActionError(g, "beast").empty(), "无尽海悬赏要求筑基");
        g.player.location = "fanghu";
        checkStr(wildActionError(g, "beast"), "", "野外气血充足可以接悬赏");
        for (int i = 0; i < 3; ++i) { societyWildBegin(g, "beast"); g.job.reset(); }
        check(!wildActionError(g, "beast").empty(), "野外悬赏限制每月三次");
        g.clock.days = 30;
        checkStr(wildActionError(g, "beast"), "", "次月恢复野外悬赏额度");
    }
    {
        Game g = richGame();
        Gear gear;
        gear.id = "regression-weapon";
        gear.slot = "weapon";
        gear.grade = 1;
        gear.bonus = 0.1;
        g.player.arts.gear.push_back(gear);
        g.player.arts.nodes = {"11-1"};
        g.player.arts.equipped["weapon"] = gear.id;
        check(shop::inscribe(g, gear.id).ok, "法器可以铭刻");
        checkNear(g.player.arts.gear.back().bonus, 0.1, "铭刻保留原始法器加成");
        checkNear(artModsOf(g.player.arts).attack, 1.112, "铭刻只生效一次 12% 增幅");
        g.player.insight = INSIGHT_MAX - 1;
        artAnnual(g, "meditate", "气");
        checkEq(g.player.insight, INSIGHT_MAX, "参悟遵守全局悟性上限");
        g.player.apt = 190;
        for (const auto& story : data::StoryDef_TABLE())
            if (story.id == "rootpool") storyComplete(g, story, 0);
        checkEq(g.player.apt, 193, "高资质机缘不被旧 150 上限倒扣");
    }
    {
        Game g = richGame();
        g.player.arts.exp.erase("金");
        g.player.arts.potions["5001"] = 7;
        const auto materials = g.player.arts.materials;
        g.player.arts.poison = 25;
        ensureArts(g);
        checkEq(static_cast<long long>(g.player.arts.exp.size()), 12, "旧档缺失大道经验被补齐");
        checkEq(g.player.arts.potions["5001"], 7, "补齐大道不吞已有丹药");
        check(g.player.arts.materials == materials && g.player.arts.poison == 25,
              "补齐大道保留材料与丹毒进度");
    }
    {
        Game g = richGame();
        g.player.sect = "jinhong";
        g.player.location = "jinhong";
        Npc& master = g.npcs.front();
        master.alive = true;
        master.location = "jinhong";
        g.bonds.master = master.id;
        const auto lesson = bondActionInfo(g, "lesson");
        checkEq(lesson.days, 10, "师尊授业耗时十天");
        checkEq(lesson.cost, 12, "师尊授业投入十二灵石");
        const int stones = g.player.stones;
        societyBondBegin(g, "lesson");
        check(g.job.has_value() && g.job->npcId == master.id, "授业安排绑定真实师尊");
        checkEq(g.player.stones, stones - 12, "授业开工扣费一次");
        Job first = *g.job;
        g.job.reset();
        monthComplete(g, first);
        checkEq(g.bonds.lessons, 1, "授业到期计入次数");
        check(!bondActionError(g, "lesson").empty(), "授业每月只能完成一次");
        g.clock.days = 30;
        societyBondBegin(g, "lesson");
        Job second = *g.job;
        g.job.reset();
        monthComplete(g, second);
        checkEq(g.bonds.lessons, 2, "次月可完成第二次授业");
        check(std::find(g.economy.books.begin(), g.economy.books.end(), "fetal") != g.economy.books.end(),
              "授业胎息经抄本同步到修习书单");
        Npc& pupil = g.npcs[1];
        pupil.alive = true;
        pupil.location = "jinhong";
        pupil.stage = 0;
        pupil.xp = 0;
        pupil.insight = 10;
        g.bonds.disciples = {pupil.id};
        societyBondBegin(g, "teach");
        Job teaching = *g.job;
        g.job.reset();
        monthComplete(g, teaching);
        checkEq(pupil.xp, 400, "扩大修为单位后日常授业给真实弟子400修为");
        checkEq(pupil.insight, 13, "日常授业给真实弟子悟性");
        Npc& partner = g.npcs[2];
        partner.alive = true;
        partner.location = "jinhong";
        g.bonds.partner = partner.id;
        societyBondBegin(g, "together");
        Job together = *g.job;
        g.job.reset();
        monthComplete(g, together);
        checkEq(g.bonds.bondDays, 10, "道侣日常相伴实际累计十天");
        check(!bondActionError(g, "together").empty(), "相伴每月只能完成一次");
        societyBondBegin(g, "part");
        Job part = *g.job;
        g.job.reset();
        monthComplete(g, part);
        check(g.bonds.partner.empty(), "解除道侣之约日常动作可完成");
        societyBondBegin(g, "dismiss");
        Job dismiss = *g.job;
        g.job.reset();
        monthComplete(g, dismiss);
        check(g.bonds.master.empty(), "辞别师门日常动作可完成");
    }
    {
        Game g = richGame();
        Npc& npc = g.npcs.front();
        npc.alive = true;
        npc.stones = 100;
        npc.herbs = 10;
        const string id = npc.id;
        EnemySpec enemy;
        enemy.name = npc.name;
        enemy.id = id;
        enemy.kind = "kill";
        enemy.lethal = true;
        battleStart(g, enemy);
        const int stones = g.player.stones;
        battleFinish(g, true);
        check(!npc.alive && npc.deathYear == g.year, "截杀战胜后目标进入逝者旧卷");
        checkEq(g.player.stones, stones + 60, "冲突胜利从目标真实行囊掠夺");
        checkEq(g.economy.huntCounts, 0, "截杀不冒充猎妖委托");
    }
}
}
