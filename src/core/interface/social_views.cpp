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
#include "core/shop.h"
#include "core/content/npc_combat_profiles.h"
#include "core/content/advanced_builds.h"
#include "core/content/game_ids.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::api::view {
namespace {
// 用途：展示实际装备构筑标签；参与仙友筛选；按合法模板或实际大道/核心神通归类，返回ID/中文名，不改NPC装备或成长。
void mfAppendNpcBuild(const Npc& Person, const Arts& Loadout, Value& Entry) {
    Entry.set("isChampion", content::isChampion(Person));
    if (const auto* Profile = content::findNpcCombatProfile(Person)) {
        Entry.set("buildId", Profile->id);
        Entry.set("buildName", Profile->name);
        Entry.set("buildTier", "advanced");
        Entry.set("advancedBuildId", Profile->BuildId);
        Value Transmission(json::Object{});
        Transmission.set("buildId", Profile->BuildId);
        Transmission.set("name", Profile->name);
        Transmission.set("note", "完整构筑可逐项请教；仍需满足境界、好感、交情、学费和当面授业条件，功法从第一层学起。");
        json::Array Manuals, Spells, Components;
        for (const auto& ManualId : Profile->manuals) {
            Manuals.push_back(str(ManualId));
            ManualInfo Definition;
            if (!findManualInfo(ManualId, Definition)) continue;
            Value Component(json::Object{});
            Component.set("kind", "manual"); Component.set("id", ManualId);
            Component.set("name", Definition.name); Component.set("stage", Definition.stage);
            Component.set("stageName", realmName(Definition.stage));
            Components.push_back(std::move(Component));
        }
        for (const auto& SpellId : Profile->spells) {
            Spells.push_back(str(SpellId));
            const auto* Definition = battleSpell(SpellId);
            if (!Definition) continue;
            Value Component(json::Object{});
            Component.set("kind", "spell"); Component.set("id", SpellId);
            Component.set("name", Definition->name); Component.set("stage", Definition->stage);
            Component.set("stageName", realmName(Definition->stage));
            Components.push_back(std::move(Component));
        }
        Transmission.set("manuals", Value(std::move(Manuals)));
        Transmission.set("spells", Value(std::move(Spells)));
        Transmission.set("components", Value(std::move(Components)));
        Entry.set("transmission", std::move(Transmission));
        return;
    }
    Entry.set("buildTier", "initial");
    const auto Focus = content::parseDaoId(Loadout.focus);
    for (const auto& Build : artBuilds()) {
        const bool HasCoreSpell = std::any_of(Build.spells.begin(), Build.spells.end(), [&](const auto& SpellId) {
            return std::find(Loadout.spells.begin(), Loadout.spells.end(), SpellId) != Loadout.spells.end();
        });
        if (Focus && content::parseDaoId(Build.dao) == Focus && HasCoreSpell) {
            Entry.set("buildId", Build.id);
            Entry.set("buildName", Build.name);
            return;
        }
    }
    Entry.set("buildId", Focus ? "dao-" + string(content::stableId(*Focus)) : "basic");
    Entry.set("buildName", Focus ? string(content::legacyDaoKey(*Focus)) + "道传承" : "基础传承");
}
}

// 用途：追加可见道友关系消息与交互预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendSocialState(const Game& game, Value& state) {
    mfAppendSocialState(game, state, shop::mfMarketSaleContext(game));
}

