# 本机完整服务版

在仓库根双击 `start-full-server.cmd`，或在 PowerShell 运行：

```powershell
.\start-full-server.cmd
```

浏览器地址为 **http://127.0.0.1:8108/**。这是正常完整服务模式，保留注册、登录及完整网页；首次进入请注册或登录此本机服务器的账号。数据独立于公网服务器及 8107 免登录测试版，原来的本地与公网存档不会被覆盖。

默认前台运行，按 **Ctrl+C** 正常保存并关闭服务。窗口只打印地址与日志位置，详细服务日志保存在 `local-server`。需在后台运行且不打开浏览器时：

```powershell
.\start-full-server.cmd -Background -NoOpen
```

也可直接调用 PowerShell 脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\start-full-server.ps1 -Background -NoOpen
```

入口选取当前仓库最新构建的 `ningzhou_web.exe`，复制到 `local-server/runtime`，并同步当前源码的完整 `web` 目录，再运行复制后的程序，因此不会锁住编译目录。请先构建 `visual-studio/ningzhou.sln` 或 CMake `mingw-debug`。如果 8108 已有同一运行路径的完整服务，入口直接复用；其他进程占用则明确报错，不会强行停止它。

| 本机文件夹/文件 | 用途 |
| --- | --- |
| `local-server/runtime` | 复制后的程序、依赖 DLL 与网页 |
| `local-server/saves` | 此服务器的账号、玩家存档和会话 |
| `local-server/feedback` | 此服务器的意见箱 |
| `local-server/server.pid` | 最近启动的服务器 PID；服务关闭后可能保留，使用前核对进程 |
| `local-server/server.json` | 最近启动的源程序、日志路径及运行信息 |
| `local-server/server-*.stdout.log` / `*.stderr.log` | 每次启动独立的标准输出与错误日志 |

重复启动会复用正在运行的版本。更新代码并构建后，先正常关闭旧服务，再运行入口同步新程序与网页。后台服务不会因启动脚本退出而关闭，须先在游戏中保存，再按 `server.pid` 核对并关闭对应进程；默认前台入口更便于用 Ctrl+C 正常收尾。

`local-server/` 已被 Git 忽略，账号、存档、反馈、运行文件和日志不会随代码提交。
