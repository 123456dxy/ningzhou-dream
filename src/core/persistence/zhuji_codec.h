#pragma once
#include "core/json.h"
#include "core/progression/zhuji_state.h"

namespace nz::persistence {
json::Value mfZhujiToJson(const ZhujiRun& Run);
bool mfZhujiFromJson(const json::Value& Input, ZhujiRun& Output, std::string& Error);
}
