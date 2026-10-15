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
| `dungeon_events.cpp` | 六类事件的冻结选项、真实代价、原子领取与领域校验 | `exploration/dungeon_events.h` |
| `dungeon_encounters.cpp` | 六型妖兽、宗门精英及共享的真实遭遇预告 | `exploration/dungeon_encounters.h` |
| `dungeon_reward_rules.cpp` | 按入口境界的灵石/材料池与旧冻结奖励白名单 | `exploration/dungeon_reward_rules.h` |
| `resource_points.cpp` | 资源点开发目录和默认关闭的功能闸门；未开放争夺、收益或时间钩子 | `resource_points.h` |

这些公共头仍保留在 core 根。交通入口为 `travelPlan` → `travelBegin` → `travelAmbushTick`；地点服务为 `locationServices` → `locationServiceBegin` → `locationServiceComplete`；秘境为 `dungeonEnter` → `dungeonChoose` / `dungeonClaim` → `dungeonLeave`。

## 数据流与依赖边界

API 根据当前位置与目标查询选项，规则入口验证后创建在途 `Job` 或秘境运行状态。耗时活动由 simulation/calendar 推进；奇遇/秘境可启动 combat，并通过 `adventureBattleEnd` / `dungeonBattleComplete` 接回领域结果。秘境奖励记录是否领取，退出时完成其结算。`dungeonStateError` 供读档与状态校验使用。

本模块依赖 foundation、progression、combat、crafting 与 simulation，使用 `data/reference_tables.h` 的既有表。当前 combat 的结束回调和 sim 的任务分发仍直接引用探索入口；目录移动没有消除这些环形调用关系。`api_exploration.h` 的 JSON 视图属于 interface，不是探索规则实现。

## 维护注意

- `service_navigation.h/.cpp` 提供 `ServiceRouteQuote` 和纯查询 `mfQuoteServiceRoute/mfQuoteMaterialRoute`。最近城市只从 `ecoIsCity` 实际坊市选，补料候选核对 `play::marketList`，不按 UI 名称或复制品阶公式判断。路线复用 `travelPlan` 的资格/费用/耗时；只推荐地点，不绕过秘境阻塞、不自动采购。前端意图为浏览器临时状态，不新增存档字段。

- 路线报价与实际执行保持一致；长途工作、跨月/跨年与途中斗法要保存可恢复的上下文。
- 秘境房间和斗法期间只累加 `run.elapsedDays`，世界日历暂停；离境先清除 run，再用 `socialElapsed` 工作结算累计天数。跨年新开兽潮或遇事时保留剩余工作，不强推到活动结束。成功响应引导至岁月纪事并说明剩余天数；重新打开秘境目录也保留精准继续入口。相关回归为 `tests/test_dungeon_tide_transition.cpp::mfRunDungeonTideTransitionTests`。
- 奖励 ID 和节点 ID 必须稳定，领取和退出不能重复发奖；旧奖励结构变更需要迁移。
- 新入场LayoutVersion=2，中途混合节点与事件，预告和实际遭遇共用同一规格；旧在途缺字段按1继续原连线。扫荡要求高一大境界、人在入口且空闲，直接进入冻结终奖选择，目标材料10份不变，不刷沿途收益，离境结算1天。事件先核对完整冻结集合，候选牌组/资源和日志分配完成后无抛提交。奖励/游商/事件费用共用入口境界倍率，不使用高境玩家境界抬低阶收益。见[DUNGEON_EXPLORATION_V2](../../../docs/DUNGEON_EXPLORATION_V2.md)。
- 秘境的活动限制应同时反映在 interface 路由策略、领域检查与 persistence 校验中。
- 小绿瓶专属秘境使用`type=bottle/targetKind=bottleMaterial`，目标必须来自`content/green_bottle_content`白名单。`mfOpenBottleMaterialDungeon`仅在实际动作的候选Game上生成新入口，四/五/六阶固定金丹/元婴/化神难度，进入资格共用洞府/瓶阶/最低人物境界报价；查询不能生成site。路线/斗法/领奖/离境复用现机制，新入口完整通关保底50/100/200份专属材料，一次足够该阶升级；数量冻结到site/run，旧缺字段保持10份及原路线，未通关退出不发。三阶瓶即可开四阶材料秘境，满六阶仍可开六阶。见[小绿瓶规则](../../../docs/GREEN_BOTTLE_V2.md)。
- 只读列表/计划不应推进时间；`ensure...`、进入与完成函数属于会改状态的入口。
- 天机阁情报可以提前购买，不设玩家境界门槛；真实进入仍校验境界。入口冻结 `max(购买时玩家境界,材料最低探索境界)`，避免低境界预购生成难度过低或无法读档的高材秘境。所在地、灵石、每月一份和五年有效期均保留；预览与真实采购共用 `dungeonIntelOptions`，真实采购在候选Game上建入口/冻结奖励后一次提交。
- 炼丹材料最低探索境界共用 `crafting::mfHerbAcquisitionStage`，按真实丹方最早用途消除突破材料闭环；器材仍按原品阶门槛。野外采集共用稳定ID生境目录与草药分类，妖丹不能按日采得，详见 [药材取得规则](../../../docs/HERB_ACQUISITION.md)。

## 已有测试

`tests/test_travel.cpp`、`test_adventure.cpp`、`test_dungeon.cpp`、`test_reward_api.cpp`、`test_material_sources.cpp`、`test_npc_learning_location.cpp`、`test_passive_life_travel.cpp`、`test_checkpoint.cpp`。统一编入 `ningzhou_tests`；这里提供已有测试导航。
`tests/test_intel_purchase.cpp::mfRunIntelPurchaseTests` 覆盖炼气提前购买全量高阶目录、真实扣款/月额、只读查询、五年窗口、购买后存读档、进入境界仍阻断，以及地点/余额/工作等拒绝原子性。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。

资源点争夺方案与安全关闭边界见 [RESOURCE_POINTS.md](../../../docs/RESOURCE_POINTS.md)。当前即使请求启用，玩法仍因未就绪而明确拒绝；关闭时不加入存档字段、不消耗随机数、不影响服务和既有收益。
