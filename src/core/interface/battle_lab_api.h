#pragma once

#include "core/combat/battle_lab.h"
#include "core/http.h"
#include "core/json.h"

namespace nz::BattleLab {

// 用途：生成独立试炼配置；流程：配置 GET；用法：无参数；返回：拥有目录 JSON，不读取账号或消费随机数。
json::Value mfConfigurationView();
// 用途：生成试炼角色、构筑、实际战况及结局；流程：只读 GET/动作响应；用法：传只读会话；返回：拥有 JSON，不改变会话/RNG/日历。
json::Value mfStateView(const Session& LabSession);
// 用途：翻译独立 HTTP 请求并原子提交领域命令；流程：试炼服务；用法：在串行 HTTP 上下文传会话和请求。
// 返回：JSON 响应；非法参数/过期 Revision 为 400/409 且完整状态不变；异常为 500、不写存档。
http::Response mfHandle(Session& LabSession, const http::Request& Request);

} // namespace nz::BattleLab
