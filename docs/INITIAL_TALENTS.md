# 初始天赋、角色方案与立绘契约

本表覆盖当前 `TALENTS()` 全部 45 项可选天赋。权威运行目录为 `src/core/progression/talent_catalog.cpp`，`model.h` 保留兼容声明，不修改生成参考表。以同一种子、同一属性分配的无天赋角色为基准；`buildCharacter` 先计算基础属性，`createGame` 再初始化功法/药材/大道经验并调用 `talentBirth`。持续型天赋在对应行动发生时才表现，不能用开局面板是否增加一个数字来判断它是否生效。

## 建角与预览

- `resolveCreateOptions` 统一处理默认天赋、属性分配、性别与立绘，并调用 `buildCharacter` 的预算/已知ID/重复/互斥/上限校验。非法方案直接拒绝，不再静默降级到第一个天赋。
- `allocation` 省略时保留旧入口的自动分配；明确提供 `{}` 表示五项全零，余点不偷偷转成资质。预览返回完整规范化分配，锁定后的正式请求应保留原 seed、天赋和分配。
- `/api/build-options` 返回 `innate.seed` 及 `portraits: [{id,name,gender,src}]`；显式 `seed: 0` 同样保持原值。
- `/api/build-check` 与 `/api/new` 共用解析函数。合法预览调用局部 `createGame`，只返回 `preview.player`、`preview.arts`、`preview.gears`、`preview.seed`、`preview.allocation`；不绑定/替换账号，不分配新 lifeId，不推进当前随机数，也不写存档、传承和检查点。`player.talents` 保留天赋名称、ID与描述。
- `portraitId` 仅接受 `playerPortraits()` 中与性别相符的稳定ID。当前复用14张已有的人类立绘；立绘不决定门派、属性或规则。Player codec 保存选择，旧档缺字段时回退同性别默认图；导入拒绝未知ID、外部URL、路径穿越和错误类型。

## 45 项作用矩阵

