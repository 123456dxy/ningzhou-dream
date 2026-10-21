#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_lab.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/poison_rules.h"
#include "core/content/npc_combat_profiles.h"
#include "core/content/game_ids.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/interface/views.h"
#include <algorithm>
#include <array>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立独立真实斗法；流程：永久毒与滋毒经营回归；返回：无文件、服务或共享存档的夹具。
Game mfPoisonEncounter() {
    Game State;
    State.seed = State.rng = 20831;
    State.player.name = "永久毒回归";
    State.player.stage = State.player.highestStage = 12;
    State.player.lifespan = realmOf(12).life;
    State.clock.ageBase = State.player.age;
    State.player.talent.clear(); State.player.traits.clear();
    State.player.arts.main.clear();
    State.player.arts.spells = {"corrode", "poisonBloom", "wood", "poisonSiphon"};
    State.player.arts.knownSpells = State.player.arts.spells;
    State.player.health = mfPlayerHealthMaximum(State.player);
    EnemySpec Opponent;
    Opponent.name = "练毒靶"; Opponent.stage = 0; Opponent.speed = 0;
    battleStart(State, Opponent);
    auto& Combat = *State.player.arts.battle;
    Combat.enemyHp = Combat.enemyMax = 100000;
    Combat.hp = Combat.max = 100000;
    Combat.enemyCards.enabled = false;
    Combat.style = "土";
    Combat.mana = 12;
    return State;
}

// 用途：从真实牌组指定单张牌；流程：定向动作与拒绝回归；返回：合法已有UID，不补造卡牌。
string mfHoldPoisonCard(Battle& Combat, const string& SpellId) {
    for (const auto& Entry : Combat.deck) {
        if (Entry.spell != SpellId) continue;
        for (auto* Pile : {&Combat.hand, &Combat.draw, &Combat.discard})
            Pile->erase(std::remove(Pile->begin(), Pile->end(), Entry.uid), Pile->end());
        Combat.hand.push_back(Entry.uid);
        return Entry.uid;
    }
    return "";
}
} // namespace

