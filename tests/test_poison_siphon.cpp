#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/artifact.h"
#include "core/combat/battle_lab.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/poison_rules.h"
#include "core/content/game_ids.h"
#include "core/friendship.h"
#include "core/interface/views.h"
#include "core/reward_receipt.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/talents.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

Game mfSiphonEncounter(int Stage = 12) {
    Game State;
    State.seed = State.rng = 20832;
    State.player.name = "借毒回春回归";
    State.player.stage = State.player.highestStage = Stage;
    State.player.lifespan = realmOf(Stage).life;
    State.clock.ageBase = State.player.age;
    State.player.talent.clear(); State.player.traits.clear();
    State.player.arts.main.clear();
    State.player.arts.spells = {"corrode", "poisonBloom", "poisonSiphon", "wood"};
    State.player.arts.knownSpells = State.player.arts.spells;
    State.player.health = mfPlayerHealthMaximum(State.player);
    EnemySpec Opponent;
    Opponent.name = "借毒靶"; Opponent.stage = 0; Opponent.speed = 0;
    battleStart(State, Opponent);
    auto& Combat = *State.player.arts.battle;
    Combat.enemyHp = Combat.enemyMax = 100000;
    Combat.hp = 500; Combat.max = 1000; Combat.mana = 12;
    Combat.enemyCards.enabled = false;
    Combat.intentKind = "guard"; Combat.intentScale = 0;
    Combat.poison = 9; Combat.poisonDamage = 17; Combat.poisonTurns = 1;
    return State;
}

string mfHoldSiphonCard(Battle& Combat) {
    for (const auto& Entry : Combat.deck) {
        if (Entry.spell != "poisonSiphon") continue;
        for (auto* Pile : {&Combat.hand, &Combat.draw, &Combat.discard})
            Pile->erase(std::remove(Pile->begin(), Pile->end(), Entry.uid), Pile->end());
        Combat.hand.push_back(Entry.uid);
        return Entry.uid;
    }
    return "";
}
} // namespace

