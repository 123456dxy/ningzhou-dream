#pragma once

#include <string>
#include <vector>

namespace nz::content {
// 用途：读取稳定地点ID对应的草药生境；流程：采集规则；用法：传LocationId，返回稳定HerbId列表；返回：不可变目录或未知地点的空表，不读取展示名称。
const std::vector<std::string>& mfHerbHabitatIds(const std::string& LocationId);
} // namespace nz::content
