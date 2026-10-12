# 存档模块重构验证

基线：`fa43732`。公开 `src/core/save.h` 的函数签名与 `SAVE_FORMAT` 保持不变。代码未提交、未推送；本工作只修改 `src/core/save.cpp`、新增 `src/core/persistence/` 与 `tests/test_persistence_refactor.cpp`。

## 模块边界

| 文件 | 责任 |
| --- | --- |
| `save.cpp` | 公开 JSON 入口；大小/格式检查、结构验证、解码、迁移、状态验证、完成恢复、最后原子赋值 |
| `persistence/internal.h` | 私有跨翻译单元声明；业务调用方继续使用 `save.h` |
| `persistence/json_helpers.h` | 数值、字符串、数值/字符串 map、成就数组和字符串数组小 codec |
| `person_codec.cpp` | 玩家/NPC 共用基础属性 |
| `player_codec.cpp` | 玩家构筑、天赋、NPC 交往、洞府配给、成就与 Arts 聚合 |
| `npc_codec.cpp` | NPC 历史、背包、装备、道途和神通 |
| `arts_codec.cpp` | 玩家功法、道法、物品、法器、配给与在途斗法聚合 |
| `battle_codec.cpp` | 双方牌组、资源、计划、冷却与不可变真人影录 |
| `dungeon_codec.cpp` | 秘境世界、路线、临时背包与永久奖励实取记录 |
| `game_codec.cpp` | 顶层世界、日历、营生、人情、兽潮、工作、抉择和纪事聚合 |
| `shape_validation.cpp` | 解码前数值/字段类型校验；递归影录在解码前拒绝 |
| `state_validation.cpp` | 跨字段业务一致性与引用校验 |
| `migrations.cpp` | 历史版本迁移、旧结局摘要和普通加载初始化；detached 不初始化世界 |
| `file_io.cpp` | UTF-8 路径、临时文件原子替换、多人目录覆盖、四个槽位与自动档摘要 |

原 `save.cpp` 2164 行变为约 68 行。各新翻译单元分别编译，没有包含 `.cpp` 的做法，也没有保留旧文件的重复实现。

## 保持的兼容契约

- JSON 字段名、格式版本、`combatRulesVersion`、中文道途/经验键和 wire 值保持原样。
- Person、Player、NPC、Arts、Battle、Dungeon 的历史字段默认值和迁移顺序保留。
- 无符号 32 位 RNG、种子、日历与小数时间余量保持；编码继续使用原来的 `dump(1)` 排版。
- 普通角色导入继续拒绝天机影录斗法；detached 使用同一结构和状态校验，不生成 NPC/秘境、不消耗 RNG、不重写冻结构筑。
- 损坏输入只在临时 `Game` 中解码/迁移，成功后才移动赋值给调用方。
- `setSaveDirOverride` 的多人账号目录语义、自动第四槽位、Windows `MoveFileExW` 替换以及失败保留旧文件逻辑均保留。

## 实际验证

PowerShell 工作目录为项目根目录；运行时将 `C:/msys64/ucrt64/bin` 加到 PATH。基线静态库是 root 在不可变源快照构建的 `build-refactor-baseline/out/libnzcore.a`，未重新配置或修改该快照。

### 新增回归基线

先在重构前写好回归，链接 `fa43732` 静态库：初始 36 项通过。扩大中文道途、未知地点和写失败覆盖后再次链接基线：44 项通过、进程 exit 0。

```powershell
& 'C:/msys64/ucrt64/bin/g++.exe' -std=c++20 -O0 -finput-charset=UTF-8 -fexec-charset=UTF-8 -DNZ_PERSISTENCE_REFACTOR_TEST_MAIN -I src -I . tests/test_persistence_refactor.cpp build-refactor-baseline/out/libnzcore.a -static -o build/persistence_characterization.exe
& './build/persistence_characterization.exe'
```

日志：`build/persistence_characterization.log`。

### 新模块回归

直接链接新存档入口及全部新 codec/validation/storage 与原基线规则库，隔离其他并行重构：44 项通过、exit 0。

```powershell
& 'C:/msys64/ucrt64/bin/g++.exe' -std=c++20 -O0 -finput-charset=UTF-8 -fexec-charset=UTF-8 -DNZ_PERSISTENCE_REFACTOR_TEST_MAIN -I src -I . tests/test_persistence_refactor.cpp src/core/save.cpp src/core/persistence/*.cpp build-refactor-baseline/out/libnzcore.a -static -o build/persistence_refactor_tests.exe
& './build/persistence_refactor_tests.exe'
```

日志：`build/persistence_refactor_tests.log`。覆盖 RNG/clock、在途 PvE、真实在途秘境、冻结构筑与真人影录、源存档不变、旧 schema 缺字段迁移、中文道途/经验/NPC 值、非法数值/形状/地点/递归影录原子拒绝、账号目录隔离、自动第四槽位及临时文件不可写时保留旧文件。

### 既有相关套件

