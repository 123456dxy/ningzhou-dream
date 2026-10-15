# combat · 斗法与冲突

## 定位与职责

负责神通卡牌、回合状态、伤害与状态结算、人类对手、留影切磋，以及法器和剑道在战斗中的能力。NPC 冲突也在此组织为斗法、掠夺和恶名结果。模块修改 `Game` 中的战斗状态；展示 JSON 由 interface 生成。
//剑道应该和其余流派一样不应该存在单独的境界增减伤,这一点直接删除掉,而后是预览回复灵力,应该是恢复到N点,而不是回复N点,这里界面没有说清楚
## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `battle.cpp` | 神通目录、开战、回合、预览、胜负及来源结算 | `battle.h` |
| `battle_scaling.cpp` | 攻击、毒伤、灼烧统一战力伤害基数 | `combat/battle_scaling.h` |
| `beast_actions.cpp` | 妖兽专属动作、境界妖力预算、冻结计划与逐段结算 | `combat/beast_actions.h` |
| `artifact.cpp` | 法器形制、主动能力与装备能力查询 | `artifact.h` |
| `sword.cpp` | 剑道境界、剑修判定、剑斗加成 | `sword.h` |
| `conflict.cpp` | NPC 冲突资格、后果、战利品与恶名 | `conflict.h` |

公共头仍保留在 core 根目录。主要入口为 `battleStart` / `battleStartShadow` → `battleTurn` → `battleFinish`；只读计算使用 `battlePreview`、`battleEnemyPreview`、`artifactAbility`、`swordDuelOf`；NPC 冲突使用 `conflict::conflictError` 与 `conflict::doConflict`。

## 数据流与依赖边界

API 或领域事件生成 `EnemySpec`，开战后牌堆、灵力、冷却和状态进入 `Battle`。回合入口验证行动、执行双方机制并返回 `TurnResult`。战斗结束根据来源转交秘境、奇遇、宗门、兽潮或经济任务的结算入口。天机留影通过双方快照创建独立 encounter。

依赖 foundation 的共享模型、progression 的功法/修炼/天赋、content 的类别与文案。`battle.cpp` 当前还直接调用 exploration、social、crafting、simulation 的结束回调；这是真实的跨域编排关系，目录归类尚未形成单向依赖图。新增战斗来源应明确建立和结束两个入口，避免把来源奖励放进 UI。

## 维护注意

- 预览和实际执行应使用一致的数值规则，预览不得消耗 RNG 或改变牌堆。
- 保存中的牌 UID、冷却键、神通 ID 和战斗来源字段具有兼容意义；改动要同步 persistence 校验和迁移。
- 新类别优先使用 `content::SpellKind` / `BattleKind`；历史物品、任务和来源仍存在字符串键，不能将所有字符串视为展示文字。
- 留影切磋与真实人生的死亡、背包和世界结算路径不同，必须保持其独立状态。

## 已有测试

`tests/test_battle.cpp`、`test_battle_state.cpp`、`test_battle_mechanisms.cpp`、`test_combat_design.cpp`、`test_shadow_combat.cpp`、`test_artifact.cpp`、`test_npc_cards.cpp`、`test_numeric_previews.cpp`；来源结算另见 `test_dungeon.cpp`、`test_sect_trial.cpp`、`test_mechanics_regression.cpp`。这些文件纳入统一 `ningzhou_tests`，这里列出的是已有覆盖入口，不代表本次文档操作重新执行了测试。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。

2026-10-13：伤害基数统一为 `战力 × 0.3 + 18`，技能倍率和原有分步取整不变。护盾、治疗、气血、灵力规则未随之缩放。旧档已冻结的敌方意图和状态伤害保留原值，避免重复缩放；新施术使用新基数。`test_battle_scaling.cpp` 覆盖双方直伤和持续伤害，`test_combat_health.cpp` 覆盖真实 API 即时扣血、护盾吸收、治疗与结算换算。桌面侧栏在斗法中读取 Battle 的当前/最大气血，结束后恢复显示场外健康度。

## 本地流派迭代（2026-10-15）

