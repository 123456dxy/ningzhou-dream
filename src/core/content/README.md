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
| `beast_portraits.*` | 六类妖兽主题与五大境界立绘白名单；纯外观映射，不从显示名或外部路径猜图 |
| `forge_content.*` | 自由炼器的材料、材类五维、器型、32项主效果及灵纹白名单；矿材显式区分普通坊市/执事，妖兽五部位标记实际击杀掉落来源 |

公共头相对 core 根为 `content/game_ids.h`、`content/text_table.h`。入口为 `parseDaoId` / `parseSectId` / `parseSpellKind` / `parseBattleKind`、`stableId`、`legacyDaoKey` / `legacyValue`、`displayName`、`text` 和 `formatText`。

## 数据流与依赖边界

来自 HTTP、存档或历史表的字符串在边界解析为枚举，规则使用枚举计算，输出时选择稳定 ASCII ID、旧协议值或显示名。文案通过 `TextId` 取模板，再用 `TextArg` 填入具名参数。实现主要依赖标准库，不反向依赖 `Game`、API 会话或持久化。

combat、progression、social 和 interface 按需使用本模块。`legacyDaoKey` 等函数是兼容边界，不能从显示名称反推存档键。现存字符串仍包括协议字段、物品/剧情/任务 ID 和未迁移的业务键；本模块不承诺清除全部字符串。

## 维护注意

- 新增枚举值应同时检查枚举目录、解析、稳定 ID、旧值转换、显示名及调用处的未知值处理。
- `SpellKind::PoisonBloom` 的稳定与协议值均为 `poisonBloom`，显示名“滋毒诀”；筑基木系经营牌进入普通建议与完整竹山模板前列，避免槽位裁剪后不可达。施放与状态数字由combat集中报价，目录仅登记内容。
- `SpellKind::PoisonSiphon` 的稳定与协议值均为 `poisonSiphon`，显示名“借毒回春”；普通 `poisonBurst` 从权威神通与获取目录退场，不改生成参考表。`PoisonBurst` 结算枚举仅保留旧版阳木法印依赖；新版法器毒袭使用自己的规则，不随普通神通替换而变化。
- `parse...` 返回 `optional`；未知输入不能悄悄成为合法默认类别。
- `formatText` / `formatTextTemplate` 对缺失、重复、未使用、畸形参数抛出 `invalid_argument`；模板和参数应成对修改。
- 显示文字可调整，稳定 ID 和兼容值变更需要明确迁移。
- `SectId::Xuandao` 追加在既有枚举之后，稳定 ID 为 `xuandao`、主传承为阵道。`mfAdvancedBuilds` 沿用八个初始流派 ID，不附加隐藏伤害；测试/装备入口仍按真实境界、辅修与神通槽位裁剪。
- `mfEnsureSectTransmissions` 只在建角、年度动作和缺第六宗的旧档初始化执行，不能在只读目录查询中调用。六宗至少一名活跃传承者来自既有人物；宗门无人时才补一名确定性的元婴执教。肖像复用已有白名单文件。
- `Appearance::mfBeastPortraitPath`由真实本命大道与敌方实际境界选图；土/火/木/神/金/体对应六主题，水显式共享凝甲主题、剑共享迅爪主题。未知风格继续使用经典五境立绘，不新增持久字段。正式战斗与独立试炼共用后端视图；前端只在缺少合法权威路径时使用`web/beast-portraits.mjs`，不按妖兽中文名识别。

## 已有测试

`tests/test_content_registry.cpp` 覆盖 ID/解析与文案规则；实际使用另见 `test_combat_design.cpp`、`test_artifact.cpp`、`test_arts.cpp`、`test_api_refactor.cpp`。测试统一编入 `ningzhou_tests`；此处是现有测试导航。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。
`gear_templates.*`登记六种事件制成品模板；`artifact_appearances.*`登记十一种器型立绘的白名单ID与PNG路径。物品存档保存配方、形状和来源，视图按合法形状解析外观，不信任请求或存档中的任意文件路径。
