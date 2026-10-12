# 后端模块与维护约定

规则引擎依然是 C++20 静态库 nzcore；控制台与 Web 服务共用它。既有 `core/*.h` 公共头保留，以避免同时改动桌面、手机和联机调用方。

| 目录 | 职责 |
| --- | --- |
| content | 稳定业务 ID、兼容解析、显示名称、带命名参数的文案词表 |
| interface | HTTP 路由目录与访问策略，玩家/功法/战斗/社交/经济等只读视图 |
| persistence | 人物、养成、战斗、秘境、游戏 codec，结构/状态校验、旧档迁移、文件 IO |
| combat | 斗法与冲突规则、法器能力、剑道规则 |
| progression | 功法与悟道、修炼、天赋、传承 |
| crafting | 制作与经济、商店报价、材料来源限制 |
| social | 道友、NPC 知识、宗门考核、任务、声望 |
| exploration | 地图交通、地点服务、奇遇、秘境 |
| simulation | 时间、工作推进、世界演化、潮汐、检查点、天机大比 |
| foundation | 基础随机数与工具、JSON 类型与解析 |
| transport | HTTP 连接与静态资源、密码哈希 |

天机大比、HTTP 联机传输、认证实现由另一位成员维护。本轮保持 `core/tianji.cpp`、`core/http.cpp`、`core/pwhash.cpp` 和 `src/web_main.cpp` 原位置与实现；transport 的说明文档用于理解和对接，不代表本轮重构了这些多人实现。

api.cpp 是会话与动作入口；save.cpp 是加载/保存编排入口。领域实现调用公开头，不直接依赖另一个模块的私有 codec 或视图工具。CMake 递归收集实现并在 Visual Studio 中按目录分组。

## 稳定标识与文案

- 规则分支使用 `content::DaoId`、`SectId`、`SpellKind`、`BattleKind` 或 `api::RouteId`；边界收到字符串时先解析。
- `stableId` 是稳定 ASCII 标识；`displayName` 和 `TextId` 是可修改的展示文字。
- `legacyDaoKey` / `legacyValue` 是旧存档和 HTTP 兼容值，不能由展示名称反推。
- `parse...` 返回 optional。未知值必须明确拒绝或在调用处选择有说明的旧兼容行为，禁止自动变成某个有效类别。
- 文案使用命名参数，例如 `{damage}`，调用 `formatText` 时必须提供正确参数；缺失、重复、未使用和畸形参数会报错。
- 不把 JSON 字段、物品 ID、剧情持久化标记机械替换为展示词表。它们属于协议与数据键，应保持稳定。

仍有历史任务/剧情/物品的字符串键；本轮先覆盖战斗类别、大道、门派、法器元素与全部核心 API 路由，后续可沿这些边界逐步迁移。新增逻辑不应再比较显示文案决定规则。

## 命名与边界

函数内使用 `game`、`player`、`battle`、`spell`、`entry` 等有含义的名字。协议字段仍保持原名。纯视图不得推进时间、消耗随机数、绑定账号或写存档；动作完成后由 API 边界执行检查点与自动保存。

导入先校验 JSON 结构，再解码到临时 Game，完成迁移与领域状态校验后整体赋值。失败不得部分覆盖当前角色。服务器留影使用 detached 加载，不执行普通世界补全，不从外部角色导入留影。

## 本机编译与打开

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
```

打开 `build-vs2022/ningzhou.sln`，或直接在 Visual Studio 2022 中打开本仓库文件夹。GCC 回归可使用 `mingw-debug` preset。两套构建目录独立，不覆盖现有游戏的 build/存档。

验证包括 C++ 规则与兼容性套件、`tools/check-web.mjs` 桌面回归、`tools/v2-contract-check.mjs` 手机契约、`tools/v2-http-check.mjs` 隔离 HTTP 服务测试。后者应将 `NZ_WEB_BINARY` 指向这轮编译的新程序。
