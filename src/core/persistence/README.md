# persistence · 存档编解码、校验与迁移

## 定位与职责

本轮新增`DungeonRun.LayoutVersion`：codec键`layoutVersion`，缺字段默认1，显式值必须为整数1或2。版本1保留旧路线，版本2按新布局重建并校验节点预告。新事件不另造持久资源字段，沿用冻结`DungeonReward`并按事件目录全字段核对；旧冻结首领奖金额保留白名单值，不在读档/查询时重算收益。

临时卡奖励策略使用`cardRewardRulesVersion`（缺省0）及`cardRewardPreference:{mode,daos}`。旧0只允许全部、空道系列表，保留旧冻结事件；新入场1必须携带完整合法偏好且selected恰好一个大道；已冻结的先前多系快照仍按原集合读取和结算，模式/道系用稳定ID，重复、未知和冲突配置均拒绝。字段只冻结本次探索，不按读档时的浏览器选择过滤已存奖励；未触发怪物信息的隐藏仅在API/UI完成，不改持久化节点名称或路线。

双方斗法新增`elementalSwordEnhancementLayers`，缺字段默认0，显式值严格为0..5整数；普通/NPC镜像共用编解码与校验，不在读取时补发强化。手牌容量改读统一`BattleCards::MaximumHandSize`（12），旧8张存档按原样载入，不自动发牌；超过12张或重复UID仍拒绝。

负责人生存档的结构编解码、数值/形状校验、领域完整性校验、历史版本迁移和文件存储。core 根的 `save.cpp` 保留公共加载/保存编排；公共头 `save.h` 也保留。天机账号留影的专用 Store 实现在 core 根的 `tianji.cpp`，由朋友维护，仅供接口对接；其数据不属于可导入的人生存档。

## 主要文件与入口

| 实现/内部头 | 职责 |
| --- | --- |
| `person_codec.cpp`、`player_codec.cpp`、`npc_codec.cpp` | 人物、玩家和 NPC 字段 |
| `arts_codec.cpp`、`battle_codec.cpp`、`dungeon_codec.cpp` | 功法、斗法和秘境状态 |
| `ascension_codec.cpp` | 升仙大会届次、连续斗法进度与入场冻结宗门 |
| `game_codec.cpp` | 整体 Game 编解码 |
| `garden_codec.cpp` | 灵田规则版本、独立批次、仅待返的旧余额/超量草药事实 |
| `shape_validation.cpp` | JSON 字段形状、数值范围与递归检查 |
| `state_validation.cpp` | 领域之间的引用、任务、战斗与状态一致性 |
| `migrations.cpp` | 旧版本迁移与读档后的状态补全 |
| `file_io.cpp` | 槽位路径、文件保存/加载与槽位目录 |
| `internal.h`、`json_helpers.h` | 仅持久化实现使用的 codec/校验声明及通用字段工具 |

公共入口（相对 core 根为 `save.h`）是 `saveToJson`、`loadFromJson`、`loadDetachedFromJson`、`saveToFile`、`loadFromFile`、`listSaveSlots`。其他领域不得直接依赖 `persistence/internal.h` 或 `json_helpers.h`。

## 数据流与依赖边界

保存：Game → 分领域 codec → JSON → 文件 IO。加载：检查文本大小/格式 → JSON 数值与形状校验 → 解码到临时 Game → 旧档迁移 → 领域状态校验 → 补全加载状态 → 最后整体赋给输出 Game。失败不会部分覆盖调用者的现有角色。

普通加载会补全世界/领域状态；detached 加载供独立留影使用，跳过普通世界补全。普通导入拒绝天机留影战斗状态。校验和迁移会依赖 combat、progression、social、exploration 和 simulation 的公共入口，这些依赖是真实存在的。IO 不应被其他领域调用来绕过完整加载路径。

## 维护注意

