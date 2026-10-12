# 宁州一梦 · 多人 Linux 网页版

本分支在单机版基础上增加了一个可以在 Linux 服务器上给多人同时游玩的网页服务端（`ningzhou_web`）。单机版规则代码与存档格式不变。

## 新增功能
- **道号登录**：输入道号（1~16 个汉字/字母/数字/下划线），首次登录设置密码（PBKDF2-SHA256 加盐哈希），可修改密码；登录有 IP+道号限流。
- **每人独立存档**：`<数据目录>/players/<道号UTF-8十六进制>/` 下保存 1~3 号手动档、4 号自动档、传承档案和续玩数据；每次操作后落盘，闲置 30 分钟释放内存。
- **天骄榜**（`GET /api/leaderboard`，服务端按境界>修为>声望排序）、**意见反馈**（`POST /api/feedback`，写入 jsonl，限流）。
- **手机界面**（≤690px）：`web/mobile.css`、`web/mobile.mjs`，底部导航、可折叠状态栏、44px 触控区等；桌面布局不变。
- **安全**：静态文件解码后做规范路径校验（防 `..`/编码穿越），每 IP 限流，存档导入上限 6MB、其余接口 512KB，连接有收发超时；每连接一线程收发，但所有游戏接口由一把全局锁串行执行。
- 新命令行参数：`--host`（默认 0.0.0.0）、`--data`（默认 ./saves）、`--idle-minutes`。

## 构建与部署（Linux）
```bash
apt install cmake g++ webp
tools/linux/package-linux.sh            # 输出到 dist/linux/app
cd dist/linux/app && ./run.sh           # 监听 0.0.0.0:25001
```
`web/assets` 下的地图与立绘已统一压缩为 WebP（`cwebp -q 82 -m 6`，约 152MB → 16MB），前端与规则代码直接引用 `.webp`。服务端收到旧的 `*.png` 请求时，若同目录下有同名 `.webp` 就返回 WebP，兼容旧存档里记录的立绘路径。新增图片可先放 PNG，打包脚本会自动转成 WebP。

## 测试
```bash
cmake -S . -B build-linux && cmake --build build-linux -j
./build-linux/ningzhou_tests
./build-linux/ningzhou_web --no-open --port 18106 --data /tmp/nzdata &
python3 tools/multiplayer-check.py http://127.0.0.1:18106
```
