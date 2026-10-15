#pragma once
#include <array>

namespace nz::crafting {
inline constexpr int GardenRulesVersion = 2;
inline constexpr int GardenMaximumPlantsPerPlot = 12;
inline constexpr int GardenHarvestMultiplier = 3;
inline constexpr std::array<int, 3> GardenNaturalDailyUnits{20, 40, 60};
inline constexpr std::array<int, 6> GardenMaturityUnits{18000, 45000, 112500, 281250, 703125, 1757812};
inline constexpr std::array<int, 7> GreenBottleDailyUnits{0, 45, 85, 150, 250, 420, 700};
inline constexpr std::array<int, 6> GreenBottleUpgradeStones{2000, 5000, 8000, 10000, 20000, 40000};
inline constexpr std::array<int, 3> GreenBottleUpgradeMaterialCounts{50, 100, 200};
}
