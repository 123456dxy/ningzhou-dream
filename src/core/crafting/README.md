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
| `cave_garden.cpp` / `garden_rules.h` | 12苗独立批次、后台永久成长、旧资产待返与纯报价；阈值/日增唯一配置 | `crafting/cave_garden.h` |
| `green_bottle.cpp` | 永久0至6阶报价、原子升级、洞府专属秘境入口；不处理存档或HTTP | `crafting/green_bottle.h` |
| `herb_acquisition.cpp` | 以真实丹方最早用途推导药材取得阶段，草药分类与采集池 | `crafting/herb_acquisition.h` |
| `forge_rules.cpp` | 自由十份投料的纯报价、原子开工、冻结成品合同与完成发放；旧三料路径继续兼容 | `crafting/forge_rules.h` |
| `material_sources.cpp` | 炼器材料显式渠道与普通坊市购买资格；列表与成交共用 | `crafting/material_sources.h` |
| `tianji_steward.cpp` | 独立固定无限货架、纯报价与候选状态原子支付；不使用NPC个人背包 | `crafting/tianji_steward.h` |

公共头仍位于 core 根。任务路径主要为 `ecoActionInfo` / `ecoActionError` → `ecoBegin` → `ecoComplete`；即时路径主要为 `shop::saleQuote` / `gearActionQuote` → `sell` / `salvage` / `inscribe`。`ordinaryHerbGradeLimit` 与 `ordinaryMaterialGradeLimit` 为跨模块来源限制提供统一计算。

## 数据流与依赖边界

只读报价从 `Game`、角色修行和声望计算可用性、价格与成本，API 再调用动作入口。耗时操作写入 `Job`，calendar 推进后由 sim 的完成分发调用 `ecoComplete`；即时交易直接修改背包、灵石、装备或供给。奖励纪事可使用 `RewardReceipt` 汇总变化。

依赖 foundation、progression、social/renown、combat 和 simulation；宗门任务也会调用 social。interface 应消费这里的报价结果，不复制售值或资格公式。材料限制可由探索、社交、天赋入口共享，特殊秘境来源需按自身规则处理。公共头互调仍存在，尚未建立独立经济服务或独立编译库。

## 维护注意

- 报价与执行应遵守同一规则；点击时必须按当前状态再次检查，不能信任过期前端预览。
- 失败动作不得先扣材料、灵石或取消任务；批量制作要检查数量、库存和数值边界。
- 物品 ID、来源键和任务类型属于稳定数据契约，改显示文字不应改这些键。
- 变动跨越在途 `Job`、装备或配给字段时，应同步 persistence 的结构与状态校验。
- 制作时长只在 `craft_timing.cpp` 定义，开工冻结到 Job；存读档不重算。药材本地交易使用HerbDef.price，不混用sourcePrice；基础成品售/买仍为材料1.5/3倍。坊市三年预算由原月预算乘50，保留基础3倍、境界1/2/4/8/16及整枚高价丹保护；1080日真实边界刷新，查询、换月和晋阶不补款，取消月售出件数限制，详见 [制作时长与预算](../../../docs/CRAFT_TIMING.md)。

## 已有测试

新版自由炼器由 `ForgeRequest` 明确十个逐份选择（允许空格与重复ID）、已投主材、器型、主效果及灵纹。`mfQuoteForge` 共用内容白名单计算五维、利用率、器能、缺料、品阶和费用；`mfBeginForge` 再核验并一次扣料，将完整 `Gear` 冻结进 `Job::ForgeProduct`。完成由 `ecoComplete` 优先转发 `mfCompleteForge`，不按完成时角色属性重新计算。没有此字段的旧在途合同继续原三料规则。

