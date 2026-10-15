# progression · 功法、修炼、天赋与传承

## 定位与职责

负责大道经验/节点、功法与加成、修炼效率及供给、天赋生命周期、跨世传承档案。`arts.cpp` 同时保留炼丹炼器、丹药、任务/成就等历史入口，目录分类尚未把这些职责全部拆给 crafting 或 social。

## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `arts.cpp` | 节点/功法查询、战斗与生活加成、丹药、制作公式、年度/即时动作和成就 | `arts.h` |
| `cultivate.cpp` | 修炼倍率、供给成本、突破/学习与体魄公式 | `cultivate.h` |
| `talents.cpp` | 天赋冲突、出生/月度/突破效果与加成 | `talents.h` |
| `legacy.cpp` | 传承档案、里程碑、结算、天赋草案和 JSON | `legacy.h` |

公共头仍在 core 根。主要入口为 `ensureArts`、`artGain`、`artModsOf`、`cultivate::rate` / `breakdown`、`talentBirth` / `talentMonthTick` / `talentBreakthrough`、`legacy::validate` / `settle`。

## 数据流与依赖边界

建号由 simulation 使用共享模型和天赋入口初始化 Player；日历/动作完成时增加修为、功法经验或触发天赋；combat 和生活系统查询 `ArtMods` 及细分 hook。只读 `BrewInfo`、`ForgeInfo`、`PillUseInfo`、`cultivate::Breakdown` 供 interface 展示，动作入口验证后才改变 Game。人生结束后传承结算生成档案结果。

依赖 foundation、content、crafting/source_limits 与 economy、combat、simulation、静态参考表。当前 arts 的多领域动作、sim 的推进和其他领域的加成查询仍互相调用。传承 Archive 与单世 Game 是两个不同数据契约，不能把账号拥有的传承结算事实直接当作可导入人生字段。

## 维护注意

- 区分纯公式/预览和会改状态的 ensure、gain、instant、annual、settle；前端预览不得偷偷初始化。
- 新增加成要明确归入战斗或生活 hook、计算时机、叠加方式和上限，避免多个调用点重复乘算。
- `cultivation_units.h`集中修为单位倍率10。境界需求与玩家/NPC正常修炼同步扩大，配给费用、突破概率和实际修炼年数不变；修为丹标称值与旧单次吸收上限保持原数值，不经过修炼倍率入口。灵田成长点、大道经验、剑意均不是人物修为单位。
- `findManualInfo` 只复制一次构造的不可变功法内容目录；玩家境界、已学状态、NPC授业资格与价格仍由动作/报价入口实时计算，不放进静态目录缓存。
- 大道新逻辑优先使用 content 枚举；现有经验/功法/任务键保留兼容语义，不进行无差别字符串替换。
- 天赋出生、月度与突破效果需分别检查；丹药限制、来源品阶与传承预算要与实际动作一致。

## 已有测试

`tests/test_arts.cpp`、`test_talents.cpp`、`test_craft.cpp`、`test_numeric_previews.cpp`、`test_core.cpp`、`test_passive_life_travel.cpp`、`test_mechanics_regression.cpp`、`test_equipment_task.cpp`、`test_api.cpp`。统一 `ningzhou_tests` 提供现有回归覆盖。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。

## 新角色初装

`ensureArts` 首次初始化携带金芒刺、焰火诀、水灵盾、聚气，并保留原有入门神通供换装。已初始化角色的配装不被替换。真实建角、劫修对照与策略限制见 [新角色初装诊断](../../../docs/STARTER_COMBAT.md)；独立 Lab 模板不代替真实开局。
