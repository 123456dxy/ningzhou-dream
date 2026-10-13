#include "harness.h"

#include "core/dungeon.h"
#include "core/save.h"
#include "core/sim.h"

#include <algorithm>
#include <array>

namespace nztest {
namespace {
// 用途：建立炼气天机阁隔离夹具；流程：情报购买真实回归；返回：不关联玩家服务或存档的Game快照。
nz::Game mfIntelPurchaseGame() {
    nz::CreateOptions Options; Options.seed = 2437; Options.traitsExplicit = true;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear();
    State.player.stage = 0; State.player.location = "tianji"; State.player.stones = 100000;
    return State;
}
}

// 用途：验证购买不设境界门槛但进入保持门槛；流程：纯目录/真实扣款/存读档；返回：无，所有失败计入测试计数。
void mfRunIntelPurchaseTests() {
    using namespace nz;
    std::printf("\n--- 天机阁情报提前购买与探索门槛 ---\n");
    const Game Base = mfIntelPurchaseGame();
    const string BeforeOptions = saveToJson(Base);
    const auto Options = dungeonIntelOptions(Base);
    check(saveToJson(Base) == BeforeOptions, "炼气天机阁情报目录只读不改日期/RNG/余额");
    std::array<bool, 4> FoundHerbGrades{};
    std::array<bool, 3> FoundMaterialGrades{};
    for (const auto& Option : Options) {
        check(Option.grade >= 3, "普通一二阶材料不会因取消购买境界门槛混入情报目录");
        check(Option.enabled && Option.reason.empty(), "炼气可预先购买目录中高阶情报：" + Option.id);
        checkEq(Option.cost, 15 + Option.grade * Option.grade * 6, "情报品阶价格保持既有规则");
        if (Option.kind == "herb" && Option.grade <= 6) FoundHerbGrades[Option.grade - 3] = true;
        if (Option.kind == "material" && Option.grade <= 5) FoundMaterialGrades[Option.grade - 3] = true;
        Game State = Base;
        const int Stones = State.player.stones;
        const auto RngBefore = State.rng;
        const auto SitesBefore = State.dungeons.sites.size();
        const string Error = dungeonIntel(State, Option.kind, Option.id);
        check(Error.empty(), "炼气真实购买高阶情报成功：" + Option.id);
        if (!Error.empty()) continue;
        checkEq(State.player.stones, Stones - Option.cost, "实际情报扣款等于当前纯报价");
        checkEq(State.dungeons.intelPurchases, 1, "真实购买占用一份月度额度");
        check(State.rng == RngBefore && State.clock.days == Base.clock.days,
              "生成位置使用局部种子，不推进活动RNG或游戏日期");
        checkEq(static_cast<int>(State.dungeons.sites.size()), static_cast<int>(SitesBefore + 1),
                "购买情报只增加一个真实秘境入口");
        const auto Site = std::find_if(State.dungeons.sites.begin(), State.dungeons.sites.end(), [&](const DungeonSite& Entry) {
            return Entry.type == "intel" && Entry.targetKind == Option.kind && Entry.targetId == Option.id;
        });
        check(Site != State.dungeons.sites.end(), "提前购买保留指定材料目标");
        if (Site == State.dungeons.sites.end()) continue;
        const string SiteId = Site->id;
        const string Location = Site->location;
        const int Stage = Site->stage;
        checkEq(Stage, Option.stage, "炼气购买入口按材料最低探索境界冻结难度");
        checkEq(Site->expiresYear - Site->openedYear, 5, "提前情报仍五年过期");
        check(dungeonStateError(State).empty(), "低境界持有高阶情报仍是合法可保存状态");
        Game Restored; string LoadError;
        check(loadFromJson(saveToJson(State), Restored, &LoadError), "提前购买高阶情报真实可保存读回：" + LoadError);
        const string Once = saveToJson(State);
        check(!dungeonIntel(State, Option.kind, Option.id).empty(), "取消境界门槛不取消每月一份限制");
        check(saveToJson(State) == Once, "同月重复购买失败不再次扣钱/加入口");
        State.player.location = Location;
        const string AtEntrance = saveToJson(State);
        if (Stage > State.player.stage) {
            check(!dungeonEnter(State, SiteId).empty(), "炼气虽已购买仍不能进入尚未达到用途阶段的秘境");
            check(saveToJson(State) == AtEntrance, "境界不足的入场拒绝不改变状态");
        }
        State.player.stage = Stage;
        check(dungeonEnter(State, SiteId).empty(), "到达入口且满足原境界后仍可正常进入");
    }
    check(std::all_of(FoundHerbGrades.begin(), FoundHerbGrades.end(), [](bool Found) { return Found; }), "目录覆盖三四五六阶药材情报");
    check(std::all_of(FoundMaterialGrades.begin(), FoundMaterialGrades.end(), [](bool Found) { return Found; }), "目录覆盖所有现有三四五阶炼器材料情报");
    if (Options.empty()) return;
    const auto& Target = Options.front();
    for (const string Blocker : {"location", "funds", "job", "pending", "run"}) {
        Game State = Base;
        if (Blocker == "location") State.player.location = "wuling";
        if (Blocker == "funds") State.player.stones = Target.cost - 1;
        if (Blocker == "job") State.job = Job{};
        if (Blocker == "pending") State.pending = Pending{};
        if (Blocker == "run") State.dungeons.run = DungeonRun{};
        const string Before = saveToJson(State);
        const auto BlockedOptions = dungeonIntelOptions(State);
        const auto Match = std::find_if(BlockedOptions.begin(), BlockedOptions.end(), [&](const DungeonIntelOption& Entry) {
            return Entry.kind == Target.kind && Entry.id == Target.id;
        });
        check(Match != BlockedOptions.end() && !Match->enabled && !Match->reason.empty(), Blocker + "仍阻断情报购买预览");
        check(!dungeonIntel(State, Target.kind, Target.id).empty(), Blocker + "仍阻断真实情报购买");
        check(saveToJson(State) == Before, Blocker + "拒绝购买不改变余额/序号/日期/存档");
    }
    {
        Game State = Base;
        check(dungeonIntel(State, Target.kind, Target.id).empty(), "次月刷新测试首次真实购买");
        State.clock.days += 30;
        check(dungeonIntel(State, Target.kind, Target.id).empty(), "进入下一月可以重新购买一份情报");
        checkEq(State.dungeons.intelPurchases, 1, "新月次数从零累计到一而非无限放行");
    }
}
} // namespace nztest
