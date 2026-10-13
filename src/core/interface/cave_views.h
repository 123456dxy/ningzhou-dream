#pragma once
#include "core/json.h"
#include "core/model.h"

namespace nz::api::view {
// 用途：把洞府进入条件、训练和灵田权威报价编码为JSON；流程：state只读页；用法：传const Game和已有洞府对象；返回：无，只追加输出，不改变Game。
void mfAppendCaveDetails(const Game& State, json::Value& Cave);
} // namespace nz::api::view