void mfAppendSocialState(const Game& GameState, Value& State, const shop::MarketSaleContext& MarketContext) {

    // 全州传承指引是常态目录，不要求兽潮活动或先认识老师；位置只读，动作仍走真实赶路和逐项请教。
    json::Array Teachers;
    for (const auto& Person : GameState.npcs) {
        const auto* Profile = content::findNpcCombatProfile(Person);
        if (!Profile || !Person.alive || Person.health <= 0 || Person.age >= Person.lifespan) continue;
        Value Teacher(json::Object{});
        Teacher.set("id", Person.id); Teacher.set("name", Person.name);
        Teacher.set("profileId", Profile->id); Teacher.set("buildId", Profile->BuildId);
        Teacher.set("buildName", Profile->name); Teacher.set("sect", Profile->sect);
        Teacher.set("stage", num(Person.stage)); Teacher.set("realmName", realmName(Person.stage));
        Teacher.set("location", Person.location);
        Teacher.set("locationName", findLoc(Person.location) ? findLoc(Person.location)->name : Person.location);
        Teacher.set("here", Person.location == GameState.player.location);
        Teachers.push_back(std::move(Teacher));
    }
    State.set("championTeachers", Value(std::move(Teachers)));
    json::Array AdvancedBuilds;
    for (const auto& Build : content::mfAdvancedBuilds()) {
        Value Entry(json::Object{});
        Entry.set("id", Build.id); Entry.set("name", Build.name); Entry.set("dao", Build.dao);
        Entry.set("desc", Build.desc); Entry.set("tier", "advanced");
        json::Array Manuals, Spells, Nodes;
        for (const auto& ManualId : Build.manuals) Manuals.push_back(str(ManualId));
        for (const auto& SpellId : Build.spells) Spells.push_back(str(SpellId));
        for (const auto& NodeId : Build.nodes) Nodes.push_back(str(NodeId));
        Entry.set("manuals", Value(std::move(Manuals))); Entry.set("spells", Value(std::move(Spells)));
        Entry.set("nodes", Value(std::move(Nodes)));
        AdvancedBuilds.push_back(std::move(Entry));
    }
    State.set("advancedBuilds", Value(std::move(AdvancedBuilds)));

    // ---- 人情 ----
    {
        const Bonds& bonds = GameState.bonds;
        Value entry(json::Object{});
        entry.set("master", str(bonds.master));
        entry.set("partner", str(bonds.partner));
        entry.set("bondDays", num(bonds.bondDays));
        entry.set("lessons", num(bonds.lessons));
        entry.set("notoriety", num(bonds.notoriety));
        entry.set("stipendBalance", num(bonds.payroll.balance));
        {
            const auto Quote = mfQuoteStipend(GameState);
            Value Action(json::Object{});
            Action.set("enabled", Quote.Enabled);
            Action.set("reason", Quote.Reason);
            Action.set("paid", Quote.Paid);
            Action.set("owed", Quote.Owed);
            entry.set("stipendAction", std::move(Action));
        }
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
        State.set("bonds", std::move(entry));
    }
    {
        json::Array actions;
        for (const char* kind : {"lesson", "teach", "together", "part", "dismiss"}) {
            const auto info = bondActionInfo(GameState, kind);
            const auto reason = bondActionError(GameState, kind);
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
        State.set("bondActions", Value(std::move(actions)));
    }

    // ---- 附近的人（带好感度，供 NPC 面板直接用）----
    {
        json::Array arr;
        for (const auto* npc : play::nearbyPeople(GameState)) {
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
                mfAppendNpcBuild(*npc, loadout, entry);
                Value lo(json::Object{}); json::Array spells, manuals, nodes;
                for (const auto& id : loadout.spells) { const auto* spell = battleSpell(id); Value value(json::Object{}); value.set("id", str(id)); value.set("name", str(spell ? spell->name : id)); spells.push_back(std::move(value)); }
                for (const auto& [id, level] : loadout.manuals) { ManualInfo manual; findManualInfo(id, manual); Value value(json::Object{}); value.set("id", str(id)); value.set("name", str(manual.found ? manual.name : id)); value.set("level", num(level)); value.set("main", Value(id == loadout.main)); manuals.push_back(std::move(value)); }
                for (const auto& id : loadout.nodes) { const auto* node = findNode(id); Value value(json::Object{}); value.set("id", str(id)); value.set("name", str(node ? node->name : id)); nodes.push_back(std::move(value)); }
                lo.set("main", str(loadout.main)); lo.set("spells", Value(std::move(spells))); lo.set("manuals", Value(std::move(manuals))); lo.set("nodes", Value(std::move(nodes)));
                entry.set("loadout", std::move(lo));
            }
            {
                json::Array lessons;
                for (const auto& item : npcLessons(GameState, npc->id)) {
                    Value lesson(json::Object{}); lesson.set("kind", str(item.kind)); lesson.set("id", str(item.id)); lesson.set("name", str(item.name)); lesson.set("dao", str(item.dao)); lesson.set("stage", num(item.stage)); lesson.set("stageName", str(realmName(item.stage))); lesson.set("days", num(item.days)); lesson.set("creditCost", num(item.creditCost)); lesson.set("stoneCost", num(item.stoneCost)); lesson.set("affinityNeed", num(item.affinityNeed)); lesson.set("learned", Value(item.learned)); lesson.set("enabled", Value(item.enabled)); lesson.set("reason", str(item.reason)); lessons.push_back(std::move(lesson));
                }
                entry.set("learning", Value(std::move(lessons)));
            }
            entry.set("apt", num(npc->apt));
            entry.set("insight", num(npc->insight));
            entry.set("relation", num(relationOf(GameState, npc->id)));
            // 道友往来：交情与「是否打过招呼」
            {
                const int cr = friendship::creditOf(GameState, npc->id);
                entry.set("credit", num(cr));
                entry.set("creditLabel", str(friendship::creditLabel(cr)));
                auto bit = GameState.player.npcBonds.find(npc->id);
                entry.set("greeted",
                      Value(bit != GameState.player.npcBonds.end() && bit->second.greeted));
            }
            entry.set("relationLabel", str(play::relationLabel(relationOf(GameState, npc->id))));
            entry.set("isMaster", Value(GameState.bonds.master == npc->id));
            entry.set("isPartner", Value(GameState.bonds.partner == npc->id));
            entry.set("isDisciple",
                  Value(std::find(GameState.bonds.disciples.begin(),
                                  GameState.bonds.disciples.end(),
                                  npc->id) != GameState.bonds.disciples.end()));
            arr.push_back(std::move(entry));
        }
        State.set("nearby", Value(std::move(arr)));
    }

    // ---- 通讯录统计 ----
    {
        int known = 0, here = 0;
        for (const auto& npc : GameState.npcs) {
            if (!npc.alive) continue;
            if (npc.location == GameState.player.location) ++here;
            const bool isBond = (GameState.bonds.master == npc.id) ||
                                (GameState.bonds.partner == npc.id) ||
                                std::find(GameState.bonds.disciples.begin(),
                                          GameState.bonds.disciples.end(),
                                          npc.id) != GameState.bonds.disciples.end();
            if (relationOf(GameState, npc.id) != 0 || isBond) ++known;
        }
        Value entry(json::Object{});
        entry.set("known", num(known));
        entry.set("here", num(here));
        State.set("rosterStat", std::move(entry));
    }

    // ---- 坊市与洞府的额外信息 ----
    State.set("contribution", num(GameState.economy.contribution));
    // 身份与恶名：宗门页常显，缺了会显示成「—」和 0
    State.set("rank", str(socRank(GameState.player)));
    State.set("notoriety", num(GameState.bonds.notoriety));

    State.set("notorietyNote", str(conflict::notorietyNote(GameState)));
    // ---- 声望：档位 + 实际加成，让玩家看得见它有什么用 ----
    {
        const int rn = GameState.player.renown;
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
        entry.set("saleCountUnlimited", true);
        entry.set("marketCashCap", num(MarketContext.NextMonthBudget));
        entry.set("marketTier", num(ecoMarketTier(GameState.player)));
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
            for (const auto& source : renown::sources(GameState)) {
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
        State.set("renownInfo", std::move(entry));
    }
}

} // namespace nz::api::view