临时 runner 组合 `runSaveTests`、`runStateIntegrityTests`、`runBattleStateTests`、`runDungeonTests`、`runCheckpointTests`、`runShadowCombatTests` 和新增入口。它为 checkpoint 的真实 API 部分绑定 `started=true` 的独立 `api::Session`，最终 runner 将存档目录覆盖到唯一临时目录。

```powershell
& 'C:/msys64/ucrt64/bin/g++.exe' -std=c++20 -O0 -finput-charset=UTF-8 -fexec-charset=UTF-8 -I src -I tests -I . 'C:/Users/24974/AppData/Local/Temp/nz_persistence_suite.cpp' tests/test_save.cpp tests/test_state_integrity.cpp tests/test_battle_state.cpp tests/test_dungeon.cpp tests/test_checkpoint.cpp tests/test_shadow_combat.cpp tests/test_persistence_refactor.cpp src/core/save.cpp src/core/persistence/*.cpp build-refactor-baseline/out/libnzcore.a -static -o build/persistence_suite.exe
& './build/persistence_suite.exe'
```

结果：806 项通过，`PERSISTENCE_SUITE_FAILURES=0`。日志：`build/persistence_suite.log`。

第一次临时 runner 没有启动 API session，checkpoint API 部分出现 18 项失败；同一 runner 链接纯 `fa43732` 时出现相同 18 项失败。源码明确要求 checkpoint API 测试在 `runApiTests` 之后运行。修正独立 runner 的会话初始化后全部通过；没有为此修改生产代码或既有测试。

### 路径与天机诊断

root 完整基线 CTest 曾报告中文路径测试与天机首次报名失败。本工作单独用基线库测试同类调用：中文临时文件 `save=1`、原子 `replace=1`、`load=1`，Win32 错误 0，替换后的灵石 110；天机 fixture 的 `freezePlayer=1`、首次报名 `status=200`、`phase=waiting`。日志为 `build/tianji_probe.log`，中文路径探针可执行文件为 `build/save_path_probe.exe`。

之后按 root 要求，在 ignored build 中复制基线 `test_tianji.cpp`/`tianji.cpp` 加诊断，链接原基线 main 与其余测试对象，保持完整调用顺序，未修改基线源码或生产文件。首次报名确切返回 `503 storage`。失败在 `create_directories`，还未到达 fstream 或原子改名；JSON 只有 2953 bytes。即时捕获结果为 `ec=13`、`errno=13`、Win32 `GetLastError=5`；临时父目录存在，同一父目录的独立 `CreateDirectoryW` 对照探针也失败、错误 5。记录的是 Windows 原生目录创建被拒绝，不能据此改动存档业务逻辑。

诊断运行命令：`build-refactor-baseline/out/ningzhou_tests_io_diagnostic.exe`。日志为 `build-refactor-baseline/out/full_suite_io_diagnostic.err` 与同名 `.log`。该程序沿用原测试在报名失败后仍 rename 的流程，所以随后因 `store.json` 不存在而终止；此结果不计为通过。root 负责 MSVC/Debug 对照与整体运行权限调查。

### MSVC 完整自测的环境对照

20:07 开始运行已于 19:59:52 构建的 `build-vs2022/Debug/ningzhou_tests.exe`。只在运行该程序的 PowerShell 过程中设置 TEMP/TMP，目标是 workspace 内 `build-backend-desktop/qa-temp`。通过 `GetShortPathNameW` 从真实目录取得 ASCII 8.3 路径 `D:\Users\desktop\QQ-DEE~1\NINGZH~1\BU4A4F~1\qa-temp`；原生目录创建探针成功，Win32 错误 0。未修改全局环境或正在运行的程序。

```powershell
& './build-backend-desktop/run-msvc-workspace-temp.ps1'
```

20:21 完成：**5746 项通过、0 项失败、exit 0**，耗时 806.06 秒，stderr 为空。中文存档 IO、天机大比、影录、新增 44 项存档回归及其他模块完整通过。日志：`build-backend-desktop/msvc-workspace-temp-fullsuite.log`、`.err`。

此运行验证的是 19:59 构建的重构程序，用于隔离运行目录权限问题；root 后续新增的 Windows CTest 临时目录配置及 UTF-8/native 路径改动，应以 root 后续重新构建的结果为准。

### 编译与差异检查

```powershell
& 'C:/msys64/ucrt64/bin/g++.exe' -std=c++20 -finput-charset=UTF-8 -fexec-charset=UTF-8 -I src -I . -Wall -Wextra -fsyntax-only src/core/save.cpp src/core/persistence/*.cpp
git diff --check -- src/core/save.cpp tests/test_persistence_refactor.cpp src/core/persistence
```

结果：exit 0，无编译错误或空白错误。GCC 16 报告原有 `std::filesystem::u8path` 弃用警告；为了保持原 UTF-8 路径行为，本次未改该策略。

本报告只声明存档隔离验证结果；CLI/Web/MSVC、完整仓库 CTest 和前端协议检查由 root 的整体交付报告给出。
