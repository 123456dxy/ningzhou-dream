# API interface extraction — 2026-10-12

基线：`fa43732`，当前工作分支 `codex/backend-desktop-ui`。本轮不提交、不推送；本文件记录 API 负责范围的实现与验证。

## 文件边界

`src/core/api.cpp` 保留公开入口、会话绑定、请求参数校验、账号目录下的传承/存档访问、动作执行以及动作完成后的自动保存。原 3217 行降为 1876 行；`api.h` 的公开函数与 `Session.g` 成员保持兼容。

| 文件 | 职责 |
| --- | --- |
| `interface/state_views.cpp` | 编排完整状态、安排/抉择、结局；无文件或会话访问 |
| `interface/player_views.cpp` | 玩家属性、日常计划预览、突破条件 |
| `interface/arts_views.cpp` | 功法/神通/悟道、增益汇总、功法与藏经目录 |
| `interface/world_views.cpp` | 日期、兽潮、世界、游历、纪事、地点/旅行、故事与野外任务 |
| `interface/social_views.cpp` | 关系、NPC与学习信息、通讯录统计、宗门身份和声望 |
| `interface/economy_views.cpp` | 丹药权威使用信息、市场/炼丹、丹毒、炼丹炉和交易报价 |
| `interface/equipment_views.cpp` | 装备投影、被动差值、法宝技能与动作报价 |
| `interface/battle_views.cpp` | 实时战斗、手牌/法宝、敌方意图及规则层权威预览 |
| `interface/view_helpers.h` | 少量内联 JSON 标量转换和小数展示工具 |
| `interface/view_policy.*` | 动作与只读预览共用的阻塞顺序 |
| `interface/routes.*` | 83 条已识别路径的 RouteId、读写类型与进行中动作许可 |
| `interface/views.h` | 各领域只读生成函数及显式 ViewContext |

所有生成函数仅接收 `const Game&` / `const Player&`，不引用当前 Session。结局是否已结算由 API 入口从当前账号的传承档读取，作为 `ViewContext.endingClaimed` 显式传入；视图不独立读取默认账号。生成器不推进 RNG、日历，也不写入存档。装备投影仅修改局部 Player 副本。

## 协议与行为

- `battleView` / `equipmentView` 继续通过原 `api.h` 调用；HTTP 路径、JSON key、旧中文值保持不变。
- 真实战斗预览、可装备状态、功法和神通已学状态、NPC学习、装备报价、天机影录类型及敌方灵力沿用基线生成逻辑。
- 路由在统一表中声明 `ReadOnly / Mutation / ReplaceGame / Persistence`；HTTP handler 对 RouteId 分派，动作保护与报价预览共享同一 metadata。
- 未识别路径的默认策略为写操作。GET 仍返回 405；空闲已开局 POST 返回 404；未开局或已有阻塞动作的 POST 仍先执行原保护。
- 修复一：`/api/state` 原来标为只读，却调用 `okWithState` 并可能更新十年快照、写第4槽。状态查询现在直接生成响应，保留 `ok` / `hasGame` / `checkpointWarning`，实际动作仍执行自动档落盘。
- 修复二：未知 `/api/dungeon/...` 不再凭字符串前缀获得秘境期间的操作豁免；只有已识别的秘境动作获得许可。已识别路径的阻塞顺序保持原样。
- 保留 `Session.g` 兼容成员名，整理被修改的局部 `g/p/a/x`；字符串字段与存档属性不参与局部名称替换。

## 验证证据

1. 独立 `g++ -std=c++20 -Wall -Wextra -fsyntax-only` 检查 API、新 interface 翻译单元和新增测试，完成后无源码错误。
2. `tests/test_api_refactor.cpp` 提供 `nztest::runApiRefactorTests`，25 项针对性检查覆盖公开状态字段、中文协议值、权威装备预览、天机影录战斗、跨会话只读、所有写路由 GET 拒绝、未知路径保护及无磁盘落盘。
3. 将新增测试链接不可变基线领域库与原 API，先复现上述两项修复对应的 3 项失败；再链接本轮 API/interface，25 项全部通过。最终修改后重新编译并复跑，退出码 0。
4. 额外以不可变基线与新 API 使用相同领域库运行 10 组完整展示探针（初始/高境界状态、装备、神通/节点目录、经济选项、炼器预览、实时战斗/战斗状态、结局）。输出分别为 1,829,930 字节，逐字节一致，SHA-256：`bcc1d80b119549efefab7a592ecfe805562896ae045358e8f095d9630df4bca0`。
5. 本负责范围 `git diff --check` 无空白错误。整体 CLI/Web/自测、desktop/V2 和递归 CMake 集成由 root 执行。

独立探针与日志位于被忽略的 `build/api-refactor/`。基线故意复现状态写档时使用了唯一测试目录，不触碰用户存档；最终只读测试成功后移除空测试目录。

交付时间：19:50 Asia/Shanghai 前，源码冻结供主任务完整集成验证。
