#include "harness.h"
#include "core/interface/views.h"
#include "core/sim.h"
#include "core/save.h"
#include "core/arts.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nztest {
using namespace nz;
namespace {
// 用途：按稳定ID读取目录条目；流程：展示契约回归；用法：传JSON数组和ID；返回：匹配对象或空值。
json::Value catalogRow(const json::Value& rows, const string& id) {
    for (const auto& row : rows.asArray()) if (row["id"].asString() == id) return row;
    return {};
}
}
// 用途：核验目录完整效果与真实品阶；流程：桌面坊市/工坊回归；用法：由自测入口调用；返回：无。
void runCatalogDisplayTests() {
    std::printf("\n--- 丹药目录完整效果与品阶 ---\n");
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