| ID | 名称 | 点数 | 真实开局 / 后续效果 | 规则入口 |
| --- | --- | ---: | --- | --- |
| `sword` | 剑心通明 | 4 | 资质 +6；剑道经验 +32 | `simulation/rules.cpp::buildCharacter；simulation/sim.cpp::createGame → artGain` |
| `herb` | 丹道初识 | 3 | 丹术 +20；总药材 +4，分入真实药材库存 | `buildCharacter → progression/arts.cpp::ensureArts` |
| `root` | 天灵根 | 6 | 资质 +22；灵石 −30 | `buildCharacter` |
| `luck` | 福缘深厚 | 5 | 福缘 +22；正向主动交往好感额外 +2 | `buildCharacter；talentAffinityBonus；friendship::greet/giftOutcome；play::socialDo；societyBondComplete；npcLearnComplete；切磋完成` |
| `longlife` | 长生根骨 | 4 | 寿元 +15，突破后保留 lifeBonus | `buildCharacter；createGame；突破结算` |
| `merchant` | 商贾世家 | 3 | 开局无额外灵石；营生收入 ×1.18，坊市购价 ×0.9 | `simulation/sim.cpp::monthComplete；simulation/play.cpp::buyPrice` |
| `body` | 淬体根骨 | 2 | 体魄 +2；场外疗养与其他角色同为每月合计10%气血上限 | `buildCharacter；统一疗养规则` |
| `steady` | 道心坚毅 | 3 | 开局基础属性不变；突破成功率 +4%，失败损失按普通15%/化神25% | `simulation/sim.cpp::breakthroughChance/breakthroughInfo` |
| `solitary` | 山野悟道 | 2 | 开局基础属性不变；无门派闭关效率 ×1.08 | `monthComplete 的 cultivate 分支；interface/player_views.cpp 计划预览` |
| `savvy` | 过目不忘 | 3 | 悟性 +12；功法研习耗时 ×0.8 | `buildCharacter；progression/cultivate.cpp::learnDays` |
| `rich` | 家财万贯 | 4 | 灵石 +100 | `buildCharacter` |
| `poor` | 家境清寒 | -2 | 灵石 −70；返还2点预算 | `buildCharacter` |
| `weak` | 体弱多病 | -2 | 资质 −8；返还2点预算 | `buildCharacter` |
| `unlucky` | 时运不济 | -2 | 福缘 −15；返还2点预算 | `buildCharacter` |
| `short` | 早年暗伤 | -2 | 寿元 −12；返还2点预算 | `buildCharacter → Player.lifeBonus` |
| `bright` | 聪慧早成 | 2 | 悟性 +7 | `buildCharacter` |
| `craft` | 锻铁学徒 | 2 | 体魄 +1、灵石 +15 | `buildCharacter` |
| `silver` | 药圃出身 | 2 | 总药材 +6，分入真实药材库存 | `buildCharacter → ensureArts` |
| `blade` | 剑术师承 | 2 | 剑道经验 +24 | `buildCharacter → createGame → artGain` |
| `calm` | 心静如水 | 1 | 悟性 +6 | `buildCharacter` |
| `wild` | 山林之子 | 2 | 福缘 +10 | `buildCharacter` |
| `sturdy` | 筋骨强健 | 3 | 体魄 +3 | `buildCharacter` |
| `water` | 水灵根 | 4 | 资质 +12；消耗水势时每层额外返还1灵力 | `buildCharacter；talentMods；combat/battle.cpp` |
| `fire` | 火灵根 | 4 | 资质 +12、丹术 +5；引爆灼烧伤害 +10% | `buildCharacter；talentMods；battle.cpp` |
| `wood` | 木灵根 | 4 | 资质 +12、药材 +2；每施加一层毒回复0.5%斗法气血 | `buildCharacter；talentMods；battle.cpp` |
| `gold` | 金灵根 | 4 | 资质 +12、金道经验 +6；每弃一张牌获得1%斗法气血护盾 | `buildCharacter；createGame 将金道奖励与剑道区分；talentMods；battle.cpp` |
| `earth` | 土灵根 | 4 | 资质 +12、体魄 +1；重击追加当前护盾15%伤害 | `buildCharacter；talentMods；battle.cpp` |
| `genius` | 悟性超群 | 6 | 悟性 +25 | `buildCharacter` |
| `fortune` | 命带贵人 | 3 | 福缘 +14 | `buildCharacter` |
| `frugal` | 勤俭持家 | 2 | 灵石 +45 | `buildCharacter` |
| `alchemy` | 丹火童子 | 3 | 丹术 +14；每炉成功产出 +1，最终每炉最多4枚 | `buildCharacter；talentMods；炼丹报价和结算` |
| `wanderer` | 远游见闻 | 2 | 悟性 +5、福缘 +3 | `buildCharacter` |
| `hard` | 百折不挠 | 3 | 体魄 +2、悟性 +4 | `buildCharacter` |
| `mortal` | 凡骨求道 | -3 | 资质 −12；返还3点预算 | `buildCharacter` |
| `daoBody` | 先天道体 | 4 | 开局基础属性不变；修炼效率 ×1.10；筑基起灵力容量 +2 | `talentCultivationMul → cultivate::rate；talentManaBonus → battleManaCap` |
| `immortal` | 转世仙人 | 6 | 悟性 +10、开局悟道点 +1；每次首次突破新大境界再 +1 | `buildCharacter；talentBirth；talentBreakthrough` |
| `blessing` | 天道福泽 | 4 | 开局基础属性不变；正向大道经验 ×1.2，再取整；同时影响剑心/师承/金灵根开局经验 | `progression/arts.cpp::daoGainPlan → artGain` |
| `dragonBlood` | 龙族血脉 | 5 | 体魄 +1；每回合结束回复3%斗法气血，不超过上限 | `buildCharacter；combat/battle.cpp 回合结算，包含留影` |
| `oddMeridian` | 先天异脉 | -3 | 返还3点预算；丹药与妖丹不会提升修为 | `progression/arts.cpp 丹药效果/计划/服用共同路径` |
| `greenBottle` | 神秘绿瓶 | 3 | 灵田每真实世界日按格内储备自动加速，每份需加速种苗每日耗品阶数量灵石，额外增加200成长点；外出照常生效，不复制背包药材 | `crafting::mfGardenDayTick → simulation/calendar.cpp；格内储备不足全额费用时自然生长、不扣主背包灵石；成熟批次不收费` |
| `lightStep` | 轻功高手 | 2 | 遁速 +6（影响先手、赶路、逃遁） | `talentMods → artEscapeSpeed → battleSpeed/travelPlan/battleEscapeChance` |
| `artisan` | 百炼匠心 | 3 | 开局不送法器；炼器耗时减少15%、自炼法器售值增加15%，仍受总上限约束 | `talentMods → 工坊耗时/自炼售值规则` |
| `family` | 修真家族 | 2 | 实际修得蛮牛护体功1层，并将聚气加入已领悟神通；不会自动替换主修/四个已装备神通 | `talentBirth` |
| `spiritEyes` | 魂穿异世 | 2 | 初始神识 +3 | `talentBirth` |
| `artifactHeir` | 护道遗宝 | 5 | 两件二品中性法器：破阵灵剑主动40点直伤、绵护灵珠入战及每回合8点护盾；占原武器/配饰槽，空槽自动装备 | `talentBirth → crafting::mfCreateGearFromTemplate；combat法器统一规则` |

