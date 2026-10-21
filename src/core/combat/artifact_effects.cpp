#include "core/combat/artifact_effects.h"

#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/hand_rules.h"
#include "core/combat/water_vitality.h"
#include "core/content/forge_content.h"
#include "core/content/game_ids.h"
#include "core/talents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

namespace nz {
namespace {
constexpr std::array<int, 5> DrawManaCosts{1, 2, 2, 3, 3};
constexpr std::array<int, 5> RedrawManaCosts{1, 1, 2, 2, 3};
constexpr std::array<int, 5> DirectDamagePoints{20, 40, 120, 450, 1800};
constexpr std::array<int, 5> ActiveShieldPoints{20, 50, 150, 500, 2000};
constexpr std::array<int, 5> RoundShieldPoints{3, 8, 25, 100, 400};
constexpr std::array<int, 5> ActiveBurnPoints{1, 2, 5, 9, 16};
constexpr std::array<int, 5> RoundBurnPoints{1, 1, 2, 4, 8};
constexpr std::array<double, 5> PoisonStrikeDamagePerLayer{1, 2, 5, 15, 45};
constexpr std::array<double, 5> BurnStrikeDamagePerPoint{1, 1.25, 1.5, 1.75, 2};

bool mfValidArtifact(const Gear& Item) {
    const auto* Shape = content::mfFindForgeShape(Item.shape);
    return Item.ForgeVersion == 2 && Item.crafted && Item.grade >= 1 && Item.grade <= 5 &&
        std::isfinite(Item.EffectPower) && Item.EffectPower >= .1 && Item.EffectPower <= 3 &&
        (Item.InscriptionId.empty() || content::mfFindForgeInscription(Item.InscriptionId)) &&
        Shape && Shape->Slot == Item.slot &&
        content::mfForgeEffectAllowed(Item.EffectId, Item.slot, Item.element);
}

double mfInscriptionPower(const Gear& Item, const Battle& State) {
    double Power = 1;
    if (Item.InscriptionId == "azure") Power *= 1.2;
    else if (Item.InscriptionId == "white" || Item.InscriptionId == "black") Power *= 1.15;
    else if (Item.InscriptionId == "vermilion" && State.hp * 2 <= State.max) Power *= 1.25;
    return Power;
}

double mfActionPower(const Gear& Item, const Battle& State) {
    return std::clamp(Item.EffectPower, 0.1, 3.0) * mfInscriptionPower(Item, State);
}

int mfAddArtifactShield(const Player& Actor, Battle& State, int Amount) {
    const int Before = State.shield;
    State.shield = std::min(jsRound(State.max * .30), State.shield + std::max(0, Amount));
    const int Gained = std::max(0, State.shield - Before);
    if (Gained > 0)
        State.charge = std::min(State.ArtifactState.ChargeCap,
            State.charge + jsFloor(talentMods(Actor).shieldCharge));
    return Gained;
}

int mfApplyArtifactPoison(const Player& Actor, Battle& State, int Layers, int DamagePerLayer) {
    const auto Mods = talentMods(Actor);
    const int Before = State.poison;
    State.poison = std::min(BATTLE_POISON_CAP, State.poison + std::max(0, Layers));
    State.poisonTurns = 1;
    const int ExistingDamage = State.poisonDamage >= 0 ? State.poisonDamage :
        jsRound(mfBattleDamageBasis(Actor.stage) * PoisonTickBasisCoefficient * Before);
    State.poisonDamage = std::min(BATTLE_BURN_CAP,
        ExistingDamage + (State.poison - Before) * DamagePerLayer);
    if (State.hp > 0)
        State.hp = std::min(State.max, State.hp + jsRound(State.max * (State.poison - Before) * Mods.poisonHeal));
    return State.poison - Before;
}

int mfApplyArtifactBurn(Battle& State, int Added) {
    const int Before = State.burn;
    State.burn = std::min(BATTLE_BURN_CAP, clampInt(State.burn, 0, BATTLE_BURN_CAP) + Added);
    State.burnTurns = 3;
    return State.burn - Before;
}

bool mfValidCardPiles(const Battle& State) {
    std::set<string> Definitions;
    for (const auto& Entry : State.deck)
        if (Entry.uid.empty() || !battleSpell(Entry.spell) || !Definitions.insert(Entry.uid).second) return false;
    std::set<string> Assigned;
    for (const auto* Pile : {&State.hand, &State.draw, &State.discard, &State.exhausted})
        for (const auto& Uid : *Pile)
            if (!Definitions.contains(Uid) || !Assigned.insert(Uid).second) return false;
    return State.hand.size() <= BattleCards::MaximumHandSize && Assigned == Definitions;
}
} // namespace

int mfArtifactBaseDamage(int Grade) {
    return Grade >= 1 && Grade <= 5 ? DirectDamagePoints[Grade - 1] : 0;
}

ArtifactStatusStrikeQuote mfQuoteArtifactStatusStrike(const Gear& Item, const Battle& State) {
    ArtifactStatusStrikeQuote Quote;
    if (Item.ForgeVersion != 2 || Item.grade < 1 || Item.grade > 5) return Quote;
    if (Item.EffectId == "poison_burst") {
        Quote.StatusId = "poison"; Quote.StatusName = "毒";
        Quote.StatusLayers = State.poisonTurns > 0 ? clampInt(State.poison, 0, BATTLE_POISON_CAP) : 0;
        Quote.DamagePerLayer = PoisonStrikeDamagePerLayer[Item.grade - 1];
    } else if (Item.EffectId == "fire_burst") {
        Quote.StatusId = "burn"; Quote.StatusName = "灼烧";
        Quote.StatusLayers = State.burnTurns > 0 ? clampInt(State.burn, 0, BATTLE_BURN_CAP) : 0;
        Quote.DamagePerLayer = BurnStrikeDamagePerPoint[Item.grade - 1];
    } else return Quote;
    Quote.InscriptionMultiplier = mfInscriptionPower(Item, State);
    Quote.Damage = jsRound(Quote.StatusLayers * Quote.DamagePerLayer * Quote.InscriptionMultiplier);
    return Quote;
}

int mfArtifactActionCost(const Player& Actor, const Gear& Item, const Battle& State) {
    if (Item.grade < 1 || Item.grade > 5) return 0;
    int Cost = Item.EffectId == "draw" ? DrawManaCosts[Item.grade - 1] :
        Item.EffectId == "redraw" ? RedrawManaCosts[Item.grade - 1] : 2;
    const bool DrawAction = Item.EffectId == "draw" || Item.EffectId == "redraw";
    if (DrawAction) Cost -= jsFloor(talentMods(Actor).drawCost);
    if (Item.InscriptionId == "white" || Item.InscriptionId == "black" ||
        (Item.InscriptionId == "azure" && DrawAction) ||
        (Item.InscriptionId == "vermilion" && State.hp * 2 <= State.max)) --Cost;
    return std::max(0, Cost);
}

string mfArtifactEffectDescription(const Gear& Item) {
    const auto* Definition = content::mfFindForgeEffect(Item.EffectId);
    if (!Definition) return "主效果无效。";
    if (Item.grade < 1 || Item.grade > 5) return "法器品阶无效。";
    const auto Number = [](double Value) {
        string Result = std::to_string(std::round(Value * 100) / 100);
        while (!Result.empty() && Result.back() == '0') Result.pop_back();
        if (!Result.empty() && Result.back() == '.') Result.pop_back();
        return Result;
    };
    Battle FullHealth; FullHealth.hp = FullHealth.max = 100;
    const double Power = mfActionPower(Item, FullHealth);
    const double NumericPower = mfInscriptionPower(Item, FullHealth);
    const int DamagePoints = jsRound(mfArtifactBaseDamage(Item.grade) * NumericPower);
    const int Layers = std::max(1, jsRound(2 * Power));
    const int CapGain = (Item.grade + 1) / 2;
    string Description = Definition->Name + "：";
    if (Item.EffectId == "draw") Description += "主动抽" + std::to_string(Item.grade) + "张牌，可受普通抽牌加成。";
    else if (Item.EffectId == "redraw") Description += "主动选择至多" + std::to_string(Item.grade) +
        "张手牌，先弃牌再抽等量牌；手牌不足仍可少选。";
    else if (Item.EffectId == "cap_water") Description += "水势上限6→" + std::to_string(6 + CapGain) + "层；不直接获得水势。";
    else if (Item.EffectId == "cap_sword") Description += "剑气上限8→" + std::to_string(8 + Item.grade) + "层；不直接获得剑气。";
    else if (Item.EffectId == "cap_soul") Description += "震荡上限6→" + std::to_string(6 + CapGain) + "层；不直接获得震荡。";
    else if (Item.EffectId == "cap_charge") Description += "蓄势上限4→" + std::to_string(4 + CapGain) + "层；不直接获得蓄势。";
    else if (Item.EffectId == "cap_sharpness") Description += "锐势增伤比例上限30%→" + std::to_string(30 + 5 * Item.grade) + "%；不直接获得锐势。";
    else if (Item.EffectId == "turn_mana") Description += "入战与每轮恢复时额外获得" + std::to_string(1 + (Item.grade >= 4 ? 1 : 0)) + "点灵力，受总容量限制。";
    else if (Item.EffectId == "turn_shield") Description += "入战与每轮获得" + std::to_string(RoundShieldPoints[Item.grade - 1]) + "点护盾，共用30%气血护盾上限。";
    else if (Item.EffectId == "turn_water" || Item.EffectId == "turn_sword" || Item.EffectId == "turn_soul")
        Description += "入战与每轮获得" + std::to_string(1 + Item.grade / 3) +
            (Item.EffectId == "turn_water" ? "层水势。" : Item.EffectId == "turn_sword" ? "层剑气。" : "层震荡。");
    else if (Item.EffectId == "turn_poison") Description += "入战与每轮施加" + std::to_string(1 + Item.grade / 2) + "层毒，每层新增每跳" + std::to_string(Item.grade) + "点伤害，本场持续，触发原有施毒回春。";
    else if (Item.EffectId == "turn_fire") Description += "入战与每轮追加" + std::to_string(RoundBurnPoints[Item.grade - 1]) + "点灼烧池，每跳等量伤害，持续3回合。";
    else if (Item.EffectId == "shield") Description += "主动获得" + std::to_string(jsRound(ActiveShieldPoints[Item.grade - 1] * NumericPower)) + "点护盾，共用30%气血护盾上限。";
    else if (Item.EffectId == "guard") Description += "主动获得本回合" + Number(8 * Power) + "%守势，守势最高30%，总减伤仍受原上限限制。";
    else if (Item.EffectId == "cleanse") Description += "选择自身一种中毒、灼烧、悬灵或扰灵，最多削除" +
        std::to_string(mfArtifactStatusRemovalLimit(Item)) + "层；不足则清完所选状态，其他负面保留。层数额度只随法器品阶变化。";
    else if (Item.EffectId == "dispel") Description += "选择对手一种守势或锐势，最多削除" +
        std::to_string(mfArtifactStatusRemovalLimit(Item)) + "层（1个百分点计1层）；保留小数余量、本场累积增伤/减伤与流派资源。";
    else if (Item.EffectId == "water_gain" || Item.EffectId == "sword_gain" || Item.EffectId == "soul_gain" || Item.EffectId == "charge_gain")
        Description += "主动获得" + std::to_string(Layers) +
            (Item.EffectId == "water_gain" ? "层水势。" : Item.EffectId == "sword_gain" ? "层剑气。" : Item.EffectId == "soul_gain" ? "层震荡。" : "层蓄势。");
    else if (Item.EffectId == "poison_apply") Description += "主动施加" + std::to_string(Layers) + "层毒，每层新增每跳" + std::to_string(Item.grade) + "点伤害，本场持续，触发施毒回春。";
    else if (Item.EffectId == "fire_apply") Description += "主动追加" + std::to_string(jsRound(ActiveBurnPoints[Item.grade - 1] * NumericPower)) + "点灼烧池，每跳等量伤害，持续3回合。";
    else if (Item.EffectId == "poison_burst" || Item.EffectId == "fire_burst") {
        const auto Strike = mfQuoteArtifactStatusStrike(Item, FullHealth);
        Description += "造成对手当前" + Strike.StatusName +
            (Item.EffectId == "poison_burst" ? "层数" : "量（每跳伤害池）") + "×" +
            Number(Strike.DamagePerLayer) + "的伤害，不附加固定直伤；保留" + Strike.StatusName +
            (Item.EffectId == "poison_burst" ? "，本场持续。" : "及剩余持续回合。");
        if (Strike.InscriptionMultiplier != 1)
            Description += "本器灵纹再将整段伤害乘" + Number(Strike.InscriptionMultiplier) + "。";
        Description += "没有有效" + Strike.StatusName + "时不能催动；不乘剩余回合，不读取人物伤害或材料效力加成。";
    }
    else {
        Description += "基础直伤" + std::to_string(DamagePoints) + "点，由法器品阶与灵纹确定。";
        if (Item.EffectId == "sharpness_burst") Description += "锐势按通常增伤比例的2倍兑现，消耗全部锐势。";
        if (Item.EffectId == "water_burst") Description += "每层水势增伤15%，沿用水势保留和返灵。";
        if (Item.EffectId == "sword_burst") Description += "每层剑气增伤7%，消耗全部剑气。";
        if (Item.EffectId == "soul_burst") Description += "每层震荡增伤25%并穿盾5%（穿盾最高30%），消耗全部震荡。";
        if (Item.EffectId == "charge_burst") Description += "每层蓄势增伤12%，消耗全部蓄势。";
    }
    if (Item.EffectId == "shield" || Item.EffectId == "turn_shield" || Item.EffectId == "fire_apply" || Item.EffectId == "turn_fire" ||
        Item.EffectId == "poison_apply" || Item.EffectId == "turn_poison" || Item.EffectId == "damage" || Item.EffectId.ends_with("_burst"))
        Description += "法器基础点数不随人物境界、战力、气血、攻击/护盾倍率或材料利用率增长。";
    if (Definition->Passive) return Description;
    if (mfArtifactSelectsStatus(Item)) Description += "固定削层不受人物属性、材料效力与灵纹增幅影响；使用后隔过两个完整自身回合方可再次催动。";
    Description += Item.InscriptionId == "azure" ? "使用后额外多冷却1个完整自身回合。" :
        mfArtifactSelectsStatus(Item) ? "" :
        "使用后隔过一个完整自身回合方可再次催动。";
    if (Item.InscriptionId == "white") Description += "每次支付气血上限4%的真实气血，支付不能致死。";
    if (Item.InscriptionId == "black") Description += "使用后悬灵1轮。";
    if (Item.InscriptionId == "vermilion") Description += mfArtifactSelectsStatus(Item)
        ? "气血不高于50%时减1灵力；固定削层不加量。"
        : "气血不高于50%时减1灵力；非抽换效果效力额外提高25%。";
    return Description;
}

string mfArtifactPassiveDescription(const Gear& Item) {
    const auto* Effect = content::mfFindForgeEffect(Item.EffectId);
    if (!Effect || !Effect->Passive) return "此法器以主动主效果为收益。";
    return mfArtifactEffectDescription(Item);
}

ArtifactActionQuote mfQuoteArtifactAction(const Player& Actor, const Battle& State,
        const Gear& Item, const vector<string>& Targets, bool RequireTargets, const string& StatusTarget) {
    ArtifactActionQuote Quote;
    const auto Reject = [&](const string& Error) {
        Quote.Error = Error;
        return Quote;
    };
    if (!mfValidArtifact(Item)) return Reject("法器主效果或器型无效。");
    const auto* Effect = content::mfFindForgeEffect(Item.EffectId);
    if (Effect->Passive) return Reject("辅助法器按入战冻结的规则自动生效。");
    Quote.Cost = mfArtifactActionCost(Actor, Item, State);
    Quote.HealthCost = Item.InscriptionId == "white" ? std::max(1, jsRound(State.max * .04)) : 0;
    Quote.AvailableCards = static_cast<int>(State.hand.size());
    Quote.MaxTargets = Item.EffectId == "redraw" ? std::min(Item.grade, Quote.AvailableCards) : 0;
    Quote.SelectedCount = static_cast<int>(Targets.size());
    const int RemainingCooldown = battleCooldown(State, "artifact:" + Item.id);
    Quote.NextAvailableTurn = State.turn + std::max(0, RemainingCooldown);
    if (mfArtifactSelectsStatus(Item)) {
        Quote.StatusTargets = mfArtifactStatusTargets(State, Item);
        Quote.StatusToken = mfArtifactStatusToken(State, Item);
        if (!Targets.empty()) return Reject("此法器只接受一种状态目标，不接受手牌多选。");
        if (Quote.StatusTargets.empty()) return Reject("当前没有可削除的状态。");
        if (StatusTarget.empty() && RequireTargets) return Reject("请选择一种要削除的状态。");
        if (!StatusTarget.empty() && std::none_of(Quote.StatusTargets.begin(), Quote.StatusTargets.end(),
            [&](const auto& Target) { return Target.Id == StatusTarget; }))
            return Reject("所选状态已消失或不属于此法器，请重新选择。");
    } else if (!StatusTarget.empty()) return Reject("此法器不接受状态目标。");
    if (Actor.stage < mfArtifactRequiredStage(Item)) return Reject("境界不足，无法催动此阶法器。");
    if (RemainingCooldown > 0) return Reject("法器冷却中。");
    if (State.played >= 24) return Reject("本回合已施术24次，请结束回合。");
    if (State.mana < Quote.Cost) return Reject("灵力不足。");
    if (State.hp <= Quote.HealthCost) return Reject("气血不足，无法支付灵纹代价。");
    const auto Strike = mfQuoteArtifactStatusStrike(Item, State);
    if (!Strike.StatusId.empty() && Strike.StatusLayers <= 0)
        return Reject("对手当前没有有效" + Strike.StatusName + "，无法催动此法器。");
    if (Item.EffectId == "draw" || Item.EffectId == "redraw") {
        if (!mfValidCardPiles(State)) return Reject("本场牌堆数据异常，请重新载入战斗。");
    }
    if (Item.EffectId == "redraw") {
        if (Quote.MaxTargets == 0) return Reject("当前没有可换的手牌。");
        if (RequireTargets && Targets.empty()) return Reject("请选择至少一张当前手牌。");
        if (Quote.SelectedCount > Quote.MaxTargets) return Reject("选择数量超过法器换牌上限。");
        std::set<string> UniqueTargets;
        for (const auto& Uid : Targets) {
            if (!UniqueTargets.insert(Uid).second) return Reject("不能重复选择同一张手牌。");
            if (std::find(State.hand.begin(), State.hand.end(), Uid) == State.hand.end())
                return Reject("选择的手牌已经变化，请重新选择。");
        }
        Quote.DrawCount = Targets.empty() ? Quote.MaxTargets : Quote.SelectedCount;
    } else if (!Targets.empty()) return Reject("此法器无需选择手牌。");
    if (Item.EffectId == "draw") {
        const int RemainingSpace = static_cast<int>(BattleCards::MaximumHandSize - State.hand.size());
        const int AvailableDraw = static_cast<int>(State.draw.size() + State.discard.size());
        Quote.DrawCount = std::min({Item.grade + std::max(0, jsFloor(talentMods(Actor).drawBonus)),
            RemainingSpace, AvailableDraw});
        if (Quote.DrawCount <= 0) return Reject("手牌已满或当前没有可抽取的牌。");
    }
    Quote.Allowed = true;
    return Quote;
}

ArtifactCombatSnapshot mfArtifactCombatSnapshot(const Player& Actor) {
    ArtifactCombatSnapshot Snapshot;
    for (const auto& [Slot, Id] : Actor.arts.equipped)
        for (const auto& Item : Actor.arts.gear) {
            if (Item.id != Id || Item.slot != Slot || Actor.stage < mfArtifactRequiredStage(Item) || !mfValidArtifact(Item)) continue;
            const auto* Effect = content::mfFindForgeEffect(Item.EffectId);
            if (!Effect->Passive) break;
            Snapshot.Version = 2;
            const int Layers = 1 + Item.grade / 3;
            if (Item.EffectId == "cap_water") Snapshot.WaterCap += (Item.grade + 1) / 2;
            else if (Item.EffectId == "cap_sword") Snapshot.SwordQiCap += Item.grade;
            else if (Item.EffectId == "cap_soul") Snapshot.SoulCap += (Item.grade + 1) / 2;
            else if (Item.EffectId == "cap_charge") Snapshot.ChargeCap += (Item.grade + 1) / 2;
            else if (Item.EffectId == "cap_sharpness") Snapshot.SharpnessCap += .05 * Item.grade;
            else if (Item.EffectId == "turn_mana") Snapshot.RoundMana += 1 + (Item.grade >= 4 ? 1 : 0);
            else if (Item.EffectId == "turn_shield") Snapshot.RoundShield += RoundShieldPoints[Item.grade - 1];
            else if (Item.EffectId == "turn_water") Snapshot.RoundWater += Layers;
            else if (Item.EffectId == "turn_sword") Snapshot.RoundSwordQi += Layers;
            else if (Item.EffectId == "turn_soul") Snapshot.RoundSoul += Layers;
            else if (Item.EffectId == "turn_poison") {
                Snapshot.RoundPoison += 1 + Item.grade / 2;
                Snapshot.RoundPoisonDamagePerLayer = Item.grade;
            } else if (Item.EffectId == "turn_fire") Snapshot.RoundBurn += RoundBurnPoints[Item.grade - 1];
            break;
        }
    return Snapshot;
}

void mfApplyArtifactRoundSupply(const Player& Actor, Battle& State) {
    const auto& Supply = State.ArtifactState;
    State.water = std::min(Supply.WaterCap, State.water + Supply.RoundWater);
    State.swordQi = std::min(Supply.SwordQiCap, State.swordQi + Supply.RoundSwordQi);
    State.soul = std::min(Supply.SoulCap, State.soul + Supply.RoundSoul);
    if (Supply.RoundShield > 0) mfAddArtifactShield(Actor, State, Supply.RoundShield);
    if (Supply.RoundPoison > 0) mfApplyArtifactPoison(Actor, State, Supply.RoundPoison, Supply.RoundPoisonDamagePerLayer);
    if (Supply.RoundBurn > 0) mfApplyArtifactBurn(State, Supply.RoundBurn);
}

string mfApplyArtifactAction(const Player& Actor, Battle& State, const Gear& Item,
        const vector<string>& Targets, double TargetReduction, double TargetCounter, int* RawDamage,
        const string& StatusTarget, WaterVitality::HitFacts* WaterHitOutput) {
    if (WaterHitOutput) *WaterHitOutput = {};
    const auto Quote = mfQuoteArtifactAction(Actor, State, Item, Targets,
        Item.EffectId == "redraw" || mfArtifactSelectsStatus(Item), StatusTarget);
    if (!Quote.Allowed) return "";
    const ArtMods Mods = talentMods(Actor);
    const double Power = mfActionPower(Item, State);
    const double NumericPower = mfInscriptionPower(Item, State);
    const auto Strike = mfQuoteArtifactStatusStrike(Item, State);
    State.mana -= Quote.Cost;
    State.hp -= Quote.HealthCost;
    string Text = Item.name + "：";
    const auto& Effect = Item.EffectId;
    const int LayerGain = std::max(1, jsRound(2 * Power));
    if (Effect == "draw") {
        State.pendingDraw += Quote.DrawCount;
        Text += "抽" + std::to_string(Quote.DrawCount) + "张牌。";
    } else if (Effect == "redraw") {
        for (const auto& Uid : Targets) {
            State.hand.erase(std::find(State.hand.begin(), State.hand.end(), Uid));
            State.discard.push_back(Uid);
        }
        mfBattleApplyActiveDiscardBenefits(Actor, State, Quote.SelectedCount);
        State.pendingDraw += Quote.SelectedCount;
        Text += "弃" + std::to_string(Quote.SelectedCount) + "张，抽等量" + std::to_string(Quote.SelectedCount) + "张。";
    } else if (Effect == "shield") {
        Text += "护盾+" + std::to_string(mfAddArtifactShield(Actor, State, jsRound(ActiveShieldPoints[Item.grade - 1] * NumericPower))) + "。";
    } else if (Effect == "guard") {
        const double Before = State.guard;
        State.guard = std::min(.30, State.guard + .08 * Power);
        Text += "本回合守势+" + std::to_string(jsRound((State.guard - Before) * 100)) + "% 。";
    } else if (mfArtifactSelectsStatus(Item)) {
        Text += mfApplyArtifactStatusRemoval(State, Item, StatusTarget);
    } else if (Effect == "fire_apply") Text += "灼烧池+" + std::to_string(mfApplyArtifactBurn(State, jsRound(ActiveBurnPoints[Item.grade - 1] * NumericPower))) + "（3回合）。";
    else if (Effect == "poison_apply") Text += "毒+" + std::to_string(mfApplyArtifactPoison(Actor, State, LayerGain, Item.grade)) + "层（本场持续）。";
    else if (Effect == "water_gain") {
        const int Before = State.water; State.water = std::min(State.ArtifactState.WaterCap, Before + LayerGain);
        Text += "水势+" + std::to_string(State.water - Before) + "层。";
    } else if (Effect == "sword_gain") {
        const int Before = State.swordQi; State.swordQi = std::min(State.ArtifactState.SwordQiCap, Before + LayerGain);
        Text += "剑气+" + std::to_string(State.swordQi - Before) + "层。";
    } else if (Effect == "soul_gain") {
        const int Before = State.soul; State.soul = std::min(State.ArtifactState.SoulCap, Before + LayerGain);
        Text += "震荡+" + std::to_string(State.soul - Before) + "层。";
    } else if (Effect == "charge_gain") {
        const int Before = State.charge; State.charge = std::min(State.ArtifactState.ChargeCap, Before + LayerGain);
        Text += "蓄势+" + std::to_string(State.charge - Before) + "层。";
    } else {
        double Damage = mfArtifactBaseDamage(Item.grade) * NumericPower;
        int SoulLayers = 0;
        if (Effect == "sharpness_burst") {
            Damage *= 1 + State.discardPower * 2;
            State.discardPower = 0;
        } else if (!Strike.StatusId.empty()) {
            Damage = Strike.Damage;
            Text += "当前" + Strike.StatusName +
                (Effect == "poison_burst" ? "层数" : "量（每跳伤害池）") + std::to_string(Strike.StatusLayers) +
                "，按本器品阶与灵纹造成" + std::to_string(Strike.Damage) +
                "点伤害；保留" + Strike.StatusName +
                (Effect == "poison_burst" ? "，本场持续。" : "及剩余持续回合。");
        } else if (Effect == "water_burst") {
            const int Layers = State.water;
            Damage *= 1 + Layers * .15;
            State.water = jsFloor(Layers * std::min(.50, Mods.waterKeep));
            const int Returned = std::min(2, jsFloor(Layers * artModsOf(Actor.arts).waterMana)) +
                (hasTalent(Actor, "water") ? Layers : 0);
            State.mana = std::min(battleManaCap(Actor), State.mana + Returned);
        } else if (Effect == "sword_burst") {
            Damage *= 1 + State.swordQi * .07; State.swordQi = 0;
        } else if (Effect == "soul_burst") {
            SoulLayers = State.soul; Damage *= 1 + SoulLayers * SoulBurstPerLayerBonus; State.soul = 0;
        } else if (Effect == "charge_burst") {
            Damage *= 1 + State.charge * .12;
            State.charge = 0;
        }
        if (State.enemyWeaken > 0) {
            Damage *= std::max(.5, 1.0 - State.enemyWeaken * .15);
            State.enemyWeaken = 0;
        }
        const int Raw = std::max(0, jsRound(Damage));
        if (RawDamage) *RawDamage = Raw;
        const int Reduced = std::max(0, jsRound(Raw * (1 - std::clamp(TargetReduction, -.10, .35))));
        const int Pierce = jsRound(Reduced * std::min(SoulBurstMaximumPierceFraction, SoulLayers * SoulBurstPiercePerLayer));
        const int TargetHealthBefore = State.enemyHp;
        const int Blocked = std::min(State.enemyShield, Reduced - Pierce);
        State.enemyShield -= Blocked; State.enemyHp -= Reduced - Blocked;
        if (Blocked > 0 && TargetCounter > 0) State.hp -= jsRound(Blocked * TargetCounter);
        State.attacked = true;
        Text += "实际气血伤害" + std::to_string(Reduced - Blocked) + "（盾挡" + std::to_string(Blocked) + "）。";
        const auto WaterHit = WaterVitality::mfApply(Actor.arts, State, TargetHealthBefore);
        if (WaterHitOutput) *WaterHitOutput = WaterHit;
        Text += WaterVitality::mfHitDescription(WaterHit);
    }
    if (State.hp > 0 && (State.played + 1) % 3 == 0 && Mods.thirdHeal > 0) {
        const int Before = State.hp;
        State.hp = std::min(State.max, State.hp + jsRound(State.max * std::min(.12, Mods.thirdHeal)));
        Text += "三牌调息：气血+" + std::to_string(State.hp - Before) + "。";
    }
    if (Item.InscriptionId == "black") State.lock = std::max(State.lock, 1);
    State.cooldowns["artifact:" + Item.id] = 2 + (mfArtifactSelectsStatus(Item) ? 1 : 0) +
        (Item.InscriptionId == "azure" ? 1 : 0);
    return Text;
}
} // namespace nz
