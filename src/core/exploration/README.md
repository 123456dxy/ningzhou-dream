# exploration · 交通、地点与探索

## 定位与职责

负责地图移动的路线/方式和途中风险、各地点服务、奇遇链以及秘境节点、战斗、奖励领取和退出。持久状态在共享模型的 `Job`、`Adventure`、`DungeonWorld` / `DungeonRun` 中；前端展示由 interface 生成。

## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `travel.cpp` | 交通计划、方式、耗时及途中遭遇 | `travel.h` |
| `location_services.cpp` | 地点服务列表、服务开始与完成 | `location_services.h` |
| `adventure.cpp` | 奇遇初始化、事件、选择与战斗后果 | `adventure.h` |
| `dungeon.cpp` | 秘境生成、进入、节点、奖励、情报与完整性 | `dungeon.h` |
| `resource_points.cpp` | 资源点开发目录和默认关闭的功能闸门；未开放争夺、收益或时间钩子 | `resource_points.h` |

这些公共头仍保留在 core 根。交通入口为 `travelPlan` → `travelBegin` → `travelAmbushTick`；地点服务为 `locationServices` → `locationServiceBegin` → `locationServiceComplete`；秘境为 `dungeonEnter` → `dungeonChoose` / `dungeonClaim` → `dungeonLeave`。

## 数据流与依赖边界

API 根据当前位置与目标查询选项，规则入口验证后创建在途 `Job` 或秘境运行状态。耗时活动由 simulation/calendar 推进；奇遇/秘境可启动 combat，并通过 `adventureBattleEnd` / `dungeonBattleComplete` 接回领域结果。秘境奖励记录是否领取，退出时完成其结算。`dungeonStateError` 供读档与状态校验使用。

本模块依赖 foundation、progression、combat、crafting 与 simulation，使用 `data/reference_tables.h` 的既有表。当前 combat 的结束回调和 sim 的任务分发仍直接引用探索入口；目录移动没有消除这些环形调用关系。`api_exploration.h` 的 JSON 视图属于 interface，不是探索规则实现。

## 维护注意

- 路线报价与实际执行保持一致；长途工作、跨月/跨年与途中斗法要保存可恢复的上下文。
- 奖励 ID 和节点 ID 必须稳定，领取和退出不能重复发奖；旧奖励结构变更需要迁移。
- 秘境的活动限制应同时反映在 interface 路由策略、领域检查与 persistence 校验中。
- 只读列表/计划不应推进时间；`ensure...`、进入与完成函数属于会改状态的入口。
- 天机阁情报可以提前购买，不设玩家境界门槛；真实进入仍校验境界。入口冻结 `max(购买时玩家境界,材料最低探索境界)`，避免低境界预购生成难度过低或无法读档的高材秘境。所在地、灵石、每月一份和五年有效期均保留；预览与真实采购共用 `dungeonIntelOptions`，真实采购在候选Game上建入口/冻结奖励后一次提交。
- 炼丹材料最低探索境界共用 `crafting::mfHerbAcquisitionStage`，按真实丹方最早用途消除突破材料闭环；器材仍按原品阶门槛。野外采集共用稳定ID生境目录与草药分类，妖丹不能按日采得，详见 [药材取得规则](../../../docs/HERB_ACQUISITION.md)。

## 已有测试

`tests/test_travel.cpp`、`test_adventure.cpp`、`test_dungeon.cpp`、`test_reward_api.cpp`、`test_material_sources.cpp`、`test_npc_learning_location.cpp`、`test_passive_life_travel.cpp`、`test_checkpoint.cpp`。统一编入 `ningzhou_tests`；这里提供已有测试导航。
`tests/test_intel_purchase.cpp::mfRunIntelPurchaseTests` 覆盖炼气提前购买全量高阶目录、真实扣款/月额、只读查询、五年窗口、购买后存读档、进入境界仍阻断，以及地点/余额/工作等拒绝原子性。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。

资源点争夺方案与安全关闭边界见 [RESOURCE_POINTS.md](../../../docs/RESOURCE_POINTS.md)。当前即使请求启用，玩法仍因未就绪而明确拒绝；关闭时不加入存档字段、不消耗随机数、不影响服务和既有收益。