## 本轮点费调整与升仙大会

调整的是新建角色的选择点费，不全体提高初始面板或持续倍率。淬体根骨改2点，避免与3点体魄+3的筋骨强健同价；心静如水改1点，使其与2点悟性+7的聪慧早成分档。悟性超群改6点，与3点悟性+12且减少研习耗时的过目不忘相较，保留高额单项投入的用途。天灵根改6点，保留资质+22和灵石代价，按纯成长定位区别于4点五行灵根的资质+12与战斗机制。丹道初识、剑术师承、修真家族分别改3/2/2点，承认现有初装已免费提供聚气，避免把重复已知神通高估成额外起手收益。已存在的角色不退还点数、不重算初始属性，也不重新发放出生物品。

护道遗宝以5点预算换开局斗法工具，不送功法层数、悟道节点或额外槽位。法器通过现有完整配方工厂生成，保存来源`talent`；40直伤和8盾与正式二品法器采用同一品阶表。遗宝只在新建时发放，普通读档与重复出生hook均不补发。两件遗宝允许炼气使用的资格由法器统一入口判断，普通取得的二品法器仍按其原有境界限制。

升仙大会在开局当年举办首届，此后按人生开局年份每5年一届，限炼气且未满40岁。邀请可先选择整备，本年内仍可报名；报名后无论落败、认输或通关，本届都不得再次参赛。四战依次为炼气初期散修、炼气初期/中期/后期的三位不同宗门弟子，宗门从六宗中在入场动作时冻结。对手使用普通修士真实功法、神通与牌堆；不读取玩家未来手牌或复制另一套伤害公式。

四场立即相接，月历暂停，禁止换装或穿插其他行动。每个前三场胜果在进入下一战前只回复20%真实气血上限，战败保留实际伤势且至少1点气血，不结束人生。第四场胜后发500灵石及1枚筑基丹，不额外发普通切磋的灵石与大道经验，也不在最后一场后回复气血。状态、冻结宗门和当前战斗共同保存；只读视图不发邀请、不推进日历、不抽宗门或发奖励。

### 2026-10-18 首年真实斗法测量

`tools/ascension-tournament-probe.cpp`链接本轮MSBuild产生的`build-vs-gameplay/Release/nzcore.lib`，库SHA256为`709607e6d17a127f9f76b7a42c649d90dfb01070612e2ea9256613f9c4e16285`。固定30个种子`31 + 997 × i`，三种构筑配对比较，共90个独立样本。策略`VisibleArtifactGuardV1`只看当前手牌、法器真实预览和公开敌方意图，在真实`battleTurn`上逐步行动；没有注入胜果、补神通、改面板或强制回满。补充分场计时后以相同策略重跑90局，所有原动作、RNG、宗门、结局和剩余气血与首轮一致；这属于确定性复核，不算新增独立样本。

| 真实10点构筑 | 四战全胜 | 逐场到达→获胜 | 失败：24轮上限 / 气血耗尽 |
| --- | ---: | --- | ---: |
| 无天赋、10点资质、原始配装 | 0/30 | 30→28、28→12、12→1、1→0 | 14 / 16 |
| 护道遗宝5、修真家族2、淬体根骨2、体魄分配1 | 10/30（33.3%） | 30→30、30→29、29→19、19→10 | 9 / 11 |
| 护道遗宝5、龙族血脉5、修真家族2、家境清寒−2 | 11/30（36.7%） | 30→30、30→29、29→20、20→11 | 10 / 9 |

