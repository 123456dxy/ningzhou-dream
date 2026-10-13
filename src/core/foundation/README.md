# foundation · 基础工具与共享模型

## 定位与职责

提供随机数、数值兼容工具、结构化基础类型，以及不依赖第三方库的 JSON 值/解析/输出。`model.h` 仍是所有领域共享的数据模型，按基础契约在本导航中归入 foundation；它包含游戏静态表和部分规则便利入口，因此不能把整个 model.h 视为纯工具层。

## 主要文件与入口

| 文件 | 职责 |
| --- | --- |
| `base.cpp`、core 根的 `base.h` | 底层类型和头文件内的工具；主要工具以内联定义提供 |
| `json.cpp`、core 根的 `json.h` | `json::Value` 解析、序列化与 JSON 值操作 |
| core 根的 `model.h` | `Game`、`Player`、`Npc`、`Battle`、`Job`、秘境等共享模型与静态表 |

公共 include 路径继续为 `core/base.h`、`core/json.h`、`core/model.h`。随机入口包括 `Rng::next`、`mixSeed`、`randInt`；JSON 入口是类成员 `Value::parse` 与 `Value::dump`，不是自由函数。`model.h` 中的 `buildCharacter`、`rollInnate`、查表等非内联入口实现实际在 `simulation/rules.cpp`，这是共享头与实现归属不同的现状。

## 数据流与依赖边界

各领域共享同一个 `Game` 结构；API 把 JSON 输入转换为领域参数，persistence 将 JSON 转换为临时模型并做校验。JSON 层只解析通用值，领域合法性由调用者检查。RNG 状态进入 `Game` 并持久化，领域通过 `randomOf` / `randIntOf` / `pickOf` 推进它。

`base.h` / `json.h` 主要依赖标准库，`model.h` 依赖 base。其他模块都可引用基础公共头。不要在 JSON 解析器中加入游戏规则，也不要让底层工具访问 API 会话、HTTP 或文件系统。共享模型仍是较大的耦合面，进一步拆分需先定义状态拥有者与兼容路径。

## 维护注意

- LCG、种子混合、JS 取整和浮点积攒语义影响可复现结果；调整要核对既有种子与存档行为。
- `Value::as...` 类型不符可能返回默认值；读档或请求验证不能仅依赖这些宽松转换。
- 模型字段名、常量、枚举或内联表变动应检查 persistence、interface、所有领域与前端契约。
- JSON 精确32位整数使用无locale编码；负零、非整数及范围外数字保留17位原格式。`tests/test_time_performance.cpp` 对照旧编码逐字节验证，不以速度断言建立不稳定门禁。
- `pick` / `pickOf` 的调用者需确保容器可选；共享状态只读查询不应调用随机入口。

## 已有测试

`tests/test_core.cpp`、`test_names.cpp`、`test_save.cpp`、`test_state_integrity.cpp`、`test_persistence_refactor.cpp`，以及各领域的确定性测试。统一 `ningzhou_tests` 验证共同基础；此处不声明独立 foundation 测试目标。

完整函数声明及内联补充清单见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。
