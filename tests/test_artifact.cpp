#include "harness.h"

#include <algorithm>

#include "core/artifact.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/beast_actions.h"
#include "core/combat/battle_scaling.h"
#include "core/calendar.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
using namespace nz;
namespace {

Game artifactGame(int stage = 6) {
    CreateOptions options; options.name = "法宝实际催动回归"; options.seed = 9384;
    options.traitsExplicit = true; options.budget = 40;
    Game g = createGame(options);
    g.npcs.clear(); g.pending.reset(); g.job.reset(); g.plan = "rest";
    auto& p = g.player;
    p.stage = p.highestStage = stage; p.body = 0;
    p.stones = 5000; p.lifespan = 3000;
    p.arts.main.clear(); p.arts.manuals.clear(); p.arts.aux.clear(); p.arts.nodes.clear();
    p.arts.gear.clear(); p.arts.equipped.clear(); p.arts.spells.clear(); p.arts.speed = 200;
    p.health = mfPlayerHealthMaximum(p);
    return g;
}

Gear artifactGear(const string& shape, const string& element = "火", bool yin = false, int grade = 2) {
    Gear gear; gear.id = "artifact-test-" + shape; gear.crafted = true;
    gear.shape = shape; gear.grade = grade; gear.element = element;
    gear.polarity = yin ? "地阴" : "天阳"; gear.bonus = 0.10; gear.craftCost = 100;
    for (const auto& def : data::ShapeDef_TABLE()) if (def.id == shape) {
        gear.slot = def.slot; gear.name = "试炼" + def.name;
    }
    return gear;
}

void artifactBattle(Game& g, const Gear& gear) {
    g.player.arts.gear = {gear}; g.player.arts.equipped = {{gear.slot, gear.id}};
    EnemySpec enemy; enemy.name = "催动试炼妖兽"; enemy.stage = g.player.stage;
    enemy.beast = true; enemy.speed = 1;
    battleStart(g, enemy);
    if (!g.player.arts.battle) return;
    Battle& b = *g.player.arts.battle;
    b.max = 1000; b.hp = 500; b.shield = 0; b.mana = 12;
    b.enemyMax = b.enemyHp = 1000000; b.enemyShield = 0;
    BeastCombat::mfPlanActions(g);
}

struct ArtifactPiles {
    vector<string> deck, hand, draw, discard, exhausted;
    explicit ArtifactPiles(const Battle& b) : hand(b.hand), draw(b.draw), discard(b.discard), exhausted(b.exhausted) {
        for (const auto& card : b.deck) deck.push_back(card.uid + "=" + card.spell);
    }
    bool equals(const Battle& b) const {
        const ArtifactPiles current(b);
        return deck == current.deck && hand == current.hand && draw == current.draw &&
               discard == current.discard && exhausted == current.exhausted;
    }
};

void rejectedUnchanged(Game& g, const string& id, const string& label) {
    const string before = saveToJson(g); const auto rng = g.rng;
    check(!battleTurn(g, id).ok, label + "实际拒绝");
    check(saveToJson(g) == before && g.rng == rng, label + "不改资源、牌堆、状态或随机数");
}

int elementalState(const Battle& b, const string& dao) {
    if (dao == "火") return b.burn;
    if (dao == "木") return b.poison;
    if (dao == "水") return b.water;
    if (dao == "神") return b.soul;
    if (dao == "剑") return b.swordQi;
    return b.charge;
}

bool loadArtifact(const Game& original, Game& loaded, const string& label) {
    string error; const bool ok = loadFromJson(saveToJson(original), loaded, &error);
    check(ok, label + (ok ? "" : "：" + error)); return ok;
}

} // namespace

