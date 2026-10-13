#include "core/sim.h"
#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/npc_learning.h"
#include "core/friendship.h"
#include "core/society.h"
#include "core/play.h"
#include "core/conflict.h"
#include "core/renown.h"
#include "core/economy.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::api::view {

// 用途：追加可见道友关系消息与交互预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendSocialState(const Game& game, Value& state) {

    // ---- 人情 ----
    {
        const Bonds& bonds = game.bonds;
        Value entry(json::Object{});
        entry.set("master", str(bonds.master));
        entry.set("partner", str(bonds.partner));
        entry.set("bondDays", num(bonds.bondDays));
        entry.set("lessons", num(bonds.lessons));
        entry.set("notoriety", num(bonds.notoriety));
        entry.set("stipendBalance", num(bonds.payroll.balance));
        {
            json::Array d;
            for (const auto& item : bonds.disciples) d.push_back(str(item));
            entry.set("disciples", Value(std::move(d)));
        }
        {
            Value st(json::Object{});
            for (const auto& [key, value] : bonds.stories) st.set(key, num(value));
            entry.set("stories", std::move(st));
        }
        state.set("bonds", std::move(entry));
    }
    {
        json::Array actions;
        for (const char* kind : {"lesson", "teach", "together", "part", "dismiss"}) {
            const auto info = bondActionInfo(game, kind);
            const auto reason = bondActionError(game, kind);
            Value action(json::Object{});
            action.set("id", str(info.id));
            action.set("name", str(info.name));
            action.set("desc", str(info.desc));
            action.set("days", num(info.days));
            action.set("cost", num(info.cost));
            action.set("enabled", Value(reason.empty()));
            action.set("reason", str(reason));
            actions.push_back(std::move(action));
        }
        state.set("bondActions", Value(std::move(actions)));
    }

    // ---- 附近的人（带好感度，供 NPC 面板直接用）----
    {
        json::Array arr;
        for (const auto* npc : play::nearbyPeople(game)) {
            Value entry(json::Object{});
            entry.set("id", str(npc->id));
            entry.set("name", str(npc->name));
            entry.set("gender", str(npc->gender));
            entry.set("age", num(npc->age));
            entry.set("stage", num(npc->stage));
            entry.set("realmName", str(realmName(npc->stage)));
            entry.set("sect", str(npc->sect));
            entry.set("sectName", npc->sect.empty() ? str("")
                            : str(findSect(npc->sect) ? findSect(npc->sect)->name : ""));
            entry.set("personality", str(npc->personality));
            entry.set("goal", str(npc->goal));
            entry.set("sword", num(npc->sword));
            entry.set("portrait", str(npcPortrait(*npc)));
            entry.set("dao", str(npc->dao));
            {
                const Arts loadout = artNpcLoadout(*npc);
                Value lo(json::Object{}); json::Array spells, manuals, nodes;
                for (const auto& id : loadout.spells) { const auto* spell = battleSpell(id); Value value(json::Object{}); value.set("id", str(id)); value.set("name", str(spell ? spell->name : id)); spells.push_back(std::move(value)); }
                for (const auto& [id, level] : loadout.manuals) { ManualInfo manual; findManualInfo(id, manual); Value value(json::Object{}); value.set("id", str(id)); value.set("name", str(manual.found ? manual.name : id)); value.set("level", num(level)); value.set("main", Value(id == loadout.main)); manuals.push_back(std::move(value)); }
                for (const auto& id : loadout.nodes) { const auto* node = findNode(id); Value value(json::Object{}); value.set("id", str(id)); value.set("name", str(node ? node->name : id)); nodes.push_back(std::move(value)); }
                lo.set("main", str(loadout.main)); lo.set("spells", Value(std::move(spells))); lo.set("manuals", Value(std::move(manuals))); lo.set("nodes", Value(std::move(nodes)));
                entry.set("loadout", std::move(lo));
            }
            {
                json::Array lessons;
                for (const auto& item : npcLessons(game, npc->id)) {
                    Value lesson(json::Object{}); lesson.set("kind", str(item.kind)); lesson.set("id", str(item.id)); lesson.set("name", str(item.name)); lesson.set("dao", str(item.dao)); lesson.set("stage", num(item.stage)); lesson.set("stageName", str(realmName(item.stage))); lesson.set("days", num(item.days)); lesson.set("creditCost", num(item.creditCost)); lesson.set("stoneCost", num(item.stoneCost)); lesson.set("affinityNeed", num(item.affinityNeed)); lesson.set("learned", Value(item.learned)); lesson.set("enabled", Value(item.enabled)); lesson.set("reason", str(item.reason)); lessons.push_back(std::move(lesson));
                }
                entry.set("learning", Value(std::move(lessons)));
            }
            entry.set("apt", num(npc->apt));
            entry.set("insight", num(npc->insight));
            entry.set("relation", num(relationOf(game, npc->id)));
            // 道友往来：交情与「是否打过招呼」
            {
                const int cr = friendship::creditOf(game, npc->id);
                entry.set("credit", num(cr));
                entry.set("creditLabel", str(friendship::creditLabel(cr)));
                auto bit = game.player.npcBonds.find(npc->id);
                entry.set("greeted",
                      Value(bit != game.player.npcBonds.end() && bit->second.greeted));
            }
            entry.set("relationLabel", str(play::relationLabel(relationOf(game, npc->id))));
            entry.set("isMaster", Value(game.bonds.master == npc->id));
            entry.set("isPartner", Value(game.bonds.partner == npc->id));
            entry.set("isDisciple",
                  Value(std::find(game.bonds.disciples.begin(),
                                  game.bonds.disciples.end(),
                                  npc->id) != game.bonds.disciples.end()));
            arr.push_back(std::move(entry));
        }
        state.set("nearby", Value(std::move(arr)));
    }

    // ---- 通讯录统计 ----
    {
        int known = 0, here = 0;
        for (const auto& npc : game.npcs) {
            if (!npc.alive) continue;
            if (npc.location == game.player.location) ++here;
            const bool isBond = (game.bonds.master == npc.id) ||
                                (game.bonds.partner == npc.id) ||
                                std::find(game.bonds.disciples.begin(),
                                          game.bonds.disciples.end(),
                                          npc.id) != game.bonds.disciples.end();
            if (relationOf(game, npc.id) != 0 || isBond) ++known;
        }
        Value entry(json::Object{});
        entry.set("known", num(known));
        entry.set("here", num(here));
        state.set("rosterStat", std::move(entry));
    }

    // ---- 坊市与洞府的额外信息 ----
    state.set("contribution", num(game.economy.contribution));
    // 身份与恶名：宗门页常显，缺了会显示成「—」和 0
    state.set("rank", str(socRank(game.player)));
    state.set("notoriety", num(game.bonds.notoriety));

    state.set("notorietyNote", str(conflict::notorietyNote(game)));
    // ---- 声望：档位 + 实际加成，让玩家看得见它有什么用 ----
    {
        const int rn = game.player.renown;
        Value entry(json::Object{});
        entry.set("value", num(rn));
        entry.set("tier", str(renown::tierOf(rn).name));
        entry.set("tierDesc", str(renown::tierOf(rn).desc));
        entry.set("describe", str(renown::describe(rn)));
        entry.set("toNext", num(renown::toNextTier(rn)));
        entry.set("progress", num(renown::tierProgress(rn)));
        entry.set("buyDiscount", num(renown::buyDiscount(rn)));
        entry.set("sellBonus", num(renown::sellBonus(rn)));
        entry.set("sellLimit", num(renown::sellLimit(rn)));
        entry.set("marketCashCap", num(ecoMarketBudget(game)));
        entry.set("marketTier", num(ecoMarketTier(game.player)));
        entry.set("marketBudgetRule", str(mfMarketBudgetRule()));
        entry.set("firstImpression", num(renown::firstImpression(rn)));
        entry.set("libraryDiscount", num(renown::libraryDiscount(rn)));
        entry.set("stipendBonus", num(renown::stipendBonus(rn)));
        entry.set("masterRequirement", num(renown::masterRequirement()));
        {
            json::Array arr;
            for (const auto& item : renown::tiers()) {
                Value to(json::Object{});
                to.set("id", str(item.id));
                to.set("name", str(item.name));
                to.set("desc", str(item.desc));
                to.set("at", num(item.at));
                to.set("reached", Value(rn >= item.at));
                arr.push_back(std::move(to));
            }
            entry.set("tiers", Value(std::move(arr)));
        }
        {
            json::Array arr;
            for (const auto& source : renown::sources(game)) {
                Value value(json::Object{});
                value.set("id", str(source.id)); value.set("name", str(source.name)); value.set("gainText", str(source.gainText));
                json::Array conditions; for (const auto& condition : source.conditions) conditions.push_back(str(condition));
                value.set("conditions", Value(std::move(conditions))); value.set("limit", str(source.limit));
                value.set("enabled", Value(source.enabled)); value.set("reason", str(source.reason));
                Value entry(json::Object{});
                entry.set("tab", str(source.entry.tab)); entry.set("sub", str(source.entry.sub));
                entry.set("location", str(source.entry.location)); entry.set("label", str(source.entry.label));
                value.set("entry", std::move(entry)); arr.push_back(std::move(value));
            }
            entry.set("sources", Value(std::move(arr)));
        }
        state.set("renownInfo", std::move(entry));
    }
}

} // namespace nz::api::view
