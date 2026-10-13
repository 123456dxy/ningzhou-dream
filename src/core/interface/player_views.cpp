#include "core/npc_learning.h"
#include "core/character_build.h"
#include "core/society.h"
#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/cultivate.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/talents.h"
#include "core/sim.h"
#include "core/interface/view_policy.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::api::view {

// 用途：追加角色基础属性进阶门槛与丹毒状态。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendPlayerState(const Game& game, Value& state) {

    // ---- 玩家 ----
    {
        const Player& player = game.player;
        Value entry(json::Object{});
        entry.set("name", str(player.name));
        entry.set("gender", str(player.gender));
        entry.set("age", num(player.age));
        // 已选天赋：建号后要能回头看自己选了什么
        {
            json::Array arr;
            for (const auto& tid : player.traits) {
                const TalentDef* td = findTalent(tid);
                Value to(json::Object{});
                to.set("id", str(tid));
                to.set("name", str(td ? td->name : tid));
                to.set("tag", str(td ? td->tag : ""));
                to.set("cost", num(td ? td->cost : 0));
                to.set("desc", str(td ? td->desc : ""));
                arr.push_back(std::move(to));
            }
            entry.set("talents", Value(std::move(arr)));
        }
        {
            json::Array arr;
            for (const auto& tid : player.traits) arr.push_back(str(tid));
            entry.set("traits", Value(std::move(arr)));
        }
        entry.set("apt", num(player.apt));
        entry.set("luck", num(player.luck));
        entry.set("insight", num(player.insight));
        entry.set("sword", num(player.sword));
        entry.set("escapeSpeed", num(artEscapeSpeed(player)));
        entry.set("portraitId", str(player.portraitId.empty() ? defaultPlayerPortraitId(player.gender) : player.portraitId));
        entry.set("portrait", str(playerPortrait(player)));
        entry.set("alchemy", num(player.alchemy));
        entry.set("body", num(player.body));
        // 体魄带来的气血加成（带软上限，界面上要说明为什么不再线性）
        {
            Value bodyDetails(json::Object{});
            bodyDetails.set("points", num(player.body));
            bodyDetails.set("bonus", num(cultivate::bodyHpBonus(player.body)));
            bodyDetails.set("softCap", num(cultivate::bodySoftCap()));
            bodyDetails.set("cap", num(cultivate::bodyHpCap()));
            bodyDetails.set("atSoftCap", Value(player.body >= cultivate::bodySoftCap()));
            entry.set("bodyHp", std::move(bodyDetails));
        }

        entry.set("health", num(player.health));
        entry.set("healthMax", num(100));
        entry.set("healthUnit", str("%"));
        {
            const Battle ref = battleReferenceState(player);
            Value combat(json::Object{});
            combat.set("reference", Value(true));
            combat.set("context", str("按当前健康度、境界、天赋、功法、悟道、装备与已有丹药准备计算的普通斗法开局参考；不含敌方行动或秘境额外效果。战斗中请以实时斗法面板为准。"));
            combat.set("hp", num(ref.hp)); combat.set("hpMax", num(ref.max));
            combat.set("mana", num(ref.mana)); combat.set("manaRegen", num(battleEnergy(player)));
            combat.set("manaCap", num(battleManaCap(player))); combat.set("shield", num(ref.shield));
            combat.set("swordQi", num(ref.swordQi)); combat.set("reduction", num(battleReduction(player, ref)));
            combat.set("speed", num(battleSpeed(player) + ref.speed)); combat.set("damageBuff", num(ref.buff));
            entry.set("combatReference", std::move(combat));
        }
        entry.set("lifespan", num(player.lifespan));
        entry.set("lifeBonus", num(player.lifeBonus));
        entry.set("stage", num(player.stage));
        entry.set("highestStage", num(player.highestStage));
        entry.set("realmName", str(realmName(player.stage)));
        entry.set("xp", num(player.xp));
        entry.set("xpNeed", num(realmOf(player.stage).need));
        entry.set("stones", num(player.stones));
        entry.set("herbs", num(player.herbs));
        entry.set("pills", num(player.pills));
        entry.set("cores", num(player.cores));
        entry.set("renown", num(player.renown));
        entry.set("sect", str(player.sect));
        entry.set("sectName",
               str(player.sect.empty() ? "散修"
                                  : (findSect(player.sect) ? findSect(player.sect)->name
                                                      : player.sect)));
        // 身份也是玩家属性，放进 player 里；
        // 之前只有顶层 rank，任务页读 S.player.rank 会显示成「—」。
        entry.set("rank", str(socRank(player)));
        entry.set("location", str(player.location));
        entry.set("locationName",
               str(findLoc(player.location) ? findLoc(player.location)->name : player.location));
        // 洞府与配给
        entry.set("cave", str(player.cave));
        entry.set("caveName",
               str(player.cave.empty()
                       ? "尚未安府"
                       : (findLoc(player.cave) ? findLoc(player.cave)->name : player.cave)));
        // 档位本身在下面的 supply 子对象里（带名称/月耗/说明），
        // 这里不再重复下发裸数字，免得两处不一致。
        entry.set("atCave", Value(cultivate::atCave(player)));
        entry.set("qiActive", Value(cultivate::qiActive(player)));
        {
            Value c(json::Object{});
            c.set("stoneLevel", num(player.stoneSupply));
            c.set("qiLevel", num(player.qiSupply));
            c.set("stoneCost", num(cultivate::supplyCost(player, "stone", player.stoneSupply)));
            c.set("qiCost", num(cultivate::supplyCost(player, "qi", player.qiSupply)));
            c.set("stoneName", str(cultivate::supplyName("stone", player.stoneSupply)));
            c.set("qiName", str(cultivate::supplyName("qi", player.qiSupply)));
            c.set("stoneDesc", str(cultivate::supplyDesc("stone", player.stoneSupply)));
            c.set("qiDesc", str(cultivate::supplyDesc("qi", player.qiSupply)));
            c.set("maxLevel", num(cultivate::maxSupplyLevel()));
            c.set("maxQiLevel", num(cultivate::unlockedSupplyLevel(player)));
            c.set("moveCost", num(40 + (player.stage / 3) * 30));
            entry.set("supply", std::move(c));
        }
        // 修炼速度拆解：让玩家看清速度是怎么来的
        {
            const cultivate::Breakdown cultivation = cultivate::breakdown(player);
            Value r(json::Object{});
            r.set("manual", num(cultivation.manual));
            r.set("apt", num(cultivation.apt));
            r.set("insight", num(cultivation.insight));
            r.set("realm", num(cultivation.realm));
            r.set("legacy", num(cultivation.legacy));
            r.set("stone", num(cultivation.stone));
            r.set("qi", num(cultivation.qi));
            r.set("cave", num(cultivation.cave));
            r.set("talentMul", num(talentCultivationMul(player)));
            r.set("total", num(cultivation.total));
            r.set("base", num(cultivate::baseRate(player)));
            r.set("note", str(cultivation.caveNote));
            r.set("aptMul", num(cultivate::aptMul(player)));
            r.set("insightMul", num(cultivate::insightMul(player)));
            entry.set("cultivation", std::move(r));
        }
        state.set("player", std::move(entry));
    }
    {
        json::Array arr;
        for (const auto& planDefinition : PLANS()) {
            Value entry(json::Object{});
            entry.set("id", str(planDefinition.id));
            entry.set("name", str(planDefinition.name));
            entry.set("icon", str(planDefinition.icon));
            entry.set("desc", str(planDefinition.desc));
            const Player& player = game.player;
            const std::string id = planDefinition.id;
            std::string preview;
            if (id == "cultivate") {
                const double mult = (player.sect.empty() ? 1.0 : 1.16) *
                    (player.health < 50 ? 0.6 : 1.0) *
                    (player.sect.empty() && hasTalent(player, "solitary") ? 1.08 : 1.0);
                const int bill = jsRound(cultivationCost(player) / 12.0) +
                    cultivate::supplyCost(player, "stone", player.stoneSupply) +
                    (cultivate::qiActive(player) ? cultivate::supplyCost(player, "qi", player.qiSupply) : 0);
                preview = "30 天：约修为 +" + std::to_string(jsRound(cultivate::rate(player) * mult / 12.0)) +
                    " · 灵石投入 " + std::to_string(bill) + "（不足时收益降低）";
            } else if (id == "work") {
                const double mul = game.world.market * (hasTalent(player, "merchant") ? 1.18 : 1.0);
                preview = "30 天：灵石约 +" + std::to_string(jsRound(25 * mul)) +
                    "～" + std::to_string(jsRound(37 * mul));
            } else if (id == "gather") {
                const LocationDef* here = findLoc(player.location);
                const int bonus = (here && std::string(here->type) == "wild" ? 2 : 0) +
                    (player.sect == "zhushan" ? 2 : 0) + (artHas(player.arts, "丹", 1) ? 1 : 0);
                preview = "30 天：药材约 +" + std::to_string(std::min(game.world.herbs, 2 + bonus)) +
                    "～" + std::to_string(std::min(game.world.herbs, 4 + bonus)) + " · 险地可能受伤";
            } else if (id == "rest") {
                preview = "30 天：气血最多 +" + std::to_string(std::min(100 - player.health,
                    20 + (hasTalent(player, "body") ? 8 : 0))) + " · 积累少量修为";
            } else if (id == "explore") {
                preview = "30 天：灵石约 +8～25 · 积累见闻与修为 · 可能遇事或斗法";
            }
            entry.set("preview", str(preview));
            arr.push_back(std::move(entry));
        }
        state.set("plans", Value(std::move(arr)));
    }

    // ---- 突破信息 ----
    {
        const BreakthroughInfo breakthrough = breakthroughInfo(game);
        Value entry(json::Object{});
        entry.set("next", str(breakthrough.next));
        entry.set("chance", num(breakthrough.chance));
        entry.set("cost", num(breakthrough.cost));
        entry.set("ready", Value(breakthrough.ready));
        entry.set("major", Value(breakthrough.major));
        const string reason = breakthroughReason(game, breakthrough);
        entry.set("enabled", Value(reason.empty())); entry.set("reason", str(reason));
        entry.set("days", num(breakthrough.major ? 90 : 30)); entry.set("failureLoss", num(breakthrough.failureLoss));
        entry.set("pillsConsumed", num(breakthrough.major && game.player.pills > 0 ? 1 : 0));
        entry.set("coresConsumed", num(breakthrough.major && game.player.cores > 0 ? 1 : 0));
        entry.set("breakBuffConsumed", Value(breakthrough.major && !game.player.arts.breakBuff.empty()));
        state.set("breakthrough", std::move(entry));
    }
}

} // namespace nz::api::view
