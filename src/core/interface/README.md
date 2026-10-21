# interface · API 边界与只读视图

## 定位与职责

将 HTTP 请求映射为规则动作、将游戏状态转换为前端 JSON，并为动作提供一致的阻止理由。`src/core/api.cpp` 仍在 core 根，负责会话、动作分发和动作后的检查点/存档编排；本目录集中路由元数据和视图实现，没有把全部入口迁入目录。

## 主要文件与入口

| 文件 | 职责 |
| --- | --- |
| `routes.h` / `routes.cpp` | `RouteId`、HTTP 方法、动作效应与进行中状态访问策略 |
| `views.h` / `state_views.cpp` | 内部视图组合契约、`ViewContext` 与完整 state |
| `player_views.cpp`、`arts_views.cpp`、`world_views.cpp`、`social_views.cpp`、`economy_views.cpp`、`equipment_views.cpp`、`battle_views.cpp` | 各领域 JSON 视图与数值预览 |
| `api_exploration.cpp` | 秘境、洞府、检查点 JSON 视图 |
| `dungeon_entry.cpp` / `dungeon_entry.h` | 普通与绿瓶入场的临时奖励偏好严格解析；全部/不出为空系，指定奖励仅接受一个稳定道系，委托领域校验 |
| `cave_views.cpp` / `cave_views.h` | 洞府管理资格、训练、12苗批次灵田与永久日增权威快照；查询不生长或扣费 |
| `green_bottle_api.cpp` / `green_bottle_api.h` | 永久瓶阶/材料/成熟周期/专属秘境视图与严格命令解析；真实规则委托crafting |
| `steward_api.cpp` / `steward_api.h` | 五城执事身份/货架/纯报价视图与严格购买参数解析；不修改个人行囊或独立存档 |
| `zhuji_api.cpp` / `zhuji_api.h` | 筑基公开快照、严格准备/报价/动作/本命绑定解析；局部随机种子不对客户端开放 |
| `craft_price_views.cpp` / `craft_price_views.h` | 完整材料市价、1.5/3倍基价及法器额外售价乘区的协议编码 |
| `view_policy.h` / `view_policy.cpp` | 视图与动作共享的阻止理由 |
| `view_helpers.h` | 视图内部的 JSON/格式工具 |

稳定调用入口公共头仍是 `api.h`、`api_exploration.h`；目录内 `interface/routes.h` 提供路由契约。入口为 `api::bindSession`、`restoreGame`、`handle`，以及 `battleView`、`equipmentView`、`dungeonView`。`interface/views.h` / `view_policy.h` 是内部跨实现文件协作头，不应当作前端协议或长期公共扩展接口。

## 数据流与依赖边界

transport 生成 `http::Request` → 外层 Web 服务处理账号与会话 → `api::handle` 查询路由和当前状态、解析参数 → 调用领域动作 → 检查点/保存 → 视图生成 `json::Value` → HTTP 响应。只读 state 使用 `const Game&` 和显式 `ViewContext`；账号拥有的事实在边界取得，不让视图自行绑定会话。朋友维护的 `web_main.cpp`、天机及多人外层实现仅记录对接关系，不在本轮修改范围。

本模块读取多个领域的公共头，这是接口编排层的实际依赖。视图不得推进时间、消耗随机数、改 Game 或访问存档。资格/数值来源应复用领域查询与统一策略；api.cpp 当前仍有不少动作组织逻辑，新增工作应明确区分协议翻译、状态策略与领域规则。

## 维护注意

- `service_views.h/.cpp` 仅编码领域路线与补料导航。完整状态新增 `serviceRoutes`，补料报价新增 `travel`；俸禄 `stipendAction` 与执行共用领域报价。`nearby.buildId/buildName` 先验证特殊模板，其余按真实大道与已带核心神通归类，避免共享辅修 `mind/zhou` 导致误分类。路由数量不变。

- 桌面一键交易消费 `tide.market.items[].maxPurchase` 与 `pillCatalog[].maxSale` 的领域纯报价，复用既有购买/出售路由。`sell` 的 `count` 缺省1保留旧调用；显式非法类型、零/负数量和过期单价拒绝。最大数量和费用不由前端自行推导，详情见 [一键交易记录](../../../docs/reviews/2026-10-14-quick-trade.md)。

- 新增路由要同步 `RouteId`、路由表、处理入口及 HTTP 方法；进行中斗法、工作、抉择、秘境的限制要显式设置。
- 前端收到的 JSON 字段是协议；内部函数或展示文字重命名不等于协议字段可随意变更。
- 预览的阻止理由与执行资格保持一致；过期请求仍要在动作入口重新检查。
- 会话绑定只在边界进行；transport 的 API 串行锁是当前并发前提。
- `/api/zhuji` 同时提供GET纯查询及POST的 `start/quote/action/bind`，具体命令再区分查询与写入。快照中的 `lifeId/revision`、动作中的 `requestId` 和持久收据共同约束过期/重放请求；`quote` 不保存、不推进时钟或活动随机流。准备完成由真实90天安排回调激活，成功定型一次结算。
- 完整state包含 `zhuji`。经典与v2界面共用 `web/zhuji.mjs`，stage2展示路线选择，active专用工作区展示玩法；成功结果保留本命选择，后续境界原突破入口仍可用。经典手机快捷突破入口定位同一面板。主app在新建、读取、导入、检查点恢复和退出账号时显式重置模块，账号/人生改变后的旧异步响应不得覆盖当前state或释放新动作busy。
- 玩家视图给出绝对 `health/healthMax`，斗法内读取冻结Battle的当前/最大值；NPC/秘境内部百分比留在对应适配边界。丹药目录用 `catalogEffect`，当前服用用 `effect/effectValue` 及实际变化，两套网页共用后端报价，不复制恢复、寿元或基础气血公式。金丹与元婴本轮保持原突破协议与规则。

## 已有测试

`tests/test_api.cpp`、`test_api_refactor.cpp`、`test_display_contract.cpp`、`test_numeric_previews.cpp`、`test_renown_artifact_api.cpp`、`test_reward_api.cpp`；三项集成补充 `test_zhuji.cpp`、`test_pill_revision.cpp`、`test_absolute_health.cpp`。端到端工具包括 `tools/check-web.mjs`、`v2-contract-check.mjs`、`v2-http-check.mjs`、`check-zhuji.mjs`、`check-pill-revision-http.mjs`。测试与工具名用于导航，本 README 不代表这些工具本轮已运行。

完整声明与内部协作接口分表见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。
