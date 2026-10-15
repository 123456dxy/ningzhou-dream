#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_lab.h"
#include "core/combat/fire_growth.h"
#include "core/combat/hand_rules.h"
#include "core/json.h"
#include "core/save.h"
#include "core/talents.h"
#include <algorithm>
#include <stdexcept>

namespace nztest {
namespace {
// 用途：复制正式试炼构筑形成独立规则夹具；流程：火道真实结算；返回：合法高境火修与普通修士，不读玩家文件。
nz::Game mfFireGrowthEncounter() {
    nz::BattleLab::Session Session;
    nz::BattleLab::Selection Selection;
    Selection.BuildId = "fire-stack"; Selection.Stage = 12; Selection.OpponentId = "lihuo";
    Selection.Seed = 317; Selection.LoadoutTier = "advanced";
    const auto Started = nz::BattleLab::mfStart(Session, Selection);
    if (!Started.Succeeded) throw std::logic_error(Started.Error);
    auto State = Session.CurrentGame();
    auto& Battle = *State.player.arts.battle;
    Battle.enemyMax = Battle.enemyHp = 1000000;
    return State;
}
// 用途：在规则夹具中指定一张已有真实牌；流程：精确预览/执行对照；返回：手牌UID，不伪造神通或修改伤害规则。
nz::string mfHoldFireGrowthCard(nz::Battle& State, const nz::string& SpellId) {
    for (const auto& Card : State.deck) {
        if (Card.spell != SpellId) continue;
        for (auto* Pile : {&State.hand, &State.draw, &State.discard, &State.exhausted}) std::erase(*Pile, Card.uid);
        if (State.hand.size() >= nz::BattleCards::MaximumHandSize) { State.discard.push_back(State.hand.back()); State.hand.pop_back(); }
        State.hand.push_back(Card.uid); State.cooldowns.erase(SpellId); State.mana = State.maxMana;
        return Card.uid;
    }
    throw std::logic_error("火道测试缺少真实牌：" + SpellId);
}
}

// 用途：验证有限本场成长代替即时叠乘；流程：定向规则/持久化回归；返回：无，失败记入统一框架。
void mfRunFireGrowthTests() {
    using namespace nz;
    {
        auto State = mfFireGrowthEncounter(); auto& Battle = *State.player.arts.battle;
        const auto Mods = talentMods(State.player);
        checkNear(Mods.BurnGrowthStackPerRound, FireGrowth::FlameNodeStackGainPerRound + FireGrowth::KindleNodeStackGainPerRound,
            "完整火道2/4层提供逐轮成长系数");
        checkNear(Mods.BurnGrowthExplosionPerRound, FireGrowth::ExplosionNodeGainPerRound, "爆焰提供逐轮引爆成长");
        Player WithoutDao = State.player; WithoutDao.arts.nodes.clear();
        checkNear(FireGrowth::mfBurnStackBonus(Mods, 0), talentMods(WithoutDao).burnStack,
            "开局仍有功法叠焰，但悟道不即时叠35%或翻倍");
        const auto First = battlePreview(State.player, Battle, "spitfire");
        const auto Uid = mfHoldFireGrowthCard(Battle, "spitfire");
        const int Before = Battle.enemyHp;
        check(battleTurn(State, Uid).ok, "实际施加第一份灼烧");
        checkEq(Before - Battle.enemyHp, First.damage, "首张施法预览与真实气血伤害一致");
        checkEq(Battle.FireGrowthRounds, 0, "多打牌本身不增长焰势");
        Battle.enemyCards.planned.clear();
        check(battleTurn(State, "end").ok, "实际经过一次灼烧结算");
        checkEq(Battle.FireGrowthRounds, 1, "真实灼烧一轮只成长一次");
        const auto Snapshot = saveToJson(State);
        const auto Grown = battlePreview(State.player, Battle, "spitfire");
        static_cast<void>(battleEnemyPreview(State));
        check(saveToJson(State) == Snapshot, "双方预览不提前积累焰势或改变RNG");
        check(FireGrowth::mfBurnStackBonus(Mods, 1) > FireGrowth::mfBurnStackBonus(Mods, 0) && Grown.valid,
            "后续叠焰受已兑现回合成长强化");
        for (int Round = 0; Round < 8; ++Round) {
            Battle.burn = 25; Battle.burnTurns = 3; Battle.enemyCards.planned.clear(); Battle.hp = Battle.max;
            check(battleTurn(State, "end").ok, "真实完整回合推进火道成长");
        }
        checkEq(Battle.FireGrowthRounds, FireGrowth::MaximumGrowthRounds, "长战斗成长封顶五轮");
        Battle.burn = 0; Battle.burnTurns = 0; Battle.enemyCards.planned.clear();
        check(battleTurn(State, "end").ok, "无灼烧回合可正常结束");
        checkEq(Battle.FireGrowthRounds, 5, "无灼烧不增长且已积累成长不倒退");
        checkNear(FireGrowth::mfExplosionMultiplier(Mods, 5) - FireGrowth::mfExplosionMultiplier(Mods, 0), .5,
            "爆焰五轮才获得完整50%增幅");
        Game Restored; string Error;
        check(loadDetachedFromJson(saveToJson(State), Restored, &Error), "本场火道成长可存读往返：" + Error);
        checkEq(Restored.player.arts.battle ? Restored.player.arts.battle->FireGrowthRounds : -1, 5, "存读不清空或重发成长");
        for (const auto Invalid : {json::Value(-1), json::Value(6), json::Value(.5), json::Value(true), json::Value{}}) {
            auto Save = json::Value::parse(saveToJson(State)); auto Player = Save["player"]; auto Arts = Player["arts"]; auto SavedBattle = Arts["battle"];
            SavedBattle.set("fireGrowthRounds", Invalid); Arts.set("battle", SavedBattle); Player.set("arts", Arts); Save.set("player", Player);
            const auto BeforeLoad = saveToJson(Restored);
            check(!loadDetachedFromJson(Save.dump(), Restored, &Error), "非法焰势严格拒绝");
            check(saveToJson(Restored) == BeforeLoad, "非法焰势导入不覆盖当前角色");
        }
    }
    {
        auto State = mfFireGrowthEncounter(); auto& Battle = *State.player.arts.battle;
        const auto* Siphon = battleSpell("fireSiphon");
        check(Siphon && battleCost(State.player, *Siphon) == 3, "噬焰回生真实消耗3灵力，费用不被旧折半误成2");
        Battle.hp = 1; Battle.burn = 40; Battle.burnTurns = 3; Battle.FireGrowthRounds = 3;
        Battle.enemyShield = 200; Battle.enemyWeaken = 2; Battle.discardPower = .3;
        const auto Uid = mfHoldFireGrowthCard(Battle, "fireSiphon");
        const auto Expected = FireGrowth::mfExplosionAmount(talentMods(State.player), 3, 40, 3);
        const auto Snapshot = saveToJson(State);
        const auto Preview = battlePreview(State.player, Battle, "fireSiphon");
        checkEq(Preview.RawDamage, 0, "噬焰回生打出伤害为0");
        checkEq(Preview.damage, 0, "噬焰回生实际伤害为0");
        checkEq(Preview.healPotential, Expected, "转换池与引爆共用倍率，不加入直伤/治疗乘区");
        check(saveToJson(State) == Snapshot, "转换治疗预览不消耗灼烧/灵力/成长");
        const int EnemyHp = Battle.enemyHp, Mana = Battle.mana;
        check(battleTurn(State, Uid).ok, "真实施放噬焰回生");
        checkEq(Battle.hp - 1, Preview.heal, "实际转换治疗等于预览");
        checkEq(Mana - Battle.mana, 3, "实际扣除3灵力");
        checkEq(Battle.enemyHp, EnemyHp, "不伤害对方气血");
        checkEq(Battle.enemyShield, 200, "不伤害对方护盾");
        checkEq(Battle.burn, 0, "转换后清除全部灼烧");
        checkEq(Battle.burnTurns, 0, "转换后清除灼烧回合");
        checkEq(Battle.FireGrowthRounds, 3, "转换不赠送或清空成长");
        checkEq(Battle.enemyWeaken, 2, "非攻击不消耗待触发虚弱");
        checkNear(Battle.discardPower, .3, "非攻击不消耗弃牌锐势");
        check(Battle.cooldowns.at("fireSiphon") > 0, "转换治疗遵守冷却");
        auto Full = mfFireGrowthEncounter(); auto& FullBattle = *Full.player.arts.battle;
        FullBattle.hp = FullBattle.max; FullBattle.burn = 40; FullBattle.burnTurns = 3;
        const auto FullUid = mfHoldFireGrowthCard(FullBattle, "fireSiphon");
        checkEq(battlePreview(Full.player, FullBattle, "fireSiphon").heal, 0, "满血预览实际回复0但仍显示转换潜力");
        check(mfBattleNpcCardPriority(Full.player, FullBattle, *Siphon) < 0, "满血NPC不浪费火池");
        check(battleTurn(Full, FullUid).ok && FullBattle.hp == FullBattle.max && FullBattle.burn == 0,
            "满血施放不溢出，仍按说明消费全部灼烧");
        auto Empty = mfFireGrowthEncounter(); auto& EmptyBattle = *Empty.player.arts.battle;
        EmptyBattle.hp = 1; EmptyBattle.burn = EmptyBattle.burnTurns = 0;
        const auto EmptyUid = mfHoldFireGrowthCard(EmptyBattle, "fireSiphon");
        checkEq(battlePreview(Empty.player, EmptyBattle, "fireSiphon").healPotential, 0, "无灼烧无法凭空治疗");
        EmptyBattle.mana = 2;
        const auto BeforeReject = saveToJson(Empty);
        check(!battleTurn(Empty, EmptyUid).ok, "两点灵力不能施放3费转换");
        check(saveToJson(Empty) == BeforeReject, "灵力不足拒绝不部分消耗");
        check(mfBattleNpcCardPriority(State.player, Battle, *Siphon) < 0, "火池已耗尽时NPC不空放");
        Battle.burn = 10; Battle.burnTurns = 2; Battle.hp = 1;
        check(mfBattleNpcCardPriority(State.player, Battle, *Siphon) > 95, "受伤NPC有火池时优先转换求生");
    }
    {
        auto State = mfFireGrowthEncounter(); auto& Battle = *State.player.arts.battle;
        for (int Level = 1; Level <= 5; ++Level) {
            const auto Node = "3-" + std::to_string(Level);
            if (std::find(Battle.enemyCards.nodes.begin(), Battle.enemyCards.nodes.end(), Node) == Battle.enemyCards.nodes.end())
                Battle.enemyCards.nodes.push_back(Node);
        }
        Battle.enemyCards.burn = 20; Battle.enemyCards.burnTurns = 3; Battle.enemyCards.planned.clear();
        check(battleTurn(State, "end").ok, "NPC所施灼烧走真实回合结算");
        checkEq(Battle.enemyCards.FireGrowthRounds, 1, "NPC同样靠本场已兑现灼烧成长");
        const auto NewBattle = battleReferenceState(State.player);
        checkEq(NewBattle.FireGrowthRounds, 0, "新战斗不继承上场焰势");
    }
}
}
