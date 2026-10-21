#include "harness.h"
#include "core/artifact.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/artifact_effects.h"
#include "core/combat/battle_scaling.h"
#include "core/content/forge_content.h"
#include "core/friendship.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/talents.h"

#include <algorithm>

namespace nztest {
using namespace nz;
namespace {
Game mfForgeBattleGame(const string& Effect, int Grade = 5, const string& Slot = "weapon") {
    CreateOptions Options;
    Options.name = "新版法器隔离战斗";
    Options.seed = 928174;
    Options.traitsExplicit = true;
    Options.budget = 40;
    Game State = createGame(Options);
    State.npcs.clear(); State.pending.reset(); State.job.reset();
    Player& Actor = State.player;
    Actor.stage = Actor.highestStage = 12;
    Actor.arts.main.clear(); Actor.arts.manuals.clear(); Actor.arts.aux.clear();
    Actor.arts.nodes.clear(); Actor.arts.gear.clear(); Actor.arts.equipped.clear();
    Actor.arts.spells.clear(); Actor.talent = "plain"; Actor.health = mfPlayerHealthMaximum(Actor);
    Gear Item;
    Item.id = "forge-v2-test"; Item.name = "试炼法器"; Item.slot = Slot;
    Item.shape = Slot == "weapon" ? "box" : Slot == "armor" ? "robe" : "ring";
    Item.element = "神"; Item.polarity = "天阳"; Item.grade = Grade;
    const auto* Definition = content::mfFindForgeEffect(Effect);
    if (Definition && !Definition->Elements.empty()) Item.element = Definition->Elements.front();
    Item.crafted = true; Item.ForgeVersion = 2; Item.EffectId = Effect; Item.EffectPower = 1;
    Item.SourceMaterials = {{"iron", 10}}; Item.ForgeEnergy = 10;
    Item.dimensions = {100, 100, 100, 100, 100};
    Actor.arts.gear.push_back(Item); Actor.arts.equipped[Slot] = Item.id;
    EnemySpec Foe; Foe.name = "隔离木桩"; Foe.beast = true; Foe.speed = 1;
    battleStart(State, Foe);
    Battle& Field = *Actor.arts.battle;
    Field.enemyMax = Field.enemyHp = 10000000; Field.enemyShield = 0;
    Field.mana = 12; Field.max = Field.hp = 1000; Field.shield = 0;
    return State;
}

void mfUseHandCount(Game& State, int Count) {
    Battle& Field = *State.player.arts.battle;
    Field.hand.clear(); Field.draw.clear(); Field.discard.clear(); Field.exhausted.clear();
    for (const auto& CardState : Field.deck)
        (static_cast<int>(Field.hand.size()) < Count ? Field.hand : Field.draw).push_back(CardState.uid);
}

void mfRejectBatchUnchanged(Game& State, const vector<string>& Targets, const string& Label) {
    const string Before = saveToJson(State); const auto Seed = State.rng;
    check(!battleTurn(State, "artifact:forge-v2-test", "", Targets).ok, Label + "拒绝");
    check(saveToJson(State) == Before && State.rng == Seed, Label + "无资源、状态或随机变化");
}

Game mfStatusArtifactGame(const string& Effect, int Grade = 1) {
    Player Actor = mfForgeBattleGame(Effect, Grade, "armor").player;
    Actor.arts.battle.reset(); Actor.arts.speed = 500;
    Player Opponent = Actor;
    Opponent.arts.gear.clear(); Opponent.arts.equipped.clear(); Opponent.arts.speed = 1;
    Game State;
    battleStartShadow(State, Actor, Opponent, 813211);
    State.player.arts.battle->mana = 12;
    return State;
}

void mfPutStatusPoison(Battle& State, int Layers, int Damage) {
    State.playerPoison = Damage; State.playerPoisonTurns = 1;
    State.enemyCards.poison = Layers; State.enemyCards.poisonTurns = 1;
    State.enemyCards.poisonDamage = Damage;
}

void mfRejectStatusUnchanged(Game& State, const string& Target, const string& Label) {
    const string Before = saveToJson(State); const auto Rng = State.rng;
    check(!battleTurn(State, "artifact:forge-v2-test", Target).ok, Label + "拒绝");
    check(saveToJson(State) == Before && State.rng == Rng, Label + "不扣费、不冷却、不改变随机数");
}

void mfRunArtifactStatusRules() {
    std::printf("\n-- 清浊与破护固定层数、单种选择 --\n");
    const int CleanseLayers[]{5, 10, 25, 45, 80};
    for (int Grade = 1; Grade <= 5; ++Grade) {
        Game Cleanse = mfStatusArtifactGame("cleanse", Grade);
        auto& Field = *Cleanse.player.arts.battle;
        mfPutStatusPoison(Field, 99, 198);
        Field.enemyCards.burn = 120; Field.enemyCards.burnTurns = 3;
        Field.lock = 2; Field.enemyWeaken = 2; Field.manaPenalty = 1; Field.hpCostPct = 8;
        const auto Before = saveToJson(Cleanse); const auto Rng = Cleanse.rng;
        const auto View = api::battleView(Cleanse);
        const auto& Artifacts = View["artifacts"].asArray();
        check(!Artifacts.empty() && Artifacts.front()["targetMode"].asString() == "status-one",
            "逐阶清浊由后端下发单状态目标模式");
        if (!Artifacts.empty()) {
            check(!Artifacts.front()["statusToken"].asString().empty(), "清浊目标报价包含状态版本");
            const auto& Targets = Artifacts.front()["targets"].asArray();
            const auto Poison = std::find_if(Targets.begin(), Targets.end(), [](const auto& Entry) {
                return Entry["id"].asString() == "poison";
            });
            check(Poison != Targets.end(), "实际中毒进入清浊可选目标");
            if (Poison != Targets.end()) {
                checkNear((*Poison)["layers"].asNumber(), 99, "清浊预览显示实际原生毒层");
                checkNear((*Poison)["removeLayers"].asNumber(), CleanseLayers[Grade - 1], "逐阶清浊预览显示固定削层数");
                checkNear((*Poison)["remainingLayers"].asNumber(), 99 - CleanseLayers[Grade - 1], "清浊预览给出削除后剩余层数");
            }
        }
        check(saveToJson(Cleanse) == Before && Cleanse.rng == Rng, "状态目标预览不推进状态或随机数");
        check(battleTurn(Cleanse, "artifact:forge-v2-test", "poison").ok, "逐阶清浊可指定中毒执行");
        checkEq(Field.enemyCards.poison, 99 - CleanseLayers[Grade - 1], "逐阶清浊只扣5/10/25/45/80原生毒层");
        checkEq(Field.playerPoison, (99 - CleanseLayers[Grade - 1]) * 2, "削毒层同步按剩余比例减少精确跳伤");
        checkEq(Field.enemyCards.poisonDamage, Field.playerPoison, "削毒后双方精确缓存保持一致");
        checkEq(Field.playerPoisonTurns, 1, "部分清毒不提前结束剩余毒持续时间");
        checkEq(Field.enemyCards.burn, 120, "选毒不顺带清除火池");
        checkEq(Field.lock, 2, "选毒不顺带清除锁灵");
        checkEq(Field.enemyWeaken, 2, "选毒不顺带清除扰灵");
        checkEq(Field.manaPenalty, 1, "定向清浊保留夺灵代价");
        checkEq(Field.hpCostPct, 8, "定向清浊保留聚气代价");
        checkEq(Field.mana, 10, "清浊成功只支付一次实际灵力费用");
        check(Cleanse.rng == Rng, "固定削层本身不掷随机数");

        Game Burning = mfStatusArtifactGame("cleanse", Grade);
        auto& BurnField = *Burning.player.arts.battle;
        mfPutStatusPoison(BurnField, 9, 18);
        BurnField.enemyCards.burn = 120; BurnField.enemyCards.burnTurns = 3;
        check(battleTurn(Burning, "artifact:forge-v2-test", "burn").ok, "逐阶清浊可指定灼烧执行");
        checkEq(BurnField.enemyCards.burn, 120 - CleanseLayers[Grade - 1], "灼烧池每点作为一层按固定额度清除");
        checkEq(BurnField.enemyCards.poison, 9, "选灼烧不扣原生毒层");
        checkEq(BurnField.playerPoison, 18, "选灼烧不改变受毒精确跳伤");

        Game Dispel = mfStatusArtifactGame("dispel", Grade);
        auto& Enemy = Dispel.player.arts.battle->enemyCards;
        Enemy.guard = .145; Enemy.discardPower = .175; Enemy.buff = .18; Enemy.reduction = .08;
        check(battleTurn(Dispel, "artifact:forge-v2-test", "guard").ok, "逐阶破护可指定守势执行");
        checkNear(Enemy.guard, .145 - Grade * .01, "破护逐阶削1/2/3/4/5百分点并保留小数余量");
        checkNear(Enemy.discardPower, .175, "选守势不顺带削锐势");
        checkNear(Enemy.buff, .18, "破护保留本场积累增伤");
        checkNear(Enemy.reduction, .08, "破护保留本场积累减伤");
    }

    Game LowPoison = mfStatusArtifactGame("cleanse");
    auto& LowField = *LowPoison.player.arts.battle;
    mfPutStatusPoison(LowField, 3, 23);
    LowField.enemyCards.burn = 6; LowField.enemyCards.burnTurns = 3;
    check(battleTurn(LowPoison, "artifact:forge-v2-test", "poison").ok, "不足清除额度的毒可正常选择");
    checkEq(LowField.enemyCards.poison, 0, "原生毒层不足额度只清现有层数");
    checkEq(LowField.enemyCards.poisonDamage, 0, "毒全清同步清空精确跳伤缓存");
    checkEq(LowField.playerPoisonTurns, 0, "毒全清同步清空玩家中毒回合");
    checkEq(LowField.enemyCards.poisonTurns, 0, "毒全清同步清空施术方毒持续回合");
    checkEq(LowField.enemyCards.burn, 6, "低毒层清空仍保留其他负面状态");

    Game BeastPoison = mfForgeBattleGame("cleanse", 1, "armor");
    auto& BeastField = *BeastPoison.player.arts.battle;
    BeastField.playerPoison = 13; BeastField.playerPoisonTurns = 1;
    check(BeastField.enemyBeast && !BeastField.enemyCards.enabled,
        "当前妖兽毒使用独立妖兽行动而非人类神通毒层");
    check(battleTurn(BeastPoison, "artifact:forge-v2-test", "poison").ok, "当前妖兽毒可定向清浊");
    checkEq(BeastField.playerPoison, 8, "当前妖兽毒每点跳伤视作一层扣五点");
    checkEq(BeastField.playerPoisonTurns, 1, "部分削除当前妖兽毒保留持续时间");

    Game Locked = mfStatusArtifactGame("cleanse");
    Locked.player.arts.battle->lock = 2; Locked.player.arts.battle->enemyWeaken = 7;
    check(battleTurn(Locked, "artifact:forge-v2-test", "lock").ok, "可单独清浊锁灵");
    checkEq(Locked.player.arts.battle->lock, 0, "锁灵每剩余回合视作一层");
    checkEq(Locked.player.arts.battle->enemyWeaken, 7, "清除锁灵不同时清扰灵");
    Game Weakened = mfStatusArtifactGame("cleanse");
    Weakened.player.arts.battle->lock = 2; Weakened.player.arts.battle->enemyWeaken = 7;
    check(battleTurn(Weakened, "artifact:forge-v2-test", "weaken").ok, "可单独清浊扰灵");
    checkEq(Weakened.player.arts.battle->enemyWeaken, 2, "扰灵按原生层数扣五层");
    checkEq(Weakened.player.arts.battle->lock, 2, "清除扰灵保留锁灵");

    for (double Power : {.1, 1.0, 3.0}) {
        Game Fixed = mfStatusArtifactGame("cleanse");
        Fixed.player.stage = Power < 1 ? 0 : 12;
        Fixed.player.body = Power < 1 ? 0 : 100;
        Fixed.player.arts.gear.front().EffectPower = Power;
        Fixed.player.arts.gear.front().InscriptionId = "azure";
        mfPutStatusPoison(*Fixed.player.arts.battle, 20, 40);
        check(battleTurn(Fixed, "artifact:forge-v2-test", "poison").ok, "不同人物属性和材料利用率均可定向清浊");
        checkEq(Fixed.player.arts.battle->enemyCards.poison, 15, "清浊额度不受境界体魄材料利用率及灵纹放大");
        Game FixedDispel = mfStatusArtifactGame("dispel");
        FixedDispel.player.arts.gear.front().EffectPower = Power;
        FixedDispel.player.arts.gear.front().InscriptionId = "azure";
        FixedDispel.player.arts.battle->enemyCards.discardPower = .175;
        check(battleTurn(FixedDispel, "artifact:forge-v2-test", "sharpness").ok, "不同材料利用率均可单独削锐势");
        checkNear(FixedDispel.player.arts.battle->enemyCards.discardPower, .165, "破护额度不受材料利用率及灵纹放大");
    }

    Game Partial = mfStatusArtifactGame("cleanse");
    mfPutStatusPoison(*Partial.player.arts.battle, 9, 18);
    check(battleTurn(Partial, "artifact:forge-v2-test", "poison").ok, "部分清毒进入持久化场景");
    Game Restored; string Error;
    check(loadDetachedFromJson(saveToJson(Partial), Restored, &Error), "部分清毒后的在途留影状态可无损读档：" + Error);
    if (Restored.player.arts.battle) {
        checkEq(Restored.player.arts.battle->enemyCards.poison, 4, "读档保留清浊后的剩余毒层");
        checkEq(Restored.player.arts.battle->enemyCards.poisonDamage, 8, "读档保留清浊后的精确跳伤");
        checkEq(Restored.player.arts.battle->playerPoison, 8, "读档玩家与敌方毒缓存一致");
    }

    Game Remainder = mfStatusArtifactGame("cleanse");
    mfPutStatusPoison(*Remainder.player.arts.battle, 7, 2);
    check(battleTurn(Remainder, "artifact:forge-v2-test", "poison").ok, "低跳伤余量也可按原生毒层清除");
    checkEq(Remainder.player.arts.battle->enemyCards.poison, 2, "低跳伤清浊保留确实未清掉的原生毒层");
    checkEq(Remainder.player.arts.battle->playerPoison, 1, "部分削毒伤向上取整避免舍入连带全清");
    checkEq(Remainder.player.arts.battle->enemyCards.poisonDamage, 1, "向上取整余量同步到精确毒缓存");
    checkEq(Remainder.player.arts.battle->playerPoisonTurns, 1, "非零毒伤余量保留原持续时间");

    Game LowGuard = mfStatusArtifactGame("dispel");
    LowGuard.player.arts.battle->enemyCards.guard = .005;
    LowGuard.player.arts.battle->enemyCards.discardPower = .175;
    check(battleTurn(LowGuard, "artifact:forge-v2-test", "guard").ok, "不足一个百分点的守势仍可破护");
    checkNear(LowGuard.player.arts.battle->enemyCards.guard, 0, "剩余半层守势清到零而不产生负值");
    checkNear(LowGuard.player.arts.battle->enemyCards.discardPower, .175, "不足额度的守势清空后不顺带削锐势");
    Game LowSharpness = mfStatusArtifactGame("dispel");
    LowSharpness.player.arts.battle->enemyCards.guard = .105;
    LowSharpness.player.arts.battle->enemyCards.discardPower = .003;
    check(battleTurn(LowSharpness, "artifact:forge-v2-test", "sharpness").ok, "不足一个百分点的锐势仍可破护");
    checkNear(LowSharpness.player.arts.battle->enemyCards.discardPower, 0, "剩余小数锐势清到零而不产生负值");
    checkNear(LowSharpness.player.arts.battle->enemyCards.guard, .105, "不足额度的锐势清空后仍保留守势");

    for (const string Effect : {string("cleanse"), string("dispel")}) {
        Game Interval = mfStatusArtifactGame(Effect);
        const string Target = Effect == "cleanse" ? "burn" : "sharpness";
        const auto SetTarget = [&] {
            auto& Field = *Interval.player.arts.battle;
            Field.mana = 12;
            Field.enemyCards.planned.clear(); Field.enemyCards.mana = 0;
            if (Effect == "cleanse") {
                Field.enemyCards.burn = 20; Field.enemyCards.burnTurns = 3;
            } else Field.enemyCards.discardPower = .20;
        };
        SetTarget();
        check(battleTurn(Interval, "artifact:forge-v2-test", Target).ok, Effect + "第一轮可催动");
        checkEq(battleCooldown(*Interval.player.arts.battle, "artifact:forge-v2-test"), 3,
            Effect + "使用后存三次轮转冷却");
        mfRejectStatusUnchanged(Interval, Target, Effect + "同一回合再次催动");
        for (int Round = 2; Round <= 3; ++Round) {
            check(battleTurn(Interval, "end").ok, Effect + "推进完整等待回合");
            SetTarget();
            mfRejectStatusUnchanged(Interval, Target, Effect + "等待第" + std::to_string(Round) + "轮仍不能催动");
        }
        check(battleTurn(Interval, "end").ok, Effect + "推进到第四轮");
        SetTarget();
        check(battleTurn(Interval, "artifact:forge-v2-test", Target).ok,
            Effect + "隔过两个完整自身回合后第四轮才可再催动");
    }
    Game Azure = mfStatusArtifactGame("cleanse");
    Azure.player.arts.gear.front().InscriptionId = "azure";
    mfPutStatusPoison(*Azure.player.arts.battle, 9, 18);
    check(battleTurn(Azure, "artifact:forge-v2-test", "poison").ok, "青龙清浊仍按单状态催动");
    checkEq(battleCooldown(*Azure.player.arts.battle, "artifact:forge-v2-test"), 4,
        "青龙在清浊自身两完整轮冷却上再增加一轮");

    Player MirrorCleanser = mfStatusArtifactGame("cleanse").player;
    MirrorCleanser.arts.battle.reset(); MirrorCleanser.arts.speed = 1;
    Player MirrorActor = MirrorCleanser;
    MirrorActor.arts.gear.clear(); MirrorActor.arts.equipped.clear(); MirrorActor.arts.speed = 500;
    Game PartialMirror;
    battleStartShadow(PartialMirror, MirrorActor, MirrorCleanser, 58134);
    auto& MirrorField = *PartialMirror.player.arts.battle;
    MirrorField.poison = 9; MirrorField.poisonTurns = 1; MirrorField.poisonDamage = 18;
    MirrorField.enemyCards.planned = {"artifact:forge-v2-test"}; MirrorField.enemyCards.mana = 12;
    const auto MirrorBefore = saveToJson(PartialMirror); const auto MirrorRng = PartialMirror.rng;
    const auto EnemyPreview = battleEnemyPreview(PartialMirror);
    check(saveToJson(PartialMirror) == MirrorBefore && PartialMirror.rng == MirrorRng,
        "敌方定向清浊预演不改变真实毒层缓存或随机数");
    check(std::find(EnemyPreview.spells.begin(), EnemyPreview.spells.end(), "artifact:forge-v2-test") !=
        EnemyPreview.spells.end(), "敌方可在预演中选自身中毒并催动清浊");
    check(battleTurn(PartialMirror, "end").ok, "镜像敌方实际催动一阶清浊部分削毒");
    checkEq(MirrorField.poison, 4, "敌方视角部分清毒后回写剩余四层而非原九层");
    checkEq(MirrorField.poisonDamage, 8, "敌方视角部分清毒后回写精确八点跳伤");
    checkEq(MirrorField.poisonTurns, 1, "镜像清毒保留已扣一轮后的毒持续时间");
    const int RemainingPoisonEnemyHp = MirrorField.enemyHp;
    MirrorField.enemyCards.planned.clear(); MirrorField.enemyCards.mana = 0;
    check(battleTurn(PartialMirror, "end").ok, "镜像部分清毒后再推进一轮");
    checkEq(MirrorField.enemyHp, RemainingPoisonEnemyHp - 8, "下轮只结算剩余毒伤而不从旧缓存回生");
    checkEq(MirrorField.poison, 4, "下轮毒成长为零时不恢复被清除的毒层");
    checkEq(MirrorField.poisonDamage, 8, "下轮镜像开合后精确毒伤仍为八点");

    Game Invalid = mfStatusArtifactGame("cleanse");
    mfPutStatusPoison(*Invalid.player.arts.battle, 9, 18);
    mfRejectStatusUnchanged(Invalid, "", "未选择要清除的负面状态");
    mfRejectStatusUnchanged(Invalid, "unknown-status", "未知状态ID");
    mfRejectStatusUnchanged(Invalid, "guard", "向清浊传入增益状态");
    mfRejectStatusUnchanged(Invalid, "burn", "已不存在的负面状态");
    Game InvalidDispel = mfStatusArtifactGame("dispel");
    InvalidDispel.player.arts.battle->enemyCards.guard = .10;
    InvalidDispel.player.arts.battle->enemyCards.buff = .20;
    InvalidDispel.player.arts.battle->enemyCards.reduction = .10;
    mfRejectStatusUnchanged(InvalidDispel, "", "破护未选择目标");
    mfRejectStatusUnchanged(InvalidDispel, "buff", "破护不能削本场积累增伤");
    mfRejectStatusUnchanged(InvalidDispel, "reduction", "破护不能削本场积累减伤");
    mfRejectStatusUnchanged(InvalidDispel, "sharpness", "破护目标状态已消失");
    Game NoRemovable = mfStatusArtifactGame("dispel");
    NoRemovable.player.arts.battle->enemyCards.buff = .20;
    NoRemovable.player.arts.battle->enemyCards.reduction = .10;
    const auto EmptyView = api::battleView(NoRemovable);
    const auto& EmptyArtifacts = EmptyView["artifacts"].asArray();
    check(!EmptyArtifacts.empty() && !EmptyArtifacts.front()["playable"].asBool(),
        "对手仅有本场积累增伤减伤时破护不可空放");
    if (!EmptyArtifacts.empty()) {
        check(EmptyArtifacts.front()["targets"].asArray().empty(), "不可移除的积累状态不会伪造可选破护目标");
        check(!EmptyArtifacts.front()["reason"].asString().empty(), "无可破护目标时下发明确原因");
    }
}
}

void mfRunForgeArtifactTests() {
    std::printf("\n-- 新版法器战斗、抽牌与等量换牌 --\n");
    mfRunArtifactStatusRules();
    Game State = mfForgeBattleGame("draw");
    const auto Ability = equippedArtifactAbility(State.player, "artifact:forge-v2-test");
    check(Ability.has_value(), "新版抽牌器匣可生成已装备主动能力");
    if (!Ability) return;
    const Preview Quote = battlePreview(State.player, *State.player.arts.battle, Ability->spell.id);
    checkEq(Quote.cost, 3, "五阶主动抽牌实际三费，不再二次折半");
    checkEq(Quote.draw, 5, "五阶主动抽牌固定基础五张");
    const int Before = static_cast<int>(State.player.arts.battle->hand.size());
    check(battleTurn(State, Ability->spell.id).ok, "新版抽牌主动真实可施放");
    checkEq(State.player.arts.battle->hand.size(), Before + 5, "抽牌主动实际补五张");
    checkEq(State.player.arts.battle->mana, 9, "抽牌实际支付三点灵力");
    const string Saved = saveToJson(State);
    check(!battleTurn(State, Ability->spell.id).ok && saveToJson(State) == Saved,
        "同回合重复点击冷却法器原子拒绝");

    const int DrawCosts[]{1, 2, 2, 3, 3};
    const int RedrawCosts[]{1, 1, 2, 2, 3};
    for (int Grade = 1; Grade <= 5; ++Grade) {
        Game DrawState = mfForgeBattleGame("draw", Grade);
        const auto DrawPreview = battlePreview(DrawState.player, *DrawState.player.arts.battle, "artifact:forge-v2-test");
        checkEq(DrawPreview.cost, DrawCosts[Grade - 1], "逐阶抽牌实际费用");
        checkEq(DrawPreview.draw, Grade, "逐阶抽牌基础张数");
        Game RedrawState = mfForgeBattleGame("redraw", Grade);
        mfUseHandCount(RedrawState, 2);
        const vector<string> Targets{RedrawState.player.arts.battle->hand.front()};
        const auto BatchPreview = battlePreview(RedrawState.player, *RedrawState.player.arts.battle, "artifact:forge-v2-test", Targets);
        checkEq(BatchPreview.cost, RedrawCosts[Grade - 1], "逐阶换牌实际费用");
        checkEq(BatchPreview.draw, 1, "只选一张即只补一张");
        check(battleTurn(RedrawState, "artifact:forge-v2-test", "", Targets).ok, "各阶均允许少选一张换牌");
        checkEq(RedrawState.player.arts.battle->hand.size(), 2, "等量换牌实际手牌总数不变");
        checkEq(RedrawState.player.arts.battle->mana, 12 - RedrawCosts[Grade - 1], "换牌真实支付实际费用");
    }

    Game BatchState = mfForgeBattleGame("redraw");
    mfUseHandCount(BatchState, 2);
    BatchState.player.arts.main = "gold";
    BatchState.player.arts.manuals = {{"gold", 5}, {"earth", 5}, {"mountain", 5}};
    BatchState.player.arts.aux = {"earth", "mountain"};
    auto Targets = BatchState.player.arts.battle->hand;
    for (auto& CardState : BatchState.player.arts.battle->deck)
        if (CardState.uid == Targets.front()) CardState.Retain = true;
    const auto Mods = talentMods(BatchState.player);
    const int ExpectedShield = std::min(300, jsRound(1000 * Mods.discardShield * 2));
    const auto ReadOnlyBefore = saveToJson(BatchState); const auto RngBefore = BatchState.rng;
    const auto BatchQuote = battlePreview(BatchState.player, *BatchState.player.arts.battle, "artifact:forge-v2-test", Targets);
    check(saveToJson(BatchState) == ReadOnlyBefore && BatchState.rng == RngBefore, "定向换牌预览完全只读且不公开未来卡");
    checkEq(BatchQuote.draw, 2, "五阶只有两张手牌可以换二补二");
    check(battleTurn(BatchState, "artifact:forge-v2-test", "", Targets).ok, "保留本命手牌允许主动弃置");
    checkEq(BatchState.player.arts.battle->hand.size(), 2, "保留牌参与等量换牌后总手牌仍二");
    checkEq(BatchState.player.arts.battle->shield, ExpectedShield, "法器主动弃牌触发完整金道凝盾");
    checkNear(BatchState.player.arts.battle->discardPower, std::min(.30, Mods.discardPower * 2), "主动弃牌触发锐势");

    Game Bonus = mfForgeBattleGame("redraw"); mfUseHandCount(Bonus, 2);
    Bonus.player.arts.main = "visual"; Bonus.player.arts.manuals = {{"visual", 5}};
    check(talentMods(Bonus.player).drawBonus >= 1, "测试构筑确实具有普通抽牌加成");
    const auto BonusTargets = Bonus.player.arts.battle->hand;
    checkEq(battlePreview(Bonus.player, *Bonus.player.arts.battle, "artifact:forge-v2-test", BonusTargets).draw,
        2, "等量换牌的核心补牌不叠加普通drawBonus");
    check(battleTurn(Bonus, "artifact:forge-v2-test", "", BonusTargets).ok, "抽牌加成构筑可正常等量换牌");
    checkEq(Bonus.player.arts.battle->hand.size(), 2, "抽牌加成不会把弃二补二变成多抽");

    Game Invalid = mfForgeBattleGame("redraw");
    const string First = Invalid.player.arts.battle->hand.front();
    mfRejectBatchUnchanged(Invalid, {}, "未选择目标");
    mfRejectBatchUnchanged(Invalid, {First, First}, "重复目标UID");
    mfRejectBatchUnchanged(Invalid, {First, "stale-card"}, "过期或未知目标UID");
    Invalid.player.arts.battle->draw.push_back("broken-card");
    mfRejectBatchUnchanged(Invalid, {First}, "牌堆异常");
    Game Empty = mfForgeBattleGame("redraw"); mfUseHandCount(Empty, 0);
    mfRejectBatchUnchanged(Empty, {}, "空手换牌");
    Game Full = mfForgeBattleGame("draw");
    auto& FullBattle = *Full.player.arts.battle;
    while (FullBattle.deck.size() < 12)
        FullBattle.deck.push_back({"full-" + std::to_string(FullBattle.deck.size()), "strike"});
    mfUseHandCount(Full, 12);
    const string FullBefore = saveToJson(Full);
    check(!battleTurn(Full, "artifact:forge-v2-test").ok && saveToJson(Full) == FullBefore, "满手抽牌无效果时不扣费不冷却");

    Game Shuffle = mfForgeBattleGame("redraw"); mfUseHandCount(Shuffle, 2);
    auto& ShuffleBattle = *Shuffle.player.arts.battle;
    ShuffleBattle.deck.resize(2); ShuffleBattle.draw.clear();
    const auto ShuffleTargets = ShuffleBattle.hand;
    check(battleTurn(Shuffle, "artifact:forge-v2-test", "", ShuffleTargets).ok,
        "牌堆空时可洗回刚弃出的牌完成等量换牌");
    checkEq(Shuffle.player.arts.battle->hand.size(), 2, "刚弃牌洗回后仍补足两张");

    for (const auto& Definition : content::mfForgeEffects()) {
        if (Definition.Id == "redraw" || Definition.Id == "cleanse" || Definition.Id == "dispel") continue;
        Game EffectState = mfForgeBattleGame(Definition.Id, 5, Definition.Slot);
        if (Definition.Id == "poison_burst") {
            EffectState.player.arts.battle->poison = 8;
            EffectState.player.arts.battle->poisonTurns = 1;
        } else if (Definition.Id == "fire_burst") {
            EffectState.player.arts.battle->burn = 20;
            EffectState.player.arts.battle->burnTurns = 3;
        }
        const Gear& Item = EffectState.player.arts.gear.front();
        check(!mfArtifactEffectDescription(Item).empty(), Definition.Id + "主效果有量化说明");
        if (Definition.Passive) {
            check(equippedArtifactAbilities(EffectState.player).empty(), Definition.Id + "辅助效果不生成伪主动按钮");
            checkEq(EffectState.player.arts.battle->ArtifactState.Version, 2, Definition.Id + "被动已在入战冻结");
        } else {
            const auto EffectQuote = battlePreview(EffectState.player, *EffectState.player.arts.battle, "artifact:forge-v2-test");
            const int BeforeHp = EffectState.player.arts.battle->enemyHp;
            const int BeforeShield = EffectState.player.arts.battle->shield;
            check(battleTurn(EffectState, "artifact:forge-v2-test").ok, Definition.Id + "真实主动可执行");
            checkEq(BeforeHp - EffectState.player.arts.battle->enemyHp, EffectQuote.damage, Definition.Id + "伤害与预览一致");
            checkEq(EffectState.player.arts.battle->shield - BeforeShield, EffectQuote.shield, Definition.Id + "护盾与预览一致");
        }
    }

    Game Support = mfForgeBattleGame("cap_water", 5, "accessory");
    checkEq(Support.player.arts.battle->ArtifactState.WaterCap, 9, "五阶辅助提高水势容量至九层");
    Support.player.arts.gear.front().grade = 1;
    checkEq(Support.player.arts.battle->ArtifactState.WaterCap, 9, "战斗冻结的上限不受场外装备数据后改影响");
    check(mfArtifactEffectDescription(Support.player.arts.gear.front()).find("6→7") != string::npos, "上限词条明确说明基础值和新上限");
    Support.player.arts.battle->water = 7;
    Support.player.arts.battle->deck.push_back({"cap-water-card", "waterSeal"});
    Support.player.arts.battle->hand.push_back("cap-water-card");
    check(battleTurn(Support, "cap-water-card").ok, "原有水系神通可在法器扩容后正常施放");
    checkEq(Support.player.arts.battle->water, 9, "原有神通真实使用本场九层上限");

    Game Interval = mfForgeBattleGame("draw");
    check(battleTurn(Interval, "artifact:forge-v2-test").ok, "第一轮催动抽牌法器");
    check(battleTurn(Interval, "end").ok, "推进到第二轮");
    const string IntervalBefore = saveToJson(Interval);
    check(!battleTurn(Interval, "artifact:forge-v2-test").ok && saveToJson(Interval) == IntervalBefore,
        "第二轮仍处于完整冷却且拒绝不变更状态");
    check(battleTurn(Interval, "end").ok, "推进到第三轮");
    check(battleTurn(Interval, "artifact:forge-v2-test").ok, "最早第三轮可以再次催动");

    Game Supply = mfForgeBattleGame("turn_sword", 5, "accessory");
    const int StartingQi = Supply.player.arts.battle->swordQi;
    checkEq(StartingQi, 2, "五阶养锋入战提供两层剑气");
    check(battleTurn(Supply, "end").ok, "携带每轮供给法器推进一轮");
    checkEq(Supply.player.arts.battle->swordQi, StartingQi + 2, "每个新自身回合再供给两层剑气");

    Player ShadowActor = mfForgeBattleGame("draw").player;
    Player ShadowFoe = ShadowActor;
    ShadowActor.arts.battle.reset(); ShadowFoe.arts.battle.reset();
    ShadowActor.arts.speed = 500; ShadowFoe.arts.speed = 1;
    Game Mirror;
    battleStartShadow(Mirror, ShadowActor, ShadowFoe, 19273);
    check(std::find(Mirror.player.arts.battle->enemyCards.planned.begin(),
        Mirror.player.arts.battle->enemyCards.planned.end(), "artifact:forge-v2-test") !=
        Mirror.player.arts.battle->enemyCards.planned.end(), "留影敌方规划可使用新版抽牌法器");
    const auto MirrorBefore = saveToJson(Mirror); const auto MirrorRng = Mirror.rng;
    const auto MirrorPreview = battleEnemyPreview(Mirror);
    check(saveToJson(Mirror) == MirrorBefore && Mirror.rng == MirrorRng, "新版敌方法器连招预览不修改活动状态");
    check(std::find(MirrorPreview.spells.begin(), MirrorPreview.spells.end(), "artifact:forge-v2-test") !=
        MirrorPreview.spells.end(), "留影真实共用法器主动路径能进入意图预览");

    Game White = mfForgeBattleGame("redraw");
    White.player.arts.gear.front().InscriptionId = "white";
    White.player.arts.battle->hp = 40;
    mfRejectBatchUnchanged(White, {White.player.arts.battle->hand.front()}, "白虎气血代价可能致死");

    Player Cleanser = mfForgeBattleGame("cleanse", 5, "armor").player;
    Cleanser.arts.battle.reset(); Cleanser.arts.speed = 500;
    Player Unarmed = Cleanser;
    Unarmed.arts.gear.clear(); Unarmed.arts.equipped.clear(); Unarmed.arts.speed = 1;
    Game Cleansing;
    battleStartShadow(Cleansing, Cleanser, Unarmed, 58132);
    auto& Poisoned = *Cleansing.player.arts.battle;
    Poisoned.playerPoison = 30; Poisoned.playerPoisonTurns = 1;
    Poisoned.enemyCards.poison = 3; Poisoned.enemyCards.poisonTurns = 1; Poisoned.enemyCards.poisonDamage = 30;
    Poisoned.enemyCards.planned.clear(); Poisoned.enemyCards.mana = 0;
    Poisoned.lock = 1; Poisoned.enemyWeaken = 1; Poisoned.manaPenalty = 1; Poisoned.hpCostPct = 8;
    Poisoned.mana = 12;
    const int PoisonedHp = Poisoned.hp;
    check(battleTurn(Cleansing, "artifact:forge-v2-test", "poison").ok, "玩家实际中毒后可定向清浊净化");
    checkEq(Cleansing.player.arts.battle->enemyCards.poisonDamage, 0, "玩家净化同时清除敌方毒伤缓存");
    checkEq(Cleansing.player.arts.battle->enemyCards.poisonTurns, 0, "玩家净化同时清除敌方毒持续回合");
    checkEq(Cleansing.player.arts.battle->manaPenalty, 1, "净化保留夺灵的永久代价");
    checkEq(Cleansing.player.arts.battle->hpCostPct, 8, "净化保留聚气的永久气血代价");
    checkEq(Cleansing.player.arts.battle->lock, 1, "定向清毒保留锁灵");
    checkEq(Cleansing.player.arts.battle->enemyWeaken, 1, "定向清毒保留扰灵");
    check(battleTurn(Cleansing, "end").ok, "净化后敌方无重施毒时推进回合");
    checkEq(Cleansing.player.arts.battle->playerPoisonTurns, 0, "敌方行动后不会从旧毒缓存写回中毒");
    checkEq(Cleansing.player.arts.battle->playerPoison, 0, "敌方行动后不会恢复旧每跳毒伤");
    checkEq(Cleansing.player.arts.battle->hp, PoisonedHp, "无重施毒回合不产生残余毒伤");

    Player MirrorCleanser = Cleanser; MirrorCleanser.arts.speed = 1;
    Unarmed.arts.speed = 500;
    Game CleansingMirror;
    battleStartShadow(CleansingMirror, Unarmed, MirrorCleanser, 58133);
    auto& TargetPoison = *CleansingMirror.player.arts.battle;
    TargetPoison.poison = 3; TargetPoison.poisonTurns = 1; TargetPoison.poisonDamage = 30;
    TargetPoison.enemyCards.planned = {"artifact:forge-v2-test"}; TargetPoison.enemyCards.mana = 12;
    check(battleTurn(CleansingMirror, "end").ok, "镜像敌方真实催动清浊净化自身中毒");
    checkEq(CleansingMirror.player.arts.battle->poison, 0, "镜像净化清除玩家施加的毒层");
    checkEq(CleansingMirror.player.arts.battle->poisonTurns, 0, "镜像净化清除玩家施加的毒持续回合");
    checkEq(CleansingMirror.player.arts.battle->poisonDamage, 0, "镜像净化清除玩家毒伤缓存");
    const int CleansedEnemyHp = CleansingMirror.player.arts.battle->enemyHp;
    check(battleTurn(CleansingMirror, "end").ok, "镜像净化后再推进一回合");
    checkEq(CleansingMirror.player.arts.battle->enemyHp, CleansedEnemyHp, "镜像无重施毒后不再结算旧毒伤");

    Game BloodCost = mfForgeBattleGame("draw");
    BloodCost.player.arts.gear.front().InscriptionId = "white";
    BloodCost.player.arts.battle->shield = 300; BloodCost.player.arts.battle->guard = .30;
    check(battleTurn(BloodCost, "artifact:forge-v2-test").ok, "白虎灵纹合法支付真实气血后抽牌");
    checkEq(BloodCost.player.arts.battle->hp, 960, "白虎4%真实气血费用不受守势减免");
    checkEq(BloodCost.player.arts.battle->shield, 300, "白虎真实气血费用不由护盾抵扣");

    Game NpcState = mfForgeBattleGame("draw");
    NpcState.player.arts.battle.reset(); NpcState.player.arts.equipped.clear();
    Npc Recipient;
    Recipient.id = "forge-npc-recipient"; Recipient.name = "法器受赠者";
    Recipient.stage = 12; Recipient.dao = "神"; Recipient.location = NpcState.player.location;
    NpcState.npcs = {Recipient};
    check(friendship::giftItem(NpcState, Recipient.id, "gear", "forge-v2-test").ok,
        "通过真实赠礼入口把新版抽牌法器交给普通NPC");
    checkStr(NpcState.npcs.front().equipped["weapon"], "forge-v2-test", "普通NPC空槽位实际装备受赠法器");
    NpcState.npcs.front().bag.clear();
    EnemySpec NpcEnemy; NpcEnemy.name = Recipient.name; NpcEnemy.id = Recipient.id;
    NpcEnemy.stage = 12; NpcEnemy.style = "神"; NpcEnemy.speed = 1;
    battleStart(NpcState, NpcEnemy);
    auto& NpcBattle = *NpcState.player.arts.battle;
    checkEq(NpcBattle.enemyCards.poisonDamage, 0, "无被动施毒的新NPC以精确零初始化毒跳伤");
    NpcBattle.enemyCards.mana = 12;
    NpcBattle.enemyCards.planned = {"artifact:forge-v2-test"};
    check(!NpcBattle.shadowPlayer && mfEnemyArtifactAvailable(NpcState, NpcBattle, "artifact:forge-v2-test"),
        "普通NPC法器ID由真实已装备能力识别，不依赖shadow豁免");
    Game LoadedNpcPlan; string PlanError;
    check(loadFromJson(saveToJson(NpcState), LoadedNpcPlan, &PlanError),
        "普通NPC已装备新版法器进入planned后可保存读取：" + PlanError);
    check(battleTurn(NpcState, "end").ok, "普通NPC真实催动受赠新版抽牌法器");
    checkEq(NpcState.player.arts.battle->enemyCards.cooldowns["artifact:forge-v2-test"], 1,
        "普通NPC催动后实际保留法器冷却");
    Game LoadedNpcCooldown; string CooldownError;
    check(loadFromJson(saveToJson(NpcState), LoadedNpcCooldown, &CooldownError),
        "普通NPC新版法器冷却存读档往返：" + CooldownError);

    Game PoisonSupply = mfForgeBattleGame("turn_poison", 5, "accessory");
    PoisonSupply.player.arts.battle.reset(); PoisonSupply.player.arts.equipped.clear();
    Npc PoisonRecipient = Recipient; PoisonRecipient.id = "forge-poison-recipient";
    PoisonRecipient.dao = "木"; PoisonRecipient.location = PoisonSupply.player.location;
    PoisonSupply.npcs = {PoisonRecipient};
    check(friendship::giftItem(PoisonSupply, PoisonRecipient.id, "gear", "forge-v2-test").ok,
        "真实赠送每轮施毒法器给普通NPC");
    PoisonSupply.npcs.front().bag.clear();
    EnemySpec PoisonEnemy; PoisonEnemy.name = PoisonRecipient.name; PoisonEnemy.id = PoisonRecipient.id;
    PoisonEnemy.stage = 12; PoisonEnemy.style = "木"; PoisonEnemy.speed = 1;
    battleStart(PoisonSupply, PoisonEnemy);
    const auto& PoisonField = *PoisonSupply.player.arts.battle;
    checkEq(PoisonField.enemyCards.poison, 3, "新NPC保留五阶法器入战施加的三层毒");
    check(PoisonField.enemyCards.poisonDamage > 0, "被动施毒新NPC初始化保留精确正数跳伤");
    checkEq(PoisonField.playerPoison, PoisonField.enemyCards.poisonDamage, "NPC入战被动施毒与玩家受到的毒跳伤一致");
}
} // namespace nztest
