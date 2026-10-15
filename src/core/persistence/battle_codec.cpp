// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"
#include "core/artifact.h"
#include "core/battle.h"
#include <algorithm>

namespace nz::persistence {

static Value enemyCombatToJson(const EnemyCombat& enemy) {
    Value enemyObject(json::Object{});
    enemyObject.set("stage", num(enemy.stage));
    enemyObject.set("speed", num(enemy.speed));
    enemyObject.set("body", num(enemy.body));
    enemyObject.set("mind", num(enemy.mind));
    enemyObject.set("mana", num(enemy.mana));
    enemyObject.set("maxMana", num(enemy.maxMana));
    enemyObject.set("shuffles", num(enemy.shuffles));
    enemyObject.set("played", num(enemy.played));
    enemyObject.set("turns", num(enemy.turns));
    enemyObject.set("swordQi", num(enemy.swordQi));
    enemyObject.set("water", num(enemy.water));
    enemyObject.set("soul", num(enemy.soul));
    enemyObject.set("charge", num(enemy.charge));
    enemyObject.set("lock", num(enemy.lock));
    enemyObject.set("carryMana", num(enemy.carryMana));
    enemyObject.set("manaBonus", num(enemy.manaBonus));
    enemyObject.set("manaPenalty", num(enemy.manaPenalty));
    enemyObject.set("drawNext", num(enemy.drawNext));
    enemyObject.set("carryCap", num(enemy.carryCap));
    enemyObject.set("hpCostPct", num(enemy.hpCostPct));
    enemyObject.set("poison", num(enemy.poison));
    enemyObject.set("poisonTurns", num(enemy.poisonTurns));
    enemyObject.set("poisonDamage", num(enemy.poisonDamage));
    enemyObject.set("burn", num(enemy.burn));
    enemyObject.set("burnTurns", num(enemy.burnTurns));
    enemyObject.set("swordSequence", num(enemy.swordSequence));
    enemyObject.set("comboDraws", num(enemy.comboDraws));
    enemyObject.set("elementalSwordManaReturns", num(enemy.ElementalSwordManaReturns));
    enemyObject.set("elementalSwordSequence", num(enemy.ElementalSwordSequence));
    enemyObject.set("elementalSwordEnhancementLayers", num(enemy.ElementalSwordEnhancementLayers));
    enemyObject.set("fireGrowthRounds", num(enemy.FireGrowthRounds));
    enemyObject.set("buff", num(enemy.buff));
    enemyObject.set("reduction", num(enemy.reduction));
    enemyObject.set("guard", num(enemy.guard));
    enemyObject.set("discardPower", num(enemy.discardPower));
    enemyObject.set("sect", str(enemy.sect));
    enemyObject.set("dao", str(enemy.dao));
    enemyObject.set("main", str(enemy.main));
    enemyObject.set("last", str(enemy.last));
    enemyObject.set("enabled", Value(enemy.enabled));
    enemyObject.set("attacked", Value(enemy.attacked));
    enemyObject.set("aux", stringVectorToJson(enemy.aux));
    enemyObject.set("nodes", stringVectorToJson(enemy.nodes));
    enemyObject.set("hand", stringVectorToJson(enemy.hand));
    enemyObject.set("draw", stringVectorToJson(enemy.draw));
    enemyObject.set("discard", stringVectorToJson(enemy.discard));
    enemyObject.set("exhausted", stringVectorToJson(enemy.exhausted));
    enemyObject.set("planned", stringVectorToJson(enemy.planned));
    enemyObject.set("manuals", mapToJson(enemy.manuals));
    enemyObject.set("cooldowns", mapToJson(enemy.cooldowns));
    { json::Array array; for (const auto& card : enemy.deck) { Value value(json::Object{}); value.set("uid", str(card.uid)); value.set("spell", str(card.spell)); array.push_back(std::move(value)); } enemyObject.set("deck", Value(std::move(array))); }
    return enemyObject;
}

static void enemyCombatFromJson(const Value& enemyObject, EnemyCombat& enemy) {
    enemy.stage = enemyObject["stage"].asInt(0);
    enemy.speed = enemyObject["speed"].asInt(0);
    enemy.body = enemyObject["body"].asInt(0);
    enemy.mind = enemyObject["mind"].asInt(0);
    enemy.mana = enemyObject["mana"].asInt(0);
    enemy.maxMana = enemyObject["maxMana"].asInt(3);
    enemy.shuffles = enemyObject["shuffles"].asInt(0);
    enemy.played = enemyObject["played"].asInt(0);
    enemy.turns = enemyObject["turns"].asInt(0);
    enemy.swordQi = enemyObject["swordQi"].asInt(0);
    enemy.water = enemyObject["water"].asInt(0);
    enemy.soul = enemyObject["soul"].asInt(0);
    enemy.charge = enemyObject["charge"].asInt(0);
    enemy.lock = enemyObject["lock"].asInt(0);
    enemy.carryMana = enemyObject["carryMana"].asInt(0);
    enemy.manaBonus = enemyObject["manaBonus"].asInt(0);
    enemy.manaPenalty = enemyObject["manaPenalty"].asInt(0);
    enemy.drawNext = enemyObject["drawNext"].asInt(0);
    enemy.carryCap = enemyObject["carryCap"].asInt(0);
    enemy.hpCostPct = enemyObject["hpCostPct"].asInt(0);
    enemy.poison = enemyObject["poison"].asInt(0);
    enemy.poisonTurns = enemyObject["poisonTurns"].asInt(0);
    enemy.poisonDamage = enemyObject["poisonDamage"].asInt(-1);
    enemy.burn = enemyObject["burn"].asInt(0);
    enemy.burnTurns = enemyObject["burnTurns"].asInt(0);
    enemy.swordSequence = enemyObject["swordSequence"].asInt(0);
    enemy.comboDraws = enemyObject["comboDraws"].asInt(0);
    enemy.ElementalSwordManaReturns = enemyObject["elementalSwordManaReturns"].asInt(0);
    enemy.ElementalSwordSequence = enemyObject["elementalSwordSequence"].asInt(0);
    enemy.ElementalSwordEnhancementLayers = enemyObject["elementalSwordEnhancementLayers"].asInt(0);
    enemy.FireGrowthRounds = enemyObject["fireGrowthRounds"].asInt(0);
    enemy.buff = enemyObject["buff"].asNumber();
    enemy.reduction = enemyObject["reduction"].asNumber();
    enemy.guard = enemyObject["guard"].asNumber();
    enemy.discardPower = enemyObject["discardPower"].asNumber();
    enemy.sect = enemyObject["sect"].asString();
    enemy.dao = enemyObject["dao"].asString();
    enemy.main = enemyObject["main"].asString();
    enemy.last = enemyObject["last"].asString();
    enemy.enabled = enemyObject["enabled"].asBool();
    enemy.attacked = enemyObject["attacked"].asBool();
    enemy.aux = jsonToStringVec(enemyObject["aux"]);
    enemy.nodes = jsonToStringVec(enemyObject["nodes"]);
    enemy.hand = jsonToStringVec(enemyObject["hand"]);
    enemy.draw = jsonToStringVec(enemyObject["draw"]);
    enemy.discard = jsonToStringVec(enemyObject["discard"]);
    enemy.exhausted = jsonToStringVec(enemyObject["exhausted"]);
    enemy.planned = jsonToStringVec(enemyObject["planned"]);
    enemy.manuals = jsonToMap<int>(enemyObject["manuals"]);
    enemy.cooldowns = jsonToMap<int>(enemyObject["cooldowns"]);
    for (const auto& card : enemyObject["deck"].asArray()) enemy.deck.push_back({card["uid"].asString(), card["spell"].asString()});
}

Value battleToJson(const Battle& battle) {
    Value battleObject(json::Object{});
    battleObject.set("enemy", str(battle.enemy));
    if (battle.shadowPlayer) {
        // Never serialize nested battles from a combat snapshot.
        Player player = *battle.shadowPlayer;
        player.arts.battle.reset();
        Value shadow = personToJson(player);
        shadow.set("talent", str(player.talent));
        shadow.set("sect", str(player.sect));
        json::Array traits;
        for (const auto& trait : player.traits) traits.push_back(str(trait));
        shadow.set("traits", Value(std::move(traits)));
        shadow.set("arts", artsToJson(player.arts));
        battleObject.set("shadowPlayer", std::move(shadow));
        battleObject.set("shadowOutcome", str(battle.shadowOutcome));
    }
    battleObject.set("enemyCards", enemyCombatToJson(battle.enemyCards));
    battleObject.set("playerFirst", Value(battle.playerFirst));
    battleObject.set("playerSpeed", num(battle.playerSpeed));
    battleObject.set("enemySpeed", num(battle.enemySpeed));
    battleObject.set("enemyStage", num(battle.enemyStage));
    battleObject.set("enemySect", str(battle.enemySect));
    battleObject.set("enemyGender", str(battle.enemyGender));
    battleObject.set("npcId", str(battle.npcId));
    battleObject.set("quest", str(battle.quest));
    battleObject.set("kind", str(battle.kind));
    battleObject.set("tideEventId",str(battle.tideEventId));
    battleObject.set("tideBattlefield",num(battle.tideBattlefield));
    battleObject.set("tideDaysCharged",num(battle.tideDaysCharged));
    battleObject.set("huntFee", num(battle.huntFee));
    battleObject.set("wildTask", str(battle.wildTask));
    battleObject.set("dungeonSite", str(battle.dungeonSite));
    battleObject.set("dungeonNode", str(battle.dungeonNode));
    battleObject.set("lethal", Value(battle.lethal));
    battleObject.set("turn", num(battle.turn));
    battleObject.set("hp", num(battle.hp));
    battleObject.set("max", num(battle.max));
    battleObject.set("enemyHp", num(battle.enemyHp));
    battleObject.set("enemyMax", num(battle.enemyMax));
    battleObject.set("enemyPower", num(battle.enemyPower));
    battleObject.set("enemySword", num(battle.enemySword));
    battleObject.set("enemySwordCult", Value(battle.enemySwordCult));
    battleObject.set("mana", num(battle.mana));
    battleObject.set("maxMana", num(battle.maxMana));
    battleObject.set("shield", num(battle.shield));
    battleObject.set("enemyShield", num(battle.enemyShield));
    battleObject.set("burn", num(battle.burn));
    battleObject.set("swordQi", num(battle.swordQi));
    battleObject.set("buff", num(battle.buff));
    battleObject.set("reduction", num(battle.reduction));
    battleObject.set("speed", num(battle.speed));
    battleObject.set("guard", num(battle.guard));
    battleObject.set("attacked", Value(battle.attacked));
    battleObject.set("played", num(battle.played));
    battleObject.set("style", str(battle.style));
    battleObject.set("retreat", Value(battle.retreat));
    battleObject.set("won", Value(battle.won));
    {
        json::Array deck;
        for (const auto& card : battle.deck) {
            Value cardObject(json::Object{});
            cardObject.set("uid", str(card.uid));
            cardObject.set("spell", str(card.spell));
            deck.push_back(std::move(cardObject));
        }
        battleObject.set("deck", Value(std::move(deck)));
    }
    battleObject.set("hand", stringVectorToJson(battle.hand));
    battleObject.set("draw", stringVectorToJson(battle.draw));
    battleObject.set("discard", stringVectorToJson(battle.discard));
    battleObject.set("exhausted", stringVectorToJson(battle.exhausted));
    battleObject.set("shuffles", num(battle.shuffles));
    battleObject.set("intentKind", str(battle.intentKind));
    battleObject.set("intentName", str(battle.intentName));
    battleObject.set("intentScale", num(battle.intentScale));
    battleObject.set("intentRaw", num(battle.intentRaw));
    battleObject.set("intentSpell", str(battle.intentSpell));
    {
        json::Array Actions;
        for (const auto& Action : battle.BeastActions) {
            Value Item(json::Object{});
            Item.set("id", str(string(BeastCombat::mfActionId(Action.Kind))));
            Item.set("rawDamage", num(Action.RawDamage)); Item.set("shieldGain", num(Action.ShieldGain));
            Item.set("healGain", num(Action.HealGain)); Item.set("poisonDamage", num(Action.PoisonDamage));
            Item.set("burnDamage", num(Action.BurnDamage)); Item.set("weakenLayers", num(Action.WeakenLayers));
            Item.set("chargeGain", num(Action.ChargeGain)); Item.set("piercePercent", num(Action.PiercePercent));
            Actions.push_back(std::move(Item));
        }
        battleObject.set("beastActions", Value(std::move(Actions)));
    }
    battleObject.set("enemyDao", str(battle.enemyDao));
    { json::Array arr; for (const auto& s : battle.enemySpells) arr.push_back(str(s));
      battleObject.set("enemySpells", Value(std::move(arr))); }
    battleObject.set("enemyBeast", Value(battle.enemyBeast));
    battleObject.set("enemyCd", mapToJson(battle.enemyCd));
    battleObject.set("enemyWeaken", num(battle.enemyWeaken));
    battleObject.set("cooldowns", mapToJson(battle.cooldowns));
    battleObject.set("poison", num(battle.poison));
    battleObject.set("poisonTurns", num(battle.poisonTurns));
    battleObject.set("poisonDamage", num(battle.poisonDamage));
    battleObject.set("playerPoison", num(battle.playerPoison));
    battleObject.set("playerPoisonTurns", num(battle.playerPoisonTurns));
    battleObject.set("burnTurns", num(battle.burnTurns));
    battleObject.set("water", num(battle.water));
    battleObject.set("soul", num(battle.soul));
    battleObject.set("charge", num(battle.charge));
    battleObject.set("lock", num(battle.lock));
    battleObject.set("carryMana", num(battle.carryMana));
    battleObject.set("manaBonus", num(battle.manaBonus));
    battleObject.set("manaPenalty", num(battle.manaPenalty));
    battleObject.set("drawNext", num(battle.drawNext));
    battleObject.set("pendingDraw", num(battle.pendingDraw));
    battleObject.set("carryCap", num(battle.carryCap));
    battleObject.set("hpCostPct", num(battle.hpCostPct));
    battleObject.set("discardPower", num(battle.discardPower));
    battleObject.set("swordSequence", num(battle.swordSequence));
    battleObject.set("comboDraws", num(battle.comboDraws));
    battleObject.set("elementalSwordManaReturns", num(battle.ElementalSwordManaReturns));
    battleObject.set("elementalSwordSequence", num(battle.ElementalSwordSequence));
    battleObject.set("elementalSwordEnhancementLayers", num(battle.ElementalSwordEnhancementLayers));
    battleObject.set("fireGrowthRounds", num(battle.FireGrowthRounds));
    battleObject.set("last", str(battle.last));
    {
        json::Array lg;
        for (const auto& s : battle.log) lg.push_back(str(s));
        battleObject.set("log", Value(std::move(lg)));
    }
    battleObject.set("advKind", str(battle.advKind));
    battleObject.set("advSeq", num(battle.advSeq));
    battleObject.set("advChoice", str(battle.advChoice));
    battleObject.set("advFee", num(battle.advFee));
    battleObject.set("advGrade", num(battle.advGrade));
    battleObject.set("advBook", str(battle.advBook));
    battleObject.set("advNpcId", str(battle.advNpcId));
    battleObject.set("advStyle", str(battle.advStyle));
    return battleObject;
}

void battleFromJson(const Value& battleObject, Battle& battle) {
    battle.enemy = battleObject["enemy"].asString();
    if (battleObject["shadowPlayer"].isObject()) {
        const Value& shadow = battleObject["shadowPlayer"];
        Player player;
        personFromJson(shadow, player);
        player.talent = shadow["talent"].asString();
        player.sect = shadow["sect"].asString();
        player.traits = jsonToStringVec(shadow["traits"]);
        artsFromJson(shadow["arts"], player.arts);
        player.arts.battle.reset();
        battle.shadowPlayer = std::make_shared<const Player>(std::move(player));
        battle.shadowOutcome = battleObject["shadowOutcome"].asString();
    }
    enemyCombatFromJson(battleObject["enemyCards"], battle.enemyCards);
    battle.playerFirst = battleObject["playerFirst"].asBool(true);
    battle.playerSpeed = battleObject["playerSpeed"].asInt();
    battle.enemySpeed = battleObject["enemySpeed"].asInt();
    battle.enemyStage = battleObject["enemyStage"].asInt();
    battle.enemySect = battleObject["enemySect"].asString();
    battle.enemyGender = battleObject["enemyGender"].asString();
    battle.npcId = battleObject["npcId"].asString();
    battle.quest = battleObject["quest"].asString();
    battle.kind = battleObject["kind"].asString("spar");
    battle.tideEventId=battleObject["tideEventId"].asString();
    battle.tideBattlefield=battleObject["tideBattlefield"].asInt(-1);
    battle.tideDaysCharged=battleObject["tideDaysCharged"].asInt();
    battle.huntFee = battleObject["huntFee"].asInt();
    battle.wildTask = battleObject["wildTask"].asString();
    battle.dungeonSite = battleObject["dungeonSite"].asString();
    battle.dungeonNode = battleObject["dungeonNode"].asString();
    battle.lethal = battleObject["lethal"].asBool();
    battle.turn = battleObject["turn"].asInt();
    battle.hp = battleObject["hp"].asInt(1);
    battle.max = battleObject["max"].asInt(1);
    battle.enemyHp = battleObject["enemyHp"].asInt(1);
    battle.enemyMax = battleObject["enemyMax"].asInt(1);
    battle.enemyPower = battleObject["enemyPower"].asInt(14);
    battle.enemySword = battleObject["enemySword"].asInt();
    battle.enemySwordCult = battleObject["enemySwordCult"].asBool();
    battle.mana = battleObject["mana"].asInt();
    battle.maxMana = battleObject["maxMana"].asInt(3);
    battle.shield = battleObject["shield"].asInt();
    battle.enemyShield = battleObject["enemyShield"].asInt();
    battle.burn = battleObject["burn"].asInt();
    battle.swordQi = battleObject["swordQi"].asInt();
    battle.buff = battleObject["buff"].asNumber();
    battle.reduction = battleObject["reduction"].asNumber();
    battle.speed = battleObject["speed"].asInt();
    battle.guard = battleObject["guard"].asNumber();
    battle.attacked = battleObject["attacked"].asBool();
    battle.played = battleObject["played"].asInt();
    battle.style = battleObject["style"].asString("剑");
    battle.retreat = battleObject["retreat"].asBool();
    battle.won = battleObject["won"].asBool();
    battle.deck.clear();
    for (const auto& cardObject : battleObject["deck"].asArray())
        battle.deck.push_back({cardObject["uid"].asString(), cardObject["spell"].asString()});
    battle.hand = jsonToStringVec(battleObject["hand"]);
    battle.draw = jsonToStringVec(battleObject["draw"]);
    battle.discard = jsonToStringVec(battleObject["discard"]);
    battle.exhausted = jsonToStringVec(battleObject["exhausted"]);
    battle.shuffles = battleObject["shuffles"].asInt();
    battle.intentKind = battleObject["intentKind"].asString();
    battle.intentName = battleObject["intentName"].asString();
    battle.intentScale = battleObject["intentScale"].asNumber();
    battle.intentRaw = battleObject["intentRaw"].asInt();
    battle.intentSpell = battleObject["intentSpell"].asString();
    for (const auto& Item : battleObject["beastActions"].asArray()) {
        const auto Kind = BeastCombat::mfParseActionKind(Item["id"].asString());
        if (!Kind) continue; // 外层结构校验已经拒绝未知ID；直接内部调用仍不伪造有效动作。
        BeastCombat::PlannedAction Action;
        Action.Kind = *Kind; Action.RawDamage = Item["rawDamage"].asInt();
        Action.ShieldGain = Item["shieldGain"].asInt(); Action.HealGain = Item["healGain"].asInt();
        Action.PoisonDamage = Item["poisonDamage"].asInt(); Action.BurnDamage = Item["burnDamage"].asInt();
        Action.WeakenLayers = Item["weakenLayers"].asInt(); Action.ChargeGain = Item["chargeGain"].asInt();
        Action.PiercePercent = Item["piercePercent"].asInt();
        battle.BeastActions.push_back(Action);
    }
    battle.enemyDao = battleObject["enemyDao"].asString();
    battle.enemySpells = jsonToStringVec(battleObject["enemySpells"]);
    battle.enemyBeast = battleObject["enemyBeast"].asBool();
    battle.enemyCd = jsonToMap<int>(battleObject["enemyCd"]);
    battle.enemyWeaken = battleObject["enemyWeaken"].asInt();
    battle.cooldowns = jsonToMap<int>(battleObject["cooldowns"]);
    battle.poison = battleObject["poison"].asInt();
    battle.poisonTurns = battleObject["poisonTurns"].asInt();
    battle.poisonDamage = battleObject["poisonDamage"].asInt(-1);
    battle.playerPoison = battleObject["playerPoison"].asInt();
    battle.playerPoisonTurns = battleObject["playerPoisonTurns"].asInt();
    battle.burnTurns = battleObject["burnTurns"].asInt();
    battle.water = battleObject["water"].asInt();
    battle.soul = battleObject["soul"].asInt();
    battle.charge = battleObject["charge"].asInt();
    battle.lock = battleObject["lock"].asInt();
    battle.carryMana = battleObject["carryMana"].asInt();
    battle.manaBonus = battleObject["manaBonus"].asInt();
    battle.manaPenalty = battleObject["manaPenalty"].asInt();
    battle.drawNext = battleObject["drawNext"].asInt();
    battle.pendingDraw = battleObject["pendingDraw"].asInt();
    battle.carryCap = battleObject["carryCap"].asInt();
    battle.hpCostPct = battleObject["hpCostPct"].asInt();
    battle.discardPower = battleObject["discardPower"].asNumber();
    battle.swordSequence = battleObject["swordSequence"].asInt();
    battle.comboDraws = battleObject["comboDraws"].asInt();
    battle.ElementalSwordManaReturns = battleObject["elementalSwordManaReturns"].asInt(0);
    battle.ElementalSwordSequence = battleObject["elementalSwordSequence"].asInt(0);
    battle.ElementalSwordEnhancementLayers = battleObject["elementalSwordEnhancementLayers"].asInt(0);
    battle.FireGrowthRounds = battleObject["fireGrowthRounds"].asInt(0);
    battle.last = battleObject["last"].asString();
    battle.log = jsonToStringVec(battleObject["log"]);
    battle.advKind = battleObject["advKind"].asString();
    battle.advSeq = battleObject["advSeq"].asInt();
    battle.advChoice = battleObject["advChoice"].asString();
    battle.advFee = battleObject["advFee"].asInt();
    battle.advGrade = battleObject["advGrade"].asInt(1);
    battle.advBook = battleObject["advBook"].asString();
    battle.advNpcId = battleObject["advNpcId"].asString();
    battle.advStyle = battleObject["advStyle"].asString();
}

} // namespace nz::persistence
