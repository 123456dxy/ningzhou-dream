// 宁州一梦 · C++ 版 — 自测框架
//
// 把各模块的测试函数汇总到一处，由 tests/test_main.cpp 统一调用。
// 每个模块导出 runXxxTests()，并用 harness 里的计数判断成败。
#pragma once

#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace nztest {

// 全局失败计数
inline int& failures() {
    static int n = 0;
    return n;
}

inline void check(bool ok, const std::string& what) {
    std::printf("  [%s] %s\n", ok ? "通过" : "失败", what.c_str());
    if (!ok) ++failures();
}

inline void checkEq(long long got, long long want, const std::string& what) {
    const bool ok = (got == want);
    std::printf("  [%s] %s (得到 %lld，期望 %lld)\n",
                ok ? "通过" : "失败", what.c_str(), got, want);
    if (!ok) ++failures();
}

inline void checkNear(double got, double want, const std::string& what,
                      double eps = 1e-6) {
    const bool ok = std::fabs(got - want) < eps;
    std::printf("  [%s] %s (得到 %.6f，期望 %.6f)\n",
                ok ? "通过" : "失败", what.c_str(), got, want);
    if (!ok) ++failures();
}

inline void checkStr(const std::string& got, const std::string& want,
                     const std::string& what) {
    const bool ok = (got == want);
    std::printf("  [%s] %s", ok ? "通过" : "失败", what.c_str());
    if (!ok) std::printf(" (得到「%s」，期望「%s」)", got.c_str(), want.c_str());
    std::printf("\n");
    if (!ok) ++failures();
}

// 各模块测试入口
void runDataTests();
void runNpcCardTests();
void runPassiveLifeTravelTests();
void runSocialIntegrityTests();
void runNpcLearningLocationTests();
void runRuleTests();
void runArtModsTests();
void runCalendarTests();
void runNameTests();
void runCardTests();
void runSwordTests();
void runAdventureTests();
void runSaveTests();
void runCraftTests();
void runSocietyTests();
void runPlayTests();   // 坊市买卖、NPC 往来、炼丹
void runEquipmentAndTaskTests();
void runFriendshipTests();   // 道友往来：好感/交情、NPC 背包、送装备换装
void runApiTests();
void runTravelTests();
void runStateIntegrityTests();
void runBattleStateTests();
void runCombatDesignTests();
void runBattleMechanismTests();
void runMechanicsRegressionTests();
void runDungeonTests();
void runMaterialSourceTests();
void runCheckpointTests();
void runTalentTests();
void runSectTrialTests();
void runArtifactTests();
void runRenownArtifactApiTests();
void runRewardApiTests();
void runEventRewardTests();
void runNumericPreviewTests();
void runDisplayContractTests();
void runSectCompletionTests();
void runTianjiTests();
void runShadowCombatTests();
void runContentRegistryTests();
void runApiRefactorTests();
void runPersistenceRefactorTests();
void runCombatFixTests();
void runPlayerFixTests();
void runCraftingFixTests();
void runCatalogDisplayTests();
void runTideRuleTests();
void runTideIntegrationTests();
void runTideApiTests();

void runCharacterBuildTests();
void runAlchemyEconomyTests();
void runStudyNpcTests();
void mfRunCraftPriceTests();
void mfRunCraftTimingTests();
void mfRunResourcePointTests();
void mfRunCaveGardenTests();
void mfRunCaveApiTests();
void mfRunBattleScalingTests();
void mfRunCombatHealthTests();
void mfRunIntelPurchaseTests();
void mfRunPillEffectsTests();
void mfRunHerbAcquisitionTests();
void mfRunTimePerformanceTests();

} // namespace nztest
