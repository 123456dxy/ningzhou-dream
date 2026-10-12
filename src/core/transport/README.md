# transport · HTTP 与密码记录

## 定位与职责

作为 HTTP 与密码接口的文档导航，记录 socket/HTTP 请求收发、静态资源响应和密码哈希记录。实现仍保留在 core 根，由朋友维护；本轮只供本地规则接口对接，不修改这些多人/服务端实现。请求处理函数由上层注入；账号、会话、天机和游戏动作分发主要位于 `src/web_main.cpp`、`src/core/api.cpp`，不由 transport 判断游戏规则。

## 主要文件与入口

| 实现 | 职责 | 公共头（相对 `src/core`） |
| --- | --- | --- |
| `../http.cpp`（core 根，朋友维护） | Winsock/POSIX socket、请求解析、响应、静态文件、服务生命周期 | `http.h` |
| `../pwhash.cpp`（core 根，朋友维护） | SHA-256、PBKDF2、编码、带参数密码记录与验证 | `pwhash.h` |

公共头仍在 core 根。HTTP 入口为 `http::serve(host, port, staticDir, api, tick, shutdown)`，使用 `Request`、`Response` 和 `Handler`。密码入口为 `pw::makeRecord` / `verifyRecord`；`sha256`、`pbkdf2`、`toHex` 提供底层算法。

## 数据流与依赖边界

socket 收取完整请求 → 生成 Request → `/api/` 路径交给注入 Handler → 返回 Response；静态路径从指定资源目录读取。连接可由各自线程收发，所有 API 调用以全局锁串行执行，tick 和 shutdown 也遵循串行语义。旧头部概述仍有“单线程”措辞，应以此处及 serve 声明附近的实际并发说明为准。

HTTP 实现依赖标准库、平台 socket 和 foundation/json，密码实现依赖标准/平台能力；不直接引用 Game 或具体领域。`host` 由调用方指定，支持本机或对外绑定，不能由头部早期“只监听 127.0.0.1”注释推断所有部署都仅本机。账号认证、节流等属于外层 web_main 的职责。

## 对接注意

- 本地动作接口应遵守现有请求 body、HTTP 方法/头与响应契约；网络收发实现的修改由朋友负责。
- 游戏 API 的串行性是当前会话与规则的重要前提，本地模块对接按此使用；并发模型的变更不在本轮范围。
- 密码记录含算法/参数/盐等兼容信息，调用方使用既有记录接口；格式或参数变更由其维护者负责。
- `serve` 阻塞运行并返回退出码；tick/shutdown 回调不能绕过既有同步假设。

## 已有测试

`tests/test_api.cpp` / `test_api_refactor.cpp` 测试的是 Request 到规则响应的内存边界，不能代替真实 socket 测试。`tools/check-http.mjs`、`tools/v2-http-check.mjs`、`tools/v2-inventory-check.mjs` 验证真实 HTTP 服务；当前 tests 中未发现独立密码哈希单元测试文件。此处列出已有验证入口，不声称本次已经运行。

完整函数声明与回调类型见 [C++ API 索引](../../../docs/CPP_API_INDEX.md)。
