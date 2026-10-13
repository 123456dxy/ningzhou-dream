# simulation · 时间与游戏编排

## 定位与职责

负责建号和世界初始化、每日/月度/年度演化、在途工作的完成分发、人生结束、兽潮、自动检查点，以及跨领域玩法入口。天机大比的服务端留影/挑战 Store 在本导航中列为对接接口，其实现仍保留在 core 根；它由朋友维护，不属于本轮本地模块修改范围。

## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `sim.cpp` | 建号、世界推进、日志、工作结算、抉择、人生结束 | `sim.h` |
| `rules.cpp` | model.h 中的查表、建号属性和基础规则声明实现 | `model.h` |
| `calendar.cpp` | 日历与余数积攒、工作逐日推进 | `calendar.h` |
| `play.cpp` | 市场/社交/制作玩法聚合与操作 | `play.h` |
| `tide.cpp` | 兽潮固定战期、日积分、金榜与结算 | `tide.h` |
| `tide_rules.*` | 战场、排名奖金、折扣和库存参数与纯计算 | `tide_rules.h` |
| `tide_market.cpp` | 兽潮战后高阶材料目录、库存和原子购买 | `tide.h` |
| `checkpoint.cpp` | 检查点开启、更新、手动记录和恢复 | `checkpoint.h` |
| `reward_receipt.cpp` | 动作前后资源/能力变化的奖励纪事 | `reward_receipt.h` |
| `../tianji.cpp`（core 根，朋友维护） | 只供对接：快照冻结、大比窗口、服务端 Store、挑战与榜单 | `tianji.h` |

公共头保留在 core 根。核心路径为 `createGame` → 领域 begin 建立 `Job` → `calendarRun` → `monthComplete` → 对应领域 complete；世界结算为 `worldYear`。奖励回执是 `RewardReceipt` 对象接口，天机服务是 `tianji::Store` 对象接口。

## 数据流与依赖边界

calendar 逐日推进工作并处理月界、年界、死亡/抉择/斗法中断；sim 分发完成事件、更新世界并记录日志。检查点经公共 save API 序列化/恢复。天机冻结永久战斗输入，创建独立 encounter，按账号持久化并通过其 Store 处理专用请求。

simulation 当前依赖几乎所有领域公共头，并被多个领域反向引用，因此它是现有编排中枢，而非已经隔离好的独立模拟层。`play.cpp` 横跨交易、社交与制作；core 根的 `tianji.cpp` 还依赖 http/json/save/api。后续本地拆分应先明确生命周期、状态拥有者和回调，不只移动文件。天机及多人逻辑仅记录已有接口关系，变更由其维护者负责。

## 维护注意

- 一个世界月为 30 天、年为 360 天；推进应按实际跨界触发，不能用动作次数代替时间。
- 中断和检查点恢复可能替换整个 Game；不要继续使用恢复前的 `Job` / Player 引用。
- 奖励只在完成/结算时落账，读取回执或预览不应重新发奖。
- 对接天机时保持既有契约：留影不写入可导入的人生存档，挑战请求遵守既有修订号与幂等参数；不在本轮调整其服务端规则。

## 已有测试

`tests/test_calendar.cpp`、`test_play.cpp`、`test_checkpoint.cpp`、`test_tianji.cpp`、`test_event_rewards.cpp`、`test_mechanics_regression.cpp`、`test_passive_life_travel.cpp`、`test_core.cpp`。统一编入 `ningzhou_tests`；此处不暗示本次文档更新已重新执行全部测试。

完整函数声明与对象入口见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。