// 用途：核验永久毒、同比滋毒、原子拒绝与真实获取；流程：独立定向/全量harness；返回：汇总断言。
void mfRunPermanentPoisonTests() {
    std::printf("\n--- 本场永久毒与滋毒诀 ---\n");
    {
        auto State = mfPoisonEncounter();
        auto& Combat = *State.player.arts.battle;
        check(battleTurn(State, mfHoldPoisonCard(Combat, "corrode")).ok, "真实腐毒术施毒成功");
        checkEq(Combat.poisonTurns, 1, "施毒使用活跃标记1，不记录倒计时");
        const int Layers = Combat.poison;
        const int Tick = Combat.poisonDamage;
        const int Hp = Combat.enemyHp;
        for (int Round = 0; Round < 5; ++Round)
            check(battleTurn(State, "end").ok, "真实完整回合可持续推进");
        checkEq(Combat.poison, Layers, "无增长的毒经过五回合仍保留全部层数");
        checkEq(Combat.poisonTurns, 1, "毒活跃标记不自然递减");
        checkEq(Hp - Combat.enemyHp, Tick * 5, "每回合自然跳毒持续五次");
    }
    {
        auto State = mfPoisonEncounter();
        auto& Combat = *State.player.arts.battle;
        Combat.playerPoison = 7; Combat.playerPoisonTurns = 1;
        const int Hp = Combat.hp;
        for (int Round = 0; Round < 5; ++Round) battleTurn(State, "end");
        checkEq(Combat.playerPoisonTurns, 1, "简化敌方及妖兽施毒也不自然过期");
        checkEq(Combat.playerPoison, 7, "敌方毒每跳伤害保持");
        check(Combat.hp <= Hp - 35, "敌方毒连续五轮真实扣血");
    }
    const auto* Bloom = battleSpell("poisonBloom");
    check(Bloom != nullptr, "滋毒诀进入真实神通目录");
    if (Bloom) {
        Player Actor;
        checkEq(Bloom->stage, 3, "滋毒诀筑基可学");
        checkEq(battleCost(Actor, *Bloom), 2, "滋毒诀真实费用2灵力");
        checkEq(Bloom->cooldown, 1, "滋毒诀冷却一个完整自身回合");
        for (const auto& [Layers, Tick, ExpectedLayers, ExpectedTick] :
             {std::array<int, 4>{8, 17, 12, 26}, {1, 1, 2, 2}, {98, 980, 99, 990}}) {
            auto State = mfPoisonEncounter();
            auto& Combat = *State.player.arts.battle;
            Combat.poison = Layers; Combat.poisonDamage = Tick; Combat.poisonTurns = 1;
            Combat.enemyWeaken = 1; Combat.buff = .4; Combat.discardPower = .2; Combat.last = "水";
            State.player.arts.nodes = {"1-4", "9-4", "9-5"};
            State.player.arts.main = "evergreen"; State.player.arts.manuals["evergreen"] = 5;
            Combat.hp = Combat.max / 2;
            const auto Uid = mfHoldPoisonCard(Combat, "poisonBloom");
            const int Hp = Combat.hp, EnemyHp = Combat.enemyHp, Mana = Combat.mana;
            const auto Preview = battlePreview(State.player, Combat, "poisonBloom");
            check(Preview.valid, "滋毒预览有效");
            checkEq(Preview.damage, 0, "滋毒预览没有直接伤害");
            check(battleTurn(State, Uid).ok, "滋毒真实动作成功");
            checkEq(Combat.poison, ExpectedLayers, "新增毒层向上取整且封顶99");
            checkEq(Combat.poisonDamage, ExpectedTick, "按实际增加比例复制现有混合毒跳伤");
            checkEq(Combat.mana, Mana - 2, "滋毒扣费与报价一致");
            checkEq(Combat.hp, Hp, "滋毒不触发回春治疗");
            checkEq(Combat.enemyHp, EnemyHp, "滋毒不附带直接伤害");
            checkEq(Combat.enemyWeaken, 1, "滋毒不消耗扰灵");
            checkNear(Combat.buff, .4, "滋毒保留既有增伤");
            checkNear(Combat.discardPower, .2, "滋毒不消耗锐势");
            checkStr(Combat.last, "木", "有效滋毒正常记录木行");
            const auto Status = std::find_if(Preview.statuses.begin(), Preview.statuses.end(), [](const Preview::Status& Entry) { return Entry.id == "poison"; });
            check(Status != Preview.statuses.end() && Status->total == ExpectedLayers && Status->perTurn == ExpectedTick,
                "滋毒结构化预览和真实层数跳伤一致");
        }
        for (int Layers : {0, 99}) {
            auto State = mfPoisonEncounter();
            auto& Combat = *State.player.arts.battle;
            Combat.poison = Layers; Combat.poisonDamage = Layers; Combat.poisonTurns = Layers ? 1 : 0;
            const auto Uid = mfHoldPoisonCard(Combat, "poisonBloom");
            const auto Before = saveToJson(State);
            const auto View = api::view::battle(State);
            bool Found = false;
            for (const auto& Card : View["hand"].asArray()) if (Card["uid"].asString() == Uid) {
                Found = true;
                check(!Card["playable"].asBool() && !Card["reason"].asString().empty(),
                    "正式/试炼共用手牌API在无毒或满层时禁用并解释滋毒");
            }
            check(Found, "真实手牌API包含滋毒实例");
            const auto Result = battleTurn(State, Uid);
            check(!Result.ok, "无毒或满层时滋毒明确拒绝");
            checkStr(saveToJson(State), Before, "拒绝不改手牌费用冷却RNG或任何战况");
        }
        {
            auto State = mfPoisonEncounter();
            auto& Combat = *State.player.arts.battle;
            Combat.poison = 10; Combat.poisonDamage = 25; Combat.poisonTurns = 1;
            check(battleTurn(State, mfHoldPoisonCard(Combat, "poisonBloom")).ok, "滋毒首次正常施放");
            checkEq(Combat.cooldowns.at("poisonBloom"), 2, "冷却补偿当前回合，内部记2");
            mfHoldPoisonCard(Combat, "poisonBloom");
            const auto Before = saveToJson(State);
            check(!battleTurn(State, "poisonBloom").ok, "同轮重复滋毒被冷却拒绝");
            checkStr(saveToJson(State), Before, "重复施放拒绝无任何副作用");
            battleTurn(State, "end");
            mfHoldPoisonCard(Combat, "poisonBloom");
            check(!battleTurn(State, "poisonBloom").ok, "下一自身回合仍完整冷却");
            battleTurn(State, "end");
            Combat.mana = 12;
            check(battleTurn(State, mfHoldPoisonCard(Combat, "poisonBloom")).ok, "隔过完整自身回合后可再次滋毒");
        }
        {
            auto State = mfPoisonEncounter();
            auto& Combat = *State.player.arts.battle;
            Combat.poison = 8; Combat.poisonDamage = -1; Combat.poisonTurns = 1;
            const auto Quote = PoisonRules::mfQuoteBloom(12, Combat);
            checkEq(Quote.BeforeDamage, 156, "无精确缓存时统一解释原混合毒跳伤");
            checkEq(Quote.AfterDamage, 234, "滋毒按已解释跳伤同比复制");
            Combat.poison = 98; Combat.poisonDamage = BATTLE_BURN_CAP;
            checkEq(PoisonRules::mfQuoteBloom(12, Combat).AfterDamage, BATTLE_BURN_CAP,
                "64位中间计算后仍受每跳伤害安全上限约束");
        }
    }
    {
        auto State = mfPoisonEncounter();
        auto& Combat = *State.player.arts.battle;
        State.player.arts.nodes = {"1-4", "1-5"};
        Combat.hp = Combat.max / 2;
        Combat.poison = 8; Combat.poisonDamage = 17; Combat.poisonTurns = 1;
        Combat.intentKind = "guard"; Combat.intentScale = 0;
        const int Hp = Combat.hp;
        battleTurn(State, "end");
        checkEq(Combat.poison, 9, "每个真实完整回合自然增长一次");
        checkEq(Combat.hp, Hp, "自然毒增长不触发施毒回春");
        const int Tick = Combat.poisonDamage;
        check(battleTurn(State, mfHoldPoisonCard(Combat, "poisonSiphon")).ok, "借毒回春使用持续毒进行真实治疗");
        checkEq(Combat.poison, 9, "借毒回春不消耗自然增长后的毒层");
        checkEq(Combat.poisonDamage, Tick, "借毒回春不更改精确毒伤缓存");
        checkEq(Combat.poisonTurns, 1, "借毒回春保留本场活跃标记");
        Combat.poison = Combat.poisonDamage = Combat.poisonTurns = 0;
        Combat.intentKind = "guard"; Combat.intentScale = 0;
        battleTurn(State, "end");
        checkEq(Combat.poison, 0, "无活跃毒时增长不能凭空复生");
    }
    {
        BattleLab::Session Session;
        BattleLab::Selection Request;
        Request.BuildId = "wood-poison"; Request.Stage = 12; Request.OpponentId = "zhushan-complete";
        check(BattleLab::mfStart(Session, Request).Succeeded, "完整竹山镜像对局真实建立");
        auto& State = Session.CurrentGame();
        auto& Combat = *State.player.arts.battle;
        auto& Enemy = Combat.enemyCards;
        Combat.enemyHp = Combat.enemyMax; Combat.hp = Combat.max;
        Enemy.nodes.clear(); Enemy.main.clear(); Enemy.aux.clear(); Enemy.manuals.clear();
        Enemy.poison = 8; Enemy.poisonDamage = 17; Enemy.poisonTurns = 1;
        Combat.playerPoison = 17; Combat.playerPoisonTurns = 1;
        Enemy.draw.insert(Enemy.draw.end(), Enemy.hand.begin(), Enemy.hand.end()); Enemy.hand.clear();
        Enemy.planned.clear();
        for (const auto& Card : Enemy.deck) if (Card.spell == "poisonBloom") {
            Enemy.draw.erase(std::remove(Enemy.draw.begin(), Enemy.draw.end(), Card.uid), Enemy.draw.end());
            Enemy.hand.push_back(Card.uid); Enemy.planned.push_back(Card.uid); break;
        }
        check(!Enemy.planned.empty(), "竹山真实牌组含滋毒诀实例");
        Enemy.mana = 12;
        const auto Rng = State.rng;
        const auto Before = saveToJson(State);
        const auto Preview = battleEnemyPreview(State);
        checkEq(State.rng, Rng, "敌方滋毒预演不推进真实RNG");
        checkStr(saveToJson(State), Before, "敌方滋毒预演不写真实任何状态");
        check(battleTurn(State, "end").ok, "敌方通过真实计划执行滋毒");
        checkEq(Enemy.poison, 12, "敌方滋毒增加玩家身上原有毒层");
        checkEq(Combat.playerPoison, 26, "敌方镜像同比复制精确跳伤");
        checkEq(Enemy.poisonDamage, 26, "敌方原生缓存与玩家受毒跳伤一致");
        checkEq(Combat.playerPoisonTurns, 1, "敌方滋毒保留本场活跃标记");
        check(Preview.damage >= 17, "敌方真实意图包含本轮自然毒伤");
        Player Foe; Foe.stage = 12;
        Battle View; View.poison = 8; View.poisonDamage = 17; View.poisonTurns = 1;
        if (Bloom) check(mfBattleNpcCardPriority(Foe, View, *Bloom) < 0, "聪明AI低毒层优先正常起毒");
        View.poison = 20;
        if (Bloom) check(mfBattleNpcCardPriority(Foe, View, *Bloom) > 0, "聪明AI高毒层可选择滋毒经营");
    }
    {
        BattleLab::Session Session;
        BattleLab::Selection Request;
        Request.BuildId = "wood-poison"; Request.Stage = 3;
        check(BattleLab::mfStart(Session, Request).Succeeded, "永久毒持久往返使用合法构筑开局");
        auto& State = Session.CurrentGame();
        auto& Combat = *State.player.arts.battle;
        Combat.poison = 8; Combat.poisonDamage = 17; Combat.poisonTurns = 1;
        Game Restored; string Error;
        check(loadDetachedFromJson(saveToJson(State), Restored, &Error), "永久毒标记及新牌可存读：" + Error);
        if (Restored.player.arts.battle) {
            checkEq(Restored.player.arts.battle->poisonTurns, 1, "存读保留活跃标记1");
            checkEq(Restored.player.arts.battle->poisonDamage, 17, "存读保留精确混合毒伤");
        }
        Combat.poisonTurns = 2;
        check(!loadDetachedFromJson(saveToJson(State), Restored, &Error), "存档不接受把毒活跃标记伪装成倒计时");
    }
    for (int Stage : {0, 3, 12}) for (const string Tier : {"initial", "advanced"}) {
        BattleLab::Session Session;
        BattleLab::Selection Request;
        Request.BuildId = "wood-poison"; Request.Stage = Stage;
        Request.LoadoutTier = Tier; Request.OpponentId = "beast-body";
        check(BattleLab::mfStart(Session, Request).Succeeded, "真实木毒试炼开局成功");
        const auto& Actor = Session.CurrentGame().player;
        const bool Equipped = std::find(Actor.arts.spells.begin(), Actor.arts.spells.end(), "poisonBloom") != Actor.arts.spells.end();
        const bool Known = std::find(Actor.arts.knownSpells.begin(), Actor.arts.knownSpells.end(), "poisonBloom") != Actor.arts.knownSpells.end();
        check(Equipped == (Stage >= 3) && Known == (Stage >= 3), "初成/完整木毒构筑按境界真实装备并学会滋毒诀");
    }
    {
        Game State;
        State.player.sect = State.player.location = "zhushan";
        State.player.stage = State.player.highestStage = 3;
        State.economy.contribution = 10000;
        const auto Quote = shop::librarySpellSource(State, "poisonBloom");
        check(Quote.valid && Quote.enabled && Quote.points > 0, "竹山正常藏经阁报价提供滋毒诀");
        const int Before = State.economy.contribution;
        check(shop::librarySpellExchange(State, "poisonBloom").ok, "通过正式贡献兑换学会滋毒诀");
        checkEq(State.economy.contribution, Before - Quote.points, "正式学习扣除报价贡献");
        check(std::find(State.player.arts.knownSpells.begin(), State.player.arts.knownSpells.end(), "poisonBloom") !=
            State.player.arts.knownSpells.end(), "正式学习进入已知神通而非仅实验目录");
    }
}
} // namespace nztest
