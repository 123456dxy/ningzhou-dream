# 独立兼容性审查 — 2026-10-12

审查基线：`fa43732`；对象：当前未提交后端重构。仅阅读生产代码与已有验证产物；本审查未提交、推送或运行全量构建。

## 结论

未发现可复现且有证据的 P0/P1/P2 问题。此结论限于下述源码对照，不替代主任务的 MSVC/GCC、实际 HTTP 与桌面/手机验证。

## 核对范围和证据

- 对照旧 `src/core/save.cpp` 与新 `save.cpp`、`persistence/` 的人物、玩家、NPC、养成、斗法、秘境、游戏 codec，以及结构/状态校验、迁移和文件 IO。按函数归一化比较局部变量名后，未发现 codec/validator 字段遗漏；原解码和初始化代码的拆分保持先迁移、再状态校验、再补齐、最后发布 `out` 的顺序。
- `loadDetachedFromJson` 仍跳过普通世界初始化；影录敌方快照仍通过 `shared_ptr<const Player>` 保存，序列化/解码前清除嵌套斗法；普通角色导入仍拒绝包含 `shadowPlayer` 的天机斗法。
- 对照旧 API 显式路径与 `interface/routes.cpp`：旧、新均为 83 条，没有删失或新增路径。已知路由的秘境、斗法、抉择和在途工作例外与旧逻辑一致。
- 旧 `buildState` 中 593 次命名字段写入的 key，在新 state/domain/equipment 视图中没有缺失。所有拆分视图读取 `const Game&` 或 `const Player&`，装备投影仅改局部副本，没有当前 Session 访问或初始化调用。
- 独立读取既有 `build/api-refactor/baseline-schema.json` 与 `refactor-schema.json`：均为 1,829,930 字节，逐字节一致；重新计算 SHA-256 均为 `bcc1d80b119549efefab7a592ecfe805562896ae045358e8f095d9630df4bca0`。该探针覆盖初始/高境界状态、装备、神通/节点目录、经济选项、炼器预览、斗法视图/完整状态和结局；这是对已有产物的复核，没有重新构建探针。
- 核对 `combat/battle.cpp`、`combat/artifact.cpp` 与旧实现中的 typed-ID 条件、五行相生、费用/状态公式、战斗类别奖励/死亡判断，以及词表参数与旧文案。旧中文大道键、camelCase 战斗/神通类别仍由兼容转换明确生成；未见数值公式变化。
- `adventure`、`api_exploration`、`base`、`calendar`、`checkpoint`、`conflict`、`cultivate`、`dungeon`、`economy`、`friendship`、`http`、`json`、`legacy`、`location_services`、`play`、`pwhash`、`reward_receipt`、`roster`、`rules`、`shop`、`sim`、`source_limits`、`sword`、`talents`、`tianji`、`tide`、`travel` 与基线逐文件文本一致，仅路径移动；`renown`、`society` 的差异仅为 UTF-8 BOM。

## 确認的行为变化

- `/api/state` 不再经过 `okWithState`，所以 GET 不触发检查点更新与自动档落盘；响应仍保留 `ok`、`state`、`hasGame`、已有保存警告。动作完成后的检查点/落盘路径保留。
- 未识别的 `/api/dungeon/...` 不再凭前缀绕过秘境保护；已识别的秘境动作保持许可。
- 未知神通类别/大道不再被试算为普通攻击；未知战斗类别不创建战斗或消费 RNG。上述变化符合新标识边界的明确拒绝要求。

## 未在本审查判定的项目

- 实际手机 V2、桌面布局和联机交互的运行结果：主任务正在执行独立运行验证，本审查不重复慢构建或把未执行的检查宣称通过。
- 历史剧情/物品等剩余字符串迁移的完整性：架构文档明确本轮迁移范围为大道、宗门、斗法/神通类别、法器元素与 API 路由；未据此要求扩大本轮范围。
