// 功法、悟道、神通必须组成实际触发链；建议中的每个 ID 都能使用。
#include "harness.h"

#include <algorithm>
#include <set>

#include "core/adventure.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/sim.h"

using namespace nz;

namespace nztest {

// 用途：验证流派组件与真实新角初装；流程：统一规则测试入口；用法：隔离内存建角后调用；返回：无，失败计入harness。
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
        check(mods.BurnGrowthStackPerRound > 0 && mods.BurnGrowthExplosionPerRound > 0 && mods.discardShield == 0,
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
        // 真正新建角色保持吐纳一层、无悟道与装备，只更换四张合法初装神通。
        CreateOptions Options;
        Options.name = "初装回归"; Options.seed = 31;
        Options.traitsExplicit = true; Options.allocationExplicit = true;
        Options.allocation = {{"apt", 10}};
        Game Starter = createGame(Options);
        const vector<string> ExpectedSpells = {"gold", "fire", "water", "gather"};
        check(Starter.player.arts.spells == ExpectedSpells, "真实新档携带金芒、焰火、水盾、聚气初装");
        check(Starter.player.arts.main == "tuna" && Starter.player.arts.manuals.at("tuna") == 1 &&
              Starter.player.arts.aux.empty() && Starter.player.arts.nodes.empty() && Starter.player.arts.gear.empty(),
              "新初装不赠高层功法、悟道或额外装备");
        bool KeepsKnownSpells = true;
        for (const string SpellId : {"sword", "water", "wood", "skim", "refresh", "purge", "gold", "fire", "gather"})
            KeepsKnownSpells = KeepsKnownSpells && std::find(Starter.player.arts.knownSpells.begin(),
                Starter.player.arts.knownSpells.end(), SpellId) != Starter.player.arts.knownSpells.end();
        check(KeepsKnownSpells, "新档保留原六种已知神通并新增三种初装神通");
        const auto KnownBefore = Starter.player.arts.knownSpells;
        const auto RngBefore = Starter.rng;
        ensureArts(Starter); ensureArts(Starter);
        check(Starter.player.arts.spells == ExpectedSpells && Starter.player.arts.knownSpells == KnownBefore && Starter.rng == RngBefore,
              "重复ensureArts不替换初装、不重复添加已知神通、不推进随机数");

        Game Existing = Starter;
        const vector<string> PreviousLoadout = {"sword", "water", "wood", "skim"};
        Existing.player.arts.spells = PreviousLoadout;
        Existing.player.arts.knownSpells = {"sword", "water", "wood", "skim", "refresh", "purge"};
        ensureArts(Existing);
        check(Existing.player.arts.spells == PreviousLoadout && Existing.player.arts.knownSpells.size() == 6,
              "已初始化角色保留旧配装，不因新开局方案赠送或替换神通");

        // The starter assertions above retain the real new-life state; the
        // following encounter starts after handling its tournament invitation.
        Starter.pending.reset();
        adventureEvent(Starter, "bandit");
        check(adventureChoose(Starter, "fight") && battleActive(Starter), "真实山道劫修事件可用新初装开战");
        if (Starter.player.arts.battle) {
            const auto& BattleState = *Starter.player.arts.battle;
            map<string, int> CardCounts;
            std::set<string> CardUids;
            bool LegalDeck = true;
            for (const auto& Card : BattleState.deck) {
                ++CardCounts[Card.spell];
                const auto* Spell = battleSpell(Card.spell);
                const bool IsBasic = Card.spell == "strike" || Card.spell == "defend";
                LegalDeck = LegalDeck && CardUids.insert(Card.uid).second && Spell && Spell->stage <= Starter.player.stage &&
                    (IsBasic || std::find(Starter.player.arts.knownSpells.begin(), Starter.player.arts.knownSpells.end(), Card.spell) !=
                        Starter.player.arts.knownSpells.end());
            }
            check(LegalDeck && BattleState.deck.size() == 18 && CardCounts["strike"] == 5 && CardCounts["defend"] == 5 &&
                  CardCounts["gold"] == 2 && CardCounts["fire"] == 2 && CardCounts["water"] == 2 && CardCounts["gather"] == 2,
                  "实际开战牌堆为18张唯一UID，四初装各两张且都已学会并满足炼气境界");
        }
    }
    {
        Game g = createGame(CreateOptions{});
        checkEq(g.player.arts.spells.size(), 4, "新档有四个额外神通");
        check(std::find(g.player.arts.spells.begin(), g.player.arts.spells.end(), "gather") !=
              g.player.arts.spells.end(), "新档带聚气支持初始攻防与叠焰，已知明心咒可自行换装");
        g.pending.reset();
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
        check(!artInstant(g, "spell", "earth").ok, "未领悟神通即使境界够也不能凭空装备");
        g.player.arts.knownSpells.push_back("earth");
        check(artInstant(g, "spell", "gold").ok, "境界提高后可配装第五个额外神通");
        check(artInstant(g, "spell", "fire").ok, "境界提高后可配装第六个额外神通");
        check(!artInstant(g, "spell", "earth").ok,
              "扩展后的神通槽仍严格受当前境界上限约束");
    }
}

} // namespace nztest
