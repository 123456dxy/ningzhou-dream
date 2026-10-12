#include "core/reward_receipt.h"

#include <algorithm>

#include "core/arts.h"
#include "data/reference_tables.h"

namespace nz {
namespace {

// 用途：合并两套抄本书单用于奖励差异比较。
// 参与流程：行动前后实际收益核算。
// 使用方式：传入const Game& game，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
vector<string> ownedBooks(const Game& game) {
    vector<string> books = game.economy.books;
    auto add = [&](const string& id) {
        if (std::find(books.begin(), books.end(), id) == books.end()) books.push_back(id);
    };
    for (const auto& id : game.player.arts.bookList) add(id);
    for (const auto& [id, level] : game.player.arts.manuals) if (level > 0) add(id);
    return books;
}

// 用途：只读查询数量映射且不插入缺失键。
// 参与流程：行动前后实际收益核算。
// 使用方式：传入const map<string, int>& values, const string& id，按本模块入口调用。
// 返回值：function valueOf() { [native code] }。
int valueOf(const map<string, int>& values, const string& id) {
    const auto found = values.find(id);
    return found == values.end() ? 0 : found->second;
}

// 用途：由器材ID解析中文名称。
// 参与流程：行动前后实际收益核算。
// 使用方式：传入const string& id，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string materialName(const string& id) {
    for (const auto& material : data::MaterialDef_TABLE())
        if (material.id == id) return material.name;
    return id;
}

// 用途：由丹药ID解析中文名称。
// 参与流程：行动前后实际收益核算。
// 使用方式：传入const string& id，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string potionName(const string& id) {
    for (const auto& recipe : data::RecipeDef_TABLE())
        if (recipe.id == id) return recipe.name;
    return id;
}

} // namespace

RewardReceipt::RewardReceipt(const Game& game)
    : player(game.player), contribution(game.economy.contribution), books(ownedBooks(game)) {}

// 用途：比较动作前后实际资源成长与传承变化。
// 参与流程：行动前后实际收益核算。
// 使用方式：传入const Game& game，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string RewardReceipt::changes(const Game& game) const {
    vector<string> parts;
    auto change = [&](const string& name, int before, int after) {
        const int delta = after - before;
        if (delta != 0) parts.push_back(name + (delta > 0 ? "+" : "−") + std::to_string(std::abs(delta)));
    };
    const auto& now = game.player;
    change("灵石", player.stones, now.stones);
    change("气血", player.health, now.health);
    change("资质", player.apt, now.apt);
    change("悟性", player.insight, now.insight);
    change("福缘", player.luck, now.luck);
    change("声望", player.renown, now.renown);
    change("丹术", player.alchemy, now.alchemy);
    change("心境", player.arts.heart, now.arts.heart);
    change("悟道点", player.arts.points, now.arts.points);
    change("宗门贡献", contribution, game.economy.contribution);
    for (const char* dao : ART_DAOS)
        change(string(dao) + "道经验", valueOf(player.arts.exp, dao), valueOf(now.arts.exp, dao));
    for (const auto& [id, count] : now.arts.materials)
        change(materialName(id), valueOf(player.arts.materials, id), count);
    int oldTyped = 0, newTyped = 0;
    for (const auto& [name, count] : player.arts.herbs) oldTyped += count;
    for (const auto& [name, count] : now.arts.herbs) {
        newTyped += count;
        change(name, valueOf(player.arts.herbs, name), count);
    }
    change("未分类灵药", std::max(0, player.herbs - oldTyped), std::max(0, now.herbs - newTyped));
    for (const auto& [id, count] : now.arts.potions)
        change(potionName(id), valueOf(player.arts.potions, id), count);
    for (const auto& id : ownedBooks(game)) {
        if (std::find(books.begin(), books.end(), id) != books.end()) continue;
        ManualInfo manual;
        if (findManualInfo(id, manual)) parts.push_back("获得「" + manual.name + "」功法抄本（需修习）");
    }
    for (const auto& id : now.arts.knownSpells) {
        if (std::find(player.arts.knownSpells.begin(), player.arts.knownSpells.end(), id) != player.arts.knownSpells.end()) continue;
        for (const auto& spell : data::SpellDef_TABLE())
            if (spell.id == id) parts.push_back("学会神通「" + spell.name + "」");
    }
    for (const auto& gear : now.arts.gear) {
        const auto existing = std::find_if(player.arts.gear.begin(), player.arts.gear.end(), [&](const Gear& previous) { return previous.id == gear.id; });
        if (existing == player.arts.gear.end()) parts.push_back("获得法器「" + gear.name + "」");
    }
    for (const auto& npc : game.npcs) {
        const int delta = valueOf(now.relations, npc.id) - valueOf(player.relations, npc.id);
        if (delta != 0) parts.push_back(npc.name + "交情" + (delta > 0 ? "+" : "−") + std::to_string(std::abs(delta)));
    }
    if (parts.empty()) return "未新增物品或属性收益";
    string result;
    for (const auto& part : parts) {
        if (!result.empty()) result += "；";
        result += part;
    }
    return result;
}

} // namespace nz
