#include "core/npc_learning.h"
#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/combat/fire_growth.h"
#include "core/combat/elemental_sword.h"
#include "core/combat/hand_rules.h"
#include "core/combat/poison_rules.h"
#include "core/combat/water_flow.h"
#include "core/content/game_ids.h"
#include "core/content/beast_portraits.h"
#include "core/arts.h"
#include "core/artifact.h"
#include "core/combat/artifact_effects.h"
#include "core/combat/artifact_status.h"
#include "core/battle.h"
#include "core/talents.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::api::view {

// 用途：将统一参考预览序列化为 API 数据。
// 参与流程：神通目录与场外法宝预览展示。
// 使用方式：传只读角色和定义，附参考上下文及状态详情。
// 返回值：包含参考数字、条件和效果说明的 JSON 对象。
Value spellPreview(const Player& player, const SpellDef& spell) {
    const Preview preview = battleReferencePreview(player, spell);
    const Battle opening = battleReferenceState(player);
    Value entry(json::Object{});
    entry.set("valid", Value(preview.valid)); entry.set("reference", Value(true));
    entry.set("elementalGenerationReady", Value(preview.ElementalGenerationReady));
    entry.set("context", str("按当前" + realmName(player.stage) + "、已装备功法/悟道/法器与开局准备计算；气血上限" +
        std::to_string(opening.max) + "、开局灵力" + std::to_string(opening.mana) +
        "；目标无护盾/减伤，无相生或额外蓄积状态。境界未满足时仅供参考。"));
    entry.set("damage", num(preview.damage)); entry.set("rawDamage", num(preview.RawDamage)); entry.set("shield", num(preview.shield));
    entry.set("heal", num(preview.heal)); entry.set("healPotential", num(preview.healPotential));
    entry.set("cost", num(preview.cost)); entry.set("energy", num(preview.energy)); entry.set("manaGain", num(preview.manaGain));
    entry.set("qi", num(preview.qi)); entry.set("draw", num(preview.draw)); entry.set("drawNext", num(preview.drawNext));
    entry.set("hpMaxChange", num(preview.hpMaxChange)); entry.set("manaRegenDelta", num(preview.manaRegenDelta));
    entry.set("storedMana", num(preview.storedMana)); entry.set("damageBonusPct", num(preview.damageBonusPct));
    entry.set("reductionPct", num(preview.reductionPct)); entry.set("guardPct", num(preview.guardPct));
    entry.set("text", str(preview.text)); entry.set("conditions", str(battlePreviewConditions(player, spell)));
    json::Array statuses;
    for (const auto& effect : preview.statuses) {
        Value item(json::Object{});
        item.set("id", str(effect.id)); item.set("name", str(effect.name)); item.set("target", str(effect.target));
        item.set("amount", num(effect.amount)); item.set("total", num(effect.total));
        item.set("turns", num(effect.turns)); item.set("cap", num(effect.cap)); item.set("perTurn", num(effect.perTurn));
        statuses.push_back(std::move(item));
    }
    entry.set("statuses", Value(std::move(statuses)));
    return entry;
}

