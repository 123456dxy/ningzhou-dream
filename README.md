# 宁州一梦 · C++ 版

当前源码进度：**2026-10-14 桌面便利与兽潮修正版**。下载或克隆完整仓库后可直接打开 [visual-studio/ningzhou.sln](visual-studio/ningzhou.sln)，选择 Debug/Release、x64，并将 `ningzhou_web` 设为启动项目。项目使用相对路径，可随仓库移动；Backend / Web UI 分组对应实际模块目录，输出位于 `build-vs-share/Debug` 或 `Release`。环境要求与同步方式见 [VS 打开说明](visual-studio/README.md)，本轮改动与验证范围见 [桌面便利功能记录](docs/reviews/2026-10-14-service-convenience.md)，此前试玩修正见 [2026-10-13 记录](docs/reviews/2026-10-13-desktop-test-followups.md)，此前模块整理见 [2026-10-12 版本说明](docs/reviews/2026-10-12-version-delivery.md)。

要保留注册、登录和完整网页，构建后双击 [start-full-server.cmd](start-full-server.cmd)，打开 [http://127.0.0.1:8108/](http://127.0.0.1:8108/)。此入口将本仓库已构建程序与当前网页复制到 `local-server/runtime`，使用独立 `local-server/saves`；已有同路径完整服务时会复用，更新版本需先正常关闭旧服务。前台关闭、后台启动及日志位置见 [本机完整服务版说明](docs/LOCAL_FULL_SERVER.md)。

只测试本地电脑端时，构建后双击 [start-desktop.cmd](start-desktop.cmd)。它使用最新仓库构建、独立 `saves-local`，以 `--local` 跳过登录和联网入口，仅监听本机。已有 8107 服务请先正常关闭；公网部署仍用原启动方式，不传 `--local`。

给群友的离线电脑版使用 `dist/宁州一梦-电脑版-离线免注册-上次更新-20261013.exe`，基于 Git `8dadce3` 的上次更新，不包含这次开发中的兽潮扩展和新增功法立绘。双击直接以「本地道友」开局，跳过道号注册和登录。只需转发 EXE；ZIP 转发包另附使用说明。复现该发行请先在 `8dadce3` 的独立目录构建，再使用本地启动器及 `build-release.ps1 -Local` 打包，避免带入当前工作区的业务改动。本地进度保存在 `%LOCALAPPDATA%\NingzhouDream\game\runtime\saves\players`；运行 `./tools/release/verify-local-release.ps1 -ReleaseRecordPath 'build/release-日期时间/release.json'` 可隔离验收本地包。详见 [本地版使用说明](tools/release/本地版使用说明.txt)。

> 在 VS Code 里打开本目录即可获得完整的跳转、补全与 F5 调试（.vscode/ 已配好）。
> 调试前先跑 .\build.ps1 -Debug 生成带符号的可执行文件。

《觅长生》同人修仙人生模拟的 C++ 版，以 JS 版（Demo 07）为基础，继续完善本地规则与界面。

历史原版是浏览器里的 JS 游戏（仓库外的 `michangsheng-demo/`，本仓库运行无需该目录）。这个版本的目的是**让人能读懂、能调试**：
所有规则都是普通的 C++ 函数，存档是一份可读 JSON，出错时能直接看到中间数值。

---

## 2026-10-13 更新

2026-10-14 补充：宗门俸禄可异地领取，仍按本月领取记录和真实府库支付、保留欠额。异地坊市、天机情报和工坊补料增加最近服务点导航，展示真实旅费和耗时；沿途遇事可继续或刷新恢复，抵达后打开目标服务，不自动购物。当地仙友增加实际构筑、大境界、好感、姓名搜索和排序，保存筛选偏好。详细验证见 [桌面便利功能](docs/reviews/2026-10-14-service-convenience.md)。

新流派、NPC 常规构筑、功法/神通获取和特殊秘境首通附魔目前只做方案，没有更改现有战斗规则或掉落。讨论稿见 [流派与特殊秘境](docs/design/2026-10-14-builds-and-special-dungeons.md)、[336 场真实引擎对照与局限](docs/design/2026-10-14-combat-balance-analysis.md)。

建角支持选择已有立绘、锁定天赋与属性方案后预览真实开局；预检与正式创建共用规则，保存选择并兼容旧档。已整理44项初始天赋的实际作用入口，补齐福缘深厚的正向交往加成。

功法研习按层数与品阶统一报价，开工冻结费用。NPC会在赠丹、年度行动和真实战前使用适用丹药，库存、耐药、丹毒与收益随存档保留。

桌面有独立「洞府」「工坊」及「行囊与面板」。灵田按洞府档位解锁3/5/8格，同格最多99份同种草药，补种按独立批次成长；离家也随真实世界日生长，成熟后手动收获。只有持有神秘绿瓶才开放格内灵石储备及付费加速，缺钱时自然生长，不自动扣背包灵石；旧版按月复制背包药材的机制已移除。成就逐项显示条件、传承点及跨世领取状态，奖励在本世结算或实际重开时领取。

制作与交易统一使用本地材料价格。基础成品卖/买价为完整材料市价的1.5/3倍，保留自炼法器等既有额外售价加成；工坊显示实际产量、期望售出收入、五城收购渠道、剩余资金及可收数量。炼丹一至六品每炉基础耗时为4/5/6/7/8/9天，炼器一阶30天，此后每阶翻倍；能力及生活减时继续生效，开工冻结费用与时长。坊市月资金先保障实际可炼品阶，再乘基础3倍与大境界1/2/4/8/16倍，同月晋阶、查询或读档不充值。普通坊市已下架标准成品法器，旧购买请求也明确拒绝；自行炼器及已有法器的装备、出售、分解仍可使用。

丹药目录区分本地标称能力与当前服用的真实收益。修为丹实际吸收受本小境25%及距圆满余量限制；例如洗髓丹、养气丹、天元神丹标称修为分别为54、94、3083，炼气初期从零修为服用时均最多吸收28。先天异脉仍不能用修为丹提升修为。

兽潮固定持续60天，每完成一回合消耗一天，快速击杀也至少一天；炼气/筑基/金丹/元婴战场每杀得1/3/9/27分，全州共用一张金榜。普通NPC每日1/6概率出战，出战后0至1杀、本期最多9杀；完整构筑精英每日1/4概率出战，0至2杀、最多12杀，积分倍率按入场境界冻结。活动期间新生成的存活修士在真实动作边界加入，之后才开始计分，不补算出生前的战绩。

金榜第1名、第2至3名、第4至10名和其他正积分参战者，分别获得参战境界标准俸禄50/25/15/5年及声望50/20/10/1；同分并列，仅入场没有积分不发保底奖。潮退后独立兽潮坊市开放30天，仅供应三至五阶药材与炼器材料，每种材料库存按全州总积分每300分递增：三阶300分首次上架，四阶3000分解锁，五阶5700分解锁。高阶解锁后低阶继续增加，每种最多60份；例如5700分对应三阶19、四阶10、五阶1份。售完不补，到期返回普通坊市；旧已生成库存和已支付奖励保持实际记录。最高战场13胜超过NPC积分上限不代表任意境界13胜都能全州第一。

秘境探索期间先累积耗时，离境后再推进世界日历。若离境跨年遇新兽潮而暂停，会显示剩余天数及“继续当前安排”；也可在重新打开的秘境目录继续结算，不额外推进到月底。

桌面兽潮坊每种材料提供“一键买满”，按当前灵石、剩余库存和行囊容量给出数量与总价。工坊丹药行囊与随身行囊提供“一键售出”，后端按持有量、月需求、坊市剩余资金和灵石余额容量核算；丹药和法器出售控件旁直接显示坊市剩余收购资金，不在一键售出按钮上列数量与收益。普通坊市、兽潮坊和灵田的数量使用可拖动滑条；拖动即时显示数量，交易仍由后端重新核验后原子结算。已有单件出售接口和存档保持兼容；细节与本轮验证见 [一键交易记录](docs/reviews/2026-10-14-quick-trade.md)。

规则与字段说明见 [初始天赋矩阵](docs/INITIAL_TALENTS.md)、[功法参悟与 NPC 服丹](docs/MANUAL_STUDY_AND_NPC_PILLS.md)、[洞府灵田](docs/CAVE_GARDEN.md)、[制作时长与预算](docs/CRAFT_TIMING.md)、[丹药实际效果](docs/PILL_EFFECTS.md)、[炼丹经济](docs/ALCHEMY_ECONOMY.md) 和 [HTTP API 参考](docs/HTTP_API_REFERENCE.md)。本地价格修正、固定种子样本及尚未消除的产销比率见 [制作经济审查](docs/reviews/2026-10-13-local-craft-economy-audit.md)，不代表全游戏经济已全面平衡。

本轮试玩修正验证：MSVC Release增量构建成功；前端契约289项、灵田展示39项、修为丹及制作定向813项、价格/时长/实际交易608项、灵田自然生长92项、NPC兽潮规则170项和标准法器下架8项通过，另执行灵田API/兽潮交易定向回归。隔离Edge浏览器的灵田/成就/兽潮分类39项、经济18项、兽潮窄屏11项、真实89→90日市场关闭8项及最终程序新增风险12项检查通过。完整720NPC的Release浏览器往返中，状态读取平均约197毫秒，出售点击296毫秒、跨月191毫秒；本轮截图、固定种子结果、波动和测试边界见 [本地试玩修正记录](docs/reviews/2026-10-13-desktop-test-followups.md)。合并发布前额外完成一次MinGW Debug完整CTest：12,387项检查、0失败，耗时356.81秒；与本次提交的核心源码逐文件核对一致。未验证公网部署、手机专属适配或多人模块；测试使用临时账号、独立目录与端口。103张运行图像均已采用WebP压缩，40张独立功法图共2,255,806字节，素材路径与透明通道已校验。

此前建角、功法与NPC服丹整合的验证记录（Windows / MSVC Debug，非本次试玩修正复测）：正式核心、控制台、Web和测试程序构建成功；完整CTest覆盖49组C++回归、6957项检查，0项失败，耗时867.85秒。前端回归244项、建角流程14项、真实浏览器建角86项、v2 DOM契约102项、旅程契约110项、真实HTTP检查161项，以及行囊54个渲染案例和19次HTTP请求通过；静态审计、布局与差异空白检查通过，4条未读API字段提示保留为兼容信息。历史日志在本地 `build-character-economy/merge-full-ctest.log` 及同目录 `final-*.log`、`merge-*.log`。开发约定见 [代码规范](docs/CODING_STANDARDS.md)。

## 快速开始

```powershell
cd ningzhou-cpp
.\build.ps1 -Run          # 构建并启动
.\build.ps1 -Web          # 构建并启动浏览器界面（推荐）
.\build.ps1 -Test         # 构建并跑规则、接口及前端交互自测
.\build.ps1 -Debug -Run   # 构建 Debug 版（带调试符号，可用 gdb）
```

首次构建会自动调用 CMake 配置 Ninja 工程。

给其他玩家使用的单文件发行入口为 [宁州一梦-插件复核版.exe](https://github.com/guy-used-for-testing/ningzhou-dream/releases/latest)，适用于 Windows 10/11 x64。地图与立绘已内置，双击自动准备资源并打开浏览器；不包含本项目的个人存档。玩家数据保存于 `%LOCALAPPDATA%\NingzhouDream\game\runtime`，正常关闭启动器保存独立续玩快照，下次自动恢复，游戏内手动档和死亡回档继续独立保留。使用说明见 [使用说明.txt](tools/release/使用说明.txt)。

宗门修正版补齐地图进入山门、拜师与受业、宗门任务板；修正事件实际收益记录与功法抄本修习入口，区分神通学习和携带状态，补上非战斗神通与炼器的真实数值预览。全部修复备注见 [CHANGELOG.md](CHANGELOG.md)。原续玩文件与手动档自动保留。

插件复核版进一步修复手机建号和道友详情操作、支线实际耗时、同种子重开人生编号、野外死亡提示、先手反震及社交交易边界。界面审查截图、规则复现和验证入口见 [AUDIT.md](AUDIT.md)。

重新生成单文件发行包：先构建最新 `build/ningzhou_web.exe`，再运行 `./tools/release/build-release.ps1`。脚本仅打包核心程序与网页运行资源，排除存档、日志及素材生成元数据；构建清单与哈希保存在新建的 `build/release-日期时间/` 中。运行 `./tools/release/verify-release.ps1 -ReleaseRecordPath "build/release-日期时间/release.json"` 可在独立目录验收解包、续玩和资源修复，不改变正在运行的正式游戏。

已构建时可直接双击 `build\ningzhou_web.exe`。移动游戏时请一起复制同目录的 `web\`；存档保存在该目录的 `saves\`。本轮检查与修复详情见 [AUDIT.md](AUDIT.md)。

源码启动可双击 [start-latest.cmd](start-latest.cmd)，脚本会构建并启动当前浏览器版本。灼烧叠加、秘境永久传承、法宝主动神通与声望来源均已实现。

斗法固定携带普通攻击、防御各五张，不占额外神通槽。炼气至化神的额外槽位依次为 4/6/8/10/12，基础每回合回灵依次为 3/4/5/6/7；功法与战斗状态另行加减。神通搭配页显示境界门槛，忘机咒可将指定神通的全部副本移出本场战斗，下场恢复；换念诀可弃手牌重抽。人类对手使用宗门、大道、境界对应的真实牌组与功法悟道，初期散修采用较弱传承。原永久剑意转入剑道经验，不再提供额外战斗倍率。

地图按用户提供的三张宁州截图重建相对方位，包含28处地点、可缩放舆图和地点目录。每处拥有独立原创插画与可操作功能，包括当地交易、宗门事务、采药采矿、感悟和材料集会；以实际按钮的条件与收益为准。原作身份、坐标与参考来源见 [location-reference.json](web/assets/map/location-reference.json)，[地点图片及实际生成描述](web/assets/map/locations/README.md)。

遁速影响战斗先手、逃脱概率和步行/御器耗时；灵舟提供护送。途中可能遇到截杀，斗法暂停期间保留原行程和费用，脱身后继续。配方成丹增产、炼器缩时、自炼法器售值以及功法悟道配合规则见 [PASSIVE-LIFE-TRAVEL.md](PASSIVE-LIFE-TRAVEL.md)。

NPC详情可见真实传承，并请教尚未领悟的功法或神通；满足境界、好感和交情要求后支付学费并研习，每人每月最多三次，神通学成后自行选择携带。好感与可消耗交情分开，赠礼偏好、实际物品转移、重复赠礼递减和社交次数限制共用统一规则。人物按门派与性别显示立绘，妖兽从炼气到化神使用五档立绘；角色侧栏与战斗均可见，斗法受击有闪烁与伤害提示。原图、生成方式及提示词见 [立绘素材说明](web/assets/portraits/README.md)。

### 秘境、材料与洞府

普通坊市的药材、炼器原料仅售一、二阶，并有境界门槛。天机阁只出售三阶及以上材料的定向情报：每月一份，购买的是目标材料的秘境入口位置，真正击败终点首领后获得该材料十份，并额外选择一份通关主奖励。购买情报后到「秘境」目录定位入口，再从舆图赶到该地点进入。

自然秘境前期每三年出现一处，元婴及以上每五十年出现一处；进入时按当前境界生成难度。无尽海每十年开放两年，需要筑基；天魔眼每五十年开放五年，需要金丹。入口沿用宁州舆图地点，关闭时显示下次开放年份。

秘境使用独立爬塔界面，从下向上沿相连节点探索，包含普通战斗、精英、事件、宝箱、营地、行商和首领。气血延续到下一战；选到的神通与遗物只影响本次探索，营地可以删掉额外神通，普攻和防御各五张始终保留。行商花本次探索获得的灵石；终点通关或主动离开时，战利品包结算到正式行囊。

自然秘境、无尽海与天魔眼以永久神通、功法抄本和大量灵石为通关主奖励。入口发现时已固定候选，目录与路线页可预览；击败守关者后三选一，必须领完才可离开。沿途临时卡牌与遗物在离开后消散，通关领到的永久神通可另行携带，功法抄本可继续修习；已有知识重复领取时折算灵石，完成页显示实际所得。提前退出后入口消失，主动退出带回已得物资，斗法中逃离只带回一半。

寻常洞府最高一档灵气配给；声望达到二十、支付一百二十灵石可升为灵脉洞府并启用二档；声望达到六十、支付四百灵石可升为聚灵仙府并启用三档。升级、配给、灵田与训练在独立「洞府」页操作；页面随时可看，实际管理须回到安置洞府的地图地点，并满足当前动作条件。

### 声望来源与法宝神通

「机缘成就 → 成就」列出真实收益、前置条件、当前进度、次数限制和入口。游历寻缘每累计九百天增加一点；成功突破小境界加三、大境界加十二；首次通过入门考核加三；路见不平出手胜利加三，掩护撤离加一；野外悬赏每胜加二、每月最多三次；旧剑归途与药圃无人指定选择每世分别加三和四；迎战兽潮获胜按本期强度加五、七或九。普通城中差事、演武、商会委托和提升交情不增加声望；拦路索财扣四，截杀扣十。

自炼法宝依器型、属性和阴阳获得主动神通。剑造成属性攻击或状态，印施加或引爆状态，甲提供护盾，钟恢复灵力，珠治疗、增伤或减伤。任务与工坊选择材料时可只读推演能力、费用、时间与成功率；角色侧栏和行囊装备页显示实际法宝能力。战斗中的「法宝神通」栏固定展示已装备法宝，直接点击施展，不需抽牌、不占额外神通槽；费用、冷却、实际效果和不可用原因由引擎提供。

### 入门考核与死亡选择

宗门卡片可定位山门。抵达对应山门并满足身份、声望条件后，支付十灵石参加三天入门考核，与使用本宗流派的弟子进行非致命卡牌切磋。实际获胜才拜入宗门，失败或撤离仍耗时，休养后可重试。入宗后在「宗门知交 → 藏经阁」用贡献请领本门神通与功法抄本；学会神通后自行选择携带。

开局写入自动存档位四，此后每满十年更新；快速保存与手动保存仍使用一至三号位。意外死亡先进入结局画面，可接受死亡并结算传承，或回到最近的手动/自动存档。接受结算后关闭该世的死亡恢复入口；寿尽正常结算，没有死亡回档选择。设置页可查看自动位与下次保存日期，自动位不能被手动覆盖。

开局现有44项天赋。新增先天道体、转世仙人、龙族血脉、神秘绿瓶、百炼匠心、轻功高手等，分别影响修炼与灵气容量、大境界悟道点、回合回复、灵田按日付费催熟、炼器时间与售价、实际遁速。五行灵根增加对应卡牌触发效果，丹火天赋增加成丹数量，过目不忘缩短功法研习；先天异脉不能用修为丹药或妖丹提升修为。新建角色只能选择一种五行灵根，互斥天赋在建号时校验，旧存档已有天赋继续保留。机制参考 [觅长生天赋介绍](https://wiki.biligame.com/mcs/全天赋简评（新）)，数值与触发条件按本地规则适配。

### 命令行参数

| 参数 | 说明 |
| --- | --- |
| （无） | 读存档位 1，没有则进入建号流程 |
| `--new` | 直接建号，忽略已有存档 |
| `--load N` | 读取存档位 N（1–4，4 为自动位） |
| `--debug` | 打印全部内部状态后退出（不用进游戏就能看数值） |
| `--help` | 显示帮助 |

`--debug` 是最常用的排查入口，它会打印种子、随机数状态、洞府加成、
战斗公式的当前计算结果等。

---

## 工具链

需要 MSYS2 的 UCRT64 GCC：

```powershell
winget install --id MSYS2.MSYS2 -e
# 装好后（国内网络建议先把 pacman 镜像指向 USTC）：
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-gdb `
                   mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

**两个容易踩的坑**（都已处理）：

1. **编译器需要 `C:\msys64\ucrt64\bin` 在 PATH 里**。
   否则 `cc1plus.exe` 找不到运行时 DLL，以 `0xC0000135` **静默失败**。
   （生成的 exe 已经静态链接，不受这条影响，见下。）
2. **PowerShell 会把编译器的 stderr 当成终止错误**，
   所以 `build.ps1` 用 `Tee-Object` 落盘再按退出码判断，
   失败时把最后 40 行日志打出来。

---

## 目录结构与文件用途

```
ningzhou-cpp/
├── CMakeLists.txt        构建定义（3 个目标：nzcore / ningzhou / ningzhou_tests）
├── build.ps1             构建入口，包装 CMake 并处理两个 Windows 坑
├── README.md             本文件
├── CONTEXT.md            会话上下文（接续进度看这个）
├── .gitignore            排除 build/ 与数据导出中间文件
├── .vscode/
│   ├── settings.json       UTF-8 编码、CMake 参数
│   ├── launch.json         F5 调试的 3 个配置
│   └── c_cpp_properties.json  IntelliSense 跳转与补全
│
├── src/
│   ├── ui.h / ui.cpp          控制台界面（菜单、斗法交互、调试视图）
│   └── core/                  纯规则层，不依赖界面
│       ├── base.h             基础类型、随机数（复刻 JS 的 LCG）
│       ├── model.h            全部数据结构（Game / Player / Arts / Battle …）
│       ├── rules.h/.cpp       规则表查找、人物生成（buildCharacter）
│       ├── sim.h/.cpp         人生与天下：世界推进、NPC、行动、结算
│       ├── calendar.h/.cpp    360 天月历、跨月工作、小数收益发放
│       ├── arts.h/.cpp        十二大道、功法神通、炼丹炼器、机缘
│       ├── battle.h/.cpp      神通卡牌斗法（抽牌/出牌/结束回合）
│       ├── sword.h/.cpp       七重剑道境界、剑修境差
│       ├── economy.h/.cpp     任务、委托、坊市、猎妖、功法获取
│       ├── society.h/.cpp     俸禄账簿、师徒道侣、原创支线、野外悬赏
│       ├── adventure.h/.cpp   游历六类遇事、后山探访、开局物品
│       ├── json.h/.cpp        极简 JSON 读写（无第三方依赖）
│       └── save.h/.cpp        存档序列化、迁移、存档位管理
│
├── tests/                     分模块规则、接口与前端回归自测
│   ├── harness.h              极简断言框架（check / checkEq / checkNear）
│   ├── test_main.cpp          汇总入口
│   ├── test_core.cpp          数据表、规则表、随机数、人物生成
│   ├── test_arts.cpp          洞府加成、大道经验
│   ├── test_battle.cpp        卡牌斗法逐次出牌、剑道境差
│   ├── test_calendar.cpp      日期换算、小数发放
│   ├── test_craft.cpp         炼丹、炼器、丹药、机缘
│   ├── test_society.cpp       俸禄、支线、悬赏、关系往来
│   ├── test_adventure.cpp     游历、后山、开局物品
│   ├── test_names.cpp         NPC 姓名生成
│   └── test_save.cpp          存档往返、JSON 边界
│
├── data/
│   └── reference_tables.h     ★ 自动生成，不要手改（836 行）
│
├── tools/                     数据导出与对照脚本，详见 tools/README.md
│   ├── dump-data.mjs          JS -> JSON（主数据表）
│   ├── dump-society.mjs       JS -> JSON（支线/悬赏）
│   ├── extract-data.py        JSON -> C++ 头文件
│   ├── *-check.mjs            各种"拿 JS 当标准答案"的对照脚本
│   ├── playtest.ps1           脚本化自动试玩
│   └── show-society.py        打印支线清单
│
└── build/                     ★ CMake 生成，可随时删（8.9 MB）
    ├── ningzhou.exe           游戏本体
    ├── ningzhou_tests.exe     自测程序
    ├── build.log              上次构建日志（编译失败先看这个）
    └── saves/slotN.json       存档
```

★ = 生成物，不要手改或纳入版本管理。

### 想改某个规则时去哪找

| 想改什么 | 看哪个文件 | 关键函数 |
| --- | --- | --- |
| 修炼速度、突破成功率 | `sim.cpp` | `cultivationRate` / `breakthroughChance` |
| 境界、宗门、地点、天赋表 | `model.h` | `REALMS()` / `SECTS()` / `TALENTS()` |
| 斗法伤害、卡牌费用 | `battle.cpp` | `battleEffect` / `battleCost` |
| 炼丹成丹率、炼器加成 | `arts.cpp` | `artBrewInfo` / `artForgeInfo` |
| 洞府加成汇总 | `arts.cpp` | `artModsOf` |
| 剑道境界与境差 | `sword.cpp` | `swordRank` / `swordDuelOf` |
| 俸禄、支线条件 | `society.cpp` | `socAccrue` / `storyRequiresMet` |
| 游历遇事 | `adventure.cpp` | `adventureEvent` / `adventureChoose` |
| 月历与耗时 | `calendar.cpp` | `calendarDate` / `calendarRun` |

### 与 JS 版的对应关系

| C++ | JS |
| --- | --- |
| `sim.cpp` | `sim.mjs` |
| `calendar.cpp` | `calendar.mjs` |
| `arts.cpp` | `arts.mjs` |
| `battle.cpp` | `battle.mjs` |
| `sword.cpp` | `sword.mjs` |
| `economy.cpp` | `economy.mjs` |
| `society.cpp` | `society.mjs` |
| `adventure.cpp` | `adventure.mjs` |

数据结构里的字段名尽量与 JS 保持一致（`g.player.arts.poison`、
`g.bonds.payroll.balance` 等），方便两边对着读。

---

## 构建是怎么管的

**用 CMake，但外面包了一层 `build.ps1`**，你平时只需要用后者。

### CMakeLists.txt 干了什么

```cmake
add_library(nzcore STATIC src/core/*.cpp)          # 全部游戏规则
add_executable(ningzhou src/ui.cpp)                # 控制台界面
target_link_libraries(ningzhou PRIVATE nzcore)
add_executable(ningzhou_tests tests/*.cpp)         # 自测
target_link_libraries(ningzhou_tests PRIVATE nzcore)
```

三个目标：`nzcore`（静态库，纯规则）、`ningzhou`（游戏）、`ningzhou_tests`（自测）。
后两个都链接 `nzcore`，所以规则只编译一份。

两个要点：

- **`CONFIGURE_DEPENDS`**。不加的话新增 `.cpp` 不会被自动编译——
  ninja 只会说 `no work to do`，你会以为改了代码其实没编进去。
  这个坑我实际踩到过，已修。
- **`-finput-charset=UTF-8 -fexec-charset=UTF-8`**。
  源码含中文，不加这个运行期输出会乱码。

### build.ps1 干了什么

它就是帮你把 CMake 调对，并绕开两个 Windows 上的坑：

1. **把 `C:\msys64\ucrt64\bin` 加进 PATH**。
   不加的话 `cc1plus.exe` 找不到运行时 DLL，以 `0xC0000135` **静默失败**——
   不打印任何错误。（这是**编译期**的问题；生成的 exe 已经静态链接，运行时不需要这个。）
2. **别让 PowerShell 把编译器 stderr 当终止错误**。
   用 `Tee-Object` 落盘，只按退出码判断，失败时打印最后 40 行日志。

### exe 是静态链接的，可以直接拷走

`CMakeLists.txt` 里有这几行：

```cmake
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()
```

**为什么需要**：GCC 默认动态链接 `libstdc++-6.dll` 和 `libgcc_s_seh-1.dll`，
这两个在 `C:\msys64\ucrt64\bin` 下、不在系统 PATH 里。不加静态链接的话，
**直接双击 exe 会静默闪退**（退出码 `0xC0000135`，没有任何提示）。

**代价**：exe 从 1.3MB 变成 3.9MB。
**收益**：可以拷到任何地方双击运行，不依赖工具链。`build/` 里放了个
`启动宁州一梦.bat` 方便直接双击。

自己验证一下：

```powershell
# 清掉 PATH 里的 msys64，模拟"换台没装工具链的机器"
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch 'msys64' }) -join ';'
.\build\ningzhou.exe --help     # 应该正常输出
```

```powershell
.\build.ps1              # Release
.\build.ps1 -Debug       # 带调试符号，体积大、能断点
.\build.ps1 -Run         # 构建后启动
.\build.ps1 -Test        # 构建后跑 507 项自测
.\build.ps1 -Clean       # 先删 build/
```

### 也可以用 CMake 直接构建

不想用脚本的话（比如在 VS Code 里点 CMake 插件）：

```powershell
cmake -S . -B build -G Ninja `
      -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build build
```

`.vscode/settings.json` 里已经配好了这些参数，装了 `cmake-tools` 扩展就能直接在
状态栏选目标构建。

### build/ 目录

全部是 CMake 生成的，**可以随时删掉重建**（8.9 MB）。里面有：

| 文件 | 说明 |
| --- | --- |
| `ningzhou.exe` | 游戏本体 |
| `ningzhou_tests.exe` | 自测程序 |
| `libnzcore.a` | 规则静态库 |
| `CMakeCache.txt` | CMake 缓存（记录了生成器与编译器路径） |
| `build.log` / `configure.log` | 上次构建的完整日志，编译失败时先看这个 |
| `saves/slotN.json` | 游戏存档（跑起来之后才有） |

`.gitignore` 已排除 `build/` 和数据导出的中间文件。

### 日常改动流程

| 你改了什么 | 要做什么 |
| --- | --- |
| `.cpp` / `.h` 里的代码 | 直接 `.\build.ps1` |
| **新增**一个 `.cpp` | 直接 `.\build.ps1`（有 `CONFIGURE_DEPENDS`，会自动纳入） |
| `CMakeLists.txt` | 直接 `.\build.ps1`（CMake 会自动重配） |
| JS 版的数据表 | 跑 `tools/` 里的三步重新生成，见 [tools/README.md](tools/README.md) |
| 换编译器 / 生成器 | `.\build.ps1 -Clean`，否则会沿用 `CMakeCache.txt` 里的旧设置 |

---

## 怎么调试

### 1. 先跑自测，看哪一项对不上

```powershell
.\build.ps1 -Test
```

自测按模块分组，每组都标注了对照对象。失败项会同时打印**得到值**和**期望值**：

```
  [失败] 主修蛮牛+辅修吐纳后修炼 1.1 (得到 1.150000，期望 1.100000)
```

### 2. 数值不对时，用对照脚本查 JS 的原值

`tools/*.mjs` 中的历史对照脚本会直接 `import` 仓库外的 JS 版模块，需要另备同级 `michangsheng-demo/`；当前 C++ 构建、游戏运行及回归自测无需该目录。
当你不确定 C++ 算得对不对时，先跑对应的脚本拿到「标准答案」：

```powershell
node tools\cards-check.mjs      # 卡牌斗法逐次出牌
node tools\artmods-check.mjs    # 洞府加成
node tools\build-check.mjs      # 人物生成
node tools\society-check.mjs    # 俸禄账簿
node tools\craft-check.mjs      # 炼丹炼器
node tools\adventure-check.mjs  # 游历（如未生成可参照 test_adventure.cpp）
```

**重要原则**：发现不一致时，先跑 JS 确认真实行为，再决定改代码还是改测试。
本项目开发过程中就出现过 3 次「我以为的期望值其实是错的」的情况
（灵击张数、守势护盾、恶名阈值），都是靠这个方法定位的。

### 3. 看运行时状态

```powershell
.\build\ningzhou.exe --debug
```

或在游戏里按 `8`。

### 4. 直接看存档

存档是 `build/saves/slotN.json`，缩进格式化、中文原样保存，
可以直接用编辑器打开核对任意字段：

```json
"clock": {
  "ageBase": 16,
  "carry": { "dao": 0, "detox": 0.333333, "xp": 0.412 },
  "days": 120
}
```

### 5. 用 gdb 断点

```powershell
.\build.ps1 -Debug          # 生成带符号的 Debug 版
gdb build\ningzhou.exe
(gdb) break nz::battleTurn
(gdb) run --new
```

---

## 自测覆盖

| 分组 | 对照对象 |
| --- | --- |
| 静态数据表 | 药材 157 / 丹方 126 / 功法 40 / 神通 40 / 天赋 39 ... |
| 随机数 | JS 的 LCG，种子 20261004 前 5 个值逐位一致 |
| 人物生成 | `buildCharacter`，4 组配点逐字段比对 |
| 洞府加成 | `artMods`，含上限压制与丹毒惩罚 |
| 月历 | 日期换算、小数收益发放、carry 余数 |
| 卡牌斗法 | **逐次出牌 8 步全中**（气血/敌血/灵力/护盾/剑气/回合/已出牌/手牌） |
| 出牌预览 | 预览与真实结算共用同一函数，逐字段一致 |
| 剑道 | 七重境界阈值、剑修判定、境差夹到 ±4 重 |
| 开局物品 | 五项天赋发放的法器/丹炉/材料逐字段比对 |
| 游历 | 累计规则、六类遇事、后山探访、非战斗选项结算 |
| 俸禄 | 累计、领取、府库不足部分支付、恶名阈值 |
| 原创支线 | 8 段的条件与奖励 |
| 关系往来 | 拜师/授业/收徒/道侣/解除 的结算效果 |
| 野外悬赏 | 开战挂赏金、胜利发赏金与声望 |
| 炼丹炼器 | 成丹率、利用率、加成（6 位小数一致） |
| 存档 | 全字段往返、二次序列化稳定、异常输入处理 |
| JSON | 中文、浮点、负数、转义往返 |

---

## 已知边界

- 同时提供浏览器界面和控制台菜单。浏览器界面包含舆图、卡牌斗法、
  通讯录、功法、炼丹、宗门及存档管理。
- 存档格式与 JS 版**不互通**（字段名接近，但顶层结构不同）。
  两边各自独立存档。
- 部分 NPC 宏观演算（年度目标、因缘）保留了核心逻辑，
  但没有逐条复刻原版每一个支线分支。
- 数值以原版数据表为基础；交易价格等存在套利漏洞的规则已修正。
