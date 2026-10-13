#include "core/interface/routes.h"
#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/shop.h"
#include "core/crafting/material_supply.h"
#include "core/talents.h"
#include "core/interface/view_policy.h"
#include "core/interface/craft_price_views.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::api::view {
// 用途：复制角色并替换指定装备供效果比较。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const Gear& gear，按本模块入口调用。
// 返回值：Player结果，包含说明所述状态或计算数据。
Player artifactProjectedPlayer(const Player& player, const Gear& gear) {
    Player projected = player;
    auto& inventory = projected.arts.gear;
    auto existing = std::find_if(inventory.begin(), inventory.end(), [&](const Gear& item) { return item.id == gear.id; });
    if (existing == inventory.end()) inventory.push_back(gear); else *existing = gear;
    projected.arts.equipped[gear.slot] = gear.id;
    return projected;
}
// 用途：比较装备替换前后被动数值变化。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const Gear& gear，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
Value artifactPassiveView(const Player& player, const Gear& gear) {
    Player projected = artifactProjectedPlayer(player, gear);
    const ArtMods before = talentMods(player), after = talentMods(projected);
    const double primary = gear.bonus * (artHas(player.arts, "器", 3) ? 1.2 : 1.0) * (gear.inscribed ? 1.12 : 1.0);
    const double element = gear.bonus * 0.5;
    const double polarity = gear.bonus * (gear.polarity == "天阳" ? 0.1 : 0.2);
    const std::string primaryName = gear.slot == "weapon" ? "攻击倍率" : gear.slot == "armor" ? "气血倍率" : "灵力属性";
    const double primaryValue = gear.slot == "accessory" ? jsRound(primary * 10) : primary * 100;
    std::string text = "装备被动：" + primaryName + "+" + decimal(primaryValue) +
        (gear.slot == "accessory" ? "点" : "个百分点") + "；";
    if (daoIndex(gear.element) >= 0) text += gear.element + "系伤害倍率+" + decimal(element * 100) + "个百分点；";
    text += (gear.polarity == "天阳" ? "攻击倍率+" : "护盾倍率+") + decimal(polarity * 100) + "个百分点。";
    text += "替换当前同槽装备后：攻击倍率" + decimal((after.attack - before.attack) * 100) +
        "个百分点、气血上限" + std::to_string(battleReferenceState(projected).max - battleReferenceState(player).max) +
        "、每回合灵力" + std::to_string(battleEnergy(projected) - battleEnergy(player)) +
        "（已计入属性上限；灵力属性每4点影响自然回复）。";
    Value entry(json::Object{});
    entry.set("text", str(text)); entry.set("primaryStat", str(primaryName)); entry.set("primaryValue", num(primaryValue));
    entry.set("elementBonusPct", num(daoIndex(gear.element) >= 0 ? element * 100 : 0));
    entry.set("polarityStat", str(gear.polarity == "天阳" ? "attack" : "shield")); entry.set("polarityBonusPct", num(polarity * 100));
    entry.set("inscribed", Value(gear.inscribed));
    Value delta(json::Object{});
    delta.set("attackPct", num((after.attack - before.attack) * 100)); delta.set("hpPct", num((after.hp - before.hp) * 100));
    delta.set("shieldPct", num((after.shield - before.shield) * 100)); delta.set("mana", num(after.mana - before.mana));
    delta.set("hpMax", num(battleReferenceState(projected).max - battleReferenceState(player).max));
    delta.set("manaRegen", num(battleEnergy(projected) - battleEnergy(player)));
    const int elementIndex = daoIndex(gear.element); delta.set("elementPct", num(elementIndex >= 0 ? (after.elements[elementIndex] - before.elements[elementIndex]) * 100 : 0));
    entry.set("delta", std::move(delta)); return entry;
}
// 用途：编码法宝主动神通与境界费用和真实预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const ArtifactAbility& ability，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
Value artifactView(const Player& player, const ArtifactAbility& ability) {
    if (!ability.valid) return Value();
    const auto& spell = ability.spell;
    Value entry(json::Object{});
    entry.set("id", str(spell.id)); entry.set("gearId", str(ability.gearId));
    entry.set("name", str(spell.name)); entry.set("shape", str(ability.shape)); entry.set("shapeName", str(ability.shapeName));
    entry.set("dao", str(spell.dao)); entry.set("kind", str(spell.kind));
    entry.set("description", str(ability.desc)); entry.set("cost", num(battleCost(player, spell)));
    entry.set("cooldown", num(spell.cooldown)); entry.set("stage", num(spell.stage)); entry.set("stageName", str(realmName(spell.stage)));
    const Player projected = artifactProjectedPlayer(player, ability.gear);
    entry.set("preview", spellPreview(projected, spell));
    entry.set("passive", artifactPassiveView(player, ability.gear));
    entry.set("canEquip", Value(player.stage >= spell.stage));
    entry.set("inscribed", Value(ability.gear.inscribed));
    return entry;
}


// 用途：追加行囊法器及其动作和拆解报价。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendEquipmentState(const Game& game, Value& state) {

    // ---- 拥有的法器 ----
    {
        json::Array arr;
        for (const auto& gr : game.player.arts.gear) {
            Value entry(json::Object{});
            entry.set("pricing", mfCraftPriceView(crafting::mfQuoteGearPrice(game, gr)));
            entry.set("id", str(gr.id));
            entry.set("name", str(gr.name));
            entry.set("slot", str(gr.slot));
            entry.set("grade", num(gr.grade));
            entry.set("bonus", num(gr.bonus));
            entry.set("price", num(gr.price));
            entry.set("element", str(gr.element)); entry.set("polarity", str(gr.polarity));
            entry.set("crafted", Value(gr.crafted)); entry.set("shape", str(artifactShape(gr)));
            {
                Value actions(json::Object{});
                constexpr std::pair<const char*, RouteId> actionRoutes[] = {
                    {"equip", RouteId::Equip}, {"unequip", RouteId::GearUnequip},
                    {"inscribe", RouteId::GearInscribe}, {"salvage", RouteId::GearSalvage}, {"sell", RouteId::Sell}
                };
                for (const auto& [action, route] : actionRoutes) {
                    actions.set(action, actionQuote(game, routePath(route), shop::gearActionQuote(game, action, gr.id)));
                }
                entry.set("actions", std::move(actions));
                entry.set("salvage", crafting::salvageQuoteJson(crafting::salvageQuote(game, gr.id)));
            }
            entry.set("inscribed", Value(gr.inscribed)); entry.set("passive", artifactPassiveView(game.player, gr));
            const ArtifactAbility ability = artifactAbility(gr);
            entry.set("shapeName", str(ability.shapeName)); entry.set("ability", artifactView(game.player, ability));
            auto it = game.player.arts.equipped.find(gr.slot);
            entry.set("equipped",
                  Value(it != game.player.arts.equipped.end() && it->second == gr.id));
            arr.push_back(std::move(entry));
        }
        state.set("gears", Value(std::move(arr)));
    }
}

// 用途：生成单件法器主动与被动效果预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const Gear& gear，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
Value equipment(const Player& player, const Gear& gear) {
    Value entry(json::Object{});
    const auto ability = artifactAbility(gear);
    entry.set("shapeName", str(ability.shapeName));
    entry.set("ability", artifactView(player, ability));
    entry.set("passive", artifactPassiveView(player, gear));
    entry.set("inscribed", Value(gear.inscribed));
    return entry;
}

} // namespace nz::api::view
