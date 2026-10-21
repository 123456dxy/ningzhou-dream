# social · NPC、宗门与声望

## 定位与职责

负责 NPC 关系与交情账、赠礼/交易/装备和成长数据、NPC 传授知识、名册与传讯、宗门入门/拜师/任务、剧情/羁绊/俸禄、声望档位及可行动来源。共享状态仍存于 Game、Player、Npc 等模型。

## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `friendship.cpp` | 交情、赠礼、NPC 背包交易与数据补全 | `friendship.h` |
| `npc_consumables.cpp` | 寿元50/25/10/5年分档、日常丹毒40节奏、战前三枚与真实年度采购；复用服丹计划 | `social/npc_consumables.h` |
| `npc_learning.cpp` | 传授候选、学习开始/完成与人物/妖兽头像路径 | `npc_learning.h` |
| `roster.cpp` | 名册、传讯邀请，以及建号/丹毒的历史辅助查询 | `roster.h` |
| `sect_trial.cpp` | 入门考核、战斗后完成、师长资格与宗门任务目录 | `sect_trial.h` |
| `society.cpp` | 社会初始化、俸禄、剧情、野外任务与羁绊 | `society.h` |
| `renown.cpp` | 声望档位、价格/门槛加成与来源导航 | `renown.h` |
| `tianji_steward.cpp` | 五座经济城市的固定驻城服务NPC目录；独立于个人行囊和年度服丹，不生成或改写游历NPC | `social/tianji_steward.h` |

公共头仍在 core 根。入口包括 `friendship::greet` / `giftItem` / `takeFromNpc`、`npcLessons` → `npcLearnBegin` → `npcLearnComplete`、`sectTrialBegin` → `sectTrialBattleComplete`、`storyBegin` → `storyComplete`、`renown::sources`。

## 数据流与依赖边界

只读候选/报价先按地点、关系、身份和资源计算；即时交互直接修改关系与背包，耗时学习/剧情/羁绊建立 `Job` 后由 simulation 完成；考核或冲突通过 combat 再回到来源结算。声望查询提供价格、宗门门槛及可行动入口，interface 将结果转为展示。

依赖 foundation、progression、combat、crafting 和 simulation。friendship 与 play 互调，society 含战斗/经济来源，roster 也仍包含建号/丹毒查询，因此当前属于职责聚类而非严密单向分层。头像函数只返回资源路径，不能因此把 UI 资源读取加入领域规则。

## 维护注意

- `stipend.cpp` 集中远程俸禄报价与原子结算；`socCollectStipend` 兼容委托，不要求位于山门。`mfQuoteStipend` 只读账簿和府库；工作、事件、斗法、秘境、月额和账目范围均复核。正实付部分领取保留旧欠，府库空不消耗本月机会。`tests/test_service_convenience.cpp` 与 `test_society.cpp` 覆盖这些边界。

- NPC ID、关系、交情、背包与历史记录各有用途；不要把赠礼价值、交情余额和亲疏度混为一个数值。
- 学习与考核必须再次检查当前位置、NPC 存活/状态、门槛和在途上下文，不能信任过期候选。
- `ensureNpcCultivation` 在真实初始化动作中从 `allSpells()` 权威目录按大道与境界筛选神通，避免继续获得生成表中已下架的普通引毒咒；借毒回春等C++新增神通按同一资格进入仙友知识来源，随机选择仍只发生在动作边界。
- 完整传承的授业目录直接读取 `content/npc_combat_profiles` 全部功法/神通，知识不受老师当前辅修槽位裁剪，仍按老师和玩家境界过滤。每次请教一项、功法从第一层获得；不发免费满级或绕过好感、交情、学费、三次月额和当面授业要求。
- 第六宗玄道宗使用通用入门考核、拜师、差事、藏经阁和俸禄流程；五行剑诀与五剑限定本宗，配套抄本只允许完整模板实际需要的列表。常态传承者位置由纯视图 `championTeachers` 提供，不依赖兽潮。
- 声望来源导航应反映真实动作条件，点击展示不能隐式报名、扣资源或推进时间。
- 跨 NPC 引用、羁绊、宗门考核和未完成任务字段变更，应同步 persistence 完整性校验。

## 已有测试

`tests/test_friendship.cpp`、`test_society.cpp`、`test_social_integrity.cpp`、`test_npc_learning_location.cpp`、`test_sect_trial.cpp`、`test_sect_completion.cpp`、`test_names.cpp`、`test_npc_cards.cpp`、`test_renown_artifact_api.cpp`。统一编入 `ningzhou_tests`；此处仅提供已有覆盖导航。

完整函数声明见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。
