#include "harness.h"

#include "core/adventure.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/content/artifact_appearances.h"
#include "core/content/gear_templates.h"
#include "core/crafting/craft_prices.h"
#include "core/crafting/forge_rules.h"
#include "core/crafting/forging_product.h"
#include "core/persistence/forge_codec.h"
#include "core/progression/lifebound_rules.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {

void mfRunGameplayTemplateTests() {
    std::printf("\n-- 事件制成品模板与纯配方 --\n");
    checkEq(nz::content::mfArtifactAppearances().size(), 11, "十一种器型均有白名单立绘");
    check(!nz::content::mfFindArtifactAppearance("../../outside"), "未知器型不生成外部资源路径");
    for (const auto& Definition : nz::content::mfGearTemplates()) {
        for (int Grade = 1; Grade <= 5; ++Grade) {
            const auto Product = nz::crafting::mfCreateGearFromTemplate(Definition.Id, Grade);
            check(Product.has_value(), "每种模板在五个品阶均可由完整配方成器");
            if (!Product) continue;
            check(nz::persistence::mfForgeGearStateError(*Product).empty(), "模板产物满足真实V2成品校验");
            checkEq(Product->grade, Grade, "模板品阶来自冻结参数");
            checkStr(Product->Origin, "event", "掉落法器不会冒充人物亲炼");
            const auto Restored = nz::persistence::mfGearFromJson(nz::persistence::mfGearToJson(*Product));
            check(Restored.SourceMaterials == Product->SourceMaterials && Restored.Origin == "event", "来源与十份配方往返一致");
        }
    }
    check(!nz::crafting::mfCreateGearFromTemplate("missing", 1), "未知奖励模板拒绝");
    check(!nz::crafting::mfCreateGearFromTemplate("damage_sword", 0), "非法奖励品阶拒绝");
    check(!nz::crafting::mfCreateGearFromTemplate("damage_sword", 1, "forged-by-someone"), "未知来源拒绝");
    nz::CreateOptions Options;
    Options.seed = 1213;
    auto State = nz::createGame(Options);
    State.pending.reset();
    const auto RngBefore = State.rng;
    auto Product = *nz::crafting::mfCreateGearFromTemplate("damage_sword", 1);
    Product.id = "test-template";
    State.player.traits.push_back("artisan");
    const auto RewardPrice = nz::crafting::mfQuoteGearPrice(State, Product);
    Product.Origin = "player";
    const auto CraftedPrice = nz::crafting::mfQuoteGearPrice(State, Product);
    check(RewardPrice.Valid && CraftedPrice.Valid && CraftedPrice.SellPrice > RewardPrice.SellPrice,
        "亲炼售价天赋仅加成亲炼成品，不奖励掉落或出生遗宝");
    checkEq(State.rng, RngBefore, "纯模板和价格查询不推进共享随机数");
    nz::adventureEvent(State, "backhill");
    check(State.pending && State.pending->adventure && State.pending->adventure->RewardVersion == 1,
        "真实后山事件在生成时冻结新模板奖励");
    if (!State.pending || !State.pending->adventure) return;
    const auto Frozen = *State.pending->adventure;
    nz::Game Restored;
    std::string Error;
    check(nz::loadFromJson(nz::saveToJson(State), Restored, &Error), "冻结事件可以完整存读档");
    if (Restored.pending && Restored.pending->adventure)
        checkStr(Restored.pending->adventure->GearTemplateId, Frozen.GearTemplateId, "读取不重新抽取奖励模板");
    State.pending.reset();
    nz::Battle Result;
    Result.won = true;
    Result.advKind = "backhill";
    Result.advGrade = Frozen.grade;
    Result.AdvRewardVersion = Frozen.RewardVersion;
    Result.AdvGearTemplateId = Frozen.GearTemplateId;
    const auto CountBefore = State.player.arts.gear.size();
    nz::adventureBattleEnd(State, Result);
    checkEq(State.player.arts.gear.size(), CountBefore + 1, "真实胜利领取已冻结的制成品");
    if (State.player.arts.gear.size() > CountBefore)
        check(State.player.arts.gear.back().ForgeVersion == 2 && State.player.arts.gear.back().Origin == "event",
            "后山奖励直接可使用新版主动与被动效果");
}

void mfRunLifeboundSlotTests() {
    std::printf("\n-- 单槽本命可换与固定招式 --\n");
    nz::CreateOptions Options; Options.seed = 1441;
    auto State = nz::createGame(Options);
    State.pending.reset();
    State.player.stage = 3;
    State.player.arts.LifeboundKind = "retain";
    const auto SpellId = State.player.arts.spells.front();
    check(nz::mfSetLifeboundSpell(State, SpellId).empty(), "已领悟神通可设为唯一的本命槽");
    const auto LogCount = State.logs.size();
    check(nz::mfSetLifeboundSpell(State, SpellId).empty() && State.logs.size() == LogCount, "同目标重复设置不重复日志或资源变化");
    check(nz::mfSetLifeboundSpell(State, "strike").empty(), "普通攻击也可设本命");
    check(nz::mfSetLifeboundSpell(State, "defend").empty(), "普通防御也可设本命");
    check(nz::mfSetLifeboundSpell(State, SpellId).empty(), "战斗外允许A到B再到A更换");
    const auto BeforeInvalid = State.player.arts.LifeboundSpell;
    check(!nz::mfSetLifeboundSpell(State, "artifact:fake").empty() && State.player.arts.LifeboundSpell == BeforeInvalid,
        "非法或法器主动不能覆盖本命");
    State.player.arts.spells.clear();
    const auto Quote = nz::mfQuoteLifeboundSpell(State, SpellId);
    check(!Quote.IsCarried && State.player.arts.LifeboundSpell == SpellId, "卸下不清空本命选择，绑定不额外增加牌组");
    State.job = nz::Job{};
    check(!nz::mfSetLifeboundSpell(State, "strike").empty(), "耗时行动中不能更换本命");
    checkStr(State.player.arts.LifeboundKind, "retain", "更换神通保持筑基路线的本命特性");
    State.job.reset();
    for (const std::string Kind : {"innate", "retain"}) {
        State.player.arts.LifeboundKind = Kind;
        check(nz::mfSetLifeboundSpell(State, "strike").empty(), "固定攻击在两种本命路线均可设置");
        nz::EnemySpec Enemy; Enemy.name = "本命固定牌检查"; Enemy.stage = 0; Enemy.kind = "spar";
        nz::battleStart(State, Enemy);
        const auto& Active = *State.player.arts.battle;
        int Marked = 0;
        for (const auto& Card : Active.deck)
            if (Card.spell == "strike" && (Kind == "innate" ? Card.Innate : Card.Retain)) ++Marked;
        checkEq(Marked, 1, "固定攻击的多个真实副本仍只有一张获得本命特性");
        State.player.arts.battle.reset();
    }
}

} // namespace nztest