两种遗宝构筑只通过合法即时操作改为蛮牛护体功1层主修、吐纳1层辅修，四神通仍为真实默认金芒刺、焰火诀、水灵盾、聚气。三组全部在第0年、第0天结束且不陨落。两组通关率的Wilson 95%区间分别为19.2%—51.2%、21.9%—54.5%，有限样本不能解释成人类胜率。

遗宝体魄组四场平均各7.7、10.1、12.8、14.9次结束回合；遗宝龙血组为7.8、10.2、12.7、15.5次。单场策略计算平均约6—18毫秒；此为本机无渲染诊断CPU时间，不包含建角、网络、动画或人类操作时间。当前策略在中/后期竹山与离火弟子处最容易失败，两组末场对这两宗均各0/4，说明这些对局需要更有针对性的出牌或前期准备，不能据此断言不可战胜，也未因此降低共享战斗规则。

完整逐步候选分值、出牌、RNG、分场生产结算原文与计时保存在`build/diagnostics/ascension-20261018-v1/runs.jsonl`，汇总及库/工具指纹为同目录`summary.json`，首轮未加分场计时的报告为`baseline-runs.jsonl`。本页测量不代替可玩浏览器检查。

## 已修复的实证缺口

1. 福缘深厚原有“交往好感额外 +2”描述，但只有基础福缘 +22 生效。现在使用 `talentAffinityBonus` 在正向主动交往结算处应用一次，赠礼预览/实际/旧赠药工作共用 `giftOutcome`；重复见礼、已拒绝的行动、负面初见、解除道侣/辞师扣分不获得奖励。切磋撤离保留旧基础好感，不额外奖励天赋。没有在通用 `addRelation` 中叠加，因此剧情关系变动不会意外加成。
2. 原 `checkBuild` 漏拒绝未知属性键，`createGame` 又会在失败时静默减少天赋。现在预检与创建统一拒绝非法方案，失败不覆盖当前角色。
3. 原显式空属性对象在预检中花费0，正式创建却会自动把余点投入资质。现在通过 `allocationExplicit` 区分“省略”与“明确零分配”。

## 边界与兼容性

- 天灵根与凡骨求道互斥；先天道体与体弱多病互斥；金木水火土只能选一种。旧档已有的多灵根组合继续兼容，不在读档时强行删除天赋。
- 资质与悟性最终受 `APT_MIN/MAX`、`INSIGHT_MIN/MAX` 限制；福缘上限150；灵石最低0。属性投入只允许 `BUILD_STATS()` 的五项，预算以账号传承预演结果为准。
- `sword`、`blade` 仍使用历史 `Build.sword` 数值作为开局经验换算标记；`gold` 的历史标记在创建时排除于剑道经验之外，只发金道经验。它不是新的剑道境界伤害加成。
- `talentBirth` 的标记防止悟道点、神识、功法与已知神通重复发放；普通读档不会重做出生奖励。传承档案与可导入的人生存档仍分开。
- `adventureBirth` 保留 forgeheir、furnaceheir、furnacescion、heirblade、artifactward 等历史兼容分支；这些ID不在当前45项目录中，不作为现行可选天赋展示或接受。

## 回归入口

`tests/test_character_build.cpp::runCharacterBuildTests` 逐项检查独立初始属性矩阵与出生经验/神识/功法奖励、预览与真实创建一致、空分配、固定种子、非法方案保持旧角色、立绘存档往返/旧档兼容及预览不写磁盘。`tests/test_talents.cpp` 覆盖初生防重、真实突破/月历/炼丹炼器/斗法/逃遁效果，以及福缘社交预览与单次结算、拒绝和负面行为不奖励。`tests/test_ascension_tournament.cpp` 的 `mfRunAscensionTalentCatalogTests` / `mfRunAscensionTournamentTests` 覆盖点费、四连战实际回调、奖励防重、资格、非致命退出、恢复上限、只读、存档拒绝原子性及遗宝真实40伤/8盾。测试执行结果由本次整合构建记录提供；文档本身不代表已运行全套测试。
