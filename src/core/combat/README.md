# combat · 斗法与冲突

## 定位与职责

负责神通卡牌、回合状态、伤害与状态结算、人类对手、留影切磋，以及法器和剑道在战斗中的能力。NPC 冲突也在此组织为斗法、掠夺和恶名结果。模块修改 `Game` 中的战斗状态；展示 JSON 由 interface 生成。
//剑道应该和其余流派一样不应该存在单独的境界增减伤,这一点直接删除掉,而后是预览回复灵力,应该是恢复到N点,而不是回复N点,这里界面没有说清楚
## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `battle.cpp` | 神通目录、开战、回合、预览、胜负及来源结算 | `battle.h` |
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