void runArtifactTests() {
    std::printf("\n-- 自炼法宝主动技能真实催动 --\n");
    for (const string shape : {"sword", "seal", "armor", "bell", "pearl"}) {
        Game g = artifactGame();
        Gear gear = artifactGear(shape, shape == "pearl" ? "木" : "火", shape == "armor");
        artifactBattle(g, gear); Battle& b = *g.player.arts.battle;
        if (shape == "seal") { b.burn = 80; b.burnTurns = 2; }
        if (shape == "bell") b.mana = 2;
        const string id = "artifact:" + gear.id;
        const auto ability = equippedArtifactAbility(g.player, id);
        check(ability.has_value(), shape + "已装备合法自炼法宝有主动能力");
        const ArtifactPiles piles(b); const string before = saveToJson(g); const auto rng = g.rng;
        const Preview preview = battlePreview(g.player, b, id);
        check(preview.valid && saveToJson(g) == before && g.rng == rng, shape + "预览有效且完全只读");
        const int hp = b.hp, enemyHp = b.enemyHp, shield = b.shield, mana = b.mana, qi = b.swordQi, played = b.played;
        check(battleTurn(g, id).ok, shape + "无需对应神通在手即可催动");
        checkEq(enemyHp - b.enemyHp, preview.damage, shape + "伤害与预览相同");
        checkEq(b.hp - hp, preview.heal, shape + "治疗与预览相同");
        checkEq(b.shield - shield, preview.shield, shape + "护盾与预览相同");
        checkEq(b.mana - mana, preview.energy, shape + "灵力变化与预览相同");
        checkEq(b.swordQi - qi, preview.qi, shape + "剑气变化与预览相同");
        check(piles.equals(b), shape + "催动不增减任意牌堆或卡牌");
        checkEq(b.played, played + 1, shape + "主动技能仍计入本回合施术次数");
        if (ability) checkEq(b.cooldowns[id], ability->spell.cooldown + 1, shape + "主动技能进入独立完整回合冷却");
        if (shape == "sword") check(b.burn > 0 && b.burnTurns == 3, "火剑真实施加灼烧");
        if (shape == "seal") check(b.burn == 0 && b.burnTurns == 0, "阳火印真实消费灼烧预算");
        if (shape == "armor") check(b.shield > 0 && b.guard > 0, "阴甲真实获得护盾及本回合减伤");
        if (shape == "bell") check(preview.cost == 0 && b.mana > mana, "钟零费用恢复灵力");
        if (shape == "pearl") check(b.hp > hp && preview.damage == 0, "木珠真实治疗且没有虚构攻击");
        rejectedUnchanged(g, id, shape + "同回合重复催动冷却中的法宝");
    }
    {
        Game g = artifactGame(); Gear gear = artifactGear("sword"); artifactBattle(g, gear);
        Battle& b = *g.player.arts.battle; const string id = "artifact:" + gear.id;
        b.mana = 0; rejectedUnchanged(g, id, "灵力不足");
        b.mana = 12; b.played = 24; rejectedUnchanged(g, id, "达到24次施术上限");
        b.played = 0; g.player.arts.equipped.clear(); rejectedUnchanged(g, id, "未装备法宝");
        check(!battlePreview(g.player, b, id).valid, "未装备法宝不能取得有效战斗预览");
        g.player.arts.equipped[gear.slot] = gear.id; g.player.arts.gear[0].crafted = false;
        rejectedUnchanged(g, id, "坊市购入法器冒充主动法宝");
        g.player.arts.gear[0].crafted = true; g.player.stage = 0; g.player.arts.gear[0].grade = 3;
        rejectedUnchanged(g, id, "高于当前artTier法宝");
        check(!battlePreview(g.player, b, id).valid, "过阶法宝不能取得有效战斗预览");
        rejectedUnchanged(g, "artifact:missing", "不存在的法宝标识");
    }
    {
        Game g = artifactGame(); Gear bell = artifactGear("bell"); artifactBattle(g, bell);
        const string id = "artifact:" + bell.id; Battle& b = *g.player.arts.battle; b.mana = 1;
        battleTurn(g, id); const int mana = b.mana;
        rejectedUnchanged(g, id, "零费钟再次回灵");
        checkEq(b.mana, mana, "零费钟不会在同回合无限增加灵力");
        Game loaded;
        if (loadArtifact(g, loaded, "法宝冷却中的真实战斗可读档")) {
            checkEq(loaded.player.arts.battle->cooldowns[id], 3, "读档完整保留钟的独立冷却");
            rejectedUnchanged(loaded, id, "读档后仍处于法宝冷却");
            for (int expected : {2, 1, 0}) {
                loaded.player.arts.battle->BeastActions.clear();
                loaded.player.arts.battle->intentKind = "guard"; battleTurn(loaded, "end");
                checkEq(loaded.player.arts.battle->cooldowns[id], expected, "法宝冷却每完整回合只递减一次");
                if (expected > 0) rejectedUnchanged(loaded, id, "等待不足完整冷却回合");
            }
            check(battleTurn(loaded, id).ok, "完整冷却后法宝重新可以催动");
        }
    }
    {
        for (const string dao : {"火", "木", "水", "神", "土", "剑"}) {
            Game yang = artifactGame(), yin = artifactGame();
            artifactBattle(yang, artifactGear("sword", dao));
            artifactBattle(yin, artifactGear("sword", dao, true));
            const string id = "artifact:artifact-test-sword";
            const auto plain = battlePreview(yang.player, *yang.player.arts.battle, id);
            const auto shadow = battlePreview(yin.player, *yin.player.arts.battle, id);
            battleTurn(yang, id); battleTurn(yin, id);
            check(shadow.damage < plain.damage, dao + "阴剑直伤低于同材阳剑");
            check(elementalState(*yin.player.arts.battle, dao) > elementalState(*yang.player.arts.battle, dao),
                  dao + "阴剑在真实底层施加更多对应状态");
            checkStr(yin.player.arts.battle->log.back(), shadow.text, dao + "法宝真实日志与共用状态预览一致");
        }
        Game gold = artifactGame(); artifactBattle(gold, artifactGear("sword", "金", true));
        battleTurn(gold, "artifact:artifact-test-sword");
        check(gold.player.arts.battle->charge > 0, "阴金剑较低直伤换取重势而非空倍率");
        Game qi = artifactGame(); artifactBattle(qi, artifactGear("sword", "混元", true));
        qi.player.arts.battle->mana = 2; battleTurn(qi, "artifact:artifact-test-sword");
        checkEq(qi.player.arts.battle->mana, 1, "陰混元剑实际支付2灵力后返还1灵力");
    }
    {
        for (const string dao : {"火", "木", "水", "神", "土"}) {
            Game g = artifactGame(); artifactBattle(g, artifactGear("seal", dao));
            Battle& b = *g.player.arts.battle; b.burn = 90; b.burnTurns = 2;
            b.poison = 9; b.poisonTurns = 1; b.water = 4; b.soul = 4; b.charge = 3;
            if (dao == "火") g.player.arts.nodes = {"3-5"};
            if (dao == "水") g.player.arts.nodes = {"2-4", "2-5"};
            const string id = "artifact:artifact-test-seal";
            const auto preview = battlePreview(g.player, b, id); const int enemyHp = b.enemyHp;
            check(battleTurn(g, id).ok, dao + "阳印实际衔接已有大道状态");
            checkEq(enemyHp - b.enemyHp, preview.damage, dao + "状态爆发配合悟道的预览与实际一致");
            if (dao == "火") check(b.burn == 0 && b.burnTurns == 0, "火印引爆并清除剩余灼烧");
            if (dao == "木") check(b.poison == 0 && b.poisonTurns == 0, "木印消费高于六层的毒");
            if (dao == "水") checkEq(b.water, 2, "水印爆发沿用留水悟道保留一半水势");
            if (dao == "神") checkEq(b.soul, 0, "神印消费全部震荡");
            if (dao == "土") checkEq(b.charge, 0, "土印消费全部重势");
        }
        Game plain = artifactGame(), fire = artifactGame();
        fire.player.arts.nodes = {"3-2", "3-4"};
        artifactBattle(plain, artifactGear("sword", "火")); artifactBattle(fire, artifactGear("sword", "火"));
        battleTurn(plain, "artifact:artifact-test-sword"); battleTurn(fire, "artifact:artifact-test-sword");
        checkEq(fire.player.arts.battle->burn, plain.player.arts.battle->burn, "火道与薪火尚未实际跳焰时不即时增强法宝灼烧");
        for (int Round = 0; Round < 2; ++Round) {
            for (Game* Encounter : {&plain, &fire}) {
                Encounter->player.arts.battle->BeastActions.clear();
                Encounter->player.arts.battle->intentKind = "guard";
                check(battleTurn(*Encounter, "end").ok, "法宝灼烧经真实回合结算并等待冷却");
            }
        }
        checkEq(fire.player.arts.battle->FireGrowthRounds, 2, "法宝灼烧真实兑现两轮火道成长");
        const int PlainBurn = plain.player.arts.battle->burn, FireBurn = fire.player.arts.battle->burn;
        check(battleTurn(plain, "artifact:artifact-test-sword").ok &&
              battleTurn(fire, "artifact:artifact-test-sword").ok, "法宝冷却后可再次真实叠焰");
        check(fire.player.arts.battle->burn - FireBurn > plain.player.arts.battle->burn - PlainBurn,
              "法宝新增灼烧受到已经兑现的火道与薪火成长强化");
        Game wood = artifactGame(); wood.player.arts.nodes = {"1-4"};
        artifactBattle(wood, artifactGear("sword", "木", true)); const int hp = wood.player.arts.battle->hp;
        battleTurn(wood, "artifact:artifact-test-sword");
        check(wood.player.arts.battle->hp > hp, "阴木剑多施毒真实触发毒疗悟道");
    }
    {
        Game g = artifactGame(); Gear old = artifactGear("bell"); old.shape.clear();
        old.name = "寒铁钟"; g.player.arts.gear = {old}; g.player.arts.equipped = {{old.slot, old.id}};
        checkStr(artifactShape(old), "bell", "旧自炼钟按器名恢复钟型而非默认甲");
        json::Value document = json::Value::parse(saveToJson(g));
        auto player = document["player"]; auto arts = player["arts"]; auto gear = arts["gear"];
        gear.asArray()[0].set("shape", json::Value("")); arts.set("gear", gear); player.set("arts", arts); document.set("player", player);
        Game loaded; string error;
        const bool oldLoaded = loadFromJson(document.dump(), loaded, &error);
        check(oldLoaded, "缺少器型字段的旧自炼钟可迁移读回");
        if (oldLoaded) {
            checkStr(loaded.player.arts.gear[0].shape, "bell", "旧钟迁移后持久保存正确shape");
            const auto abilities = equippedArtifactAbilities(loaded.player);
            check(abilities.size() == 1 && abilities[0].spell.kind == "mana", "旧钟恢复正确回灵能力");
        }
        gear.asArray()[0].set("shape", json::Value("seal")); arts.set("gear", gear); player.set("arts", arts); document.set("player", player);
        const string prior = saveToJson(loaded);
        check(!loadFromJson(document.dump(), loaded, &error), "将甲位钟篡改为武器印型的存档被拒绝");
        check(saveToJson(loaded) == prior, "器型篡改读档失败不污染目标Game");
    }
    {
        vector<Gear> made;
        for (const string material : {"iron", "copper"}) {
            Game g = artifactGame(); g.player.arts.exp["器"] = 100;
            g.player.arts.materials[material] = 10;
            const string recipe = material + "|" + material + "|" + material + "|sword";
            const auto forge = artForgeInfo(g.player, material, material, material, "sword");
            checkStr(forge.shapeId, "sword", "炼器预览冻结完整器型ID");
            check(ecoActionError(g, "forge", recipe).empty(), "真实日程炼器接受合法已持有材料");
            const auto prior = g.player.arts.gear.size(); ecoBegin(g, "forge", recipe);
            check(g.job.has_value(), "炼器实际创建耗时工作");
            if (g.job) {
                g.job->chance = 1;
                for (int i = 0; i < 30 && g.job && !g.ended && !g.pending; ++i) calendarRun(g, 30);
                check(!g.job && !g.ended && g.player.arts.gear.size() == prior + 1, "日历真实完成炼器并产生法宝");
            }
            if (g.player.arts.gear.size() == prior + 1) {
                Gear actual = g.player.arts.gear.back(); made.push_back(actual);
                checkStr(actual.shape, "sword", "真实炼器产物持久保存器型ID");
                check(actual.crafted && actual.grade == forge.grade && actual.element == forge.mainElement,
                      "真实炼器产物保存材料品阶、属性与自炼身份");
                artifactBattle(g, actual); const string id = "artifact:" + actual.id;
                const auto preview = battlePreview(g.player, *g.player.arts.battle, id);
                const int hp = g.player.arts.battle->enemyHp;
                check(battleTurn(g, id).ok, "真实炼器产物装备后无需卡牌即可催动");
                checkEq(hp - g.player.arts.battle->enemyHp, preview.damage, "真实产物的主动伤害符合预览");
            }
        }
        check(made.size() == 2 && made[0].grade == 1 && made[1].grade == 2, "一二阶真实材料产出对应不同品质法宝");
        if (made.size() == 2) {
            Gear high = made[0]; high.grade = made[1].grade; high.bonus = made[1].bonus;
            check(artifactAbility(high).spell.scale > artifactAbility(made[0]).spell.scale,
                  "同属性器型提高材料品质实际增强主动能力系数");
        }
    }
}

} // namespace nztest
