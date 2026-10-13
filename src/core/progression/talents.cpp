#include "core/talents.h"

#include <algorithm>
#include <limits>

#include "core/sim.h"
#include "core/source_limits.h"

namespace nz {

int talentAffinityBonus(const Player& player) {
    return hasTalent(player, "luck") ? 2 : 0;
}

// 用途：查询先天道体的修炼速度乘数。
// 参与流程：基础修炼速度汇总。
// 使用方式：传角色只读状态。
// 返回值：有先天道体时 1.10，否则 1.0。
double talentCultivationMul(const Player& player) {
    return hasTalent(player, "daoBody") ? 1.10 : 1.0;
}

// 用途：查询筑基后先天道体提供的灵力容量。
// 参与流程：斗法容量汇总。
// 使用方式：传角色，要求先天道体且 stage 至少三。
// 返回值：额外两点容量，否则为零。
int talentManaBonus(const Player& player) {
    return hasTalent(player, "daoBody") && player.stage >= 3 ? 2 : 0;
}

// 用途：在功法和悟道系数上叠加天赋机制。
// 参与流程：战斗、炼丹炼器与遁速的统一属性查询。
// 使用方式：传角色，从 artModsOf 复制后追加天赋并限制生活收益。
// 返回值：汇总 ArtMods 值，不改变角色或基础缓存。
ArtMods talentMods(const Player& player) {
    ArtMods mods = artModsOf(player.arts);
    if (hasTalent(player, "gold")) mods.discardShield += 0.01;
    if (hasTalent(player, "water")) mods.waterMana += 1;
    if (hasTalent(player, "wood")) mods.poisonHeal += 0.005;
    if (hasTalent(player, "fire")) mods.burnExplosion += 0.10;
    if (hasTalent(player, "earth")) mods.shieldStrike += 0.15;
    if (hasTalent(player, "alchemy")) mods.brewYield += 1;
    if (hasTalent(player, "artisan")) {
        mods.forgeSpeed += 0.15;
        mods.craftedSale += 0.15;
    }
    if (hasTalent(player, "lightStep")) mods.escapeSpeed += 6;
    mods.brewYield = clamp(mods.brewYield, 0.0, 3.0);
    mods.forgeSpeed = clamp(mods.forgeSpeed, 0.0, 0.6);
    mods.craftedSale = clamp(mods.craftedSale, 0.0, 0.5);
    mods.escapeSpeed = clamp(mods.escapeSpeed, 0.0, 200.0);
    return mods;
}

// 用途：检查建号天赋之间的互斥组合。
// 参与流程：角色创建校验。
// 使用方式：传待选天赋 ID 列表，检查体质和五行灵根互斥。
// 返回值：错误文案；可共同选择时为空。
string talentConflict(const vector<string>& traits) {
    const auto has = [&](const char* id) {
        return std::find(traits.begin(), traits.end(), string(id)) != traits.end();
    };
    if (has("root") && has("mortal")) return "天灵根与凡骨求道不能同时选择。";
    if (has("daoBody") && has("weak")) return "先天道体与体弱多病不能同时选择。";
    int roots = 0;
    for (const char* id : {"gold", "wood", "water", "fire", "earth"}) roots += has(id) ? 1 : 0;
    if (roots > 1) return "金、木、水、火、土灵根只能选择一种。";
    return "";
}

// 用途：只发放一次建号天赋初始收益。
// 参与流程：新建角色初始化。
// 使用方式：传游戏，talentBirth 标记阻止读档重复发放。
// 返回值：无；修改悟道点、神识、基础功法、已知神通和标记。
void talentBirth(Game& game) {
    if (game.flags.count("talentBirth")) return;
    Player& player = game.player;
    if (hasTalent(player, "immortal")) {
        player.arts.points++;
        game.flags["talentImmortalMajor"] = std::max(0, player.stage / 3);
    }
    if (hasTalent(player, "spiritEyes")) player.arts.mind += 3;
    if (hasTalent(player, "family")) {
        player.arts.manuals["bull"] = std::max(1, player.arts.manuals["bull"]);
        if (std::find(player.arts.knownSpells.begin(), player.arts.knownSpells.end(), "gather") ==
            player.arts.knownSpells.end()) player.arts.knownSpells.push_back("gather");
    }
    game.flags["talentBirth"] = 1;
}

// 用途：保留既有月度天赋接口兼容门面；流程：旧日历调用；返回：无且不改状态。
// 神秘绿瓶改由灵田日tick按格内储备加速，不再按月复制背包草药、扣主背包灵石或写旧标记。
void talentMonthTick(Game&) {}

// 用途：为不灭道心每次新大境界奖励悟道点。
// 参与流程：大境界突破成功后的天赋结算。
// 使用方式：传游戏及是否大突破，标记防止同境重复奖励。
// 返回值：无；增加玩家悟道点并更新已奖励的大境界标记。
void talentBreakthrough(Game& game, bool major) {
    if (!major || !hasTalent(game.player, "immortal") || game.player.stage < 3 ||
        game.player.stage % 3 != 0) return;
    const int group = game.player.stage / 3;
    const auto previous = game.flags.find("talentImmortalMajor");
    if (previous != game.flags.end() && previous->second >= group) return;
    game.player.arts.points++;
    game.flags["talentImmortalMajor"] = group;
}

} // namespace nz
