#include "harness.h"
#include "core/interface/views.h"
#include "core/sim.h"
#include "core/save.h"
#include "core/arts.h"
#include "data/reference_tables.h"
#include <algorithm>
#include <array>
#include <utility>

namespace nztest {
using namespace nz;
namespace {
// 用途：按稳定ID读取目录条目；流程：展示契约回归；用法：传JSON数组和ID；返回：匹配对象或空值。
json::Value catalogRow(const json::Value& rows, const string& id) {
    for (const auto& row : rows.asArray()) if (row["id"].asString() == id) return row;
    return {};
}

// 用途：核验修为丹标称能力不受角色小境限制；流程：目录、预览与真实服用回归；返回：无，使用独立角色与手算期望。
void mfCheckCultivationPillCatalog() {
    const std::array<std::pair<const char*, int>, 3> NominalEffects = {{{"5002", 54}, {"5102", 94}, {"5502", 3083}}};
    for (const auto& [RecipeId, NominalValue] : NominalEffects) {
        CreateOptions Options; Options.seed = 20261013; Options.traitsExplicit = true;
        auto State = createGame(Options);
        State.npcs.clear(); State.pending.reset(); State.job.reset();
        auto& Player = State.player;
        Player.stage = Player.highestStage = 0; Player.xp = 0; Player.traits.clear(); Player.talent.clear();
        Player.location = "wuling"; Player.arts.poison = 0; Player.arts.potions[RecipeId] = 3;
        const auto Before = saveToJson(State);
        json::Value View(json::Object{}); api::view::appendEconomyState(State, View);
        const auto Pill = catalogRow(View["pillCatalog"], RecipeId);
        checkEq(Pill["catalogEffectValue"].asInt(), NominalValue, "完整修为丹目录提供本地标称值：" + string(RecipeId));
        check(Pill["catalogEffect"].asString().find("+" + std::to_string(NominalValue)) != string::npos,
            "完整修为丹目录不把初炼气28点上限当成药效：" + string(RecipeId));
        checkNear(Pill["effectValue"].asNumber(), 28, "扩大修为单位后初炼气服丹上限仍为28：" + string(RecipeId));
        check(Pill["usage"].asString().find("本小境单次上限（28）") != string::npos &&
            Pill["usage"].asString().find("距圆满余量（1100）") != string::npos,
            "当前服用说明列明小境上限与圆满余量：" + string(RecipeId));
        check(saveToJson(State) == Before, "修为丹完整与当前预览不改状态：" + string(RecipeId));
        check(artUsePill(State, RecipeId).empty(), "初炼气实际服用修为丹成功：" + string(RecipeId));
        checkEq(Player.xp, 28, "初炼气实际吸收与当前预览一致：" + string(RecipeId));

        Player.stage = Player.highestStage = 9; Player.xp = 0; Player.lifespan = 1000;
        const auto FullQuote = artPillUseInfo(Player, *findRecipe(RecipeId));
        View = json::Value(json::Object{}); api::view::appendEconomyState(State, View);
        checkEq(catalogRow(View["pillCatalog"], RecipeId)["catalogEffectValue"].asInt(), NominalValue,
            "晋阶不改变丹药标称能力：" + string(RecipeId));
        checkNear(FullQuote.effectValue, NominalValue, "元婴初期可完整吸收三种标称修为：" + string(RecipeId));
        check(artUsePill(State, RecipeId).empty(), "高境界实际服用修为丹成功：" + string(RecipeId));
        checkEq(Player.xp, NominalValue, "高境界真实结算与完整吸收预览一致：" + string(RecipeId));

        Player.xp = realmOf(Player.stage).need - 1;
        const auto NearFullQuote = artPillUseInfo(Player, *findRecipe(RecipeId));
        checkNear(NearFullQuote.effectValue, 1, "临近圆满只可吸收剩余1点：" + string(RecipeId));
        check(NearFullQuote.usage.find("距圆满余量（1）") != string::npos,
            "临近圆满预览明确剩余可吸收量：" + string(RecipeId));
        check(artUsePill(State, RecipeId).empty(), "临近圆满实际服用成功：" + string(RecipeId));
        checkEq(Player.xp, realmOf(Player.stage).need, "实际修为不超过圆满：" + string(RecipeId));
    }
    CreateOptions Options; Options.seed = 20261013; Options.traitsExplicit = true;
    auto State = createGame(Options); State.npcs.clear(); State.pending.reset(); State.job.reset();
    State.player.xp = 0; State.player.traits = {"oddMeridian"}; State.player.arts.potions["5502"] = 1;
    json::Value View(json::Object{}); api::view::appendEconomyState(State, View);
    const auto Pill = catalogRow(View["pillCatalog"], "5502");
    checkEq(Pill["catalogEffectValue"].asInt(), 3083, "先天异脉不改变丹药本地标称能力");
    checkNear(Pill["effectValue"].asNumber(), 0, "先天异脉当前无法吸收修为");
    const auto Before = saveToJson(State);
    check(!artUsePill(State, "5502").empty() && saveToJson(State) == Before, "异脉拒绝实际服用且完整能力不绕过资格");
    Game Restored; string Error;
    auto Legacy = json::Value::parse(Before);
    Legacy.asObject()["player"].asObject()["arts"].asObject().erase("combatPrep");
    check(loadDetachedFromJson(Legacy.dump(), Restored, &Error), "旧档缺少准备字段仍可读取修为丹：" + Error);
    View = json::Value(json::Object{}); api::view::appendEconomyState(Restored, View);
    checkEq(catalogRow(View["pillCatalog"], "5502")["catalogEffectValue"].asInt(), 3083, "旧档目录使用同一本地标称规则");
}
}
// 用途：核验目录完整效果与真实品阶；流程：桌面坊市/工坊回归；用法：由自测入口调用；返回：无。
void runCatalogDisplayTests() {
    std::printf("\n--- 丹药目录完整效果与品阶 ---\n");
    mfCheckCultivationPillCatalog();
    nz::CreateOptions options; options.seed=20261012; options.traitsExplicit=true;
    auto game=nz::createGame(options); game.npcs.clear(); game.pending.reset(); game.job.reset();
    game.player.location="wuling"; game.player.health=100;
    const auto before=nz::saveToJson(game);
    nz::json::Value state(nz::json::Object{}); nz::api::view::appendEconomyState(game,state);
    const auto pill=catalogRow(state["pillCatalog"],"5001");
    const auto* recipe=nz::findRecipe("5001");
    check(recipe && pill["catalogEffect"].asString().find(std::to_string(std::min(100,recipe->value)))!=string::npos,
          "满健康时目录仍显示完整恢复量");
    check(pill["effect"].asString().find("+0")!=string::npos,"服用详情继续显示当前实际增量零");
    const auto market=catalogRow(state["market"],"5001");
    checkEq(market["grade"].asInt(),recipe?recipe->grade:1,"坊市丹药拥有真实品阶");
    check(!market["catalogEffect"].asString().empty() && market["catalogEffect"].asString()==pill["catalogEffect"].asString(),"坊市与完整丹药目录说明一致");
    const auto brew=catalogRow(state["brewOptions"],"5001");
    checkEq(brew["grade"].asInt(),recipe?recipe->grade:1,"工坊丹方拥有真实品阶");
    check(!brew["catalogEffect"].asString().empty() && brew["catalogEffect"].asString()==pill["catalogEffect"].asString(),"工坊不把当前零增益写成丹药能力");
    bool allGrades=true;
    for(const auto& row:state["market"].asArray()) allGrades &= row["grade"].asInt()>0;
    check(allGrades,"药材器材丹药法器的可售条目全部提供品阶");
    check(nz::saveToJson(game)==before,"查询效果与品阶不修改时间随机数库存或存档");
    for(const auto& definition:nz::data::RecipeDef_TABLE()) if(definition.kind=="apt") {
        game.player.apt=nz::APT_MAX; game.player.arts.aptPills=10;
        state=nz::json::Value(nz::json::Object{}); nz::api::view::appendEconomyState(game,state);
        const auto row=catalogRow(state["pillCatalog"],definition.id);
        check(!row["catalogEffect"].asString().empty() && row["catalogEffect"].asString().find("资质 +0")==string::npos,"属性已封顶不把丹药目录能力截断成零");
        check(row["effectValue"].asNumber()==0,"完整目录说明不改变实际服药封顶规则");
        break;
    }
}
}
