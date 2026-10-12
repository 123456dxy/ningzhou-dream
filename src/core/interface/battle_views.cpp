#include "core/npc_learning.h"
#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/arts.h"
#include "core/artifact.h"
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
    entry.set("context", str("按当前" + realmName(player.stage) + "、已装备功法/悟道/法器与开局准备计算；气血上限" +
        std::to_string(opening.max) + "、开局灵力" + std::to_string(opening.mana) +
        "；目标无护盾/减伤，无相生或额外蓄积状态。境界未满足时仅供参考。"));
    entry.set("damage", num(preview.damage)); entry.set("shield", num(preview.shield));
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
    const string enemyPortrait = battle.enemyBeast ? beastPortrait(battle.enemyStage) :
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
    entry.set("intentName", str(battle.intentName));
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
            cardView.set("shield", num(preview.shield));
            cardView.set("heal", num(preview.heal));
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
            artifact.set("damage", num(preview.damage)); artifact.set("shield", num(preview.shield)); artifact.set("heal", num(preview.heal));
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
    entry.set("comboDraws", num(battle.comboDraws));
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
        entry.set("removalOptions", pileInfo(active));
        entry.set("exhaustedSpells", pileInfo(removed));
    }
    return entry;
}

} // namespace nz::api::view
