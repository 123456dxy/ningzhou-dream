#include "core/talents.h"

#include <algorithm>
#include <limits>

#include "core/sim.h"
#include "core/source_limits.h"

namespace nz {

double talentCultivationMul(const Player& player) {
    return hasTalent(player, "daoBody") ? 1.10 : 1.0;
}

int talentManaBonus(const Player& player) {
    return hasTalent(player, "daoBody") && player.stage >= 3 ? 2 : 0;
}

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

void talentMonthTick(Game& game) {
    Player& player = game.player;
    if (!hasTalent(player, "greenBottle") || game.ended || player.health <= 0 ||
        player.age >= player.lifespan || game.dungeons.run || player.arts.battle ||
        game.pending || player.stones < 5 || player.herbs >= std::numeric_limits<int>::max()) return;
    const int month = game.clock.days / 30;
    if (month <= 0) return;
    const auto previous = game.flags.find("greenBottleMonth");
    if (previous != game.flags.end() && previous->second >= month) return;
    for (auto& [name, count] : player.arts.herbs) {
        const auto* herb = findHerbByName(name);
        if (!herb || count <= 0 || count >= std::numeric_limits<int>::max() ||
            herb->grade < 1 || herb->grade > ordinaryHerbGradeLimit(player) ||
            herb->name.find("妖丹") != string::npos) continue;
        ++count; ++player.herbs; player.stones -= 5;
        game.flags["greenBottleMonth"] = month;
        logEvent(game, "self", "神秘绿瓶耗费5灵石，催熟「" + name + "」一份。", true);
        return;
    }
}

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
