#include "core/crafting/material_supply.h"
#include "core/arts.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/sim.h"
#include "data/reference_tables.h"
#include <algorithm>
#include <limits>
#include <map>

namespace nz::crafting {
namespace {
constexpr int inventoryLimit=1000000000;
// 用途：生成稳定配料记录键；流程：炼器与拆解；用法：传法器/材料 ID；返回：既有 flags 存储键。
string sourceKey(const string& gear, const string& material) { return "forge-materials-v1:"+gear+":"+material; }
// 用途：统一空闲状态校验；流程：采购和拆解；用法：传游戏快照；返回：阻塞原因，空串可执行。
string busyReason(const Game& g) {
    if(g.ended || g.player.health<=0) return "此世已结束或伤势过重。";
    if(g.pending || g.job || g.player.arts.battle || g.dungeons.run) return "请先完成当前安排、事件、斗法或秘境。";
    return {};
}
// 用途：查询已存数量且不插入键；流程：报价；用法：传库存和键；返回：已有数量。
int held(const std::map<string,int>& values, const string& key) { auto it=values.find(key); return it==values.end()?0:it->second; }
// 用途：序列化材料行；流程：报价输出；用法：传合并后的材料列表；返回：JSON 数组。
json::Value rowsJson(const vector<MaterialRow>& rows) {
    json::Array items;
    for(const auto& r:rows) { json::Value item(json::Object{}); item.set("kind",r.kind); item.set("id",r.id); item.set("name",r.name);
        item.set("required",r.required); item.set("owned",r.owned); item.set("missing",r.missing); item.set("count",r.required);
        item.set("unitPrice",r.unitPrice); item.set("cost",r.cost); item.set("buyable",r.buyable); item.set("reason",r.reason); items.push_back(std::move(item)); }
    return json::Value(std::move(items));
}
}
// 用途：汇总指定制作份数的缺料；流程：工坊只读报价；用法：传丹方或炼器组合；返回：本地可购报价，库存已足时无需采购。
MaterialQuote materialQuote(const Game& game, const string& kind, const string& id, int batch) {
    MaterialQuote q; q.batch=batch;
    if(batch<1 || batch>100 || (kind=="brew" && batch!=1 && batch!=3 && batch!=5 && batch!=10)) { q.reason="炼丹数量为1、3、5、10炉；炼器备料份数为1至100。"; return q; }
    std::map<string,MaterialRow> needs;
    if(kind=="brew") {
        const auto* recipe=findRecipe(id); if(!recipe) { q.reason="没有这个丹方。"; return q; }
        for(const auto& [name,count]:parseIngredients(recipe->ingredients)) {
            const auto* herb=findHerbByName(name); if(!herb) { q.reason="丹方包含未知药材。"; return q; }
            auto& row=needs[herb->id]; row.kind="herb"; row.id=herb->id; row.name=name; row.required+=count*batch; row.owned=held(game.player.arts.herbs,name);
        }
    } else if(kind=="forge") {
        const size_t a=id.find('|'), b=a==string::npos?string::npos:id.find('|',a+1), c=b==string::npos?string::npos:id.find('|',b+1);
        if(c==string::npos || id.find('|',c+1)!=string::npos) { q.reason="请选择完整的三种配料与器型。"; return q; }
        const string main=id.substr(0,a), aux=id.substr(a+1,b-a-1), core=id.substr(b+1,c-b-1), shape=id.substr(c+1);
        if(!artForgeInfo(game.player,main,aux,core,shape).valid) { q.reason="请选择有效配料与器型。"; return q; }
        for(const auto& material:artForgeMaterials(game.player,main,aux,core)) {
            auto& row=needs[material.id]; row.kind="material"; row.id=material.id; row.name=material.name; row.required=material.required*batch; row.owned=material.owned;
        }
    } else { q.reason="仅支持炼丹与炼器材料采购。"; return q; }
    q.valid=true; const auto market=play::marketList(game); long long total=0, herbsToBuy=0; bool missing=false;
    q.reason=busyReason(game);
    for(auto& [idKey,row]:needs) {
        row.missing=std::max(0,row.required-row.owned); missing|=row.missing>0;
        if(row.kind=="herb") herbsToBuy+=row.missing;
        const auto item=std::find_if(market.begin(),market.end(),[&](const auto& m){return m.kind==row.kind && m.id==row.id;});
        row.buyable=item!=market.end(); if(row.buyable) row.unitPrice=item->price;
        if(row.missing>0 && !row.buyable) row.reason=row.name+"当地不可购买；请从探索或交易获取。";
        if(row.missing>1000 || row.owned>inventoryLimit-row.missing) row.reason=row.name+"超过单次购买或库存上限。";
        row.cost=row.unitPrice*row.missing; total+=row.cost;
        if(q.reason.empty() && !row.reason.empty()) q.reason=row.reason;
        q.materials.push_back(row);
    }
    if(total>std::numeric_limits<int>::max()) { q.reason="采购金额超过支持范围。"; return q; }
    q.totalCost=static_cast<int>(total);
    long long typedHerbs=0;
    for (const auto& [name,count] : game.player.arts.herbs) typedHerbs+=count;
    if (q.reason.empty() && (static_cast<long long>(game.player.herbs)+herbsToBuy>inventoryLimit || typedHerbs+herbsToBuy>inventoryLimit))
        q.reason="采购后草药总库存超过上限。";
    if(q.reason.empty() && !missing) q.reason="材料已备齐，无需购买。";
    if(q.reason.empty() && game.player.stones<q.totalCost) q.reason="灵石不足：预计需要"+std::to_string(q.totalCost)+"枚。";
    q.enabled=q.reason.empty(); return q;
}
// 用途：购买缺料并保证整单原子性；流程：工坊一键购买；用法：确认报价后传原需求；返回：成功空串或错误原因。
string buyMaterials(Game& game, const string& kind, const string& id, int batch, int expectedCost) {
    const auto quote=materialQuote(game,kind,id,batch); if(!quote.enabled) return quote.reason;
    if(expectedCost>=0 && expectedCost!=quote.totalCost) return "材料报价已变化，请重新确认预计灵石消耗。";
    Game projected=game;
    for(const auto& row:quote.materials) if(row.missing>0) {
        const auto error=play::marketBuy(projected,row.kind,row.id,row.missing); if(!error.empty()) return error;
    }
    game=std::move(projected); return {};
}
// 用途：输出工坊材料报价；流程：HTTP 预览；用法：传纯计算结果；返回：可供 UI 展示的 JSON。
json::Value materialQuoteJson(const MaterialQuote& q) {
    json::Value v(json::Object{}); v.set("valid",q.valid); v.set("enabled",q.enabled); v.set("reason",q.reason); v.set("batch",q.batch);
    v.set("totalCost",q.totalCost); v.set("materials",rowsJson(q.materials)); return v;
}
// 用途：保存准确原料记录；流程：法器制作成功；用法：生成法器后传三种原材料 ID；返回：无。
void recordForgeMaterials(Game& g, const string& gear, const string& main, const string& aux, const string& core) {
    for(const auto& row:artForgeMaterials(g.player,main,aux,core)) g.flags[sourceKey(gear,row.id)]=row.required;
}
// 用途：按记录或旧档可确认主材计算返还；流程：行囊分解确认；用法：传法器 ID；返回：返还清单与手续费。
SalvageQuote salvageQuote(const Game& g, const string& id) {
    SalvageQuote q; q.reason=busyReason(g);
    const auto& gears=g.player.arts.gear; auto gear=std::find_if(gears.begin(),gears.end(),[&](const Gear& item){return item.id==id;});
    if(gear==gears.end()) { q.reason="没有这件法器。"; return q; }
    q.cost=5*std::clamp(gear->grade,1,6);
    for(const auto& [slot,equipped]:g.player.arts.equipped) if(equipped==id) q.reason="请先卸下装备再分解。";
    if(!gear->crafted && q.reason.empty()) q.reason="此法器没有自炼配料记录，可在坊市出售。";
    bool recorded=false; int sourceTotal=0;
    for(const auto& material:data::MaterialDef_TABLE()) {
        const int count=held(g.flags,sourceKey(id,material.id)); if(count<=0) continue; recorded=true; sourceTotal+=count;
        if(count>10) { q.reason="原始配料记录无效。"; continue; }
        if(count/2>0) { MaterialRow row; row.kind="material"; row.id=material.id; row.name=material.name; row.required=count/2; row.owned=held(g.player.arts.materials,material.id); q.materials.push_back(row); }
    }
    if(recorded && sourceTotal!=10) q.reason="原始配料记录不完整。";
    q.sourceNote="按原始配料合计返还50%，不足一份向下取整。";
    if(!recorded) {
        q.estimated=true; q.sourceNote="旧档未记录完整配方，仅按法器名称可确认的主材预估回收2份。";
        const data::MaterialDef* matched=nullptr;
        for(const auto& material:data::MaterialDef_TABLE()) if(gear->name.starts_with(material.name) && (!matched || material.name.size()>matched->name.size())) matched=&material;
        if(matched) { MaterialRow row; row.kind="material"; row.id=matched->id; row.name=matched->name; row.required=2; row.owned=held(g.player.arts.materials,matched->id); q.materials.push_back(row); }
    }
    if(q.reason.empty() && q.materials.empty()) q.reason="无法确认此法器的原材料，请保留或出售。";
    for(const auto& row:q.materials) if(row.owned>inventoryLimit-row.required) q.reason="回收后材料库存超过上限。";
    if(q.reason.empty() && g.player.stones<q.cost) q.reason="分解手续费需要"+std::to_string(q.cost)+"灵石。";
    q.enabled=q.reason.empty(); return q;
}
// 用途：根据同一报价实际分解；流程：行囊法器操作；用法：传未装备自炼法器 ID；返回：成功空串或错误原因。
string salvageMaterials(Game& g, const string& id) {
    const auto q=salvageQuote(g,id); if(!q.enabled) return q.reason;
    g.player.stones-=q.cost; for(const auto& row:q.materials) g.player.arts.materials[row.id]+=row.required;
    auto& gears=g.player.arts.gear; gears.erase(std::remove_if(gears.begin(),gears.end(),[&](const Gear& gear){return gear.id==id;}),gears.end());
    for(const auto& material:data::MaterialDef_TABLE()) g.flags.erase(sourceKey(id,material.id));
    return {};
}
// 用途：输出拆解预览；流程：HTTP 装备状态；用法：传报价结果；返回：包含回收份数、手续费和估算说明的 JSON。
json::Value salvageQuoteJson(const SalvageQuote& q) {
    json::Value v(json::Object{}); v.set("enabled",q.enabled); v.set("reason",q.reason); v.set("estimated",q.estimated); v.set("sourceNote",q.sourceNote);
    v.set("costStones",q.cost); v.set("returnStones",0); v.set("materials",rowsJson(q.materials)); return v;
}
}
