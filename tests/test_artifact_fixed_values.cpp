#include "harness.h"
#include "core/api.h"
#include "core/artifact.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/artifact_effects.h"
#include "core/combat/battle_scaling.h"
#include "core/content/forge_content.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/talents.h"

#include <array>

namespace nztest {
using namespace nz;
namespace {
Game mfFixedArtifactGame(const string& Effect, int Grade, int Stage, double MaterialPower = 1) {
    CreateOptions Options;
    Options.name = "固定法器隔离测试"; Options.seed = 623891; Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.npcs.clear(); State.pending.reset(); State.job.reset();
    auto& Actor = State.player;
    Actor.stage = Actor.highestStage = Stage; Actor.talent = "plain";
    Actor.arts.main.clear(); Actor.arts.manuals.clear(); Actor.arts.aux.clear();
    Actor.arts.nodes.clear(); Actor.arts.gear.clear(); Actor.arts.equipped.clear();
    Actor.arts.spells.clear(); Actor.health = mfPlayerHealthMaximum(Actor);
    const auto* Definition = content::mfFindForgeEffect(Effect);
    Gear Item;
    Item.id = "fixed-artifact"; Item.name = "固定数值法器"; Item.grade = Grade;
    Item.slot = Definition ? Definition->Slot : "weapon";
    Item.shape = Item.slot == "weapon" ? "box" : Item.slot == "armor" ? "robe" : "ring";
    Item.element = Definition && !Definition->Elements.empty() ? Definition->Elements.front() : "神";
    Item.polarity = "天阳"; Item.crafted = true; Item.ForgeVersion = 2;
    Item.EffectId = Effect; Item.EffectPower = MaterialPower;
    Item.SourceMaterials = {{"iron", 10}}; Item.ForgeEnergy = 10;
    Item.dimensions = {100, 100, 100, 100, 100};
    Actor.arts.gear.push_back(Item); Actor.arts.equipped[Item.slot] = Item.id;
    EnemySpec Foe; Foe.name = "固定法器木桩"; Foe.beast = true; Foe.speed = 1;
    battleStart(State, Foe);
    auto& Field = *Actor.arts.battle;
    Field.enemyMax = Field.enemyHp = 10000000; Field.enemyShield = 0;
    Field.mana = 12; Field.max = Field.hp = 100000; Field.shield = 0;
    Field.poison = Field.poisonTurns = Field.burn = Field.burnTurns = 0;
    Field.poisonDamage = 0; Field.water = Field.swordQi = Field.soul = Field.charge = 0;
    return State;
}

void mfAmplifyFixedArtifactActor(Player& Actor) {
    Actor.arts.main = "fire";
    Actor.arts.aux = {"sword", "earth", "playfire", "venom"};
    Actor.arts.manuals = {{"fire", 5}, {"sword", 5}, {"earth", 5}, {"playfire", 5}, {"venom", 5}};
    Actor.arts.nodes = {"0-1", "0-2", "0-3", "1-1", "1-2", "3-2", "3-4", "3-5", "4-1", "7-1", "7-2", "7-3"};
    Actor.arts.combatBuff = 10; Actor.body = 100; Actor.arts.hpBonus = 1000;
}
}

void mfRunArtifactFixedValueTests() {
    std::printf("\n-- 法器固定品阶数值及人物属性隔离 --\n");
    const std::array<int, 5> Damage{20, 40, 120, 450, 1800};
    const std::array<int, 5> Shield{20, 50, 150, 500, 2000};
    const std::array<int, 5> SupplyShield{3, 8, 25, 100, 400};
    const std::array<string, 6> Effects{"damage", "water_burst", "sword_burst", "soul_burst",
        "charge_burst", "sharpness_burst"};
    checkEq(mfArtifactBaseDamage(0), 0, "零阶不产生法器基础伤害");
    checkEq(mfArtifactBaseDamage(6), 0, "六阶不产生法器基础伤害");
    for (int Grade = 1; Grade <= 5; ++Grade) {
        const string Label = std::to_string(Grade) + "阶";
        checkEq(mfArtifactBaseDamage(Grade), Damage[Grade - 1], Label + "固定直伤表");
        for (int Stage : {(Grade - 1) * 3, 12}) {
            for (const double MaterialPower : {.1, 1., 3.}) {
                for (const auto& Effect : Effects) {
                    Game State = mfFixedArtifactGame(Effect, Grade, Stage, MaterialPower);
                    auto& Field = *State.player.arts.battle;
                    const auto Before = saveToJson(State); const auto Rng = State.rng;
                    const auto Quote = battlePreview(State.player, Field, "artifact:fixed-artifact");
                    check(Quote.valid, Label + Effect + "真实预览可用");
                    checkEq(Quote.RawDamage, Damage[Grade - 1], Label + Effect + "空资源直伤不随人物境界或材料效力变化");
                    check(saveToJson(State) == Before && State.rng == Rng, "固定法器预览保持状态与随机数不变");
                    const int BeforeHp = Field.enemyHp;
                    check(battleTurn(State, "artifact:fixed-artifact").ok, Label + Effect + "实际催动成功");
                    checkEq(BeforeHp - Field.enemyHp, Damage[Grade - 1], Label + Effect + "实际伤害等于固定品阶点数");
                }
                Game State = mfFixedArtifactGame("shield", Grade, Stage, MaterialPower);
                auto& Field = *State.player.arts.battle;
                const auto Quote = battlePreview(State.player, Field, "artifact:fixed-artifact");
                checkEq(Quote.shield, Shield[Grade - 1], Label + "护盾预览不随境界、气血和材料变化");
                check(battleTurn(State, "artifact:fixed-artifact").ok, Label + "固定护盾实际催动");
                checkEq(Field.shield, Shield[Grade - 1], Label + "实际获得固定护盾");
            }
            Game Supply = mfFixedArtifactGame("turn_shield", Grade, Stage);
            auto& Field = *Supply.player.arts.battle;
            checkEq(Field.ArtifactState.RoundShield, SupplyShield[Grade - 1], Label + "冻结每轮固定供盾");
            mfApplyArtifactRoundSupply(Supply.player, Field);
            checkEq(Field.shield, SupplyShield[Grade - 1], Label + "实际每轮供盾固定");
        }
        Game Buffed = mfFixedArtifactGame("damage", Grade, 12);
        auto& Field = *Buffed.player.arts.battle;
        Field.buff = 5; Field.discardPower = .30; Field.FireGrowthRounds = 5;
        mfAmplifyFixedArtifactActor(Buffed.player);
        const auto Mods = talentMods(Buffed.player);
        check(Mods.attack > 1 && Mods.sword > 1 && Mods.shield > 1 && Mods.burnBonus > 0,
            "人物夹具真实具有攻击、剑系、护盾及灼烧加成");
        const auto Quote = battlePreview(Buffed.player, Field, "artifact:fixed-artifact");
        checkEq(Quote.RawDamage, Damage[Grade - 1], Label + "临时增伤、锐势、火成长和人物准备不放大普通法器");
        check(battleTurn(Buffed, "artifact:fixed-artifact").ok, Label + "增益人物实际催动固定法器");
        checkNear(Field.discardPower, .30, "非锐势引爆法器不消耗锐势");
    }

    const std::array<int, 5> ActiveBurn{1, 2, 5, 9, 16};
    const std::array<int, 5> RoundBurn{1, 1, 2, 4, 8};
    const std::array<int, 5> PoisonStrikePerLayer{1, 2, 5, 15, 45};
    const std::array<double, 5> BurnStrikePerPoint{1, 1.25, 1.5, 1.75, 2};
    for (int Grade = 1; Grade <= 5; ++Grade) {
        Game Fire = mfFixedArtifactGame("fire_apply", Grade, 12, 3);
        mfAmplifyFixedArtifactActor(Fire.player);
        auto& FireField = *Fire.player.arts.battle;
        FireField.FireGrowthRounds = 5; FireField.buff = 5;
        check(battleTurn(Fire, "artifact:fixed-artifact").ok, "固定灼烧实际施加成功");
        checkEq(FireField.burn, ActiveBurn[Grade - 1], "法器灼烧池按品阶固定，不受人物火道或材料效力增加");
        checkEq(FireField.burnTurns, 3, "法器灼烧固定持续三轮");

        Game SupplyFire = mfFixedArtifactGame("turn_fire", Grade, 12, 3);
        mfAmplifyFixedArtifactActor(SupplyFire.player);
        auto& SupplyField = *SupplyFire.player.arts.battle;
        mfApplyArtifactRoundSupply(SupplyFire.player, SupplyField);
        checkEq(SupplyField.burn, RoundBurn[Grade - 1], "每轮供火使用固定品阶灼烧点数");

        Game Poison = mfFixedArtifactGame("poison_apply", Grade, 12);
        mfAmplifyFixedArtifactActor(Poison.player);
        auto& PoisonField = *Poison.player.arts.battle;
        check(battleTurn(Poison, "artifact:fixed-artifact").ok, "固定法器施毒成功");
        checkEq(PoisonField.poison, 2, "法器施毒不额外读取人物施毒层数加成");
        checkEq(PoisonField.poisonDamage, 2 * Grade, "每层法器毒新增跳伤为法器品阶点数");

        Game SupplyPoison = mfFixedArtifactGame("turn_poison", Grade, 12);
        auto& SupplyPoisonField = *SupplyPoison.player.arts.battle;
        mfApplyArtifactRoundSupply(SupplyPoison.player, SupplyPoisonField);
        checkEq(SupplyPoisonField.poison, 1 + Grade / 2, "每轮法器供毒保留品阶层数");
        checkEq(SupplyPoisonField.poisonDamage, (1 + Grade / 2) * Grade, "每轮供毒每层仅新增品阶跳伤");

        Game Burst = mfFixedArtifactGame("poison_burst", Grade, 12);
        auto& BurstField = *Burst.player.arts.battle;
        BurstField.poison = 7; BurstField.poisonDamage = 9999; BurstField.poisonTurns = 1;
        const auto PoisonPreview = battlePreview(Burst.player, BurstField, "artifact:fixed-artifact");
        checkEq(PoisonPreview.RawDamage, PoisonStrikePerLayer[Grade - 1] * 7,
            "毒袭仅按现有毒层乘品阶系数，不加固定直伤或毒跳伤");
        const int BeforePoisonHp = BurstField.enemyHp;
        check(battleTurn(Burst, "artifact:fixed-artifact").ok, "固定毒发真实催动");
        checkEq(BeforePoisonHp - BurstField.enemyHp, PoisonPreview.damage, "毒袭真实伤害等于预览");
        checkEq(BurstField.poison, 7, "法器毒袭保留全部毒层");
        checkEq(BurstField.poisonTurns, 1, "法器毒袭不刷新或消耗毒持续回合");
        checkEq(BurstField.poisonDamage, 9999, "法器毒袭保留混合毒跳伤缓存");

        Game FireBurst = mfFixedArtifactGame("fire_burst", Grade, 12);
        mfAmplifyFixedArtifactActor(FireBurst.player);
        auto& BurstFire = *FireBurst.player.arts.battle;
        BurstFire.burn = 11; BurstFire.burnTurns = 3; BurstFire.FireGrowthRounds = 5;
        const auto FirePreview = battlePreview(FireBurst.player, BurstFire, "artifact:fixed-artifact");
        checkEq(FirePreview.RawDamage, jsRound(11 * BurnStrikePerPoint[Grade - 1]),
            "灼击仅按灼烧量乘品阶系数，不叠固定直伤或人物火道成长");
        check(FirePreview.text.find("保留灼烧") != string::npos,
            "法器灼击预览明确保留灼烧");
        checkEq(BurstFire.burn, 11, "引爆信息预览不消耗真实灼烧池");
        Battle LastTurn = BurstFire; LastTurn.burnTurns = 1;
        checkEq(battlePreview(FireBurst.player, LastTurn, "artifact:fixed-artifact").RawDamage,
            FirePreview.RawDamage, "法器灼击追加伤害不读取剩余持续回合");
        check(battleTurn(FireBurst, "artifact:fixed-artifact").ok, "信息优化后的引爆仍走真实结算");
        checkEq(BurstFire.burn, 11, "法器灼击真实结算保留灼烧层数");
        checkEq(BurstFire.burnTurns, 3, "法器灼击真实结算保留灼烧持续回合");

        for (const auto& Effect : {string("poison_burst"), string("fire_burst")}) {
            for (const int Stage : {(Grade - 1) * 3, 12}) {
                for (const double MaterialPower : {.1, 1., 3.}) {
                    Game Strike = mfFixedArtifactGame(Effect, Grade, Stage, MaterialPower);
                    auto& StrikeField = *Strike.player.arts.battle;
                    StrikeField.poison = 8; StrikeField.poisonTurns = 1; StrikeField.poisonDamage = 900;
                    StrikeField.burn = 20; StrikeField.burnTurns = 2;
                    const int Expected = Effect == "poison_burst" ? 8 * PoisonStrikePerLayer[Grade - 1] :
                        jsRound(20 * BurnStrikePerPoint[Grade - 1]);
                    const auto Before = saveToJson(Strike); const auto Rng = Strike.rng;
                    const auto StrikePreview = battlePreview(Strike.player, StrikeField, "artifact:fixed-artifact");
                    const auto View = api::battleView(Strike);
                    const auto& Breakdown = View["artifacts"].asArray().front()["breakdown"];
                    checkEq(StrikePreview.RawDamage, Expected, "状态打击不随人物境界或材料效力变化");
                    checkNear(Breakdown["statusDamage"].asNumber(), Expected, "API明细下发同一规则计算的状态伤害");
                    check(!Breakdown["consumesStatus"].asBool(), "API明细明确状态打击不消耗状态");
                    check(saveToJson(Strike) == Before && Strike.rng == Rng, "状态打击与API预览不改状态或随机数");
                    check(battleTurn(Strike, "artifact:fixed-artifact").ok, "不同境界和材料效力可真实使用状态打击");
                    checkEq(StrikeField.enemyMax - StrikeField.enemyHp, Expected, "不同境界状态打击实伤保持品阶固定系数");
                    checkEq(StrikeField.poison, 8, "状态打击保留全部毒层");
                    checkEq(StrikeField.burn, 20, "状态打击保留全部灼烧量");
                }
            }

            for (const bool IsExpired : {false, true}) {
                Game Empty = mfFixedArtifactGame(Effect, Grade, 12);
                auto& EmptyField = *Empty.player.arts.battle;
                if (IsExpired) { EmptyField.poison = 99; EmptyField.burn = 9999; }
                const auto Before = saveToJson(Empty); const auto Rng = Empty.rng;
                check(!mfQuoteArtifactAction(Empty.player, EmptyField, Empty.player.arts.gear.front()).Allowed,
                    IsExpired ? "持续期已结束的残值不能用于状态打击" : "零对应状态不能催动状态打击");
                check(!battleTurn(Empty, "artifact:fixed-artifact").ok, "零状态或过期状态真实催动拒绝");
                check(saveToJson(Empty) == Before && Empty.rng == Rng, "状态打击拒绝不扣费、不冷却、不动随机数");
            }

            for (const auto& Inscription : {string("azure"), string("white"), string("black"), string("vermilion")}) {
                Game Inscribed = mfFixedArtifactGame(Effect, Grade, 12, 3);
                auto& InscribedField = *Inscribed.player.arts.battle;
                auto& Item = Inscribed.player.arts.gear.front();
                Item.InscriptionId = Inscription;
                InscribedField.poison = 8; InscribedField.poisonTurns = 1;
                InscribedField.burn = 20; InscribedField.burnTurns = 2;
                if (Inscription == "vermilion") InscribedField.hp = InscribedField.max / 2;
                const double InscriptionPower = Inscription == "azure" ? 1.2 : Inscription == "vermilion" ? 1.25 : 1.15;
                const double Base = Effect == "poison_burst" ? 8 * PoisonStrikePerLayer[Grade - 1] :
                    20 * BurnStrikePerPoint[Grade - 1];
                const auto InscribedPreview = battlePreview(Inscribed.player, InscribedField, "artifact:fixed-artifact");
                checkEq(InscribedPreview.RawDamage, jsRound(Base * InscriptionPower), "状态打击只受自身明确灵纹倍率增幅");
                check(battleTurn(Inscribed, "artifact:fixed-artifact").ok, "带代价灵纹的状态打击真实成功");
                checkEq(InscribedField.enemyMax - InscribedField.enemyHp, InscribedPreview.damage, "灵纹状态打击实伤等于预览");
            }

            Game Guarded = mfFixedArtifactGame(Effect, Grade, 12, 3);
            mfAmplifyFixedArtifactActor(Guarded.player);
            auto& GuardedField = *Guarded.player.arts.battle;
            GuardedField.buff = 5; GuardedField.discardPower = .30; GuardedField.FireGrowthRounds = 5;
            GuardedField.poison = 8; GuardedField.poisonTurns = 1; GuardedField.poisonDamage = 900;
            GuardedField.burn = 20; GuardedField.burnTurns = 2;
            GuardedField.enemyCards.enabled = true; GuardedField.enemyCards.guard = .20;
            GuardedField.enemyWeaken = 2; GuardedField.enemyShield = 7;
            const double LayerDamage = Effect == "poison_burst" ? 8 * PoisonStrikePerLayer[Grade - 1] :
                20 * BurnStrikePerPoint[Grade - 1];
            const int Weakened = jsRound(LayerDamage * .70);
            const int Reduced = jsRound(Weakened * .80);
            const auto GuardedPreview = battlePreview(Guarded.player, GuardedField, "artifact:fixed-artifact");
            checkEq(GuardedPreview.RawDamage, Weakened, "人物输出加成不放大状态打击，扰灵仍削减整段伤害");
            checkEq(GuardedPreview.damage, std::max(0, Reduced - 7), "状态打击仍经过目标减伤和护盾");
            check(battleTurn(Guarded, "artifact:fixed-artifact").ok, "有防护目标可真实承受状态打击");
            checkEq(GuardedField.enemyMax - GuardedField.enemyHp, GuardedPreview.damage, "带减伤护盾的状态打击预览与真实相同");
            checkNear(GuardedField.discardPower, .30, "状态打击不额外消耗自身锐势");
            checkEq(GuardedField.enemyWeaken, 0, "状态打击沿用普通主动伤害消耗扰灵");

            Game Capped = mfFixedArtifactGame(Effect, Grade, 12);
            auto& CappedField = *Capped.player.arts.battle;
            CappedField.poison = BATTLE_POISON_CAP; CappedField.poisonTurns = 1;
            CappedField.burn = BATTLE_BURN_CAP; CappedField.burnTurns = 1;
            const int CapDamage = Effect == "poison_burst" ? BATTLE_POISON_CAP * PoisonStrikePerLayer[Grade - 1] :
                jsRound(BATTLE_BURN_CAP * BurnStrikePerPoint[Grade - 1]);
            checkEq(battlePreview(Capped.player, CappedField, "artifact:fixed-artifact").RawDamage, CapDamage,
                "合法毒层与灼烧池上限计算无溢出且不读取剩余回合");
            check(battleTurn(Capped, "artifact:fixed-artifact").ok, "满层状态打击实际执行");
            checkEq(CappedField.poison, BATTLE_POISON_CAP, "满层状态打击后毒上限保持");
            checkEq(CappedField.burn, BATTLE_BURN_CAP, "满层状态打击后灼烧上限保持");
        }
    }

    for (const auto& Effect : {string("poison_burst"), string("fire_burst")}) {
        Player Enemy = mfFixedArtifactGame(Effect, 3, 6).player;
        Enemy.arts.battle.reset(); Enemy.arts.speed = 1;
        Player Target = Enemy;
        Target.arts.gear.clear(); Target.arts.equipped.clear(); Target.arts.speed = 500;
        Game Mirror;
        battleStartShadow(Mirror, Target, Enemy, 618237);
        auto& Field = *Mirror.player.arts.battle;
        Field.enemyCards.planned = {"artifact:fixed-artifact"}; Field.enemyCards.mana = 12;
        Field.hp = Field.max = 100000; Field.shield = 0;
        const bool IsPoison = Effect == "poison_burst";
        if (IsPoison) {
            Field.playerPoison = 16; Field.playerPoisonTurns = 1;
            Field.enemyCards.poison = 8; Field.enemyCards.poisonTurns = 1; Field.enemyCards.poisonDamage = 16;
        } else {
            Field.enemyCards.burn = 20; Field.enemyCards.burnTurns = 3;
        }
        const string Before = saveToJson(Mirror); const auto Rng = Mirror.rng;
        const auto EnemyPreview = battleEnemyPreview(Mirror);
        check(saveToJson(Mirror) == Before && Mirror.rng == Rng, "镜像状态打击预演只推进局部副本");
        const int Expected = IsPoison ? 16 + 8 * 5 : 20 + jsRound(20 * 1.5);
        checkEq(EnemyPreview.damage, Expected, "镜像状态打击先正常跳伤再按仍有效状态量攻击");
        check(battleTurn(Mirror, "end").ok, "镜像敌方状态打击走真实共用路径");
        checkEq(100000 - Field.hp, EnemyPreview.damage, "镜像状态打击实伤等于只读意图");
        if (IsPoison) {
            checkEq(Field.enemyCards.poison, 8, "镜像毒袭保留八层真实毒层");
            checkEq(Field.playerPoison, 16, "镜像毒袭保留精确毒跳伤");
            checkEq(Field.playerPoisonTurns, 1, "镜像毒袭不额外扣除自然跳伤后的持续期");
        } else {
            checkEq(Field.enemyCards.burn, 20, "镜像灼击保留灼烧量");
            checkEq(Field.enemyCards.burnTurns, 2, "镜像灼击不额外扣除自然跳伤后的持续期");
        }
    }

    {
        Game Mixed = mfFixedArtifactGame("poison_apply", 1, 12);
        auto& Field = *Mixed.player.arts.battle;
        check(battleTurn(Mixed, "artifact:fixed-artifact").ok, "混合毒先使用一阶法器");
        checkEq(Field.poisonDamage, 2, "一阶法器两层毒缓存仅两点");
        Field.deck.push_back({"fixed-mixed-poison", "corrode"});
        Field.hand.push_back("fixed-mixed-poison");
        const double Basis = mfBattleDamageBasis(12) * PoisonTickBasisCoefficient;
        const int ExpectedMixed = 2 + jsRound(Basis * 5) - jsRound(Basis * 2);
        const auto Quote = battlePreview(Mixed.player, Field, "corrode");
        check(Quote.valid, "混合毒后续真实神通预览可用");
        check(battleTurn(Mixed, "fixed-mixed-poison").ok, "混合毒后续化神腐毒真实可用");
        checkEq(Field.poison, 5, "法器两层毒与神通三层毒正常叠加");
        checkEq(Field.poisonDamage, ExpectedMixed, "普通毒只追加新增三层的跳伤差额，不按化神基数重算法器旧层");
        Mixed.player.arts.nodes = {"1-5"};
        check(battleTurn(Mixed, "end").ok, "混合毒可以正常推进至回合增长");
        checkEq(Field.poison, 6, "木道毒增长仍追加一层");
        checkEq(Field.poisonDamage, ExpectedMixed + jsRound(Basis * 6) - jsRound(Basis * 5),
            "普通毒增长仅追加新增层差额，不重算法器毒");
    }
}
} // namespace nztest
