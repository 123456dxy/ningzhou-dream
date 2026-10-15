# content · 标识与文案

## 定位与职责

把部分核心业务类别的稳定标识、旧值兼容和显示文案集中管理，使规则判断不依赖可修改的中文描述。当前范围包括大道、宗门、神通类别、战斗类别，以及神通和法器等文案。静态配方、剧情等表仍在 `data/reference_tables.h`，共享模型也仍有静态表；content 尚未接管全部游戏内容。

## 主要文件与入口

| 文件 | 职责 |
| --- | --- |
| `game_ids.h` / `game_ids.cpp` | `DaoId`、`SectId`、`SpellKind`、`BattleKind`，枚举目录、解析、稳定值和兼容值 |
| `text_table.h` / `text_table.cpp` | `TextId`、显示名、严格的命名参数格式化 |
| `npc_combat_profiles.*` | 六宗完整传承模板、稳定流派关联和常态世界传承者初始化 |
| `advanced_builds.*` | 八种进阶构筑目录；六宗数据直接派生自 NPC 模板，剑/神识保留玩家建议 |

公共头相对 core 根为 `content/game_ids.h`、`content/text_table.h`。入口为 `parseDaoId` / `parseSectId` / `parseSpellKind` / `parseBattleKind`、`stableId`、`legacyDaoKey` / `legacyValue`、`displayName`、`text` 和 `formatText`。

## 数据流与依赖边界

来自 HTTP、存档或历史表的字符串在边界解析为枚举，规则使用枚举计算，输出时选择稳定 ASCII ID、旧协议值或显示名。文案通过 `TextId` 取模板，再用 `TextArg` 填入具名参数。实现主要依赖标准库，不反向依赖 `Game`、API 会话或持久化。

combat、progression、social 和 interface 按需使用本模块。`legacyDaoKey` 等函数是兼容边界，不能从显示名称反推存档键。现存字符串仍包括协议字段、物品/剧情/任务 ID 和未迁移的业务键；本模块不承诺清除全部字符串。

## 维护注意

- 新增枚举值应同时检查枚举目录、解析、稳定 ID、旧值转换、显示名及调用处的未知值处理。
- `parse...` 返回 `optional`；未知输入不能悄悄成为合法默认类别。
- `formatText` / `formatTextTemplate` 对缺失、重复、未使用、畸形参数抛出 `invalid_argument`；模板和参数应成对修改。
- 显示文字可调整，稳定 ID 和兼容值变更需要明确迁移。
- `SectId::Xuandao` 追加在既有枚举之后，稳定 ID 为 `xuandao`、主传承为阵道。`mfAdvancedBuilds` 沿用八个初始流派 ID，不附加隐藏伤害；测试/装备入口仍按真实境界、辅修与神通槽位裁剪。
- `mfEnsureSectTransmissions` 只在建角、年度动作和缺第六宗的旧档初始化执行，不能在只读目录查询中调用。六宗至少一名活跃传承者来自既有人物；宗门无人时才补一名确定性的元婴执教。肖像复用已有白名单文件。

## 已有测试

`tests/test_content_registry.cpp` 覆盖 ID/解析与文案规则；实际使用另见 `test_combat_design.cpp`、`test_artifact.cpp`、`test_arts.cpp`、`test_api_refactor.cpp`。测试统一编入 `ningzhou_tests`；此处是现有测试导航。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。
