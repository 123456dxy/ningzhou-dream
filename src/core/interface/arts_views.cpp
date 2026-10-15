#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/cultivate.h"
#include "core/economy.h"
#include "core/shop.h"
#include "core/talents.h"
#include "core/content/text_table.h"
#include "core/legacy.h"
#include "data/reference_tables.h"
#include <algorithm>
#include <stdexcept>

namespace nz::api::view {

namespace {
// 用途：将权威成就进度键映射为集中维护的明确条件文案。
// 流程：成就 JSON 展示；目标直接来自静态成就定义。
// 返回值：拥有的中文条件文本；未知键明确抛错，不伪造规则。
std::string mfAchievementCondition(const data::AchievementDef& Achievement) {
    using content::TextId;
    const TextId ConditionId = [&]() {
        if (Achievement.key == "brew") return TextId::AchievementBrewCondition;
        if (Achievement.key == "forge") return TextId::AchievementForgeCondition;
        if (Achievement.key == "wins") return TextId::AchievementWinsCondition;
        if (Achievement.key == "nodes") return TextId::AchievementNodesCondition;
        if (Achievement.key == "manuals") return TextId::AchievementManualsCondition;
        if (Achievement.key == "quests") return TextId::AchievementQuestsCondition;
        if (Achievement.key == "wealth") return TextId::AchievementWealthCondition;
        throw std::invalid_argument("Unknown achievement progress key");
    }();
    const std::string Target = std::to_string(Achievement.target);
    return content::formatText(ConditionId, {{"Target", Target}});
}
} // namespace

// 用途：组合成就的真实进度、条件与跨世奖励说明。
// 流程：成就只读查询；奖励点数及领取状态复用传承里程碑查询。
// 使用方式：借用稳定的人生与账号档案快照，不保存引用。
// 返回值：拥有数据的 JSON；未知定义抛错，无时间、RNG、状态或文件副作用。
Value mfAchievementsView(const Game& GameState, const legacy::Archive& ArchiveState) {
    const auto Milestones = legacy::milestonesOf(GameState, ArchiveState);
    Value Result(json::Object{});
    Result.set("ok", Value(true));
    const std::string PointLimit = std::to_string(MAX_POINTS - BASE_POINTS);
    Result.set("rewardNote", str(content::formatText(content::TextId::AchievementRewardNote,
        {{"PointLimit", PointLimit}})));
    json::Array Achievements;
    for (const auto& Achievement : data::AchievementDef_TABLE()) {
        const auto Milestone = std::find_if(Milestones.begin(), Milestones.end(),
            [&](const legacy::Milestone& Entry) { return Entry.key == Achievement.id; });
        if (Milestone == Milestones.end()) throw std::invalid_argument("Unknown achievement milestone");
        const int Progress = artProgress(GameState.player, Achievement.key);
        const std::string RewardPoints = std::to_string(Milestone->points);
        Value Entry(json::Object{});
        Entry.set("id", str(Achievement.id));
        Entry.set("name", str(Achievement.name));
        Entry.set("key", str(Achievement.key));
        Entry.set("target", num(Achievement.target));
        Entry.set("progress", num(Progress));
        Entry.set("points", num(Achievement.points));
        Entry.set("done", Value(Progress >= Achievement.target));
        Entry.set("condition", str(mfAchievementCondition(Achievement)));
        Entry.set("rewardClaimed", Value(Milestone->alreadyHad));
        Entry.set("reward", str(content::formatText(Milestone->alreadyHad
            ? content::TextId::AchievementClaimedReward : content::TextId::AchievementReward,
            {{"Points", RewardPoints}})));
        Achievements.push_back(std::move(Entry));
    }
    Result.set("achievements", Value(std::move(Achievements)));
    return Result;
}

// 用途：追加功法神通悟道目录与实际状态。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendArtsState(const Game& game, Value& state) {


    // ---- 功法与神通 ----