`elemental_sword.*` 集中五剑顺序、回灵/抽牌上限、木剑吸血和土剑临时减伤；初始推荐替换为五行归剑，稳定build ID仍为five-elements。新临时状态需要同步战斗codec、合法范围、双方镜像和回合重置。普通修士行动预算为2+大境界序号，完整传承为3+序号，散修与留影既有预算保留。

五行相生的`last`只由有效金木水火土牌改写，跨回合保留；气/神/普通攻防等中性牌透明。五行剑用独立`ElementalSwordSequence`保存0..3周期进度，跨回合保持，有效五行错序重开、非剑五行牌结束剑链；普通`swordSequence`仍按原回合与非剑打断规则。双方/NPC/留影通过同一施术、镜像与codec路径。NPC只看当前手牌优先匹配相生剑，起手优先可延续的链，暂缺匹配时先经营并保留可出招回退，不偷看洗牌。预览`ElementalGenerationReady`直接取真实相生直伤/返灵/补牌的生效事实，界面无需复制规则。

金水火木土剑各新增经营手牌的神通，神修震荡提升终结收益；忘机咒按单一UID移除一个实例。`Preview.RawDamage` 表示打出伤害，既有damage表示实际气血损失，预览与真实施法共用结算。毒/灼烧系数及火道额外叠加集中在原规则，不由前端推导。完整构筑的实际强弱取决于对手和抽牌，不能仅凭组件更全保证胜率更高。

独立实验领域在 `battle_lab.*`，接口编码在interface；正式游戏不添加实验页入口，详见 [本地斗法试炼](../../../docs/BATTLE_LAB.md)。

妖兽筑基起按独立妖力预算多行动，逐段凝甲/吐息/咆哮/蓄势/扑击，不获得修士神通；整轮先冻结总伤再分配，预览与执行共用。旧空计划当轮保留冻结单意图，下一真实开轮迁入新节奏。参数、持久字段与接口见[妖兽专属行动](../../../docs/BEAST_ACTIONS.md)。

普通修士冻结前缀不猜重洗后的牌序；该前缀真实抽牌跨重洗后，才对已经到手的牌按相同评分继续至原境界行动上限。真实与预览委托`mfExecuteEnemyPrefix`，预览只推进副本RNG，不把未来弃牌UID写入持久计划。未跨重洗的旧前缀与Shadow指定前缀保持原语义。界面显示预计总行动数与上限，护盾/治疗/补牌也占动作；当前无招式时标题明确不行动，不保留旧链。定向回归见`tests/test_npc_shuffle_continuation.cpp`，自然普通PvE诊断见`tools/npc_plan_trace.cpp`。

`fire_growth.*`集中火道2/4/5层的本场成长：实际灼烧结算后，后续新增叠焰和引爆逐轮增强，最多5轮；不随出牌次数或查询增长。天然叠焰、功法附加与持续3轮保留。噬焰回生（稳定ID `fireSiphon`，实际3灵力、筑基可学、冷却2轮）消耗敌方全部灼烧，将与引焰爆共用的灼烧池转换为自身治疗，不含引焰爆直伤，不受攻击/相生/治疗乘区增幅，不溢出自身气血上限。两者共享`mfExplosionAmount`与真实预览路径；NPC仅在受伤且有火池时优先转换。

五境界各七轮瑞士赛、均匀六宗普通/完整对五兽矩阵与八套玩家建议补充见[本轮数值结果](../../../docs/BATTLE_BALANCE_RESULTS_2026_10_15.md)；复跑方法与样本限制见[离线诊断](../../../docs/BATTLE_SWISS_DIAGNOSTIC.md)。建议配装不是新建角色，不能把宗门普通弟子的胜率称作新手胜率。

弃牌凝盾的时机集中在`discard_rules.h`：主动换念全额、回合末免费余牌50%，锐势仍全额。四条双方真实/预演路径共用`battleDiscard`，先取整后按真实得盾增加蓄势；零增量不额外触发。摄魂术完全穿盾但仍受减伤，五行剑没有隐藏穿盾。
