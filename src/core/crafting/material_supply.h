#pragma once
#include "core/model.h"
#include "core/json.h"

namespace nz::crafting {
struct MaterialRow { string kind, id, name, reason; int required=0, owned=0, missing=0, unitPrice=0, cost=0; bool buyable=false; };
struct MaterialQuote { bool valid=false, enabled=false; string reason; int batch=1, totalCost=0; vector<MaterialRow> materials; };
struct SalvageQuote { bool enabled=false, estimated=false; string reason, sourceNote; int cost=0; vector<MaterialRow> materials; };
// 用途：计算配方缺料和本地采购总价；流程：工坊报价/购买；用法：丹方 ID 或主辅核器型组合与份数；返回：只读报价，不推进时间或随机数。
MaterialQuote materialQuote(const Game& game, const string& kind, const string& id, int batch);
// 用途：原子采购所有缺料；流程：工坊一键购买；用法：传原配方、份数和可选确认总价；返回：空串成功，否则原因，失败不修改角色。
string buyMaterials(Game& game, const string& kind, const string& id, int batch, int expectedCost=-1);
// 用途：序列化工坊报价；流程：HTTP 输出；用法：传只读报价；返回：含材料缺口、可购原因与预计总价的 JSON。
json::Value materialQuoteJson(const MaterialQuote& quote);
// 用途：记录新法器真实来源；流程：炼器成功；用法：传新法器 ID 与原配料 ID；返回：无，写入既有存档 flags，保留旧存档格式。
void recordForgeMaterials(Game& game, const string& gearId, const string& main, const string& aux, const string& core);
// 用途：预估拆解材料与手续费；流程：行囊确认/拆解；用法：传未装备自产法器 ID；返回：只读报价，旧档仅回收可确认主材。
SalvageQuote salvageQuote(const Game& game, const string& id);
// 用途：执行材料回收；流程：法器分解；用法：传法器 ID；返回：空串成功或失败原因，成功扣手续费并回收材料。
string salvageMaterials(Game& game, const string& id);
// 用途：序列化分解预估；流程：行囊操作面板；用法：传分解报价；返回：材料、费用和旧档估算说明 JSON。
json::Value salvageQuoteJson(const SalvageQuote& quote);
}
