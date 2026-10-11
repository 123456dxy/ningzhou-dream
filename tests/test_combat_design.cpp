// 功法、悟道、神通必须组成实际触发链；建议中的每个 ID 都能使用。
#include "harness.h"

#include <algorithm>
#include <set>

#include "core/arts.h"
#include "core/battle.h"
#include "core/sim.h"

using namespace nz;

namespace nztest {

void runCombatDesignTests() {
    std::printf("\n-- 流派组合与功法触发回归 --\n");
    {
        checkEq(artBuilds().size(), 8, "八种流派提供完整组合建议");
        std::set<string> ids;
        for (const auto& build : artBuilds()) {
            bool valid = ids.insert(build.id).second && daoIndex(build.dao) >= 0 &&
                         !build.desc.empty() && !build.manuals.empty() &&
                         !build.spells.empty() && !build.nodes.empty();
            for (const auto& id : build.manuals) {
                ManualInfo info;
                valid = findManualInfo(id, info) && valid;
            }
            for (const auto& id : build.spells) valid = battleSpell(id) && valid;
            for (const auto& id : build.nodes) valid = findNode(id) && valid;
            check(valid, build.name + "全部引用可查询的实际功法、神通与节点");
        }
    }
    {
        Arts main;
        main.main = "gold"; main.manuals = {{"gold", 5}};
        ArtMods full = artModsOf(main);
        Arts aux = main; aux.main = ""; aux.aux = {"gold"};
        ArtMods half = artModsOf(aux);
        checkNear(full.discardShield, half.discardShield * 2,
                  "金虹诀辅修的弃牌凝盾系数为主修一半");
        checkNear(full.discardPower, half.discardPower * 2,
                  "金虹诀辅修的锐势系数为主修一半");
        check(full.discardShield > 0 && full.waterMana == 0 && full.poisonHeal == 0,
              "金虹诀只提供自身触发方向而不会启用所有流派");
        ManualInfo info;
        check(findManualInfo("gold", info) && info.effect.find("弃牌") != string::npos,
              "功法查询包含真实弃牌触发说明");
        check(findManualInfo("fetal", info) && info.effect.find("累计打出3张牌") != string::npos &&
              info.effect.find("第三回合") == string::npos,
              "胎息经查询同步三牌调息机制");
    }
    {
        Player full; full.stage = 6;
        full.arts.main = "mind"; full.arts.manuals = {{"mind", 5}};
        Player half = full; half.arts.main = ""; half.arts.aux = {"mind"};
        const auto* draw = battleSpell("skim");
        check(draw && battleCost(full, *draw) == 0 && battleCost(half, *draw) == 1,
              "养魂诀主修满层实际降低明心咒费用，半效未过整数阈值");

        full.arts.main = "wave"; full.arts.manuals = {{"wave", 5}};
        half = full; half.arts.main = ""; half.arts.aux = {"wave"};
        Battle b; b.max = 100; b.hp = 100; b.enemyMax = 10000; b.enemyHp = 10000;
        b.mana = 5; b.water = 6;
        const auto mainWave = battlePreview(full, b, "wavehit");
        const auto auxWave = battlePreview(half, b, "wavehit");
        check(mainWave.valid && auxWave.valid && mainWave.energy == auxWave.energy + 1,
              "浪击诀主修与辅修在实际水势返灵上有可验证区别");
        checkEq(b.water, 6, "流派预览不提前消耗真实水势");
    }
    {
        Arts fire; fire.nodes = {"3-4", "3-5"};
        ArtMods mods = artModsOf(fire);
        check(mods.burnStack > 0 && mods.burnExplosion > 0 && mods.discardShield == 0,
              "高阶火道改变灼烧积累与引爆而非开启其它道机制");
        checkNear(mods.cult, 1, "高阶节点不再统一提供修炼面板加成");
        checkNear(mods.attack, 1, "高阶节点不再统一提供攻击面板加成");
        checkNear(mods.hp, 1, "高阶节点不再统一提供气血面板加成");

        Player sword; sword.stage = 6; sword.arts.nodes = {"7-4"};
        Battle b; b.max = 100; b.hp = 100; b.enemyMax = 10000; b.enemyHp = 10000;
        b.mana = 12;
        const auto first = battlePreview(sword, b, "sword");
        b.swordSequence = 1;
        const auto second = battlePreview(sword, b, "sword");
        check(first.valid && second.damage > first.damage,
              "剑道真境要求连续剑招才在实际预览中追加伤害段");
    }
    {
        Game g = createGame(CreateOptions{});
        checkEq(g.player.arts.spells.size(), 4, "新档有四个额外神通");
        check(std::find(g.player.arts.spells.begin(), g.player.arts.spells.end(), "skim") !=
              g.player.arts.spells.end(), "新档带明心咒建立初始抽牌组合");
        check(!artInstant(g, "spell", "strike").ok &&
              !artInstant(g, "spell", "defend").ok,
              "固定灵击和守势不能被配装卸下");
        const auto worn = g.player.arts.spells;
        for (const auto& id : worn) check(artInstant(g, "spell", id).ok, "主动卸下" + id);
        ensureArts(g);
        check(g.player.arts.spells.empty(), "重复初始化保留主动卸空的配装");
        g.player.arts.spells = {"strike", "sword", "water", "wood", "sword", "defend"};
        ensureArts(g);
        checkEq(g.player.arts.spells.size(), 3, "旧档只去固定牌与重复牌，不擅自补新牌");

        g.player.stage = 3;
        g.player.arts.spells = {"sword", "water", "wood", "skim"};
        check(!artInstant(g, "spell", "gold").ok, "未领悟神通即使境界够也不能凭空装备");
        g.player.arts.knownSpells.insert(g.player.arts.knownSpells.end(), {"gold", "fire", "earth"});
        check(artInstant(g, "spell", "gold").ok, "境界提高后可配装第五个额外神通");
        check(artInstant(g, "spell", "fire").ok, "境界提高后可配装第六个额外神通");
        check(!artInstant(g, "spell", "earth").ok,
              "扩展后的神通槽仍严格受当前境界上限约束");
    }
}

} // namespace nztest
