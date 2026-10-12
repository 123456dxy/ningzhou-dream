# Visual Studio 2022 入口

下载或克隆仓库后，打开本目录的 **ningzhou.sln**。安装 Visual Studio 2022 的“使用 C++ 的桌面开发”、MSVC v143 和 Windows 10/11 SDK；选择 Debug/Release、x64，启动项目为 `ningzhou_web`。

解决方案中的 Backend 子目录对应 `src/core` 的实际模块：combat、content、crafting、exploration、foundation、interface、persistence、progression、simulation、social、transport。公共兼容头保留在 Backend 根；Web UI 对应 `web` 的实际目录。模块 README 与 Documentation 文档也在项目中可查。

- `nzcore`：共享游戏规则。
- `ningzhou_web`：浏览器服务与完整前端源码，构建后复制网页和素材。
- `ningzhou`：控制台入口。
- `ningzhou_tests`：完整规则与 API 自测。

项目全部使用相对路径，不依赖开发者的 D 盘目录。构建输出位于仓库的 `build-vs-share/Debug` 或 `Release`；网页资源跟随 `ningzhou_web.exe`，包含地图、立绘和移动版资源。

CMake 仍是跨平台构建的权威配置，原 `build-vs2022/ningzhou.sln` 可继续使用。今后新增或移除源码、UI 文件后，执行 `python tools/generate-vs-projects.py` 同步本目录的静态项目列表。不要提交 `.vs`、个人调试设置、编译中间文件或玩家存档。