新材料目录位于 `content/forge_content.*`，包括旧十五种与八属性、阴阳、五阶、五部位和两类矿物的新ID。一二阶新矿物64种接入普通坊市，三至五阶新矿物96种由五城天机阁执事固定价供应；五部位妖兽材料由真实猎妖、驱妖、奇遇、兽潮与秘境妖兽胜利结算取得，不进入商店或情报随机池。品阶和属性取所杀妖兽，五部位各1份，详见[妖兽部位掉落](../../../docs/BEAST_MATERIAL_DROPS.md)。新版成品通过 `SourceMaterials` 回收原始材料，按每个ID的一半向下取整；不再用名称猜配方。新版成器前选灵纹，旧铭刻入口拒绝再次叠加通用强度。

器能是实际成器预算：各份品阶能量合计乘利用率向下取整，主效果需同阶单份能量的七倍，灵纹再需三倍。十份同阶满利用率可同时承载主效果与灵纹；单材类十份可无纹成器。器能不再次作为战斗倍率叠加。新版伤害、护盾及新增持续伤害基础点数按品阶固定，详见[炼器数值表](../../../docs/FORGING_V2.md)；材料效力仍用于资源增层和守势。成品预览和实际回收共用 `mfQuoteGearPrice`，新版按 `SourceMaterials` 的完整市值计价（含较高阶混料），不因尚无装备ID而退回标准估价；报价不开放新材料购买资格。

`tests/test_forge_redesign.cpp` 覆盖纯报价无随机/时间副作用、十格多ID、空格与重复份数、原子拒绝、六阶专属材料门槛、冻结合同、旧合同兼容和拆解边界。

`potion_sales.cpp` 提供 `mfQuoteMaxPotionSale` / `mfSellPotions`：每种丹药一键出售的数量由真实拥有量、需求余量、收购资金和灵石余额容量共同限制。报价使用本次只读响应的 `MarketSaleContext`，实际动作重新核算，不接收前端伪造的上下文。成功仅产生一条批量纪事，拒绝不部分扣丹或增加需求计数；旧 `sell` 单件接口保留。定向验收见 `tests/test_potion_bulk_sales.cpp` 和 `test_trade_quick_actions_api.cpp`。

`tests/test_craft.cpp`、`test_equipment_task.cpp`、`test_material_sources.cpp`、`test_passive_life_travel.cpp`、`test_numeric_previews.cpp`；API 与收益另见 `test_api.cpp`、`test_renown_artifact_api.cpp`、`test_event_rewards.cpp`。测试统一编入 `ningzhou_tests`；此处仅列出已有覆盖入口。
新增时长与账期预算回归见 `tests/test_craft_timing.cpp`，包含真实核心 API、旧工作合同和同月余额保持。
洞府灵田版本2规则见 [灵田说明](../../../docs/CAVE_GARDEN.md) 和 [永久小绿瓶](../../../docs/GREEN_BOTTLE_V2.md)。自然日增20/40/60，加永久瓶阶0/45/85/150/250/420/700；不再读灵气配给、存取储备或收每日费。成熟预报按共享阈值直接向上取整，不逐日模拟。`tests/test_cave_garden.cpp` / `test_green_bottle.cpp` / `test_cave_api.cpp`覆盖本轮批次、周期、升级/专属秘境、持久化及HTTP边界。历史[时间流程性能记录](../../../docs/reviews/2026-10-13-time-performance.md)仅代表当时检查，不能视为本轮结果。
丹方用途、稳定生境与丹炉资格闭环见 [药材取得说明](../../../docs/HERB_ACQUISITION.md)，全目录及真实动作回归为 `tests/test_herb_acquisition.cpp`。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。

2026-10-18：`alchemy_rules.*::mfQuoteAlchemy`统一整批开炉资格、材料需有缺、费用、时长、耐久与期望产量；`ecoBegin`与前端`brewOptions[].batchQuotes`消费同一结果。报价支持1/3/5/10炉，领域入口同时拒绝斗法、秘境、工作与事件中的开炉。`forging_product.*::mfEvaluateForgeRecipe`只计算配方本身，玩家开炉再叠加库存、技艺和费用校验；事件及出生遗宝通过`mfCreateGearFromTemplate`使用同一完整配方工厂，不冒充人物亲炼。定向测试见`test_alchemy_quote.cpp`、`test_gameplay_templates.cpp`。
