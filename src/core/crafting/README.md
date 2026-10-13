# crafting · 制作与经济

## 定位与职责

负责经济状态、制作任务、商店与藏经阁交易、装备处置、洞府配给和普通材料来源上限。它把价格、资格、成本和收益落到游戏状态。炼丹炼器的部分公式与动作仍在 progression/arts，玩法面板的聚合仍在 simulation/play；当前目录不是制作逻辑的唯一归属。

## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `economy.cpp` | 行情、书籍取得、制作/任务安排与完成 | `economy.h` |
| `shop.cpp` | 交易报价、出售、换书、法器处理、丹炉、洞府 | `shop.h` |
| `source_limits.cpp` | 普通草药/材料品阶限制与错误信息 | `source_limits.h` |
| `craft_timing.cpp` | 逐品阶时长、丹术/器道与生活减时；`ecoCraftDays` 为既有接口的薄适配 | `crafting/craft_timing.h` |
| `cave_garden.cpp` | 同格批次种植、储备自动催熟、后台生长与纯报价 | `crafting/cave_garden.h` |
| `herb_acquisition.cpp` | 以真实丹方最早用途推导药材取得阶段，草药分类与采集池 | `crafting/herb_acquisition.h` |

公共头仍位于 core 根。任务路径主要为 `ecoActionInfo` / `ecoActionError` → `ecoBegin` → `ecoComplete`；即时路径主要为 `shop::saleQuote` / `gearActionQuote` → `sell` / `salvage` / `inscribe`。`ordinaryHerbGradeLimit` 与 `ordinaryMaterialGradeLimit` 为跨模块来源限制提供统一计算。

## 数据流与依赖边界

只读报价从 `Game`、角色修行和声望计算可用性、价格与成本，API 再调用动作入口。耗时操作写入 `Job`，calendar 推进后由 sim 的完成分发调用 `ecoComplete`；即时交易直接修改背包、灵石、装备或供给。奖励纪事可使用 `RewardReceipt` 汇总变化。

依赖 foundation、progression、social/renown、combat 和 simulation；宗门任务也会调用 social。interface 应消费这里的报价结果，不复制售值或资格公式。材料限制可由探索、社交、天赋入口共享，特殊秘境来源需按自身规则处理。公共头互调仍存在，尚未建立独立经济服务或独立编译库。

## 维护注意

- 报价与执行应遵守同一规则；点击时必须按当前状态再次检查，不能信任过期前端预览。
- 失败动作不得先扣材料、灵石或取消任务；批量制作要检查数量、库存和数值边界。
- 物品 ID、来源键和任务类型属于稳定数据契约，改显示文字不应改这些键。
- 变动跨越在途 `Job`、装备或配给字段时，应同步 persistence 的结构与状态校验。
- 制作时长只在 `craft_timing.cpp` 定义，开工冻结到 Job；存读档不重算。药材本地交易使用HerbDef.price，不混用sourcePrice；基础成品售/买仍为材料1.5/3倍。坊市月预算底座保障artTier可炼丹药和同阶标准法器，先乘基础3倍再乘境界1/2/4/8/16，查询和同月晋阶不补充资金，详见 [制作时长与预算](../../../docs/CRAFT_TIMING.md)。

## 已有测试

`tests/test_craft.cpp`、`test_equipment_task.cpp`、`test_material_sources.cpp`、`test_passive_life_travel.cpp`、`test_numeric_previews.cpp`；API 与收益另见 `test_api.cpp`、`test_renown_artifact_api.cpp`、`test_event_rewards.cpp`。测试统一编入 `ningzhou_tests`；此处仅列出已有覆盖入口。
新增时长与月预算回归见 `tests/test_craft_timing.cpp`，包含真实核心 API、旧工作合同和同月余额保持。
洞府灵田规则见 [灵田说明](../../../docs/CAVE_GARDEN.md)，定向批次/储备/日历回归为 `tests/test_cave_garden.cpp`。
灵田真实日结算只计算下一日成长与费用，成熟时间预报仅在纯视图中隔离执行；两者共用单日报价值。性能定位与等价验证见 [时间流程性能记录](../../../docs/reviews/2026-10-13-time-performance.md)。
丹方用途、稳定生境与丹炉资格闭环见 [药材取得说明](../../../docs/HERB_ACQUISITION.md)，全目录及真实动作回归为 `tests/test_herb_acquisition.cpp`。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。