    {
        const Arts& arts = game.player.arts;
        Value entry(json::Object{});
        entry.set("focus", str(arts.focus));
        entry.set("main", str(arts.main));
        entry.set("points", num(arts.points));
        entry.set("poison", num(arts.poison));
        entry.set("heart", num(arts.heart));
        entry.set("mind", num(arts.mind)); entry.set("speed", num(arts.speed));
        entry.set("hpBonus", num(arts.hpBonus)); entry.set("combatBuff", num(arts.combatBuff));
        entry.set("repel", num(arts.repel));
        entry.set("repelDescription", str("每逢岁末效力递减1；有效时降低游历受伤风险。"));
        entry.set("furnace", str(arts.furnace));
        entry.set("durability", num(arts.durability));
        {
            Value e(json::Object{});
            for (const auto& [key, value] : arts.exp) e.set(key, num(value));
            entry.set("exp", std::move(e));
        }
        {
            json::Array arr;
            for (const auto& npc : arts.nodes) arr.push_back(str(npc));
            entry.set("nodes", Value(std::move(arr)));
        }
        {
            json::Array arr;
            for (const auto& item : arts.spells) arr.push_back(str(item));
            entry.set("spells", Value(std::move(arr)));
        }
        entry.set("spellSlots", num(battleSpellSlots(game.player)));
        {
            json::Array known, books;
            for (const auto& id : arts.knownSpells) known.push_back(str(id));
            vector<string> ownedBooks = arts.bookList;
            for (const auto& id : game.economy.books)
                if (std::find(ownedBooks.begin(), ownedBooks.end(), id) == ownedBooks.end()) ownedBooks.push_back(id);
            for (const auto& id : ownedBooks) books.push_back(str(id));
            entry.set("knownSpells", Value(std::move(known))); entry.set("bookList", Value(std::move(books)));
        }
        entry.set("manaRegen", num(battleEnergy(game.player)));
        {
            json::Array mandatory;
            for (const char* id : {"strike", "defend"}) {
                const SpellDef* spell = battleSpell(id);
                Value item(json::Object{});
                item.set("id", str(id));
                item.set("name", str(spell ? spell->name : id));
                item.set("copies", num(5));
                item.set("mandatory", Value(true));
                item.set("description", str(spell ? battleSpellDescription(*spell) : ""));
                if (spell) item.set("preview", spellPreview(game.player, *spell));
                mandatory.push_back(std::move(item));
            }
            entry.set("mandatorySpells", Value(std::move(mandatory)));
        }
        // 携带神通的完整信息：光给 id，玩家看不出这是什么东西
        {
            json::Array arr;
            for (const auto& sid : arts.spells) {
                const SpellDef* spell = battleSpell(sid);
                Value so(json::Object{});
                so.set("id", str(sid));
                so.set("name", str(spell ? spell->name : sid));
                so.set("dao", str(spell ? spell->dao : ""));
                so.set("kind", str(spell ? spell->kind : ""));
                so.set("cost", num(spell ? spell->cost : 0));
                so.set("actualCost", num(spell ? battleCost(game.player, *spell) : 0));
                so.set("cooldown", num(spell ? spell->cooldown : 0));
                so.set("stage", num(spell ? spell->stage : 0));
                so.set("stageName", str(spell ? realmName(spell->stage) : ""));
                so.set("description", str(spell ? battleSpellDescription(*spell) : ""));
                if (spell) so.set("preview", spellPreview(game.player, *spell));
                arr.push_back(std::move(so));
            }
            entry.set("spellList", Value(std::move(arr)));
        }
        {
            Value modifiers(json::Object{});
            for (const auto& [key, value] : arts.manuals) modifiers.set(key, num(value));
            entry.set("manuals", std::move(modifiers));
        }
        // 已修功法的完整信息：名称、所属大道、层数、效果
        {
            json::Array arr;
            for (const auto& [mid, lv] : arts.manuals) {
                ManualInfo mv;
                if (!findManualInfo(mid, mv)) continue;
                Value mo(json::Object{});
                mo.set("id", str(mid));
                mo.set("name", str(mv.name));
                mo.set("dao", str(mv.dao));
                mo.set("level", num(lv));
                mo.set("effect", str(mv.effect));
                mo.set("stage", num(mv.stage));
                mo.set("price", num(mv.price));
                mo.set("isMain", Value(arts.main == mid));
                mo.set("isAux",
                       Value(std::find(arts.aux.begin(), arts.aux.end(), mid) !=
                             arts.aux.end()));
                const auto study = ecoActionInfo(game, "learn", mid);
                mo.set("nextDays", num(study.days));
                mo.set("nextCost", num(study.cost));
                mo.set("maxed", Value(lv >= 5));
                arr.push_back(std::move(mo));
            }
            entry.set("manualList", Value(std::move(arr)));
        }
        {
            Value modifiers(json::Object{});
            for (const auto& [key, value] : arts.herbs) modifiers.set(key, num(value));
            entry.set("herbs", std::move(modifiers));
        }
        {
            Value modifiers(json::Object{});
            for (const auto& [key, value] : arts.materials) modifiers.set(key, num(value));
            entry.set("materials", std::move(modifiers));
        }
        // 丹药与服用记录：前端「丹药行囊」要用，
        // 之前没下发，Object.entries(undefined) 会让整个坊市页崩掉。
        {
            Value modifiers(json::Object{});
            for (const auto& [key, value] : arts.potions) modifiers.set(key, num(value));
            entry.set("potions", std::move(modifiers));
        }
        {
            Value modifiers(json::Object{});
            for (const auto& [key, value] : arts.used) modifiers.set(key, num(value));
            entry.set("used", std::move(modifiers));
        }
        {
            Value modifiers(json::Object{});
            for (const auto& [key, value] : arts.combatPrep) modifiers.set(key, num(value));
            entry.set("combatPrep", std::move(modifiers));
        }
        entry.set("aptPills", num(arts.aptPills));
        entry.set("breakBuff", str(arts.breakBuff));

        // 加成汇总，前端可直接显示
        {
            const ArtMods& modifiers = talentMods(game.player);
            Value mo(json::Object{});
            mo.set("cult", num(modifiers.cult));
            mo.set("attack", num(modifiers.attack));
            mo.set("hp", num(modifiers.hp));
            mo.set("mana", num(modifiers.mana));
            mo.set("sword", num(modifiers.sword));
            mo.set("heal", num(modifiers.heal));
            mo.set("shield", num(modifiers.shield));
            mo.set("brew", num(modifiers.brew));
            mo.set("forge", num(modifiers.forge));
            mo.set("reduction", num(modifiers.reduction));
            mo.set("charge", num(modifiers.charge));
            mo.set("water", num(modifiers.water));
            mo.set("poison", num(modifiers.poison));
            mo.set("soul", num(modifiers.soul));
            mo.set("drain", num(modifiers.drain));
            mo.set("fire", num(modifiers.fire));
            mo.set("steal", num(modifiers.steal));
            mo.set("roundShield", num(modifiers.roundShield));
            mo.set("openingShield", num(modifiers.openingShield));
            mo.set("openingMana", num(modifiers.openingMana));
            mo.set("openingQi", num(modifiers.openingQi));
            mo.set("quietHeal", num(modifiers.quietHeal));
            mo.set("combo", num(modifiers.combo));
            mo.set("burnBonus", num(modifiers.burnBonus));
            mo.set("thirdHeal", num(modifiers.thirdHeal));
            mo.set("qiGain", num(modifiers.qiGain));
            mo.set("discardShield", num(modifiers.discardShield));
            mo.set("discardPower", num(modifiers.discardPower));
            mo.set("waterMana", num(modifiers.waterMana));
            mo.set("waterKeep", num(modifiers.waterKeep));
            mo.set("burnStack", num(modifiers.burnStack));
            mo.set("burnExplosion", num(modifiers.burnExplosion));
            mo.set("poisonHeal", num(modifiers.poisonHeal));
            mo.set("poisonGrowth", num(modifiers.poisonGrowth));
            mo.set("shieldCharge", num(modifiers.shieldCharge));
            mo.set("shieldStrike", num(modifiers.shieldStrike));
            mo.set("swordCombo", num(modifiers.swordCombo));
            mo.set("swordMana", num(modifiers.swordMana));
            mo.set("drawCost", num(modifiers.drawCost));
            mo.set("drawBonus", num(modifiers.drawBonus));
            mo.set("carryBonus", num(modifiers.carryBonus));
            mo.set("quietMana", num(modifiers.quietMana));
            mo.set("comboMana", num(modifiers.comboMana));
            mo.set("comboDraw", num(modifiers.comboDraw));
            mo.set("bodyCounter", num(modifiers.bodyCounter));
            mo.set("poisonCleanse", num(modifiers.poisonCleanse));
            Value elements(json::Object{});
            for (int i = 0; i < 12; ++i) elements.set(ART_DAOS[i], num(modifiers.elements[i]));
            mo.set("elements", std::move(elements));
            mo.set("brewYield", num(modifiers.brewYield));
            mo.set("forgeSpeed", num(modifiers.forgeSpeed));
            mo.set("craftedSale", num(modifiers.craftedSale));
            mo.set("escapeSpeed", num(modifiers.escapeSpeed));
            entry.set("mods", std::move(mo));
        }
        state.set("arts", std::move(entry));
    }
    {
        json::Array builds;
        for (const auto& build : artBuilds()) {
            Value item(json::Object{});
            item.set("id", str(build.id));
            item.set("name", str(build.name));
            item.set("dao", str(build.dao));
            item.set("desc", str(build.desc));
            auto ids = [](const std::vector<std::string>& list) {
                json::Array arr;
                for (const auto& id : list) arr.push_back(str(id));
                return Value(std::move(arr));
            };
            item.set("manuals", ids(build.manuals));
            item.set("spells", ids(build.spells));
            item.set("nodes", ids(build.nodes));
            builds.push_back(std::move(item));
        }
        state.set("artBuilds", Value(std::move(builds)));
    }
    {
        json::Array arr;
        for (const auto& [ManualId, Info] : mfManualCatalog()) {
            static_cast<void>(ManualId);
            Value item(json::Object{});
            item.set("id", str(Info.id)); item.set("name", str(Info.name)); item.set("dao", str(Info.dao));
            item.set("stage", num(Info.stage)); item.set("stageName", str(realmName(Info.stage)));
            item.set("effect", str(Info.effect));
            arr.push_back(std::move(item));
        }
        state.set("manualCatalog", Value(std::move(arr)));
    }
    {
        json::Array arr;
        for (const auto& ms : shop::availableManuals(game)) {
            Value entry(json::Object{});
            entry.set("id", str(ms.id));
            entry.set("name", str(ms.name));
            entry.set("city", str(ms.city));
            entry.set("sect", str(ms.sect));
            entry.set("price", num(ms.price));
            entry.set("points", num(ms.points));
            entry.set("stage", num(ms.stage));
            entry.set("how", str(ms.how));
            const Arts& arts = game.player.arts;
            const bool owned = arts.manuals.count(ms.id) > 0 ||
                std::find(arts.bookList.begin(), arts.bookList.end(), ms.id) != arts.bookList.end() ||
                std::find(game.economy.books.begin(), game.economy.books.end(), ms.id) != game.economy.books.end();
            entry.set("owned", Value(owned));
            const std::string learnReason = ecoActionError(game, "learn", ms.id);
            const EcoActionInfo learn = ecoActionInfo(game, "learn", ms.id);
            entry.set("canLearn", Value(learnReason.empty() && !game.ended && !game.pending && !game.job && !arts.battle));
            entry.set("learnReason", str(learnReason));
            entry.set("learnDays", num(learn.days));
            entry.set("learnCost", num(learn.cost));
            arr.push_back(std::move(entry));
        }
        state.set("manualSources", Value(std::move(arr)));
    }
    {
        json::Array arr;
        for (const auto& spell : shop::availableLibrarySpells(game)) {
            Value item(json::Object{});
            item.set("id",str(spell.id));
            item.set("name",str(spell.name));
            item.set("dao",str(spell.dao));
            item.set("desc",str(spell.desc));
            item.set("sect",str(spell.sect));
            item.set("stage",num(spell.stage));
            item.set("stageName",str(realmName(spell.stage)));
            item.set("points",num(spell.points));
            item.set("known",Value(spell.known));
            item.set("enabled",Value(spell.enabled));
            item.set("reason",str(spell.reason));
            arr.push_back(std::move(item));
        }
        state.set("librarySpells",Value(std::move(arr)));
    }
}

} // namespace nz::api::view