// 用途：序列化当前战况、实际手牌与敌方意图。
// 参与流程：状态 API 中的斗法视图。
// 使用方式：传只读游戏；nextMana 表示下一轮恢复到的总值。
// 返回值：战况 JSON；没有战斗时为 null，不修改游戏。
Value battle(const Game& game) {
    if (!game.player.arts.battle) return Value();
    const Battle& battle = *game.player.arts.battle;
    Value entry(json::Object{});
    entry.set("enemy", str(battle.enemy));
    entry.set("npcId", str(battle.npcId));
    entry.set("style", str(battle.style));
    entry.set("intentKind", str(battle.intentKind));
    const auto enemyNpc = std::find_if(game.npcs.begin(), game.npcs.end(), [&](const Npc& npc) {
        return !battle.npcId.empty() && npc.id == battle.npcId;
    });
    const string enemyPortrait = battle.enemyBeast ? Appearance::mfBeastPortraitPath(
        battle.enemyDao.empty() ? battle.style : battle.enemyDao, battle.enemyStage) :
        enemyNpc != game.npcs.end() ? npcPortrait(*enemyNpc) :
        npcPortrait(battle.npcId.empty() ? battle.enemy : battle.npcId, battle.enemyGender, battle.enemySect);
    entry.set("enemyPortrait", str(enemyPortrait));
    entry.set("enemyStage", num(battle.enemyStage));
    entry.set("dungeonSite", str(battle.dungeonSite));
    entry.set("dungeonNode", str(battle.dungeonNode));
    entry.set("enemyRealmName", str(realmName(battle.enemyStage)));
    entry.set("playerSpeed", num(battle.playerSpeed));
    entry.set("enemySpeed", num(battle.enemySpeed));
    entry.set("playerFirst", Value(battle.playerFirst));
    entry.set("escapeChance", num(battleEscapeChance(game.player, battle)));
    entry.set("enemyDao", str(battle.enemyDao));
    entry.set("enemyBeast", Value(battle.enemyBeast));
    entry.set("enemySect", str(battle.enemySect));
    entry.set("enemyMana", num(battle.enemyCards.mana));
    entry.set("enemyMaxMana", num(battle.enemyCards.maxMana));
    const auto intent = battleEnemyPreview(game);
    entry.set("intentDescription", str(intent.description));
    entry.set("intentDamage", num(intent.damage));
    entry.set("intentShield", num(intent.shield));
    entry.set("intentHeal", num(intent.heal));
    entry.set("enemyActionCount", num(static_cast<int>(intent.spells.size())));
    entry.set("enemyActionLimit", num(battle.enemyCards.enabled ? mfBattleEnemyActionLimit(game, battle) :
        battle.BeastActions.empty() ? 1 : BeastCombat::mfActionLimit(battle.enemyStage)));
    entry.set("beastActionLimit", num(battle.enemyBeast ? BeastCombat::mfActionLimit(battle.enemyStage) : 0));
    entry.set("beastEnergyBudget", num(battle.enemyBeast ? BeastCombat::mfEnergyBudget(battle.enemyStage) : 0));
    {
        json::Array Actions;
        for (const auto& Outcome : intent.BeastActions) {
            const auto& Action = Outcome.Action;
            Value Item(json::Object{});
            Item.set("id", str(string(BeastCombat::mfActionId(Action.Kind))));
            Item.set("name", str(string(BeastCombat::mfActionName(Action.Kind))));
            Item.set("cost", num(BeastCombat::mfActionCost(Action.Kind)));
            Item.set("rawDamage", num(Action.RawDamage)); Item.set("damage", num(Outcome.Damage));
            Item.set("blocked", num(Outcome.Blocked)); Item.set("counterDamage", num(Outcome.CounterDamage));
            Item.set("shieldGain", num(Action.ShieldGain)); Item.set("healGain", num(Action.HealGain));
            Item.set("poisonDamage", num(Action.PoisonDamage)); Item.set("burnDamage", num(Action.BurnDamage));
            Item.set("weakenLayers", num(Action.WeakenLayers)); Item.set("chargeGain", num(Action.ChargeGain));
            Item.set("piercePercent", num(Action.PiercePercent)); Item.set("executed", Value(Outcome.Executed));
            Actions.push_back(std::move(Item));
        }
        entry.set("beastActions", Value(std::move(Actions)));
    }
    { json::Array spells; for (const auto& id : intent.spells) { const auto* spell = battleSpell(id); spells.push_back(str(spell ? spell->name : id)); } entry.set("intentSpells", Value(std::move(spells))); }
    entry.set("hp", num(battle.hp));
    entry.set("max", num(battle.max));
    entry.set("enemyHp", num(battle.enemyHp));
    entry.set("enemyMax", num(battle.enemyMax));
    entry.set("mana", num(battle.mana));
    entry.set("maxMana", num(battle.maxMana));
    entry.set("shield", num(battle.shield));
    entry.set("enemyShield", num(battle.enemyShield));
    entry.set("swordQi", num(battle.swordQi));
    entry.set("turn", num(battle.turn));
    string CurrentIntentName = battle.intentName;
    if (battle.enemyCards.enabled) {
        CurrentIntentName.clear();
        for (const auto& Id : intent.spells) {
            if (!CurrentIntentName.empty()) CurrentIntentName += " → ";
            const auto* Spell = battleSpell(Id);
            CurrentIntentName += Spell ? Spell->name : Id;
        }
        if (CurrentIntentName.empty())
            CurrentIntentName = battle.shadowOutcome.empty() ? "本轮不行动" : "斗法已结束";
    }
    entry.set("intentName", str(CurrentIntentName));
    entry.set("intentRaw", num(battle.intentRaw));
    // 生死斗法要在界面上明确提示，否则玩家会莫名其妙死掉
    entry.set("lethal", Value(battleDefeatEndsLife(battle)));
    entry.set("kind", str(battle.kind));
    entry.set("quest", str(battle.quest));

    {
        json::Array messages;
        for (const auto& message : battle.log) messages.push_back(str(message));
        entry.set("log", Value(std::move(messages)));
    }
    // 手牌：带上真实预览（和出牌共用同一函数，所见即所得）
    {
        json::Array hand;
        const vector<string> spells = handSpells(battle);
        for (size_t i = 0; i < battle.hand.size(); ++i) {
            const SpellDef* spell = battleSpell(spells[i]);
            if (!spell) continue;
            const Preview preview = battlePreview(game.player, battle, spells[i]);
            const int cost = battleCost(game.player, *spell);
            Value cardView(json::Object{});
            cardView.set("uid", str(battle.hand[i]));
            cardView.set("id", str(spell->id));
            cardView.set("name", str(spell->name));
            const auto CardState = std::find_if(battle.deck.begin(), battle.deck.end(), [&](const Card& Item) { return Item.uid == battle.hand[i]; });
            cardView.set("innate", Value(CardState != battle.deck.end() && CardState->Innate));
            cardView.set("retain", Value(CardState != battle.deck.end() && CardState->Retain));
            cardView.set("dao", str(spell->dao));
            cardView.set("kind", str(spell->kind));
            cardView.set("cost", num(cost));
            cardView.set("stage", num(spell->stage));
            cardView.set("stageName", str(realmName(spell->stage)));
            cardView.set("description", str(battleSpellDescription(*spell)));
            cardView.set("requiresTarget", Value(battleNeedsTarget(*spell)));
            // 能不能出这张牌，以及不能的原因。
            // 之前手牌永远可点，点了才报错，玩家得一张张试。
            {
                std::string why;
                auto cooldown = battle.cooldowns.find(spell->id);
                if (game.player.stage < spell->stage)
                    why = "境界不足，需达到" + realmName(spell->stage) + "。";
                else if (cooldown != battle.cooldowns.end() && cooldown->second > 0)
                    why = "冷却中（还剩 " + std::to_string(cooldown->second) +
                          " 回合）";
                else if (battle.mana < cost)
                    why = "灵力不足（需 " + std::to_string(cost) +
                          "，有 " + std::to_string(battle.mana) + "）";
                else if (battle.played >= 24)
                    why = "本回合出牌已达上限";
                else if (content::parseSpellKind(spell->kind) == content::SpellKind::PoisonBloom) {
                    const auto Quote = PoisonRules::mfQuoteBloom(game.player.stage, battle);
                    if (!Quote.Allowed) why = Quote.Error;
                }
                else if (content::parseSpellKind(spell->kind) == content::SpellKind::PoisonSiphon) {
                    const auto Quote = PoisonRules::mfQuoteSiphon(game.player.stage, battle);
                    if (!Quote.Allowed) why = Quote.Error;
                }
                else if (battleNeedsTarget(*spell)) {
                    const bool hasTarget = std::any_of(battle.deck.begin(), battle.deck.end(), [&](const Card& card) {
                        return card.spell != spell->id &&
                            std::find(battle.exhausted.begin(), battle.exhausted.end(), card.uid) == battle.exhausted.end();
                    });
                    if (!hasTarget) why = "本场已无其它可移除神通";
                }
                cardView.set("playable", Value(why.empty()));
                cardView.set("reason", str(why));
            }
            cardView.set("damage", num(preview.damage));
            cardView.set("rawDamage", num(preview.RawDamage));
            cardView.set("elementalGenerationReady", Value(preview.ElementalGenerationReady));
            cardView.set("shield", num(preview.shield));
            cardView.set("heal", num(preview.heal));
            cardView.set("healPotential", num(preview.healPotential));
            cardView.set("draw", num(preview.draw));
            cardView.set("qi", num(preview.qi));
            cardView.set("text", str(preview.text));
            hand.push_back(std::move(cardView));
        }
        entry.set("hand", Value(std::move(hand)));
    }
    {
        json::Array artifacts;
        for (const auto& ability : equippedArtifactAbilities(game.player)) {
            Value artifact = artifactView(game.player, ability);
            const auto& spell = ability.spell;
            const Preview preview = battlePreview(game.player, battle, spell.id);
            const int cooldown = battleCooldown(battle, spell.id), cost = battleCost(game.player, spell);
            const string reason = cooldown > 0 ? "法宝冷却中，还剩" + std::to_string(cooldown) + "个回合。" :
                battle.mana < cost ? "灵力不足，需" + std::to_string(cost) + "点。" :
                battle.played >= 24 ? "本回合施术已达上限。" : "";
            artifact.set("playable", Value(reason.empty())); artifact.set("reason", str(reason)); artifact.set("cooldownRemaining", num(cooldown));
            if (ability.gear.ForgeVersion == 2) {
                const auto Quote = mfQuoteArtifactAction(game.player, battle, ability.gear);
                artifact.set("playable", Value(Quote.Allowed)); artifact.set("reason", str(Quote.Error));
                artifact.set("cost", num(Quote.Cost)); artifact.set("healthCost", num(Quote.HealthCost));
                artifact.set("nextAvailableTurn", num(Quote.NextAvailableTurn));
                if (ability.gear.EffectId == "fire_burst" || ability.gear.EffectId == "poison_burst") {
                    const auto Strike = mfQuoteArtifactStatusStrike(ability.gear, battle);
                    Value Breakdown(json::Object{});
                    Breakdown.set("statusId", str(Strike.StatusId));
                    Breakdown.set("statusName", str(Strike.StatusName));
                    Breakdown.set("statusLayers", num(Strike.StatusLayers));
                    Breakdown.set("damagePerLayer", num(Strike.DamagePerLayer));
                    Breakdown.set("inscriptionMultiplier", num(Strike.InscriptionMultiplier));
                    Breakdown.set("statusDamage", num(Strike.Damage));
                    Breakdown.set("consumesStatus", false);
                    Breakdown.set("remainingTurns", Strike.StatusId == "burn" ? battle.burnTurns : battle.poisonTurns);
                    Breakdown.set("rawDamage", preview.RawDamage);
                    Breakdown.set("healthDamage", preview.damage);
                    Breakdown.set("text", preview.text);
                    artifact.set("breakdown", std::move(Breakdown));
                }
                if (mfArtifactSelectsStatus(ability.gear)) {
                    artifact.set("targetMode", str("status-one")); artifact.set("statusToken", str(Quote.StatusToken));
                    json::Array Targets;
                    for (const auto& Status : Quote.StatusTargets) {
                        Value Target(json::Object{}); Target.set("id", str(Status.Id)); Target.set("name", str(Status.Name));
                        Target.set("unit", str(Status.Unit)); Target.set("description", str(Status.Description));
                        Target.set("layers", num(Status.Layers)); Target.set("removeLayers", num(Status.RemoveLayers));
                        Target.set("remainingLayers", num(Status.RemainingLayers)); Targets.push_back(std::move(Target));
                    }
                    artifact.set("targets", Value(std::move(Targets)));
                    Value CurrentPreview = artifact["preview"];
                    CurrentPreview.set("reference", Value(false)); CurrentPreview.set("text", str(preview.text));
                    CurrentPreview.set("context", str("按当前斗法状态选择一种状态，确认后按本法器品阶削层。"));
                    CurrentPreview.set("cost", num(Quote.Cost)); artifact.set("preview", std::move(CurrentPreview));
                }
                if (ability.gear.EffectId == "redraw") {
                    artifact.set("targetMode", str("hand-many")); artifact.set("targetMin", num(1));
                    artifact.set("targetMax", num(Quote.MaxTargets));
                    json::Array Targets;
                    for (const auto& Uid : battle.hand) {
                        Value Target(json::Object{}); Target.set("id", str(Uid)); Target.set("uid", str(Uid));
                        const auto CardIt = std::find_if(battle.deck.begin(), battle.deck.end(), [&](const Card& Item) { return Item.uid == Uid; });
                        const auto* Definition = CardIt == battle.deck.end() ? nullptr : battleSpell(CardIt->spell);
                        Target.set("name", str(Definition ? Definition->name : Uid)); Targets.push_back(std::move(Target));
                    }
                    artifact.set("targets", Value(std::move(Targets)));
                }
            }
            artifact.set("damage", num(preview.damage)); artifact.set("shield", num(preview.shield)); artifact.set("heal", num(preview.heal));
            artifact.set("rawDamage", num(preview.RawDamage));
            artifact.set("elementalGenerationReady", Value(preview.ElementalGenerationReady));
            artifact.set("energy", num(preview.energy)); artifact.set("qi", num(preview.qi)); artifact.set("text", str(preview.text));
            artifacts.push_back(std::move(artifact));
        }
        entry.set("artifacts", Value(std::move(artifacts)));
    }
    {
        Value cooldown(json::Object{});
        for (const auto& [spellId, remaining] : battle.cooldowns)
            if (remaining > 0) cooldown.set(spellId, num(remaining));
        entry.set("cooldowns", std::move(cooldown));
    }
    // 自身状态：卡牌游戏的核心信息，之前一个都没显示
    entry.set("poison", num(battle.poison));
    entry.set("burn", num(battle.burn));
    entry.set("playerPoison", num(battle.playerPoison));
    entry.set("playerPoisonTurns", num(battle.playerPoisonTurns));
    entry.set("poisonTurns", num(battle.poisonTurns));
    entry.set("burnTurns", num(battle.burnTurns));
    entry.set("water", num(battle.water));
    {
        Value Caps(json::Object{});
        Caps.set("water", num(battle.ArtifactState.WaterCap)); Caps.set("swordQi", num(battle.ArtifactState.SwordQiCap));
        Caps.set("soul", num(battle.ArtifactState.SoulCap)); Caps.set("charge", num(battle.ArtifactState.ChargeCap));
        Caps.set("discardPower", num(battle.ArtifactState.SharpnessCap)); entry.set("resourceCaps", std::move(Caps));
    }
    entry.set("soul", num(battle.soul));
    entry.set("charge", num(battle.charge));
    entry.set("swordQi", num(battle.swordQi));
    entry.set("guard", num(battle.guard));
    entry.set("reduction", num(battle.reduction));
    entry.set("buff", num(battle.buff));
    entry.set("manaBonus", num(battle.manaBonus));
    entry.set("manaPenalty", num(battle.manaPenalty));
    entry.set("carryMana", num(battle.carryMana));
    entry.set("carryCap", num(battle.carryCap));
    entry.set("drawNext", num(battle.drawNext));
    entry.set("lock", num(battle.lock));
    entry.set("enemyWeaken", num(battle.enemyWeaken));
    entry.set("weakened", num(battle.enemyWeaken));
    entry.set("weakenedPenalty", num(std::min(0.5, battle.enemyWeaken * 0.15)));
    entry.set("hpCostPct", num(battle.hpCostPct));
    entry.set("discardPower", num(battle.discardPower));
    entry.set("swordSequence", num(battle.swordSequence));
    entry.set("elementalSwordSequence", num(battle.ElementalSwordSequence));
    entry.set("elementalSwordEnhancementLayers", num(battle.ElementalSwordEnhancementLayers));
    entry.set("elementalSwordEnhancementLimit", num(ElementalSword::MaximumEnhancementLayers));
    entry.set("elementalSwordEnhancementPercent", num(jsRound((ElementalSword::mfEnhancementMultiplier(talentMods(game.player), battle) - 1.0) * 100)));
    entry.set("enemyElementalSwordEnhancementLayers", num(battle.enemyCards.ElementalSwordEnhancementLayers));
    entry.set("handLimit", num(BattleCards::MaximumHandSize));
    entry.set("fireGrowthRounds", num(battle.FireGrowthRounds));
    entry.set("fireGrowthMaxRounds", num(FireGrowth::MaximumGrowthRounds));
    entry.set("enemyFireGrowthRounds", num(battle.enemyCards.FireGrowthRounds));
    entry.set("enemyElementalSwordSequence", num(battle.enemyCards.ElementalSwordSequence));
    entry.set("comboDraws", num(battle.comboDraws));
    entry.set("waterFlowDraws", num(battle.WaterFlowDraws));
    entry.set("waterFlowDrawLimit", num(WaterFlow::DrawLimit));
    entry.set("enemyWaterFlowDraws", num(battle.enemyCards.WaterFlowDraws));
    entry.set("lastDao", str(battle.last));
    entry.set("manaCap", num(battleManaCap(game.player)));
    entry.set("nextMana", num(battleNextMana(game.player, battle)));
    entry.set("baseReduction", num(talentMods(game.player).reduction));
    entry.set("effectiveReduction", num(battleReduction(game.player, battle)));
    entry.set("played", num(battle.played));
    entry.set("shuffles", num(battle.shuffles));
    entry.set("style", str(battle.style));
    entry.set("enemyShield", num(battle.enemyShield));
    entry.set("mana", num(battle.mana));
    entry.set("maxMana", num(battle.maxMana));
    entry.set("draw", num(static_cast<int>(battle.draw.size())));
    entry.set("discard", num(static_cast<int>(battle.discard.size())));
    entry.set("exhausted", num(static_cast<int>(battle.exhausted.size())));
    {
        std::map<std::string, int> active, removed;
        for (const auto& card : battle.deck) {
            if (std::find(battle.exhausted.begin(), battle.exhausted.end(), card.uid) != battle.exhausted.end())
                ++removed[card.spell];
            else if (std::find(battle.hand.begin(), battle.hand.end(), card.uid) != battle.hand.end() ||
                     std::find(battle.draw.begin(), battle.draw.end(), card.uid) != battle.draw.end() ||
                     std::find(battle.discard.begin(), battle.discard.end(), card.uid) != battle.discard.end())
                ++active[card.spell];
        }
        auto pileInfo = [&](const std::map<std::string, int>& pile) {
            json::Array arr;
            for (const auto& [id, count] : pile) {
                const SpellDef* spell = battleSpell(id);
                if (!spell) continue;
                Value item(json::Object{});
                item.set("id", str(id));
                item.set("name", str(spell->name));
                item.set("dao", str(spell->dao));
                item.set("copies", num(count));
                arr.push_back(std::move(item));
            }
            return Value(std::move(arr));
        };
        json::Array RemovalOptions;
        for (const auto& Card : battle.deck) {
            if (std::find(battle.exhausted.begin(), battle.exhausted.end(), Card.uid) != battle.exhausted.end()) continue;
            const bool IsAvailable = std::find(battle.hand.begin(), battle.hand.end(), Card.uid) != battle.hand.end() ||
                std::find(battle.draw.begin(), battle.draw.end(), Card.uid) != battle.draw.end() ||
                std::find(battle.discard.begin(), battle.discard.end(), Card.uid) != battle.discard.end();
            const SpellDef* Definition = battleSpell(Card.spell);
            if (!IsAvailable || !Definition) continue;
            Value Option(json::Object{});
            Option.set("uid", str(Card.uid)); Option.set("id", str(Definition->id));
            Option.set("name", str(Definition->name)); Option.set("dao", str(Definition->dao));
            Option.set("copies", num(1));
            RemovalOptions.push_back(std::move(Option));
        }
        entry.set("removalOptions", Value(std::move(RemovalOptions)));
        entry.set("exhaustedSpells", pileInfo(removed));
    }
    return entry;
}

} // namespace nz::api::view
