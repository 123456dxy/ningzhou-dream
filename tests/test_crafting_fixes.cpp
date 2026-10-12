#include "harness.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/economy.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/shop.h"
#include "core/crafting/material_supply.h"
#include "data/reference_tables.h"

namespace nztest {
namespace {
using namespace nz;
using json::Value;
// 用途：在内存隔离工坊请求；流程：材料报价与购买回归；用法：栈上创建；返回：无，析构恢复默认会话。
struct CraftScope {
    api::Session session;
    CraftScope() { CreateOptions o; o.seed=123; o.traitsExplicit=true; session.g=createGame(o); session.started=true;
        session.g.pending.reset(); session.g.job.reset(); session.g.npcs.clear(); session.g.player.stones=100000;
        session.g.player.location="wuling"; session.g.player.arts.herbs.clear(); session.g.player.herbs=0;
        session.g.player.arts.materials.clear(); api::bindSession(&session); }
    ~CraftScope() { api::bindSession(nullptr); }
};
// 用途：执行真实 API 请求；流程：工坊接口回归；用法：传路径和参数；返回：HTTP 响应。
http::Response craftCall(const string& path, const Value& input) {
    http::Request request; request.method="POST"; request.path="/api/"+path; request.body=input.dump(); return api::handle(request);
}
// 用途：构建材料需求；流程：报价/购买；用法：选丹方或三料一器型；返回：请求对象。
Value requestFor(const string& kind, const string& id, int batch=1) {
    Value v(json::Object{}); v.set("kind",kind); v.set("id",id); v.set("batch",batch); return v;
}
}
// 用途：核验缺料购买与法器回收；流程：完整自测；用法：test_main 调用；返回：无，断言计入 harness。
void runCraftingFixTests() {
    std::printf("\n--- 工坊购买与分解回归 ---\n");
    CraftScope scope; auto& game=scope.session.g;
    const auto& recipe=data::RecipeDef_TABLE().front(); auto input=requestFor("brew",recipe.id,3);
    const auto before=saveToJson(game);
    auto response=craftCall("craft/materials/quote",input);
    checkEq(response.status,200,"整批买料报价接口存在");
    auto quote=Value::parse(response.body)["quote"];
    check(quote["enabled"].asBool() && quote["totalCost"].asInt()>0,"本地可以买足当前丹方三炉材料");
    check(saveToJson(game)==before,"报价不修改资源、时间或随机数");
    const int cost=quote["totalCost"].asInt(), stones=game.player.stones, days=game.clock.days;
    response=craftCall("craft/materials/buy",input);
    checkEq(response.status,200,"一键购买成功");
    checkEq(game.player.stones,stones-cost,"扣费严格符合展示总价");
    for(const auto& [name,count]:parseIngredients(recipe.ingredients)) checkEq(game.player.arts.herbs[name],count*3,"购入三炉指定草药");
    checkEq(game.clock.days,days,"采购不推进炼制时间");
    const auto enough=saveToJson(game);
    checkEq(craftCall("craft/materials/buy",input).status,400,"材料已足不重复扣费");
    check(saveToJson(game)==enough,"无需购买完全不修改状态");
    game.player.arts.herbs.clear(); game.player.herbs=0; game.player.stones=0;
    const auto poor=saveToJson(game);
    checkEq(craftCall("craft/materials/buy",input).status,400,"总价不足拒绝整单");
    check(saveToJson(game)==poor,"灵石不足无部分采购");
    game.player.stones=100000; game.player.location="jinhong";
    const auto outside=saveToJson(game);
    checkEq(craftCall("craft/materials/buy",input).status,400,"非坊市不能购买");
    check(saveToJson(game)==outside,"非坊市失败不修改库存");
    game.player.location="wuling";
    const auto stale=saveToJson(game); input.set("expectedCost",1);
    checkEq(craftCall("craft/materials/buy",input).status,400,"报价变化需重新确认");
    check(saveToJson(game)==stale,"失效报价拒绝后无部分采购");
    for (const auto& invalid : {Value("incorrect"),Value(-1),Value(1.5),Value(1e20)}) {
        input.set("expectedCost",invalid); const auto unchanged=saveToJson(game);
        checkEq(craftCall("craft/materials/buy",input).status,400,"无效确认金额不能退化为未确认采购");
        check(saveToJson(game)==unchanged,"无效确认金额保留完整状态");
    }
    input=requestFor("brew",recipe.id,3); game.player.herbs=1000000000;
    quote=Value::parse(craftCall("craft/materials/quote",input).body)["quote"];
    check(!quote["enabled"].asBool(),"总草药库存上限在报价阶段就拒绝");
    game.player.herbs=0;
    for(const auto& high:data::MaterialDef_TABLE()) if(high.grade>=3) {
        const auto blocked=saveToJson(game);
        auto highInput=requestFor("forge",high.id+"|"+high.id+"|"+high.id+"|sword");
        checkEq(craftCall("craft/materials/buy",highInput).status,400,"高阶材料限制不能被补料按钮绕过");
        check(saveToJson(game)==blocked,"不可售材料失败无部分买入"); break;
    }
    const auto& material=data::MaterialDef_TABLE().front();
    auto forge=requestFor("forge",material.id+"|"+material.id+"|"+material.id+"|sword",2);
    quote=Value::parse(craftCall("craft/materials/quote",forge).body)["quote"];
    checkEq(quote["materials"].asArray().size(),size_t{1},"主辅灵核重复材料合并报价");
    checkEq(quote["materials"][0]["required"].asInt(),20,"两份炼器配料共二十份");
    checkEq(craftCall("craft/materials/buy",forge).status,200,"可买多份配料供逐件炼器");
    Job job; job.actionType="forge"; job.destination=material.id+"|"+material.id+"|"+material.id+"|sword";
    job.name="铸炼"+material.name+"剑"; job.chance=1; job.cost=33; job.total=8;
    ecoComplete(game,job); const string gearId=game.player.arts.gear.back().id;
    const int held=game.player.arts.materials[material.id], oldMoney=game.player.stones;
    auto gearState=Value::parse(craftCall("state",Value(json::Object{})).body)["state"];
    check(!gearState["gears"].asArray().empty(),"装备状态可读取");
    auto salvage=crafting::salvageQuote(game,gearId);
    check(salvage.enabled && !salvage.estimated && salvage.materials.front().required==5,"新法器拆解显示准确回收材料");
    Game restored; string loadError; check(loadFromJson(saveToJson(game),restored,&loadError),"原配料元数据可以随旧格式存档往返");
    checkEq(crafting::salvageQuote(restored,gearId).materials.front().required,5,"读档后回收份数不变");
    game.player.arts.equipped["weapon"]=gearId; const auto equipped=saveToJson(game);
    check(!shop::salvage(game,gearId).ok && saveToJson(game)==equipped,"已装备不能分解且状态不变");
    game.player.arts.equipped.clear(); game.player.stones=0; const auto noFee=saveToJson(game);
    check(!shop::salvage(game,gearId).ok && saveToJson(game)==noFee,"手续费不足保留法器与配料");
    game.player.stones=oldMoney;
    const auto result=shop::salvage(game,gearId);
    check(result.ok,"自产未装备法器可以分解");
    checkEq(game.player.arts.materials[material.id],held+5,"原配料合并后返还一半");
    checkEq(game.player.stones,oldMoney-5,"一阶分解收取五灵石");
    check(game.player.arts.gear.empty(),"成功后才移除法器");
    Gear oldGear; oldGear.id="old-forge"; oldGear.name=material.name+"剑"; oldGear.crafted=true; oldGear.grade=1;
    game.player.arts.gear.push_back(oldGear);
    const auto legacy=crafting::salvageQuote(game,oldGear.id);
    check(legacy.enabled && legacy.estimated && legacy.materials.front().required==2,"旧法器明确提示仅回收可确认主材");
    game.player.arts.gear.back().name="无名古剑";
    const auto unknown=saveToJson(game);
    check(!shop::salvage(game,oldGear.id).ok && saveToJson(game)==unknown,"旧档无法确认配料时不会销毁法器");
}
}
