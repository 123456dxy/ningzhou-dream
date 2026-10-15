#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/elemental_sword.h"
#include "core/combat/hand_rules.h"
#include "core/combat/fire_growth.h"
#include "core/json.h"
#include "core/save.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立不带天赋、装备的同阶普通修士对局；流程：精确清毒回归；返回：独立游戏，无文件或服务操作。
Game mfCleanseEncounter(const vector<string>& Spells = {"wood", "corrode"}) {
    Game Encounter;
    Encounter.seed = Encounter.rng = 773;
    Encounter.player.name = "清毒计量测试";
    Encounter.player.stage = Encounter.player.highestStage = 12;
    Encounter.player.lifespan = realmOf(12).life;
    Encounter.clock.ageBase = Encounter.player.age;
    Encounter.player.talent.clear(); Encounter.player.traits.clear();
    Encounter.player.arts.main.clear();
    Encounter.player.arts.spells = Spells;
    Encounter.player.arts.knownSpells = Encounter.player.arts.spells;
    Npc Opponent;
    Opponent.id = "cleanse-opponent"; Opponent.name = "回春修士";
    Opponent.stage = 12; Opponent.sect = "zhushan"; Opponent.dao = "木";
    Encounter.npcs.push_back(Opponent);
    EnemySpec Enemy;
    Enemy.name = Opponent.name; Enemy.id = Opponent.id;
    Enemy.stage = 12; Enemy.sect = Opponent.sect; Enemy.style = Opponent.dao;
    Enemy.speed = 0;
    battleStart(Encounter, Enemy);
    return Encounter;
}

// 用途：只在隔离规则夹具指定下一张治疗；流程：清毒平减双方比较；返回：无，保持真实牌UID与真实施法入口。
void mfPlanEnemyCleanse(Game& Encounter) {
    auto& State = *Encounter.player.arts.battle;
    auto& EnemyState = State.enemyCards;
    EnemyState.nodes.clear();
    for (int Level = 1; Level <= 5; ++Level)
        EnemyState.nodes.push_back("10-" + std::to_string(Level));
    EnemyState.draw.insert(EnemyState.draw.end(), EnemyState.hand.begin(), EnemyState.hand.end());
    EnemyState.hand.clear(); EnemyState.planned.clear();
    for (const auto& Card : EnemyState.deck) {
        if (Card.spell != "wood") continue;
        EnemyState.draw.erase(std::remove(EnemyState.draw.begin(), EnemyState.draw.end(), Card.uid), EnemyState.draw.end());
        EnemyState.hand.push_back(Card.uid); EnemyState.planned.push_back(Card.uid);
        break;
    }
    EnemyState.mana = 12;
    State.enemyHp = State.enemyMax / 2;
}

// 用途：从真实牌组取一张指定神通进入手牌；流程：单次主动清毒回归；返回：合法UID，没有增牌或补费。
string mfHoldCard(Battle& State, const string& SpellId, const string& ExcludedUid = "") {
    for (const auto& Card : State.deck) {
        if (Card.spell != SpellId || Card.uid == ExcludedUid) continue;
        for (auto* Pile : {&State.hand, &State.draw, &State.discard})
            Pile->erase(std::remove(Pile->begin(), Pile->end(), Card.uid), Pile->end());
        State.hand.push_back(Card.uid);
        return Card.uid;
    }
    return "";
}

// 用途：准备合法五剑被动夹具；流程：序列、资源封顶及生命汲取回归；返回：内存对局，不把固定手牌当自然胜率。
Game mfElementalEncounter() {
    Game Encounter = mfCleanseEncounter({"earthSword", "metalSword", "waterSword", "woodSword", "fireSword", "swordDraw", "gather", "skim"});
    auto& PlayerState = Encounter.player;
    PlayerState.arts.main = "fiveSword";
    PlayerState.arts.manuals["fiveSword"] = 5;
    for (int Level = 1; Level <= 5; ++Level) {
        PlayerState.arts.nodes.push_back("7-" + std::to_string(Level));
        PlayerState.arts.nodes.push_back("9-" + std::to_string(Level));
    }
    EnemySpec Enemy;
    Enemy.name = Encounter.npcs.front().name; Enemy.id = Encounter.npcs.front().id;
    Enemy.stage = 12; Enemy.sect = "zhushan"; Enemy.style = "木"; Enemy.speed = 0;
    battleStart(Encounter, Enemy);
    auto& State = *PlayerState.arts.battle;
    State.draw.insert(State.draw.end(), State.hand.begin(), State.hand.end()); State.hand.clear();
    return Encounter;
}
// 用途：用合法牌UID指定敌方单轮探针；流程：NPC/留影镜像序列回归；返回：无，控制手牌用于规则测试，不当自然胜率。
void mfPlanElementalEnemy(Game& Encounter, const vector<string>& SpellIds) {
    auto& State = *Encounter.player.arts.battle;
    auto& Enemy = State.enemyCards;
    Enemy.main = "fiveSword"; Enemy.manuals = {{"fiveSword", 5}}; Enemy.aux.clear();
    Enemy.nodes = Encounter.player.arts.nodes;
    Enemy.deck = State.deck;
    Enemy.hand.clear(); Enemy.draw.clear(); Enemy.discard.clear(); Enemy.exhausted.clear(); Enemy.planned.clear();
    for (const auto& Card : Enemy.deck) Enemy.draw.push_back(Card.uid);
    for (const auto& SpellId : SpellIds) {
        const auto Card = std::find_if(Enemy.deck.begin(), Enemy.deck.end(), [&](const nz::Card& Entry) {
            return Entry.spell == SpellId && std::find(Enemy.hand.begin(), Enemy.hand.end(), Entry.uid) == Enemy.hand.end();
        });
        if (Card == Enemy.deck.end()) continue;
        Enemy.draw.erase(std::remove(Enemy.draw.begin(), Enemy.draw.end(), Card->uid), Enemy.draw.end());
        Enemy.hand.push_back(Card->uid); Enemy.planned.push_back(Card->uid);
    }
    Enemy.mana = 12;
}
} // namespace

