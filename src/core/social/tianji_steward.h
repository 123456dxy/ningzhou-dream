#pragma once
#include "core/model.h"
#include <span>
#include <string_view>

namespace nz::social {
// 固定公共服务NPC快照，不属于世界游历NPC/个人行囊；无持久资源，无随机成长、死亡或服丹。
// 五城分号的值班身份长期存在，交易只借用当前位置；只读目录可并行读取。
struct TianjiStewardDefinition {
    string Id;
    string Name;
    string Location;
    int Stage = 3;
};
[[nodiscard]] std::span<const TianjiStewardDefinition> mfTianjiStewards();
[[nodiscard]] const TianjiStewardDefinition* mfFindTianjiSteward(std::string_view NpcId);
}
