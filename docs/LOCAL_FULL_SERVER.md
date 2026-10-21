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

当常规输出被已运行的程序锁定时，可将同一解决方案的 Release 输出指定为 `build-vs-current/Release/`，本入口也会选取这里的新产物。8106 的旧发行包拥有独立的程序和网页，编译源码不会自动更新它。8108若已运行，也需要先正常关闭旧服务再启动，才能同步新程序与网页。

2026-10-18玩法改动构建输出为`build-vs-gameplay/Release/`，启动入口也将其纳入最新产物选择。该目录包含本轮服务器EXE和同步网页；它不是发行压缩包。已有8106服务不自动切换版本，使用本机完整服入口仍运行于8108，并保留各自存档目录。

2026-10-17法器固定点数修正版已输出为 `dist/宁州一梦-完整服务器版-20261017-r3.zip`，另使用8110端口。解压后运行包内的 `启动完整服务器.cmd`，再打开 http://127.0.0.1:8110/；该包不会替换已有8106/8108服务或作者存档。

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
