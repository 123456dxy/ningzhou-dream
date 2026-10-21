#pragma once

#include "core/arts.h"
#include "core/crafting/forging_product.h"

#include <array>

namespace nz::crafting {

// 自由炼器请求：每格一份，空字符串表示未投入；主材必须来自已投入材料。
// 只表达玩家选择，不信任客户端传来的价格、品阶或效果数值。
struct ForgeRequest {
    vector<string> MaterialIds;
    string MainMaterialId;
    string ShapeId;
    string EffectId;
    string InscriptionId;
};

// 无外部资源的只读报价与冻结成品快照；Enabled 才允许开工，Valid 可用于未满十格的中间预览。
struct ForgeQuote : ForgeRecipeResult {
    bool Enabled = false;
    vector<ForgeMaterialCost> Materials;
    int CostSpiritStoneCount = 0;
    int DurationDays = 0;
    double SuccessProbability = 0;
};

// 用途：同一规则计算中间预览、完整配方、缺料、资格、真实费用与成品；流程：自由炼器；无时间、随机或存档副作用。
[[nodiscard]] ForgeQuote mfQuoteForge(const Game& GameState, const ForgeRequest& Request);
// 用途：重新核验报价后一次扣取全部投入，写入冻结合同；流程：自由炼器开工；错误不部分修改状态。
[[nodiscard]] string mfBeginForge(Game& GameState, const ForgeRequest& Request, int ExpectedCost = -1);
// 用途：完成冻结合同，成功只发放冻结成品；流程：ecoComplete；返回是否属于新版合同，旧合同返回false。
[[nodiscard]] bool mfCompleteForge(Game& GameState, const Job& Contract);

} // namespace nz::crafting
