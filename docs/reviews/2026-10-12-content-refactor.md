# 稳定业务标识、词表与斗法/功法迁移

本次只修改内容注册表、`battle.cpp`、`arts.cpp`、`artifact.cpp` 与独立测试，未提交、未推送、未切换分支。

## 接口与兼容边界

- `nz::content::DaoId`、`SectId`、`SpellKind`、`BattleKind` 分别覆盖十二大道、五宗门、三十类神通和十类斗法。
- `parseDaoId` / `parseSectId` / `parseSpellKind` / `parseBattleKind` 返回 `std::optional`；未知、空值不会默认变成有效类别。
- `stableId` 提供 ASCII 标识，`legacyDaoKey` / `legacyValue` 提供固定的旧存档键及 wire 值。`fire_burst` 与 `fireBurst`、`sect_trial` 与 `sectTrial` 可以解析为同一类别，但输出旧协议继续采用旧值。
- `daoOrdinal` 固定旧节点与元素加成数组顺序；公开 `ART_DAOS` 由旧键注册表生成。存档中的经验键、专注值、战斗类别与神通类别保留原值。
- `parseArtifactElement` 单独兼容旧法器的“神念”“混元”。旧 NPC 未知亲和采用剑、旧法器未知元素采用气的历史行为，在消费边界明确写出；解析器本身仍返回空结果。
- `displayName` 与旧别名/ID 注册表独立。词表集中神通说明、法器主动描述、回合规则与大道错误文案，展示名称不参与类别匹配。
- `formatText` / `formatTextTemplate` 使用命名参数；缺失、重复、多余或损坏的占位符抛出 `invalid_argument`，插入的参数内容不再解析。

## 实际业务迁移

斗法伤害、相生连击、神通分支、费用、意图归类、NPC 出牌优先级和战斗类别结算改用类型化解析。未知神通不能当作普通攻击试算或意图，未知战斗类别不创建战斗也不消耗 RNG。功法按宗门 ID 选择 NPC 传承；稳定大道 ID 在获得经验、年度参悟和即时专注入口归一为旧中文键。法器元素按 DaoId 选择能力，输出旧神通类型。主要已修改函数中的局部 `g/p/a/b/sp/k` 改为 game/player/arts/battle/spell/spellKind 等语义名称，公开结构字段保持兼容。

## 验证记录

1. 新增 `nztest::runContentRegistryTests`。新增行为测试先链接不可变 fa43732 库，观察稳定 ID、未知值、兼容键及改名语义检查失败，再链接修改后的实现确认通过。全部旧大道键和节点序号、每类稳定 ID 往返、camelCase 别名、未知值、词表参数、目录神通与改名效果、NPC 传承、旧混元法宝均有覆盖。
2. 使用 GCC C++20、`-Wall -Wextra -static` 独立编译新注册表与三个迁移模块，无编译警告。工具环境需将 `C:/msys64/ucrt64/bin` 加入 PATH。
3. 私有回归 runner 同时运行新增测试及既有 `runArtModsTests`、`runBattleStateTests`、`runCombatDesignTests`、`runBattleMechanismTests`、`runMechanicsRegressionTests`、`runArtifactTests`、`runShadowCombatTests`，961 项检查通过、0 项失败，退出码 0（2026-10-12 19:48 Asia/Shanghai）。
4. 基线与重构 oracle 比较五个境界的全神通说明/试算、五宗门 NPC 传承，以及十四元素 × 五法器形状 × 两种阴阳 × 六品阶 × 两种铭刻，共 1,960 行。输出完全一致；两份 SHA256 均为 `764B0755B36B0AA8F3F7F55959404F666075DE288F92CEB711BBD7B6D9F36223`。
5. `git diff --check` 对独占文件通过。全量 CMake/ctest、桌面/V2 契约与最终目录移动由主代理集成验证；以上结果没有宣称替代全量集成测试。

私有验证文件位于已忽略的 `build/content-registry-check/`，不进入源码。验证复用 `build-refactor-baseline/out/libnzcore.a` 及对应测试对象，未重配或修改不可变基线库。

编译与回归命令示例（PowerShell）：

```powershell
$env:PATH = 'C:/msys64/ucrt64/bin;' + $env:PATH
$taskTestDir = 'build-refactor-baseline/out/CMakeFiles/ningzhou_tests.dir/tests'
$taskObjects = @('test_arts.cpp.obj','test_battle_state.cpp.obj','test_combat_design.cpp.obj',
    'test_battle_mechanisms.cpp.obj','test_mechanics_regression.cpp.obj','test_artifact.cpp.obj',
    'test_shadow_combat.cpp.obj') | ForEach-Object { Join-Path $taskTestDir $_ }
& 'C:/msys64/ucrt64/bin/g++.exe' -std=c++20 -Wall -Wextra -static -Isrc -Itests -I. `
    'tests/test_content_registry.cpp' 'build/content-registry-check/regression-main.cpp' `
    'src/core/content/game_ids.cpp' 'src/core/content/text_table.cpp' `
    'src/core/combat/battle.cpp' 'src/core/progression/arts.cpp' 'src/core/combat/artifact.cpp' `
    $taskObjects 'build-refactor-baseline/out/libnzcore.a' `
    -o 'build/content-registry-check/regression.exe'
& 'build/content-registry-check/regression.exe'
```
