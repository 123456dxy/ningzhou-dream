# progression · 功法、修炼、天赋与传承

## 定位与职责

负责大道经验/节点、功法与加成、修炼效率及供给、正式筑基、天赋生命周期、跨世传承档案。`arts.cpp` 同时保留炼丹炼器、丹药、任务/成就等历史入口，目录分类尚未把这些职责全部拆给 crafting 或 social。

## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `arts.cpp` | 节点/功法查询、战斗与生活加成、丹药、制作公式、年度/即时动作和成就 | `arts.h` |
| `cultivate.cpp` | 修炼倍率、供给成本、突破/学习与体魄公式 | `cultivate.h` |
| `pill_poison_rules.cpp` | 按稳定丹药用途集中查询0/1/2/4点丹毒，未知用途拒绝 | `progression/pill_poison_rules.h` |
| `zhuji_rules.cpp` / `zhuji_board.cpp` | 双路线筑基准备、纯报价、局部随机流、动作和一次性结算；固定36穴65边 | `progression/zhuji_rules.h` / `zhuji_board.h` / `zhuji_state.h` |
| `talents.cpp` | 天赋冲突、出生/月度/突破效果与加成 | `talents.h` |
| `talent_catalog.cpp` | 现行可选天赋与点费目录，保留`TALENTS()`公共兼容名 | `model.h` |
| `legacy.cpp` | 传承档案、里程碑、结算、天赋草案和 JSON | `legacy.h` |

既有公共头仍在 core 根；新筑基头放在 progression。主要入口为 `ensureArts`、`artGain`、`artModsOf`、`cultivate::rate` / `breakdown`、`talentBirth` / `talentMonthTick` / `talentBreakthrough`、`legacy::validate` / `settle`。筑基使用 `mfStartZhuji` → `mfCompleteZhujiPreparation` → `mfApplyZhujiAction` / `mfSettleZhujiRun`，只读动作报价为 `mfQuoteZhujiAction`。

## 数据流与依赖边界

建号由 simulation 使用共享模型和天赋入口初始化 Player；日历/动作完成时增加修为、功法经验或触发天赋；combat 和生活系统查询 `ArtMods` 及细分 hook。只读 `BrewInfo`、`ForgeInfo`、`PillUseInfo`、`cultivate::Breakdown` 供 interface 展示，动作入口验证后才改变 Game。人生结束后传承结算生成档案结果。

依赖 foundation、content、crafting/source_limits 与 economy、combat、simulation、静态参考表。当前 arts 的多领域动作、sim 的推进和其他领域的加成查询仍互相调用。传承 Archive 与单世 Game 是两个不同数据契约，不能把账号拥有的传承结算事实直接当作可导入人生字段。

## 维护注意

- 区分纯公式/预览和会改状态的 ensure、gain、instant、annual、settle；前端预览不得偷偷初始化。
- 新增加成要明确归入战斗或生活 hook、计算时机、叠加方式和上限，避免多个调用点重复乘算。
- 浪击诀主修满五层的主动直伤回血属于门槛效果，辅修或不足五层不触发；由 `combat/water_vitality.*` 读取真实主修资格并在命中后结算，功法目录附加同源说明。既有水悟道留水继续保留。
- `cultivation_units.h`集中修为单位倍率10。境界需求与玩家/NPC正常修炼同步扩大，配给费用、突破概率和实际修炼年数不变；修为丹标称值与旧单次吸收上限保持原数值，不经过修炼倍率入口。灵田成长点、大道经验、剑意均不是人物修为单位。
- `mfPillPoisonPoints`按用途统一丹毒，不读品阶、境界或原始参考表的毒量；目录`basePoison`与玩家/NPC实际服用共用服丹计划。恢复/修为1点，悟道/永久属性2点，战斗准备/突破/延寿4点，化毒0点；越阶无额外丹毒，旧已积累丹毒不重算。诱妖/驱妖用途退休，自然年化毒统一5点。
- 疗伤走绝对气血与回满标记；`BaseHealthBonus`是新体魄丹的基础值，旧`hpBonus`保留倍率语义。`PillLifeBonus`独立限制丹药500年额度，不占天赋寿元。资质/悟性丹无业务累计上限，仍检查耐药和整数溢出。NPC使用同一服丹计划的1.45上限系数，百分比只留在NPC持久化边界。
- 玩家气血上限统一查询 `combat/battle_scaling.h` 的 `mfPlayerHealthMaximum`；`BaseHealthBonus` 与 `FoundationHealthBonus` 相加后经过2.4及生命乘区，不能算入伤害。服丹实际收益和上限变动复用同一公式，恢复量在动作边界夹到真实上限。
- 新筑基仅替代 stage 2→3。准备冻结真实天赋和旧失败修为比例，扣已选库存、灵石并安排90天；已服且目标匹配stage2的突破准备计入6份丹药上限。五行与周天共用持久修订号/收据，失败可重试，成功后不得补修；归元额外基础气血只发一次。金丹、元婴等后续突破及旧在途job保持原概率流程。
- 五行成功记录本命固有，周天成功记录本命保留；在神通页单槽选择当前境界可用的已领悟普通神通，包含固定攻防，空闲时可更换；本命选择不永久锁定。特性只随已携带神通的第一张副本进入战斗；牌实例行为由combat负责，不在修炼或查询入口补发。
- `findManualInfo` 只复制一次构造的不可变功法内容目录；玩家境界、已学状态、NPC授业资格与价格仍由动作/报价入口实时计算，不放进静态目录缓存。
- 大道新逻辑优先使用 content 枚举；现有经验/功法/任务键保留兼容语义，不进行无差别字符串替换。
- 天赋出生、月度与突破效果需分别检查；丹药限制、来源品阶与传承预算要与实际动作一致。
- 护道遗宝只在新建出生hook发两件二品中性V2法器，复用`crafting::mfCreateGearFromTemplate`，不写生成参考表、不重放旧档奖励。仅空武器/配饰槽自动装备，遗宝炼气资格由共享法器门槛查询决定；点费调整与45项当前作用矩阵见[初始天赋](../../../docs/INITIAL_TALENTS.md)。

## 已有测试

`tests/test_arts.cpp`、`test_talents.cpp`、`test_craft.cpp`、`test_numeric_previews.cpp`、`test_core.cpp`、`test_passive_life_travel.cpp`、`test_mechanics_regression.cpp`、`test_equipment_task.cpp`、`test_api.cpp`。三项集成的定向入口为 `test_zhuji.cpp`、`test_pill_revision.cpp`、`test_absolute_health.cpp` 和 `test_lifebound_cards.cpp`；统一 `ningzhou_tests` 纳入这些覆盖。此处列出测试入口，不代表本轮全量回归通过。

面向玩家的准备、操作、失败重试和本命说明见[正式筑基集成](../../../docs/ZHUJI_INTEGRATION.md)，丹药实际口径见[丹药规则](../../../docs/PILL_EFFECTS.md)。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。

## 新角色初装

`ensureArts` 首次初始化携带金芒刺、焰火诀、水灵盾、聚气，并保留原有入门神通供换装。已初始化角色的配装不被替换。真实建角、劫修对照与策略限制见 [新角色初装诊断](../../../docs/STARTER_COMBAT.md)；独立 Lab 模板不代替真实开局。
