# 本地重构交付与验证

基准提交：fa43732。当前分支：`codex/backend-desktop-ui`。本轮没有新增提交、推送重构代码或创建 PR；main 未被本地重构修改。

## 交付内容

- 实现按 11 个职责目录导航，保留 core 根公共头和 API/save 编排入口；没有将目录分类表述为已经彻底隔离的 11 个独立库。
- 十二大道、五宗门、神通/战斗类别使用类型标识、稳定 ID、兼容解析；展示词表与持久化键分离，命名参数严格校验。
- API 的 83 条核心路由集中为 enum/策略表；状态和预览拆分为领域视图，局部变量使用职责名称。
- save.cpp 缩至约 68 行；人物/养成/战斗/秘境/游戏 codec、校验、迁移、文件 IO 拆为 11 个实现文件。
- 修复状态查询触发自动存档、未知秘境路径绕过阻塞、未知战斗/神通类别回退到有效行为等边界问题。
- 保留旧 JSON 字段、旧存档键、普通导入和 detached 留影语义，保留两个前端使用的权威数值预览。
- CMake 支持递归模块、MSVC UTF-8/多配置、Windows NOMINMAX、Visual Studio 文件分组与可重复的隔离测试目录。
- 各模块 README 已放入对应目录并显示在 Visual Studio；另有架构、HTTP API、C++ API、协作者改动对比文档。

## 明确排除范围

用户最新指定：天机大比等多人模块不由我们更改。因此 `core/tianji.cpp/.h`、`core/http.cpp/.h`、`core/pwhash.cpp/.h`、`src/web_main.cpp` 保持原位置和原内容。root 的临时路径后缀调整已撤回；对应 Tianji 测试文件也还原。联机接口列入文档仅用于对接。

共享规则、视图和存档层会被两端调用，本轮对其重构保持协议和留影兼容。历史剧情/物品字符串键仍存在，后续可沿已建立的类型/目录边界迁移。

## 实际验证

| 验证 | 结果与范围 |
| --- | --- |
| 最终 GCC Debug 完整 CTest | 5746 项断言、0 失败，exit 0；217.24 秒，职责边界撤回后重新编译执行 |
| MSVC Debug 全量环境对照 | 19:59 构建的整体版本 5746/5746、exit 0；806.06 秒。最终源随后重新编译，见 build-complete.log |
| 桌面前端静态/行为回归 | tools/check-web.mjs：235 项通过 |
| 手机 V2 纯契约 | tools/v2-contract-check.mjs：67 项通过 |
| 实际 HTTP 隔离验收 | 164 项通过；250 个请求、91 个界面命令、84 次天机战斗动作、4 次已提交响应重试；使用临时服务与两个测试账号 |
| 内容兼容 oracle | 1960 行完全一致，SHA256 764B0755B36B0AA8F3F7F55959404F666075DE288F92CEB711BBD7B6D9F36223 |
| API 展示兼容 oracle | 1829930 字节完全一致，SHA256 bcc1d80b119549efefab7a592ecfe805562896ae045358e8f095d9630df4bca0 |
| 专项回归 | 内容 961、API 新增 25、存档组合 806 项；存档新增 44 项在基线与新实现均通过 |
| 独立审查 | 未发现有证据的 P0/P1/P2；逐项核对路由、展示字段、旧档和留影 |
| 差异检查 | git diff --check 无错误；多人实现与 HEAD 无差异 |

这些测试没有在公网正式账号上运行。没有进行本轮真实浏览器布局验收；桌面与 V2 为上述脚本验证，HTTP 为真实隔离服务验证。

初次未重构 fa43732 全量测试，以及初次 HTTP 测试，在工作区外临时目录遭遇 Win32 ACCESS_DENIED（错误 5），原生 CreateDirectoryW 同样失败。隔离到当前工作区后完整测试通过。CTest 现在自动使用各构建目录下的 test-temp，并在 Windows 取得可用的短路径，以兼容现有跨 UTF-8 边界的测试夹具；不更改系统全局 TEMP，也不绕过生产存储错误处理。

tools/audit.mjs 已适配拆分后的视图和 enum 路由。它仍是启发式静态审计，会报旧的未读字段、CSS 模板函数/回调等候选项；本交付没有将它宣称为零发现，也没有为消除这些提示修改朋友维护的多人或手机代码。

## 文档与本机入口

- [模块架构](../BACKEND_ARCHITECTURE.md)
- [HTTP API：95 个端点](../HTTP_API_REFERENCE.md)
- [C++ API：407 条声明 / 405 个去重签名](../CPP_API_INDEX.md)
- [协作者改动对比](2026-10-12-collaborator-changes.md)
- [独立审查](2026-10-12-independent-review.md)

Visual Studio 解决方案：项目根下 `build-vs2022/ningzhou.sln`。可重新生成和验证：

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
ctest --preset vs2022-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

最终验证产物分别保存在 `build-backend-desktop/` 与 `build-vs2022/`，不进入源码版本控制。