- 新持久字段需同时检查编码、解码默认值、形状/数值校验、状态校验和旧档迁移。
- `player.ascensionTournament`缺字段保留未初始化默认状态，不在读档或后续自动动作补发邀请或奖励；玩家主动报名可初始化当届状态。显式字段严格校验整型届次、布尔活动、0..4进度与三宗白名单；活动时必须存在本轮对应的非致命大会战斗，且不能与工作、秘境、筑基或抉择并存。
- 不把失败数据直接解码到正在玩的 Game；导入失败要保持现状。
- 迁移应按版本与原始 JSON 区分缺失字段，避免重新发奖、消耗 RNG 或覆盖已保存的牌组。
- 顶层`cultivationUnitScale`记录当前修为单位10，缺失或显式1为旧单位；其它值/类型拒绝。旧人物与NPC修为乘10，玩家`clock.carry.xp`的小数同步换算并将整数结入修为，保持小境进度比例。现代往返不重复扩大；只迁移进度，不重放丹药、推进时钟或消费RNG。
- 文件路径及槽位保留规则属于产品行为；自动检查点使用的槽位不能被普通保存随意覆盖。
- 小绿瓶`player.arts.greenBottleGrade`缺省0，严格0至6整数且正阶需天赋。灵田版本1先校验旧阈值与99容量再按比例迁至版本2/12苗，超量种苗及旧余额仅登记待返；load与只读查询不返现，实际日或园艺命令原子兑现。专属秘境目标/瓶阶/冻结通关数量状态交由`dungeonStateError`校验；`targetRewardCount`缺省10，新入口按品阶50/100/200，旧在途不重绘或补奖。往返及恶意输入定向入口见`test_green_bottle.cpp`、`test_cave_garden.cpp`。

## 已有测试

`tests/test_save.cpp`、`test_persistence_refactor.cpp`、`test_state_integrity.cpp`、`test_social_integrity.cpp`、`test_battle_state.cpp`、`test_reward_api.cpp`、`test_checkpoint.cpp`、`test_tianji.cpp`。统一编入 `ningzhou_tests`；此处是现有测试导航，未新增独立测试目标。

公共 API 清单见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)；内部 codec 不计入公共声明统计。

## 绝对气血与筑基（2026-10-16）

玩家绝对气血使用顶层`healthRulesVersion=2`；缺失/1才按百分比迁移，不能按health是否小于100猜版本。原始类型与旧0..100范围先校验，再迁移到权威新上限；冻结战斗不重算。NPC、副本仍用内部百分比，动作边界换算。`baseHealthBonus`、`foundationHealthBonus`、`pillLifeBonus`分别保存体魄丹、不可补修根基和最多500年的丹药寿元；旧总lifeBonus扣除天赋后恢复丹药贡献，不重放服药。

`breakthroughPillStage`绑定指定境界；旧stage准备绑定加载时的2/5/8/11，其他旧准备清退。`combatPillDoseCount`严格0..3，旧档按准备种类推导。5519库存迁移为5420，退役三丹历史使用次数保留；5318/5319库存及6323/6324药材和灵田引用清退。

NPC行囊同时识别potion:/herb:前缀及裸ID旧格式，化毒丹统一为potion:5420；原始负数先拒绝，合并超上限拒绝整档。旧brew的丹方ID存destination：5519映到5420，冻结合同其余字段保留；5318/5319取消后退已付cost、每炉幻心草2及混元果/鸡冠草1，退役主药按旧HerbDef.price=112/份折灵石。依据动作阻塞期不能换炉/改节点重建丹道4/高阶炉免损条件，恢复已扣耐久且最多40，不退已过天数，不发成丹/经验。所有补偿先在候选局部量核算，坏paid/数量/类型及溢出拒绝，不异常逸出或重复补发。

`player.zhuji`完整保存两路局面、局部RNG、收据与永久结算；codec先校验再发布。准备/运行状态与stage、job及其他活动交叉检查，成功后不重新发奖，死亡会终止未完成尝试。新增Card的innate/retain缺省false，已在途旧牌组不补发；本命只附加到指定神通第一张副本。
