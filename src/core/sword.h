// 宁州一梦 · C++ 版 — 旧剑意接口兼容与统一剑道悟道
//
// 旧档剑意字段和名称查询保留；永久剑意不再影响攻防。
// 剑道成长与其他大道共用悟道经验、功法和本场状态机制。
#pragma once

#include <string>

#include "core/model.h"
#include "data/reference_tables.h"

namespace nz {

// 七重剑道境界：初识剑路 / 剑随心动 / 剑气凝锋 / 剑意通明 /
//               剑心圆融 / 剑道化境 / 万剑归一
// 用途：保留旧剑意称号的阈值表。
// 参与流程：旧存档兼容查询。
// 使用方式：读取静态表，不用于斗法倍率。
// 返回值：七个旧称号定义的常量引用。
const vector<data::SwordRealmDef>& swordRealms();

// 当前剑意所处的重数（0..6）
// 用途：按旧剑意值查历史称号序号。
// 参与流程：旧剑意名称查询。
// 使用方式：传旧字段值，超最高阈值仍取最后一项。
// 返回值：从零开始的称号序号。
int swordRank(int swordValue);
// 当前境界名
// 用途：查旧剑意值对应的历史称号。
// 参与流程：兼容旧剑意展示或迁移诊断。
// 使用方式：传剑意数值；新战斗不依赖此名。
// 返回值：历史称号；表为空时为空字符串。
string swordRealmName(int swordValue);

// 是否剑修：金虹弟子、主修剑道功法、或携带两种御剑/人剑类神通
// 用途：按宗门、主修或携带剑招识别剑修身份。
// 参与流程：流派身份兼容查询。
// 使用方式：传角色只读状态。
// 返回值：满足任一剑修条件时为真。
bool swordCultivator(const Player& p);
// 用途：保留历史游戏级剑修查询声明。
// 参与流程：旧公共接口兼容；当前没有实现或调用。
// 使用方式：新代码使用 swordCultivator(g.player)。
// 返回值：历史声明的布尔结果；本项目无可链接实现。
bool swordCultivatorOf(const Game& g);

// 旧剑修对阵接口的兼容结果，所有对局均返回中性值。
struct SwordDuel {
    bool active = false;
    int gap = 0;              // 恒为零，不再产生独立剑道境差
    double attack = 1.0;      // 恒为一，不增减伤害
    double reduction = 0.0;   // 恒为零，不增减来伤
};
// 用途：让旧境差查询保持中性，永久剑意不再增减伤。
// 参与流程：旧调用兼容；实际攻防走统一功法与悟道。
// 使用方式：可传任意双方旧剑意值，均不产生修正。
// 返回值：active 为假、gap 和 reduction 为零、attack 为一。
SwordDuel swordDuelOf(const Player& p, const Battle& b);

// 剑道活动增加统一悟道经验；保留旧剑意值。
// 用途：将剑道活动收益计入统一悟道经验。
// 参与流程：剑道成长事件。
// 使用方式：传游戏与收益量，负收益按零处理。
// 返回值：无；更新玩家剑道经验和升级悟道点，保留旧剑意。
void swordGain(Game& g, int amount);

} // namespace nz
