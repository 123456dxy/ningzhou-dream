// 自测总入口：汇总各模块检查项。
#include <cstdio>

#include "harness.h"

int main() {
    std::printf("===== 宁州一梦 C++ 自测 =====\n");

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
    nztest::runCraftTests();
    nztest::runSocietyTests();
    nztest::runPlayTests();
    nztest::runEquipmentAndTaskTests();
    nztest::runFriendshipTests();
    nztest::runStateIntegrityTests();
    nztest::runBattleStateTests();
    nztest::runCombatDesignTests();
    nztest::runBattleMechanismTests();
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

    const int f = nztest::failures();
    std::printf("\n===== 结果: %s（%d 项失败）=====\n",
                f == 0 ? "全部通过" : "有失败", f);
    return f == 0 ? 0 : 1;
}