// 用途：检查清毒公平、旧层数状态与codec往返；流程：本轮斗法平衡定向回归；返回：无，通过harness汇总断言。
void mfRunCombatBalanceIterationTests() {
    {
        nz::Player Neutral;
        const auto* Strike = nz::battleSpell("goldslash");
        check(Strike != nullptr, "金罡斩保持真实神通注册");
        if (Strike) {
            checkEq(nz::battleCost(Neutral, *Strike), 2, "无金道减费的金罡斩仍为2费");
            Neutral.arts.nodes = {"0-1", "0-2"};
            checkEq(nz::battleCost(Neutral, *Strike), 2, "御金减费后金罡斩也需2费，不能1费重复兑现护盾");
        }
    }
    {
        auto Encounter = mfElementalEncounter();
        Encounter.player.arts.battle->enemyCards.guard = .15;
        nz::Game Restored; std::string Error;
        check(nz::loadFromJson(nz::saveToJson(Encounter), Restored, &Error),
            "玄道对手土剑叠至15%守势后真实战斗仍可存读：" + Error);
    }
    std::printf("\n--- 战斗平衡迭代：毒伤精确结算 ---\n");
    {
        const Game Encounter = mfCleanseEncounter();
        checkEq(Encounter.player.arts.battle->poisonDamage, 0, "新普通PvE以零初始化精确毒跳伤");
        checkEq(Encounter.player.arts.battle->enemyCards.poisonDamage, 0, "新NPC与玩家共用精确毒跳伤模式");
    }
    for (bool LegacyMode : {false, true}) {
        Game Encounter = mfCleanseEncounter();
        auto& State = *Encounter.player.arts.battle;
        State.poison = 3; State.poisonTurns = 3;
        const double Coefficient = LegacyMode ? LegacyPoisonTickBasisCoefficient : PoisonTickBasisCoefficient;
        const int OriginalTick = jsRound(mfBattleDamageBasis(12) * Coefficient * 3);
        State.poisonDamage = LegacyMode ? -1 : OriginalTick;
        mfPlanEnemyCleanse(Encounter);
        const uint32_t RngBefore = Encounter.rng;
        const auto Preview = battleEnemyPreview(Encounter);
        checkEq(Encounter.rng, RngBefore, "清毒意图查询不消费随机数");
        checkEq(Encounter.player.arts.battle->poisonDamage, LegacyMode ? -1 : OriginalTick,
                "清毒预演不把精确迁移提交到真实状态");
        const int HpBefore = State.hp;
        check(battleTurn(Encounter, "end").ok, "NPC真实回合执行清毒");
        const auto& After = *Encounter.player.arts.battle;
        checkEq(HpBefore - After.hp, Preview.damage, "清毒对局敌方预览和实际来伤一致");
        checkEq(After.poison, 3, "平减两点毒伤不错误削除整层毒");
        checkEq(After.poisonDamage, OriginalTick - 2, "新局及旧层数状态清毒都精确减两点");
        checkEq(After.poisonTurns, 2, "清毒保留原毒持续回合");
        Game Loaded;
        string Error;
        const bool LoadedSuccessfully = loadDetachedFromJson(saveToJson(Encounter), Loaded, &Error);
        if (!LoadedSuccessfully) std::printf("  codec拒绝原因：%s\n", Error.c_str());
        check(LoadedSuccessfully, "清毒后的精确毒跳伤可以codec往返");
        if (Loaded.player.arts.battle) {
            checkEq(Loaded.player.arts.battle->poisonDamage, OriginalTick - 2, "往返保留清毒剩余伤害而不重算整层");
            checkEq(Loaded.player.arts.battle->poison, 3, "往返保留供引毒咒使用的毒层");
        }
    }
    {
        Game Encounter = mfCleanseEncounter();
        auto& State = *Encounter.player.arts.battle;
        State.poison = 3; State.poisonTurns = 3; State.poisonDamage = -1;
        auto SaveObject = json::Value::parse(saveToJson(Encounter));
        auto& BattleObject = SaveObject.asObject().at("player").asObject().at("arts").asObject().at("battle");
        BattleObject.asObject().erase("poisonDamage");
        BattleObject.asObject().at("enemyCards").asObject().erase("poisonDamage");
        Game Loaded;
        string Error;
        const bool LoadedSuccessfully = loadDetachedFromJson(SaveObject.dump(), Loaded, &Error);
        check(LoadedSuccessfully, "真正缺少毒伤字段的旧档仍能合法加载");
        if (LoadedSuccessfully) {
            checkEq(Loaded.player.arts.battle->poisonDamage, -1, "缺字段保留旧毒层默认语义");
            checkEq(Loaded.player.arts.battle->poison, 3, "旧档加载不把在途毒归零");
            checkEq(Loaded.player.arts.battle->poisonTurns, 3, "旧档加载不提前消耗持续回合");
            mfPlanEnemyCleanse(Loaded);
            check(battleTurn(Loaded, "end").ok, "旧缺字段档在真实动作边界转换");
            checkEq(Loaded.player.arts.battle->poisonDamage, 57, "旧三层冻结59点在转换清毒后精确保留57");
        }
    }
    {
        Game Encounter = mfCleanseEncounter();
        auto& Player = Encounter.player;
        for (int Level = 1; Level <= 5; ++Level) Player.arts.nodes.push_back("1-" + std::to_string(Level));
        auto& State = *Player.arts.battle;
        const string CardUid = mfHoldCard(State, "corrode");
        const auto Preview = battlePreview(Player, State, "corrode");
        const auto Poison = std::find_if(Preview.statuses.begin(), Preview.statuses.end(),
            [](const nz::Preview::Status& Status) { return Status.id == "poison"; });
        check(Poison != Preview.statuses.end(), "真实施毒预览公开精确每跳量");
        check(battleTurn(Encounter, CardUid).ok, "施毒通过真实手牌动作提交");
        if (Poison != Preview.statuses.end()) {
            checkEq(State.poisonDamage, Poison->perTurn, "真实毒缓存与施毒预览一致");
            checkEq(State.poisonDamage, jsRound(mfBattleDamageBasis(12) * PoisonTickBasisCoefficient * State.poison),
                    "新施毒使用统一具名系数");
        }
        State.enemyCards.planned.clear();
        const int PreviousPoisonLayers = State.poison;
        check(battleTurn(Encounter, "end").ok, "毒增长按完整回合结算");
        checkEq(State.poison, PreviousPoisonLayers + 1, "蔓毒真实增加一层");
        checkEq(State.poisonDamage, jsRound(mfBattleDamageBasis(12) * PoisonTickBasisCoefficient * State.poison),
                "毒增长刷新真实每跳缓存而不遗留旧层伤害");
    }
    {
        Game Encounter = mfCleanseEncounter();
        auto& State = *Encounter.player.arts.battle;
        auto& EnemyState = State.enemyCards;
        EnemyState.draw.insert(EnemyState.draw.end(), EnemyState.hand.begin(), EnemyState.hand.end());
        EnemyState.hand.clear(); EnemyState.planned.clear();
        for (const auto& Card : EnemyState.deck) {
            if (Card.spell != "corrode") continue;
            EnemyState.draw.erase(std::remove(EnemyState.draw.begin(), EnemyState.draw.end(), Card.uid), EnemyState.draw.end());
            EnemyState.hand.push_back(Card.uid); EnemyState.planned.push_back(Card.uid);
            break;
        }
        EnemyState.mana = 12;
        check(battleTurn(Encounter, "end").ok, "真实NPC通过共享神通入口施毒");
        check(State.enemyCards.poison > 0, "NPC真实施毒登记毒层");
        checkEq(State.playerPoison, jsRound(mfBattleDamageBasisFromPower(State.enemyPower) * PoisonTickBasisCoefficient * State.enemyCards.poison),
                "NPC每跳毒伤使用与玩家同一具名系数");
        checkEq(State.enemyCards.poisonDamage, State.playerPoison, "NPC精确缓存与玩家将受毒伤一致");
    }
    {
        Game Encounter = mfCleanseEncounter();
        auto& Player = Encounter.player;
        for (int Level = 1; Level <= 5; ++Level) Player.arts.nodes.push_back("10-" + std::to_string(Level));
        auto& State = *Player.arts.battle;
        State.playerPoison = 59; State.playerPoisonTurns = 3;
        State.enemyCards.poison = 3; State.enemyCards.poisonTurns = 3;
        State.enemyCards.poisonDamage = 59;
        const string CardUid = mfHoldCard(State, "wood");
        const auto Preview = battlePreview(Player, State, "wood");
        const int HpBefore = State.hp;
        check(battleTurn(Encounter, CardUid).ok, "玩家治疗走真实卡牌入口");
        checkEq(State.hp - HpBefore, Preview.heal, "主动治疗预览和实际一致");
        checkEq(State.playerPoison, 57, "玩家平减两点毒伤与NPC计量一致");
        checkEq(State.enemyCards.poisonDamage, 57, "玩家清毒同步敌方每跳缓存");
        checkEq(State.enemyCards.poison, 3, "玩家清毒同样保留毒层供爆发计算");
    }
    for (int Stage : {0, 3, 6, 9, 12}) {
        const auto Builds = artBuilds();
        const auto Build = std::find_if(Builds.begin(), Builds.end(),
            [](const BuildInfo& Entry) { return Entry.id == "mind-cycle"; });
        int LegalSpellCount = 0;
        bool HasSoulBuilder = false, HasFinisher = false;
        Player PlayerState;
        PlayerState.stage = Stage;
        if (Build != Builds.end()) {
            for (const auto& SpellId : Build->spells) {
                const auto* Spell = battleSpell(SpellId);
                if (!Spell || Spell->stage > Stage || LegalSpellCount >= battleSpellSlots(PlayerState)) continue;
                ++LegalSpellCount;
                HasSoulBuilder |= Spell->kind == "soul";
                HasFinisher |= Spell->kind == "soulBurst";
            }
        }
        check(HasSoulBuilder && HasFinisher, "各境神识推荐都能在合法槽位内积魂和爆发");
    }
    {
        Game Encounter = mfCleanseEncounter({"fire"});
        auto& State = *Encounter.player.arts.battle;
        const string FirstUid = mfHoldCard(State, "fire");
        const int TickDamage = jsRound(mfBattleDamageBasis(12) * BurnTickBasisCoefficient);
        const auto FirstPreview = battlePreview(Encounter.player, State, "fire");
        check(battleTurn(Encounter, FirstUid).ok, "灼烧真实首张神通施放成功");
        checkEq(State.burn, TickDamage, "基础灼烧使用统一具名系数");
        const auto FirstBurn = std::find_if(FirstPreview.statuses.begin(), FirstPreview.statuses.end(),
            [](const nz::Preview::Status& Status) { return Status.id == "burn"; });
        check(FirstBurn != FirstPreview.statuses.end() && FirstBurn->perTurn == State.burn,
              "灼烧预览和真实每跳量一致");
        const string SecondUid = mfHoldCard(State, "fire", FirstUid);
        check(battleTurn(Encounter, SecondUid).ok, "自然灵力允许的第二张真实副本施放成功");
        checkEq(State.burn, TickDamage * 2, "降低单次灼烧系数仍保留天然累加");
        checkEq(State.burnTurns, 3, "重复施火仍刷新三回合");
        Arts FireArts;
        FireArts.main.clear(); FireArts.nodes = {"3-4"};
        checkNear(artModsOf(FireArts).BurnGrowthStackPerRound, FireGrowth::KindleNodeStackGainPerRound, "火四逐轮成长与具名平衡参数一致");
        const auto& Nodes = artNodes();
        const auto FireNode = std::find_if(Nodes.begin(), Nodes.end(),
            [](const NodeDef& Entry) { return Entry.dao == 3 && Entry.level == 4; });
        check(FireNode != Nodes.end() && FireNode->desc.find(std::to_string(jsRound(FireGrowth::KindleNodeStackGainPerRound * 100)) + "%") != string::npos,
              "火四成长说明由同一参数生成，不保留即时叠乘文案");
    }
    for (int Stage : {0, 3, 6, 9, 12}) {
        Game Encounter = mfCleanseEncounter();
        auto& State = *Encounter.player.arts.battle;
        State.enemyCards.stage = Stage;
        const uint32_t RngBefore = Encounter.rng;
        const string Before = saveToJson(Encounter);
        checkEq(mfBattleEnemyActionLimit(Encounter, State), CommonSectActionBase + Stage / 3,
                "普通宗门预算逐境增长并受实际规则约束");
        checkStr(saveToJson(Encounter), Before, "敌方预算查询不改变任何活动状态");
        checkEq(Encounter.rng, RngBefore, "敌方预算查询不消费随机数");
        Encounter.npcs.front().combatProfileId = "zhushan-complete";
        checkEq(mfBattleEnemyActionLimit(Encounter, State), ChampionSectActionBase + Stage / 3,
                "完整传承仍沿用原宗门预算，高于普通修士一张");
        State.enemyCards.sect.clear();
        checkEq(mfBattleEnemyActionLimit(Encounter, State), 2 + Stage / 6,
                "散修仍沿用原预算，未随宗门改变");
        State.shadowPlayer = std::make_shared<const Player>(Encounter.player);
        checkEq(mfBattleEnemyActionLimit(Encounter, State), 24,
                "影身保留原24张上限，不改变联机战斗预算");
    }
    {
        Game Encounter = mfCleanseEncounter({"purge", "gold"});
        auto& State = *Encounter.player.arts.battle;
        const string PurgeUid = mfHoldCard(State, "purge");
        const string SelectedUid = mfHoldCard(State, "gold");
        const string OtherUid = mfHoldCard(State, "gold", SelectedUid);
        const string BeforeInvalidAction = saveToJson(Encounter);
        for (const auto& InvalidTarget : {string("unknown-card"), PurgeUid}) {
            check(!battleTurn(Encounter, PurgeUid, InvalidTarget).ok, "非法或同神通移除目标明确拒绝");
            checkStr(saveToJson(Encounter), BeforeInvalidAction, "拒绝移除不扣灵力、不动牌堆或随机数");
        }
        const int ManaBefore = State.mana;
        const int PurgeCost = battleCost(Encounter.player, *battleSpell("purge"));
        check(battleTurn(Encounter, PurgeUid, SelectedUid).ok, "忘机可以按UID移除指定一张卡");
        checkEq(State.exhausted.size(), 1, "单次忘机仅追加一个UID，不消灭同神通全部副本");
        check(State.exhausted.front() == SelectedUid, "移除的是玩家明确选择的副本");
        bool OtherCardRemains = false;
        for (const auto* Pile : {&State.hand, &State.draw, &State.discard})
            OtherCardRemains |= std::find(Pile->begin(), Pile->end(), OtherUid) != Pile->end();
        check(OtherCardRemains, "同神通另一张副本仍在本场循环中");
        checkEq(State.mana, ManaBefore - PurgeCost, "单卡移除仍支付统一实际费用");
        const string AfterExhaust = saveToJson(Encounter);
        check(!battleTurn(Encounter, "purge", SelectedUid).ok, "已移出的UID不能再次选中");
        checkStr(saveToJson(Encounter), AfterExhaust, "过期UID拒绝后状态保持完整");
        Game Loaded;
        string Error;
        check(loadDetachedFromJson(AfterExhaust, Loaded, &Error), "单UID移除状态可以codec往返");
        if (Loaded.player.arts.battle)
            check(Loaded.player.arts.battle->exhausted == State.exhausted, "往返保留单张移除记录");
    }
    {
        Game Encounter = mfCleanseEncounter({"gold"});
        auto& State = *Encounter.player.arts.battle;
        State.enemyCards.main.clear(); State.enemyCards.manuals.clear(); State.enemyCards.nodes.clear();
        State.enemyCards.reduction = .2;
        State.enemyShield = 1000;
        const string CardUid = mfHoldCard(State, "gold");
        const auto Preview = battlePreview(Encounter.player, State, "gold");
        const int ExpectedRaw = jsRound(mfBattleDamageBasis(12) * battleSpell("gold")->scale);
        checkEq(Preview.RawDamage, ExpectedRaw, "打出伤害在目标减伤和护盾前由真实效果路径取样");
        checkEq(Preview.damage, 0, "足够护盾时实际气血伤害显示零");
        const int HpBefore = State.enemyHp;
        check(battleTurn(Encounter, CardUid).ok, "双伤害神通真实施放成功");
        checkEq(HpBefore - State.enemyHp, Preview.damage, "双伤害显示仍与真实气血损失一致");
        check(State.log.back().find("打出伤害" + std::to_string(ExpectedRaw)) != string::npos,
              "真实战报同样标明施术者打出伤害");
    }
    for (const auto& SpellId : {string("metalRedraw"), string("waterDraw"), string("fireDraw"),
                               string("woodDraw"), string("earthDraw"), string("swordDraw")}) {
        const auto* Spell = battleSpell(SpellId);
        check(Spell && Spell->stage == 3 && Spell->cooldown == 1, "本系补牌卡有真实境界与冷却限制");
        if (!Spell) continue;
        Player PlayerState;
        PlayerState.stage = 3;
        PlayerState.arts.main.clear(); PlayerState.talent.clear(); PlayerState.traits.clear();
        const auto Preview = battlePreview(PlayerState, battleReferenceState(PlayerState), SpellId);
        check(Preview.valid && Preview.cost > 0 && Preview.draw > 0, "未拥有跨道减费时补牌必须支付灵力");
        Game Encounter = mfCleanseEncounter({SpellId});
        auto& State = *Encounter.player.arts.battle;
        const string CardUid = mfHoldCard(State, SpellId);
        check(battleTurn(Encounter, CardUid).ok, "补牌经真实卡牌入口施放");
        check(State.hand.size() <= BattleCards::MaximumHandSize, "补牌仍遵守统一手牌容量");
        Game Loaded;
        string Error;
        check(loadDetachedFromJson(saveToJson(Encounter), Loaded, &Error), "新补牌业务ID可通过存档白名单往返");
    }
    {
        ManualInfo Manual;
        check(findManualInfo("fiveSword", Manual) && Manual.stage == 0 && Manual.price == 180,
              "五行剑诀位于权威功法目录并有真实境界价格");
        check(mfManualCatalog().count("fiveSword") == 1, "完整不可变功法目录包含本地新定义");
        Game Encounter = mfElementalEncounter();
        auto& State = *Encounter.player.arts.battle;
        State.swordQi = 0; // 本段只计量五剑施放所得，不计剑道节点提供的开局剑气。
        int ExpectedSequence = 0;
        for (const auto& SpellId : {string("earthSword"), string("metalSword"), string("waterSword"), string("woodSword"), string("fireSword")}) {
            const auto* Spell = battleSpell(SpellId);
            check(Spell && ElementalSword::mfIsElementalSword(*Spell) && Spell->kind == "sword",
                  "五属性剑统一使用剑招白名单而非五印复用");
            const string CardUid = mfHoldCard(State, SpellId);
            const auto Preview = battlePreview(Encounter.player, State, SpellId);
            check(Preview.RawDamage > 0, "五剑预览在真实增伤后提供原始伤害");
            check(battleTurn(Encounter, CardUid).ok, "有限真实灵力支持正确五剑顺序施放");
            checkEq(State.swordSequence, ++ExpectedSequence, "正确相生连续推进剑链");
            checkEq(State.ElementalSwordSequence, (ExpectedSequence - 1) % 3 + 1, "五行剑独立保存三剑周期进度");
            check(State.comboDraws <= ElementalSword::DrawLimitPerTurn && State.hand.size() <= BattleCards::MaximumHandSize,
                  "五行手牌引擎不突破补牌及手牌硬上限");
        }
        checkEq(State.ElementalSwordManaReturns, 3, "五行剑诀本轮最多触发三次返灵");
        checkEq(State.swordQi, 6, "五行剑诀不额外送剑气，仅五剑基础积气与水剑自带额外一层");
        const string NextEarthUid = mfHoldCard(State, "earthSword");
        check(battleTurn(Encounter, NextEarthUid).ok, "五剑可从火回土继续有限连锁");
        check(State.guard >= ElementalSword::EarthComboGuardGain, "土剑相生提供当轮守势");
        checkEq(State.ElementalSwordManaReturns, 3, "继续连锁不能突破本轮返灵次数");
        State.enemyCards.ElementalSwordManaReturns = 1;
        State.enemyCards.ElementalSwordSequence = 2;
        Game Loaded;
        string Error;
        const string Saved = saveToJson(Encounter);
        check(loadDetachedFromJson(Saved, Loaded, &Error), "五剑本轮计数及新增功法神通可codec往返");
        if (Loaded.player.arts.battle) {
            checkEq(Loaded.player.arts.battle->ElementalSwordManaReturns, 3, "保存保留玩家当轮返灵次数");
            checkEq(Loaded.player.arts.battle->enemyCards.ElementalSwordManaReturns, 1, "保存保留NPC当轮返灵次数");
            checkEq(Loaded.player.arts.battle->ElementalSwordSequence, State.ElementalSwordSequence, "保存保留玩家跨轮五行剑进度");
            checkEq(Loaded.player.arts.battle->enemyCards.ElementalSwordSequence, 2, "保存保留NPC跨轮五行剑进度");
        }
        auto LegacyObject = json::Value::parse(Saved);
        auto& LegacyBattle = LegacyObject.asObject().at("player").asObject().at("arts").asObject().at("battle");
        LegacyBattle.asObject().erase("elementalSwordManaReturns");
        LegacyBattle.asObject().at("enemyCards").asObject().erase("elementalSwordManaReturns");
        LegacyBattle.asObject().erase("elementalSwordSequence");
        LegacyBattle.asObject().at("enemyCards").asObject().erase("elementalSwordSequence");
        check(loadDetachedFromJson(LegacyObject.dump(), Loaded, &Error), "旧档缺少五剑计数可合法默认");
        if (Loaded.player.arts.battle) {
            checkEq(Loaded.player.arts.battle->ElementalSwordManaReturns, 0, "旧档缺字段默认零而非继承随机数值");
            checkEq(Loaded.player.arts.battle->ElementalSwordSequence, 0, "旧档缺独立五剑进度默认零");
            checkEq(Loaded.player.arts.battle->enemyCards.ElementalSwordSequence, 0, "旧NPC档缺独立五剑进度默认零");
        }
        auto InvalidObject = json::Value::parse(Saved);
        InvalidObject.asObject().at("player").asObject().at("arts").asObject().at("battle").set("elementalSwordManaReturns", .5);
        check(!loadDetachedFromJson(InvalidObject.dump(), Loaded, &Error), "外部五剑次数必须整数，不能截断小数默许");
        for (const auto& Invalid : {json::Value(-1), json::Value(4), json::Value(1.5), json::Value(true), json::Value{}}) {
            for (const bool IsEnemy : {false, true}) {
                auto Broken = json::Value::parse(Saved);
                auto& Combat = Broken.asObject().at("player").asObject().at("arts").asObject().at("battle");
                (IsEnemy ? Combat.asObject().at("enemyCards") : Combat).set("elementalSwordSequence", Invalid);
                const auto BeforeLoad = saveToJson(Loaded);
                check(!loadDetachedFromJson(Broken.dump(), Loaded, &Error) && saveToJson(Loaded) == BeforeLoad,
                    "双方独立五剑进度拒绝类型/范围错误并保留目标状态");
            }
        }
        const int SequenceBeforeEnd = State.ElementalSwordSequence;
        check(battleTurn(Encounter, "end").ok, "五剑整回合按真实规则结束");
        if (Encounter.player.arts.battle) {
            checkEq(Encounter.player.arts.battle->ElementalSwordManaReturns, 0, "新回合重置五剑返灵次数");
            checkEq(Encounter.player.arts.battle->swordSequence, 0, "新回合仍重置普通连剑序列");
            checkEq(Encounter.player.arts.battle->ElementalSwordSequence, SequenceBeforeEnd, "新回合保留独立五行剑进度");
            checkEq(jsRound(Encounter.player.arts.battle->guard * 100), 0, "土剑守势当轮消散，不永久叠加");
        }
    }
    {
        Game Encounter = mfElementalEncounter();
        auto& State = *Encounter.player.arts.battle;
        check(battleTurn(Encounter, mfHoldCard(State, "earthSword")).ok, "错序探针先打土剑");
        check(battleTurn(Encounter, mfHoldCard(State, "fireSword")).ok, "错序火剑仍可合法施放");
        checkEq(State.ElementalSwordSequence, 1, "土后火不相生，独立五剑连锁重开");
        checkEq(State.ElementalSwordManaReturns, 0, "错序不给五行剑诀额外返灵");
        check(battleTurn(Encounter, mfHoldCard(State, "swordDraw")).ok, "剑系补牌仍作为经营牌付费使用");
        checkEq(State.swordSequence, 0, "非剑招补牌仍打断普通连剑");
        checkEq(State.ElementalSwordSequence, 1, "剑道补牌属于中性牌，不打断五行剑进度");
    }
    {
        Game Encounter = mfElementalEncounter(); auto& State = *Encounter.player.arts.battle;
        check(battleTurn(Encounter, mfHoldCard(State, "earthSword")).ok, "跨轮五剑先施土剑");
        for (const string Neutral : {"gather", "skim", "strike", "defend"}) {
            check(battleTurn(Encounter, mfHoldCard(State, Neutral)).ok, "五行剑间穿插合法中性牌 " + Neutral);
            checkStr(State.last, "土", "中性牌不覆盖最近有效五行");
            checkEq(State.ElementalSwordSequence, 1, "气/神/普通攻防保留五剑进度");
        }
        State.enemyCards.planned.clear();
        check(battleTurn(Encounter, "end").ok, "五行剑跨真实回合边界");
        checkStr(State.last, "土", "回合结束不清五行轨迹");
        const auto Preview = battlePreview(Encounter.player, State, "metalSword");
        check(Preview.ElementalGenerationReady, "跨轮土生金的五行剑由真实预览提示相生可用");
        check(!battlePreview(Encounter.player, State, "skim").ElementalGenerationReady &&
              !battlePreview(Encounter.player, State, "strike").ElementalGenerationReady,
              "神道与普通攻击永不伪装成相生高亮");
        const auto BeforePreview = saveToJson(Encounter);
        static_cast<void>(battlePreview(Encounter.player, State, "metalSword"));
        check(saveToJson(Encounter) == BeforePreview, "跨轮相生预览不推进活动链条或RNG");
        const int HpBefore = State.enemyHp;
        check(battleTurn(Encounter, mfHoldCard(State, "metalSword")).ok, "下一轮金剑接回土生金");
        checkEq(HpBefore - State.enemyHp, Preview.damage, "跨轮相生实际气血损失与预览一致");
        checkEq(State.ElementalSwordSequence, 2, "跨轮后接回第二剑");
        checkEq(State.ElementalSwordManaReturns, 1, "回灵按新回合限额重新计数");
        check(battleTurn(Encounter, mfHoldCard(State, "waterSword")).ok, "再接水剑抵达第三剑");
        checkEq(State.ElementalSwordSequence, 3, "穿插/跨轮后的第三剑补牌进度不丢失");
        check(State.comboDraws > 0, "跨轮三剑仍可触发真实有限补牌");
    }
    {
        Game Encounter = mfCleanseEncounter({"earth", "gold", "gather", "skim"}); auto& State = *Encounter.player.arts.battle;
        Encounter.player.arts.nodes = {"9-1", "9-2", "9-3", "9-4"};
        check(battleTurn(Encounter, mfHoldCard(State, "earth")).ok, "普通相生轨迹先打土盾");
        check(battleTurn(Encounter, mfHoldCard(State, "gather")).ok, "普通相生中穿插气牌");
        check(battleTurn(Encounter, mfHoldCard(State, "skim")).ok, "普通相生中穿插神牌");
        State.enemyCards.planned.clear(); check(battleTurn(Encounter, "end").ok, "普通五行相生跨回合");
        const int ManaBefore = State.mana;
        check(battleTurn(Encounter, mfHoldCard(State, "gold")).ok, "土生金在下一轮仍兑现");
        checkEq(State.mana, ManaBefore - battleCost(Encounter.player, *battleSpell("gold")) + 1, "无五行剑诀的普通相生也跨轮返灵");
        checkEq(State.ElementalSwordSequence, 0, "普通五行牌不冒充五行剑链");
        Battle NoPassive = State; Player Plain = Encounter.player;
        Plain.arts.nodes.clear(); Plain.arts.main.clear(); Plain.arts.aux.clear();
        NoPassive.last = "金";
        check(!battlePreview(Plain, NoPassive, "water").ElementalGenerationReady,
              "无相生被动的普通水盾不虚报收益高亮");
    }
    for (const bool IsShadow : {false, true}) {
        Game Encounter = mfElementalEncounter();
        if (IsShadow) {
            const Player Challenger = Encounter.player;
            battleStartShadow(Encounter, Challenger, Challenger, 20261015);
        }
        auto& State = *Encounter.player.arts.battle;
        mfPlanElementalEnemy(Encounter, {"earthSword", "gather", "skim", "defend"});
        const auto BeforePreview = saveToJson(Encounter);
        const auto Preview = battleEnemyPreview(Encounter);
        check(saveToJson(Encounter) == BeforePreview, "NPC/留影五行链意图纯只读");
        const int HpBefore = State.hp;
        check(battleTurn(Encounter, "end").ok, "NPC/留影真实施放土剑并穿插中性牌");
        checkEq(HpBefore - State.hp, Preview.damage, "NPC/留影实际伤害与共享预览一致");
        checkEq(State.enemyCards.ElementalSwordSequence, 1, "NPC/留影回合末保留五剑进度");
        checkStr(State.enemyCards.last, "土", "NPC/留影中性牌不改写五行轨迹");
        Game Loaded; string Error;
        check(loadDetachedFromJson(saveToJson(Encounter), Loaded, &Error), "NPC/留影五行链跨轮状态可往返：" + Error);
        mfPlanElementalEnemy(Encounter, {"metalSword"});
        check(battleTurn(Encounter, "end").ok, "NPC/留影下一轮继续金剑");
        checkEq(State.enemyCards.ElementalSwordSequence, 2, "NPC/留影跨轮继续第二剑");
        mfPlanElementalEnemy(Encounter, {"fireSword"});
        check(battleTurn(Encounter, "end").ok, "NPC/留影错误五行顺序可正常施放");
        checkEq(State.enemyCards.ElementalSwordSequence, 1, "NPC/留影金后火错序按原规则重开");
    }
    {
        Game Encounter = mfElementalEncounter(); auto& State = *Encounter.player.arts.battle;
        mfPlanElementalEnemy(Encounter, {"earthSword", "metalSword", "waterSword"});
        auto& Enemy = State.enemyCards;
        Enemy.draw.insert(Enemy.draw.end(), Enemy.hand.rbegin(), Enemy.hand.rend());
        Enemy.hand.clear(); Enemy.planned.clear();
        check(battleTurn(Encounter, "end").ok, "推进真实NPC抽牌和计划边界");
        vector<string> PlannedSpells;
        for (const auto& Uid : Enemy.planned) {
            const auto Found = std::find_if(Enemy.deck.begin(), Enemy.deck.end(), [&](const Card& Entry) { return Entry.uid == Uid; });
            if (Found != Enemy.deck.end()) PlannedSpells.push_back(Found->spell);
        }
        check(PlannedSpells.size() >= 3 && PlannedSpells[0] == "earthSword" &&
              PlannedSpells[1] == "metalSword" && PlannedSpells[2] == "waterSword",
              "NPC仅看已抽到手牌，土金水三剑按相生牌序规划");
        Enemy.last = "金"; Enemy.ElementalSwordSequence = 1;
        mfPlanElementalEnemy(Encounter, {"fireSword", "earthSword", "strike"});
        Enemy.draw.insert(Enemy.draw.end(), Enemy.hand.rbegin(), Enemy.hand.rend());
        Enemy.hand.clear(); Enemy.planned.clear();
        check(battleTurn(Encounter, "end").ok, "无匹配剑时重新生成真实计划");
        check(!Enemy.planned.empty(), "无匹配五行剑仍有合理正分回退，不无限等待");
    }
    {
        Game Encounter = mfElementalEncounter();
        auto& State = *Encounter.player.arts.battle;
        State.hp = State.max - 500;
        State.enemyCards.main.clear(); State.enemyCards.manuals.clear(); State.enemyCards.nodes.clear();
        State.enemyShield = 1000;
        const string FirstUid = mfHoldCard(State, "woodSword");
        const int HpBeforeBlocked = State.hp;
        check(battleTurn(Encounter, FirstUid).ok, "木剑满盾探针可真实施放");
        checkEq(State.hp, HpBeforeBlocked, "木剑被全挡时没有虚构吸血");
        State.enemyShield = 0;
        const string SecondUid = mfHoldCard(State, "woodSword", FirstUid);
        const auto Preview = battlePreview(Encounter.player, State, "woodSword");
        const int HpBeforeHit = State.hp;
        check(battleTurn(Encounter, SecondUid).ok, "木剑无盾探针可真实施放");
        checkEq(State.hp - HpBeforeHit, jsRound(Preview.damage * ElementalSword::WoodLifeStealFraction),
                "木剑仅按真实气血损失吸血四分之一");
        checkEq(State.hp - HpBeforeHit, Preview.heal, "木剑实际吸血与预览一致");
    }
    {
        Game Encounter = mfCleanseEncounter({"soulspark", "soulshock"});
        auto& State = *Encounter.player.arts.battle;
        State.enemyCards.main.clear(); State.enemyCards.manuals.clear(); State.enemyCards.nodes.clear();
        State.enemyShield = jsRound(State.enemyMax * .30);
        State.soul = 6;
        const string CardUid = mfHoldCard(State, "soulshock");
        const auto Preview = battlePreview(Encounter.player, State, "soulshock");
        check(Preview.RawDamage > 0 && Preview.damage > 0 && Preview.damage < Preview.RawDamage,
              "六层震荡爆发可以部分穿盾，仍非全额穿透");
        checkEq(Preview.damage, jsRound(Preview.RawDamage * SoulBurstMaximumPierceFraction),
                "满盾六层震荡穿透该次伤害百分之三十");
        const int HpBefore = State.enemyHp;
        check(battleTurn(Encounter, CardUid).ok, "震荡爆发真实施放成功");
        checkEq(State.soul, 0, "爆发消耗全部震荡，没有无成本复用");
        checkEq(HpBefore - State.enemyHp, Preview.damage, "震荡穿盾预览和真实损血一致");
    }
}
} // namespace nztest
