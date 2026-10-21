// 自测总入口：汇总各模块检查项。
#include <cstdio>
#include <exception>

#include "harness.h"

int main(int ArgumentCount, char** Arguments) try {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("===== 宁州一梦 C++ 自测 =====\n");
    if (ArgumentCount == 2 && std::string(Arguments[1]) == "--gameplay-update") {
        nztest::mfRunPillBatchTests();
        nztest::mfRunAlchemyQuoteTests();
        nztest::runTianjiStewardTests();
        nztest::mfRunAscensionTalentCatalogTests();
        nztest::mfRunAscensionTournamentTests();
        nztest::mfRunGameplayTemplateTests();
        nztest::mfRunLifeboundSlotTests();
        return nztest::failures() == 0 ? 0 : 1;
    }
    if (ArgumentCount == 2 && std::string(Arguments[1]) == "--poison-siphon") {
        nztest::mfRunPoisonSiphonTests();
        return nztest::failures() == 0 ? 0 : 1;
    }
    if (ArgumentCount == 2 && std::string(Arguments[1]) == "--water-flow") {
        nztest::mfRunSoftwaterRefillTests();
        nztest::mfRunWaterFlowTests();
        nztest::mfRunWaterFlowIntegrationTests();
        nztest::mfRunWaterFlowEdgeTests();
        nztest::mfRunWaterVitalityTests();
        nztest::mfRunWaterVitalityIntegrationTests();
        nztest::mfRunWaterVitalityEdgeTests();
        nztest::mfRunNpcPlanRecoveryTests();
        return nztest::failures() == 0 ? 0 : 1;
    }
    if (ArgumentCount == 2 && std::string(Arguments[1]) == "--permanent-poison") {
        nztest::mfRunPermanentPoisonTests();
        return nztest::failures() == 0 ? 0 : 1;
    }
    if (ArgumentCount == 2 && std::string(Arguments[1]) == "--portraits") {
        nztest::runCharacterBuildTests();
        return nztest::failures() == 0 ? 0 : 1;
    }
    if (ArgumentCount == 2 && std::string(Arguments[1]) == "--three-changes") {
        nztest::mfRunAbsoluteHealthTests();
        nztest::mfRunHealthMigrationTests();
        nztest::mfRunPillRevisionTests();
        nztest::mfRunZhujiTests();
        nztest::mfRunLifeboundCardTests();
        return nztest::failures() == 0 ? 0 : 1;
    }

    if (ArgumentCount == 2 && std::string(Arguments[1]) == "--forge") {
        nztest::mfRunForgeApiTests();
        nztest::mfRunForgeArtifactTests();
        nztest::mfRunForgeRedesignTests();
        nztest::mfRunArtifactFixedValueTests();
        return nztest::failures() == 0 ? 0 : 1;
    }

    nztest::mfRunPillBatchTests();
    nztest::mfRunAlchemyQuoteTests();
    nztest::runTianjiStewardTests();
    nztest::mfRunAscensionTalentCatalogTests();
    nztest::mfRunAscensionTournamentTests();
    nztest::mfRunGameplayTemplateTests();
    nztest::mfRunLifeboundSlotTests();
    nztest::mfRunForgeApiTests();
    nztest::mfRunForgeArtifactTests();
    nztest::mfRunForgeRedesignTests();
    nztest::mfRunArtifactFixedValueTests();
    nztest::runDataTests();
    nztest::runNpcCardTests();
    nztest::runPassiveLifeTravelTests();
    nztest::runSocialIntegrityTests();
    nztest::runNpcLearningLocationTests();
    nztest::runRuleTests();
    nztest::runArtModsTests();
    nztest::runNameTests();
    nztest::runCalendarTests();
    nztest::runCardTests();
    nztest::runSwordTests();
    nztest::runAdventureTests();
    nztest::runSaveTests();
    nztest::mfRunHealthMigrationTests();
    nztest::mfRunZhujiTests();
    nztest::mfRunAbsoluteHealthTests();
    nztest::mfRunPillRevisionTests();
    nztest::mfRunLifeboundCardTests();
    nztest::runCraftTests();
    nztest::runSocietyTests();
    nztest::runPlayTests();
    nztest::runEquipmentAndTaskTests();
    nztest::runFriendshipTests();
    nztest::runStateIntegrityTests();
    nztest::runBattleStateTests();
    nztest::runCombatDesignTests();
    nztest::runBattleMechanismTests();
    nztest::mfRunBattleLabTests();
    nztest::mfRunHandCapacityTests();
    nztest::mfRunElementalEnhancementTests();
    nztest::mfRunCombatBalanceIterationTests();
    nztest::mfRunPermanentPoisonTests();
    nztest::mfRunXuandaoSectTests();
    nztest::mfRunPoisonSiphonTests();
    nztest::mfRunWaterFlowTests();
    nztest::mfRunWaterFlowIntegrationTests();
    nztest::mfRunWaterFlowEdgeTests();
    nztest::mfRunWaterVitalityTests();
    nztest::mfRunWaterVitalityIntegrationTests();
    nztest::mfRunWaterVitalityEdgeTests();
    nztest::mfRunNpcPlanRecoveryTests();
    nztest::mfRunSoftwaterRefillTests();
    nztest::mfRunMarketCycleTests();
    nztest::mfRunGreenBottleTests();
    nztest::mfRunFireGrowthTests();
    nztest::mfRunBeastActionTests();
    nztest::mfRunBeastLootTests();
    nztest::mfRunNpcShuffleContinuationTests();
    nztest::mfRunDiscardTimingTests();
    nztest::mfRunDungeonSweepTests();
    nztest::mfRunDungeonEventTests();
    nztest::mfRunDungeonCardPreferenceTests();
    nztest::mfRunDungeonEncounterTests();
    nztest::mfRunBeastPortraitTests();
    nztest::mfRunCultivationUnitTests();
    nztest::mfRunDungeonRewardScalingTests();
    nztest::runMechanicsRegressionTests();
    nztest::runApiTests();
    nztest::runDungeonTests();
    nztest::runMaterialSourceTests();
    nztest::runCheckpointTests();
    nztest::runTalentTests();
    nztest::runSectTrialTests();
    nztest::runTravelTests();
    nztest::runArtifactTests();
    nztest::runRenownArtifactApiTests();
    nztest::runRewardApiTests();
    nztest::runEventRewardTests();
    nztest::runNumericPreviewTests();
    nztest::runDisplayContractTests();
    nztest::runSectCompletionTests();
    nztest::runTianjiTests();
    nztest::runShadowCombatTests();
    nztest::runContentRegistryTests();
    nztest::runApiRefactorTests();
    nztest::runPersistenceRefactorTests();
    nztest::runCombatFixTests();
    nztest::runPlayerFixTests();
    nztest::runCraftingFixTests();
    nztest::runCatalogDisplayTests();
    nztest::runTideRuleTests();
    nztest::runTideIntegrationTests();
    nztest::runTideApiTests();
    nztest::mfRunTideStockRewardTests();
    nztest::mfRunTideEnrollmentTests();
    nztest::mfRunDungeonTideTransitionTests();
    nztest::mfRunServiceConvenienceTests();
    nztest::mfRunTradeQuickActionsApiTests();
    nztest::mfRunTideMaxPurchaseTests();
    nztest::mfRunPotionBulkSalesTests();

    nztest::runCharacterBuildTests();
    nztest::runAlchemyEconomyTests();
    nztest::runStudyNpcTests();
    nztest::mfRunCraftPriceTests();
    nztest::mfRunCraftTimingTests();
    nztest::mfRunResourcePointTests();
    nztest::mfRunCaveGardenTests();
    nztest::mfRunCaveApiTests();
    nztest::mfRunBattleScalingTests();
    nztest::mfRunCombatHealthTests();
    nztest::mfRunIntelPurchaseTests();
    nztest::mfRunPillEffectsTests();
    nztest::mfRunHerbAcquisitionTests();
    nztest::mfRunTimePerformanceTests();

    const int f = nztest::failures();
    std::printf("\n===== 结果: %s（%d 项失败）=====\n",
                f == 0 ? "全部通过" : "有失败", f);
    return f == 0 ? 0 : 1;
} catch (const std::exception& Error) {
    std::fprintf(stderr, "自测异常：%s\n", Error.what());
    return 2;
}
