#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/social/npc_consumables.h"
#include "data/reference_tables.h"

#include <algorithm>
#include <set>

namespace nztest {
namespace {
// 用途：建立缺少相应属性、拥有合法库存的隔离角色；流程：服丹实效验收；用法：测试内部调用；返回：不依赖玩家账号的 Game。
nz::Game mfPillTestGame() {
    nz::CreateOptions Options; Options.seed = 4151; Options.traitsExplicit = true;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear(); State.logs.clear();
    auto& Player = State.player;
    Player.stage = Player.highestStage = 6; Player.lifespan = nz::realmOf(6).life;
    Player.health = 10; Player.xp = 0; Player.apt = 100; Player.insight = 30;
    Player.lifeBonus = 0; Player.body = 0; Player.talent.clear(); Player.traits.clear();
    auto& Arts = Player.arts;
    Arts.aux.clear(); Arts.nodes.clear(); Arts.gear.clear(); Arts.equipped.clear(); Arts.spells.clear();
    Arts.poison = 30; Arts.hpBonus = Arts.heart = Arts.mind = Arts.speed = Arts.aptPills = 0;
    Arts.points = Arts.repel = 0; Arts.breakBuff.clear(); Arts.combatPrep.clear(); Arts.combatBuff = 0;
    Arts.potions.clear(); Arts.used.clear(); Arts.materials.clear();
    for (const auto* Dao : nz::ART_DAOS) Arts.exp[Dao] = 0;
    return State;
}

// 用途：读取指定准备种类在真实 Battle 中的数值；流程：目录、开局与消耗时机核对；用法：传 Battle 和固定协议键；返回：实际状态值。
double mfPreparationValue(const nz::Battle& Battle, const nz::string& Key) {
    if (Key == "shield") return Battle.shield;
    if (Key == "reduction") return Battle.reduction;
    if (Key == "qi") return Battle.swordQi;
    if (Key == "mana") return Battle.mana;
    if (Key == "speed") return Battle.speed;
    return Battle.buff;
}

// 用途：建立玩家先手的实际对局；流程：战斗丹收益测试；用法：传隔离 Game，保持其丹药准备；返回：无，调用真实 battleStart。
void mfStartPillBattle(nz::Game& State) {
    nz::EnemySpec Enemy;
    Enemy.name = "丹药验收对手"; Enemy.stage = 6; Enemy.speed = 0;
    Enemy.kind = "spar"; Enemy.style = "体"; Enemy.beast = true;
    nz::battleStart(State, Enemy);
}

// 用途：核对具有不同效果的战斗丹实际进入对局、且不免费带到下一场；流程：六类准备的生命周期验收；返回：无，失败记测试计数。
void mfCheckCombatPill(const nz::string& RecipeId, const nz::string& Key) {
    auto State = mfPillTestGame(); State.player.health = 100; State.player.arts.poison = 0;
    State.player.arts.potions[RecipeId] = 2;
    const auto Baseline = nz::battleReferenceState(State.player);
    check(nz::artUsePill(State, RecipeId).empty(), "战斗丹实际服下：" + RecipeId);
    const auto Prepared = nz::battleReferenceState(State.player);
    check(mfPreparationValue(Prepared, Key) > mfPreparationValue(Baseline, Key), "战斗准备实际提升对应状态：" + Key);
    const auto SecondQuote = nz::artPillUseInfo(State.player, *nz::findRecipe(RecipeId));
    checkNear(SecondQuote.effectValue, 0, "同类战斗丹再次服用不叠加：" + Key);
    check(nz::artUsePill(State, RecipeId).empty(), "耐药未满时同类覆盖服用仍按旧规则允许：" + Key);
    checkNear(mfPreparationValue(nz::battleReferenceState(State.player), Key), mfPreparationValue(Prepared, Key),
        "同类覆盖不能翻倍准备：" + Key);
    mfStartPillBattle(State);
    check(State.player.arts.battle.has_value(), "服丹后进入真实对局：" + Key);
    if (!State.player.arts.battle) return;
    checkNear(mfPreparationValue(*State.player.arts.battle, Key), mfPreparationValue(Prepared, Key),
        "真实开局与服丹预览同值：" + Key);
    check(State.player.arts.combatPrep.empty() && State.player.arts.combatBuff == 0,
        "开局成功后只消费一次准备：" + Key);
    if (Key == "speed") checkEq(State.player.arts.battle->playerSpeed, nz::battleSpeed(State.player) + 10,
        "疾行准备真实参与先手速度");
    if (Key == "mana") {
        checkEq(nz::battleManaCap(State.player), 12, "开局灵力丹不虚增永久灵力容量");
        checkEq(State.player.arts.battle->maxMana, Baseline.maxMana, "开局灵力丹不虚增每轮恢复量");
    }
    const auto Used = State.player.arts.used[RecipeId];
    nz::Game Restored; nz::string Error;
    check(nz::loadDetachedFromJson(nz::saveToJson(State), Restored, &Error), "在战服丹状态可保存读取：" + Key + Error);
    if (Restored.player.arts.battle) {
        checkNear(mfPreparationValue(*Restored.player.arts.battle, Key), mfPreparationValue(*State.player.arts.battle, Key),
            "在战读档保留真实效果、不重放服丹：" + Key);
        checkEq(Restored.player.arts.used[RecipeId], Used, "在战读档不重复累计耐药：" + Key);
    }
    State.player.arts.battle.reset();
    mfStartPillBattle(State);
    checkNear(mfPreparationValue(*State.player.arts.battle, Key), mfPreparationValue(Baseline, Key),
        "下一场不能再次免费取得旧准备：" + Key);
}
} // namespace

// 用途：覆盖全部丹方实服、失败原子性、六类真实斗法收益和存档；流程：核心回归；用法：由测试主入口调用；返回：无。
void mfRunPillEffectsTests() {
    using namespace nz;
    std::printf("\n--- 全部丹药服用与实际斗法收益 ---\n");
    std::set<string> CoveredKinds;
    for (const auto& Recipe : data::RecipeDef_TABLE()) {
        auto State = mfPillTestGame();
        auto& Player = State.player; auto& Arts = Player.arts;
        Arts.potions[Recipe.id] = 2;
        if (Recipe.kind == "breakthrough") {
            Player.stage = Player.highestStage = artPillUseInfo(Player, Recipe).requiredStage;
            Player.lifespan = realmOf(Player.stage).life;
        }
        const auto Before = Player;
        const auto Snapshot = saveToJson(State);
        const auto Quote = artPillUseInfo(Player, Recipe);
        check(saveToJson(State) == Snapshot, "服丹预览不修改真实状态：" + Recipe.name);
        check(Quote.reason.empty() && artUsePill(State, Recipe.id).empty(), "目录每味丹药均可实际执行：" + Recipe.name);
        check(Arts.potions.at(Recipe.id) == 1 && Arts.used.at(Recipe.id) == 1 && Arts.poison == Quote.poisonAfter,
            "库存/耐药/丹毒实际结算：" + Recipe.name);
        const auto& Kind = Recipe.kind; CoveredKinds.insert(Kind);
        double ActualChange = 0;
        if (Kind == "heal") ActualChange = Player.health - Before.health;
        else if (Kind == "xp") ActualChange = Player.xp - Before.xp;
        else if (Kind == "apt") ActualChange = Player.apt - Before.apt;
        else if (Kind == "insight") ActualChange = Player.insight - Before.insight;
        else if (Kind == "life") ActualChange = Player.lifespan - Before.lifespan;
        else if (Kind == "body") ActualChange = Arts.hpBonus - Before.arts.hpBonus;
        else if (Kind == "heart") ActualChange = Arts.heart - Before.arts.heart;
        else if (Kind == "mind") ActualChange = Arts.mind - Before.arts.mind;
        else if (Kind == "speed") ActualChange = Arts.speed - Before.arts.speed;
        else if (Kind == "points") ActualChange = Arts.points - Before.arts.points;
        else if (Kind == "dao") ActualChange = Arts.exp.at(Recipe.dao) - Before.arts.exp.at(Recipe.dao);
        else if (Kind == "detox") {
            check(Arts.poison < Before.arts.poison, "化毒丹实际降低丹毒：" + Recipe.name);
            continue;
        } else if (Kind == "breakthrough") {
            check(Arts.breakBuff == "stage", "突破丹实际生成可消费准备：" + Recipe.name);
            continue;
        } else if (Kind == "repel") {
            checkEq(Arts.repel, 2, "驱妖液实际生成年度避险状态");
            continue;
        } else if (Kind == "lure") {
            int MaterialCount = 0;
            for (const auto& [Id, Count] : Arts.materials) MaterialCount += Count;
            checkEq(MaterialCount, 2, "诱妖液实际取得两份材料");
            continue;
        } else if (Kind == "combat") {
            const auto Preparation = artCombatPill(Recipe);
            checkNear(Arts.combatPrep.at(Preparation.key), Preparation.value, "战斗丹写入真实准备：" + Recipe.name);
            check(mfPreparationValue(battleReferenceState(Player), Preparation.key)
                > mfPreparationValue(battleReferenceState(Before), Preparation.key), "每味战斗丹存在实际下场收益：" + Recipe.name);
            continue;
        } else check(false, "新丹药种类须增加实际效果测试：" + Kind);
        check(ActualChange > 0 && ActualChange == Quote.effectValue, "属性/修为/悟道真实增加且预览一致：" + Recipe.name);
    }
    checkEq(CoveredKinds.size(), 16, "所有现有丹药种类均被遍历");

    mfCheckCombatPill("5006", "attack"); mfCheckCombatPill("5008", "shield");
    mfCheckCombatPill("5009", "reduction"); mfCheckCombatPill("5113", "qi");
    mfCheckCombatPill("5005", "mana"); mfCheckCombatPill("5103", "speed");
    mfCheckCombatPill("5121", "reduction"); mfCheckCombatPill("5225", "reduction");

    for (const auto& [RecipeId, SpellId] : vector<std::pair<string, string>>{{"5006", "strike"}, {"5113", "sword"}}) {
        auto Baseline = mfPillTestGame(); Baseline.player.health = 100; Baseline.player.arts.poison = 0;
        Baseline.player.arts.spells = {"sword"}; Baseline.player.arts.potions[RecipeId] = 1;
        auto Prepared = Baseline;
        check(artUsePill(Prepared, RecipeId).empty(), "实际伤害对照先服丹：" + RecipeId);
        mfStartPillBattle(Baseline); mfStartPillBattle(Prepared);
        for (auto* State : {&Baseline, &Prepared}) {
            auto& Battle = *State->player.arts.battle;
            const auto Card = std::find_if(Battle.deck.begin(), Battle.deck.end(), [&](const auto& Entry) { return Entry.spell == SpellId; });
            if (Card != Battle.deck.end()) {
                Battle.hand = {Card->uid};
                std::erase(Battle.draw, Card->uid); std::erase(Battle.discard, Card->uid);
            }
        }
        const int Before = Baseline.player.arts.battle->enemyHp;
        check(battleTurn(Baseline, SpellId).ok && battleTurn(Prepared, SpellId).ok, "调用真实出牌结算：" + SpellId);
        check(Baseline.player.arts.battle && Prepared.player.arts.battle &&
            Before - Prepared.player.arts.battle->enemyHp > Before - Baseline.player.arts.battle->enemyHp,
            "服丹后同牌真正造成更多伤害：" + RecipeId);
    }
    for (const auto* RecipeId : {"5008", "5009", "5121", "5225"}) {
        auto Baseline = mfPillTestGame(); Baseline.player.health = 100; Baseline.player.arts.poison = 0;
        Baseline.player.arts.potions[RecipeId] = 1; auto Prepared = Baseline;
        check(artUsePill(Prepared, RecipeId).empty(), "实际来伤对照先服丹：" + string(RecipeId));
        if (string(RecipeId) != "5008") checkNear(Prepared.player.arts.combatBuff, 0,
            "减伤丹不能误增攻击：" + string(RecipeId));
        mfStartPillBattle(Baseline); mfStartPillBattle(Prepared);
        check(battleTurn(Baseline, "end").ok && battleTurn(Prepared, "end").ok, "调用真实敌方回合结算：" + string(RecipeId));
        check(Baseline.player.arts.battle && Prepared.player.arts.battle &&
            Prepared.player.arts.battle->hp > Baseline.player.arts.battle->hp,
            "护盾或减伤丹真正减少同一轮气血损失：" + string(RecipeId));
    }
    for (const auto& [RecipeId, Key] : vector<std::pair<string, string>>{
        {"5006", "attack"}, {"5008", "shield"}, {"5009", "reduction"},
        {"5113", "qi"}, {"5005", "mana"}, {"5103", "speed"}}) {
        auto State = mfPillTestGame(); State.player.health = 100; State.player.arts.speed = 500;
        Npc Npc;
        Npc.id = "pill-effects-npc"; Npc.name = "真实服丹道友"; Npc.stage = 6; Npc.lifespan = realmOf(6).life;
        Npc.dao = Key == "qi" ? "剑" : "水"; Npc.bag["potion:" + RecipeId] = 1;
        State.npcs.push_back(Npc);
        EnemySpec Enemy;
        Enemy.id = Npc.id; Enemy.name = Npc.name; Enemy.stage = Npc.stage; Enemy.style = Npc.dao; Enemy.kind = "spar";
        battleStart(State, Enemy);
        checkEq(State.npcs[0].bag.at("potion:" + RecipeId), 0, "NPC真实开局自动使用适用战斗丹：" + RecipeId);
        checkEq(State.npcs[0].pillState.used.at(RecipeId), 1, "NPC实际耐药只累计一次：" + RecipeId);
        check(State.npcs[0].pillState.combatPrep.empty(), "NPC准备进入对局后被消费：" + Key);
        const auto& Battle = *State.player.arts.battle;
        double Value = 0;
        if (Key == "attack") Value = Battle.enemyCards.buff;
        else if (Key == "shield") Value = Battle.enemyShield;
        else if (Key == "reduction") Value = Battle.enemyCards.reduction;
        else if (Key == "qi") Value = Battle.enemyCards.swordQi;
        else if (Key == "mana") Value = Battle.enemyCards.mana - Battle.enemyCards.maxMana;
        else Value = Battle.enemySpeed - battleSpeed([&] { Player Foe; static_cast<Person&>(Foe) = Npc;
            Foe.talent.clear(); Foe.arts = artNpcLoadout(Npc); return Foe; }());
        check(Value > 0, "NPC药效真实进入对应战斗字段：" + Key);
    }

    {
        auto State = mfPillTestGame(); State.player.arts.potions["5005"] = 1;
        const auto Preparation = artCombatPill(*findRecipe("5005"));
        check(Preparation.text.find("上限") == string::npos, "凝神丹完整说明不承诺不存在的容量提升");
        check(artCombatPill(*findRecipe("5103")).text.find("先手") != string::npos,
            "疾行丹完整说明包括真实生效的先手速度");
        auto Renamed = *findRecipe("5008"); Renamed.sourceEffect = "改版文案";
        checkStr(artCombatPill(Renamed).key, "shield", "战斗丹类别不依赖可改中文文案");
        checkStr(artCombatPill(*findRecipe("5121")).key, "reduction", "龟甲丹按防御用途生成减伤准备");
        checkStr(artCombatPill(*findRecipe("5225")).key, "reduction", "玄武丹按防御用途生成减伤准备");
        auto Ascent = *findRecipe("5406"); Ascent.sourceEffect = "改版文案";
        checkEq(artPillUseInfo(State.player, Ascent).requiredStage, 8, "朝元丹稳定属于结婴前金丹后期准备");
    }
    {
        auto State = mfPillTestGame(); State.player.health = 100; State.player.arts.potions["5001"] = 1;
        const auto Quote = artPillUseInfo(State.player, *findRecipe("5001"));
        check(artUsePill(State, "5001").empty(), "满血服丹保留既有允许规则");
        check(State.logs.back().text.find(Quote.effect) != string::npos,
            "服丹日志显示当前实际收益而非虚报完整能力");
        State.player.arts.poison = 30; State.player.arts.potions["5316"] = 1;
        check(artUsePill(State, "5316").empty(), "真实化毒丹执行成功");
        check(State.logs.back().delta.find("丹毒 -20") != string::npos, "化毒日志记录真实负丹毒变化");
    }
    {
        auto State = mfPillTestGame(); State.player.arts.potions["5001"] = 1;
        State.player.arts.used["5001"] = findRecipe("5001")->tolerance;
        const auto Snapshot = saveToJson(State);
        check(!artUsePill(State, "5001").empty(), "耐药满后拒绝服药");
        check(!artUsePill(State, "unknown-pill").empty(), "未知丹药拒绝执行");
        check(saveToJson(State) == Snapshot, "拒绝服丹保持全状态、RNG、时间与日志原子不变");
    }
    {
        auto State = mfPillTestGame(); State.player.arts.potions["5005"] = 2;
        check(artUsePill(State, "5005").empty(), "场外准备存档夹具服丹");
        Game Restored; string Error;
        auto Document = json::Value::parse(saveToJson(State));
        Document.asObject()["player"].asObject()["arts"].asObject().erase("combatPrep");
        check(loadFromJson(Document.dump(), Restored, &Error), "缺少准备字段的旧档仍可读取：" + Error);
        check(Restored.player.arts.combatPrep.empty(), "旧档缺省准备不凭空重放历史服用");
        checkEq(Restored.player.arts.used["5005"], 1, "旧档保留已结算服用次数");
        check(loadFromJson(saveToJson(State), Restored, &Error), "现代场外准备存档可读取：" + Error);
        checkNear(Restored.player.arts.combatPrep.at("mana"), 2, "场外存档保留准备直到真实下场开局");
    }
}
} // namespace nztest