void mfRunPoisonSiphonTests() {
    std::printf("\n--- 借毒回春替换普通毒爆 ---\n");
    check(battleSpell("poisonBurst") == nullptr, "普通引毒咒退出权威神通目录");
    const auto* Siphon = battleSpell("poisonSiphon");
    check(Siphon != nullptr, "借毒回春进入真实神通目录");
    if (Siphon) {
        checkStr(Siphon->name, "借毒回春", "新神通使用独立名称");
        checkStr(Siphon->kind, "poisonSiphon", "新神通使用独立结算类型");
        checkEq(Siphon->stage, 3, "借毒回春筑基可学");
        checkEq(Siphon->cooldown, 2, "借毒回春冷却两个完整自身回合");
        checkEq(battleCost(Player{}, *Siphon), 2, "借毒回春实际费用2灵力");
        for (int Stage : {3, 6, 9, 12}) for (int Layers : {1, 3, 99}) {
            auto State = mfSiphonEncounter(Stage);
            auto& Combat = *State.player.arts.battle;
            Combat.poison = Layers; Combat.poisonDamage = -1;
            Combat.enemyWeaken = 2; Combat.buff = .6; Combat.discardPower = .3; Combat.last = "水";
            State.player.arts.main = "wood"; State.player.arts.manuals["wood"] = 5;
            State.player.arts.aux = {"evergreen"}; State.player.arts.manuals["evergreen"] = 5;
            State.player.arts.nodes = {"1-1", "1-4", "9-4", "9-5"};
            check(talentMods(State.player).heal > 1 && talentMods(State.player).poisonHeal > 0,
                "倍率隔离用例确实携带正治疗增幅与施毒回春");
            const auto Uid = mfHoldSiphonCard(Combat);
            const auto Before = saveToJson(State);
            const auto Preview = battlePreview(State.player, Combat, "poisonSiphon");
            const int Expected = std::min(jsRound(mfBattleDamageBasis(Stage) * .04 * Layers), 120);
            checkStr(saveToJson(State), Before, "借毒预览不改变资源、牌堆或RNG");
            checkEq(Preview.healPotential, Expected, "本术潜力只读取境界基数、毒层与12%上限");
            checkEq(Preview.heal, Expected, "增伤、治疗与相生修正不放大借毒本术");
            checkEq(Preview.damage, 0, "借毒回春预览没有直接伤害");
            check(battleTurn(State, Uid).ok, "真实手牌施放借毒回春");
            checkEq(Combat.hp, 500 + Expected, "实际治疗量与本术预览完全一致");
            checkEq(Combat.mana, 10, "借毒只扣2灵力，不获得相生返灵");
            checkEq(Combat.enemyHp, 100000, "借毒不造成直接伤害");
            checkEq(Combat.poison, Layers, "借毒不消耗毒层");
            checkEq(Combat.poisonDamage, -1, "借毒不重写精确或缺失毒缓存");
            checkEq(Combat.poisonTurns, 1, "借毒不修改本场活跃标记");
            checkEq(Combat.enemyWeaken, 2, "借毒不消耗扰灵");
            checkNear(Combat.buff, .6, "借毒不消耗既有增伤");
            checkNear(Combat.discardPower, .3, "借毒不消耗锐势");
            checkStr(Combat.last, "木", "成功借毒正常记录木行");
        }
        for (int Case = 0; Case < 4; ++Case) {
            auto State = mfSiphonEncounter();
            auto& Combat = *State.player.arts.battle;
            if (Case == 0) Combat.poison = 0;
            if (Case == 1) Combat.poisonTurns = 0;
            if (Case == 2) Combat.hp = Combat.max;
            if (Case == 3) { Combat.max = 7; Combat.hp = 1; }
            const auto Uid = mfHoldSiphonCard(Combat);
            const auto Before = saveToJson(State);
            const auto View = api::view::battle(State);
            bool Found = false;
            for (const auto& Card : View["hand"].asArray()) if (Card["uid"].asString() == Uid) {
                Found = true;
                check(!Card["playable"].asBool() && !Card["reason"].asString().empty(),
                    "无毒、未活跃、满血或零潜力时真实手牌禁用并解释");
            }
            check(Found, "借毒手牌实例在真实API中可见");
            check(!battleTurn(State, Uid).ok, "不满足条件时借毒在动作前拒绝");
            checkStr(saveToJson(State), Before, "借毒拒绝不扣费、不移卡、不改冷却出牌数或RNG");
        }
        {
            auto State = mfSiphonEncounter(); auto& Combat = *State.player.arts.battle;
            Combat.hp = 995;
            const auto Preview = battlePreview(State.player, Combat, "poisonSiphon");
            checkEq(Preview.healPotential, 120, "缺血少于本术潜力仍显示完整潜力");
            checkEq(Preview.heal, 5, "实际借毒治疗不超过缺血");
            check(battleTurn(State, mfHoldSiphonCard(Combat)).ok, "少量缺血可施放借毒");
            checkEq(Combat.hp, 1000, "借毒不会溢出最大气血");
        }
        {
            auto State = mfSiphonEncounter(); auto& Combat = *State.player.arts.battle;
            Combat.played = 2; State.player.arts.nodes = {"6-4"};
            const auto Preview = battlePreview(State.player, Combat, "poisonSiphon");
            checkEq(Preview.healPotential, 120, "本术12%上限不包含独立第三牌回春");
            checkEq(Preview.heal, 140, "独立第三牌节点按原规则另回复2%气血");
            check(battleTurn(State, mfHoldSiphonCard(Combat)).ok, "第三牌可真实触发借毒及独立节点");
            checkEq(Combat.hp, 640, "独立第三牌回春没有被本术上限误截断");
        }
        {
            auto State = mfSiphonEncounter(); auto& Combat = *State.player.arts.battle;
            check(battleTurn(State, mfHoldSiphonCard(Combat)).ok, "T1借毒施放成功");
            checkEq(Combat.cooldowns.at("poisonSiphon"), 3, "当前回合冷却补偿后内部记录3");
            for (int Round = 0; Round < 2; ++Round) {
                battleTurn(State, "end"); mfHoldSiphonCard(Combat);
                const auto Before = saveToJson(State);
                check(!battleTurn(State, "poisonSiphon").ok, "T2与T3完整自身回合禁止借毒");
                checkStr(saveToJson(State), Before, "冷却借毒拒绝无副作用");
            }
            battleTurn(State, "end"); Combat.mana = 12;
            check(battleTurn(State, mfHoldSiphonCard(Combat)).ok, "T4借毒重新可用");
        }
        {
            Player Foe; Foe.stage = 12;
            Battle View; View.max = 1000; View.hp = 500; View.poison = 9; View.poisonTurns = 1;
            check(mfBattleNpcCardPriority(Foe, View, *Siphon) > 0, "受伤且对方有毒时NPC愿意借毒治疗");
            View.hp = View.max;
            check(mfBattleNpcCardPriority(Foe, View, *Siphon) < 0, "满血NPC不会浪费借毒");
            View.hp = 500; View.poisonTurns = 0;
            check(mfBattleNpcCardPriority(Foe, View, *Siphon) < 0, "无活跃毒的NPC不会选择借毒");
        }
        {
            BattleLab::Session Session; BattleLab::Selection Request;
            Request.BuildId = "wood-poison"; Request.Stage = 12; Request.OpponentId = "zhushan-complete";
            check(BattleLab::mfStart(Session, Request).Succeeded, "借毒镜像使用真实完整竹山对手");
            auto& State = Session.CurrentGame(); auto& Combat = *State.player.arts.battle;
            auto& Enemy = Combat.enemyCards;
            State.player.arts.main.clear(); State.player.arts.aux.clear();
            State.player.arts.manuals.clear(); State.player.arts.nodes.clear();
            State.player.talent.clear(); State.player.traits.clear();
            Combat.enemyHp = Combat.enemyMax / 2;
            Enemy.nodes.clear(); Enemy.main.clear(); Enemy.aux.clear(); Enemy.manuals.clear();
            Enemy.poison = 8; Enemy.poisonDamage = 17; Enemy.poisonTurns = 1;
            Combat.playerPoison = 17; Combat.playerPoisonTurns = 1;
            Enemy.draw.insert(Enemy.draw.end(), Enemy.hand.begin(), Enemy.hand.end()); Enemy.hand.clear();
            Enemy.planned.clear();
            for (const auto& Card : Enemy.deck) if (Card.spell == "poisonSiphon") {
                Enemy.draw.erase(std::remove(Enemy.draw.begin(), Enemy.draw.end(), Card.uid), Enemy.draw.end());
                Enemy.hand.push_back(Card.uid); Enemy.planned.push_back(Card.uid); break;
            }
            check(!Enemy.planned.empty(), "NPC真实牌组确有新借毒卡实例");
            Enemy.mana = 12;
            const auto Before = saveToJson(State); const auto Preview = battleEnemyPreview(State);
            checkStr(saveToJson(State), Before, "敌方借毒预演不修改真实状态或RNG");
            const int Hp = Combat.hp, EnemyHp = Combat.enemyHp;
            check(battleTurn(State, "end").ok, "NPC通过真实计划施放借毒回春");
            checkEq(Combat.enemyHp - EnemyHp, Preview.heal, "敌方借毒镜像治疗与权威预览一致");
            checkEq(Hp - Combat.hp, 17, "敌方借毒本身无直伤，仅结算已有17点自然毒伤");
            checkEq(Enemy.poison, 8, "敌方借毒不消耗玩家身上毒层");
            checkEq(Enemy.poisonDamage, 17, "敌方借毒不重算原生精确毒缓存");
            checkEq(Combat.playerPoison, 17, "敌方借毒保持玩家受毒跳伤");
            checkEq(Combat.playerPoisonTurns, 1, "敌方借毒保持永久毒活跃标记");
        }
        {
            auto State = mfSiphonEncounter(); auto& Combat = *State.player.arts.battle;
            State.player.arts.nodes = {"10-5"};
            Combat.playerPoison = 7; Combat.playerPoisonTurns = 1;
            check(battleTurn(State, mfHoldSiphonCard(Combat)).ok, "丹道清浊节点下可借毒治疗");
            checkEq(Combat.playerPoison, 7, "借毒不冒充普通治疗触发丹道清自身毒");
            checkEq(Combat.playerPoisonTurns, 1, "借毒不清除自身活跃毒标记");
        }
    }
    for (int Stage : {0, 3, 12}) for (const string Tier : {"initial", "advanced"}) {
        BattleLab::Session Session; BattleLab::Selection Request;
        Request.BuildId = "wood-poison"; Request.Stage = Stage; Request.LoadoutTier = Tier;
        check(BattleLab::mfStart(Session, Request).Succeeded, "真实木毒构筑可建立");
        const auto& Arts = Session.CurrentGame().player.arts;
        const bool Found = std::find(Arts.spells.begin(), Arts.spells.end(), "poisonSiphon") != Arts.spells.end();
        check(Found == (Stage >= 3), "初成与完整木毒按境界实际装备借毒回春");
        check(std::find(Arts.knownSpells.begin(), Arts.knownSpells.end(), "poisonBurst") == Arts.knownSpells.end(),
            "真实木毒构筑不再学会旧引毒咒");
    }
    {
        Game State; State.player.sect = State.player.location = "zhushan";
        State.player.stage = State.player.highestStage = 3; State.economy.contribution = 10000;
        RewardReceipt Receipt(State);
        const auto Quote = shop::librarySpellSource(State, "poisonSiphon");
        check(Quote.valid && Quote.enabled && Quote.points > 0, "宗门藏经阁真实提供借毒回春");
        if (Quote.valid && Quote.enabled) {
            const int Before = State.economy.contribution;
            check(shop::librarySpellExchange(State, "poisonSiphon").ok, "通过正式贡献兑换借毒回春");
            checkEq(State.economy.contribution, Before - Quote.points, "学习费用与后端报价相同");
            check(Receipt.changes(State).find("学会神通「借毒回春」") != string::npos, "真实学习奖励回执解析C++新增神通名称");
        }
        check(!shop::librarySpellSource(State, "poisonBurst").valid, "旧引毒咒不能继续从宗门获取");
    }
    {
        bool Offered = false, Retired = false, Illegal = false, BasicKnowledge = false;
        for (int Seed = 1; Seed <= 256; ++Seed) {
            Game State; State.seed = State.rng = Seed;
            Npc Friend; Friend.sect = "zhushan"; Friend.stage = 3;
            friendship::ensureNpcCultivation(State, Friend);
            Offered = Offered || std::find(Friend.spells.begin(), Friend.spells.end(), "poisonSiphon") != Friend.spells.end();
            Retired = Retired || std::find(Friend.spells.begin(), Friend.spells.end(), "poisonBurst") != Friend.spells.end();
            for (const auto& Id : Friend.spells) { const auto* Spell = battleSpell(Id); Illegal = Illegal || !Spell || Spell->stage > Friend.stage; }
            Npc Wanderer; Wanderer.stage = 0;
            friendship::ensureNpcCultivation(State, Wanderer);
            for (const auto& Id : Wanderer.spells) BasicKnowledge = BasicKnowledge || Id == "strike" || Id == "defend";
        }
        check(Offered && !Retired && !Illegal, "仙友真实养成使用合格权威目录，可获得借毒而不抽旧神通");
        check(!BasicKnowledge, "切换权威目录不把内置普通攻防当作可获取神通知识");
    }
    {
        Gear Legacy; Legacy.id = "legacy-wood-seal"; Legacy.name = "旧木法印";
        Legacy.crafted = true; Legacy.shape = "seal"; Legacy.slot = "weapon";
        Legacy.element = "木"; Legacy.polarity = "天阳"; Legacy.grade = 2;
        const auto Ability = artifactAbility(Legacy);
        check(Ability.valid && content::parseSpellKind(Ability.spell.kind) == content::SpellKind::PoisonBurst,
            "普通毒爆下架后旧版阳木法印仍保留原独立结算类型");
    }
    {
        const auto* Wood4 = findNode("1-4"); const auto* Wood5 = findNode("1-5");
        const auto* Sword4 = findNode("7-4"); const auto* Alchemy5 = findNode("10-5");
        check(Wood4 && Wood4->desc.find("普通施毒实际新增") != string::npos &&
            Wood4->desc.find("借毒回春不触发") != string::npos, "毒息回春说明区分真实施毒与经营增长");
        check(Wood5 && Wood5->desc.find("本场持续") != string::npos && Wood5->desc.find("到期") == string::npos,
            "蔓毒说明不再误写自然到期");
        check(Sword4 && Sword4->desc.find("五行剑不享受") != string::npos, "普通连剑说明明确排除五行剑增伤");
        check(Alchemy5 && Alchemy5->desc.find("借毒回春、噬焰回生除外") != string::npos,
            "丹道清浊准确说明普通治疗的适用边界");
        for (const auto& Build : artBuilds()) if (Build.id == "five-elements") {
            check(Build.desc.find("功法不额外赠气") != string::npos, "五行建议区分五剑自身积气与功法强化");
            check(std::find(Build.nodes.begin(), Build.nodes.end(), "7-4") != Build.nodes.end(),
                "保留剑返灵必要前置，不把删推荐误当成改前置规则");
        }
    }
}
} // namespace nztest
