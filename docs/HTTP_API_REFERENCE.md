# HTTP API 参考与统计

核对日期：2026-10-18。来源为 `src/core/interface/routes.cpp`、`src/core/api.cpp`、`src/web_main.cpp` 与 `src/core/tianji.cpp` 的实际分派。统计对象为精确 `/api/...` JSON 端点；相同路径的 GET/POST 计一个端点。`/api/tianji/` 前缀不是端点，静态页面/资源文件不计 API。

2026-10-15 补充核对：新增小绿瓶升阶、专属秘境及低阶秘境扫荡三个写端点，旧灵田储备端点保留为明确拒绝的兼容入口。下方按当前 RouteInfo 重新统计。独立斗法实验服务的四个本机端点不计入正式游戏。

2026-10-16秘境协议补充（无新增端点）：普通`dungeon/enter`和洞府`cave/green-bottle/dungeon`均接受可选`cardRewardPreference:{mode,daos}`。`mode`为`all/none/selected`，`daos`是`dungeons.cardRewardOptions.daos`提供的稳定ID数组；all/none必须空数组，selected恰好一个有效道系；新入场多系请求明确拒绝，既有多系在途快照保留原奖励，缺字段默认all。新入场冻结偏好，事件/商人/战后临时卡共用筛选，永久终奖不变。未触发战斗节点返回通用name/text与`detailsRevealed:false`，不返回具体encounterName/Hint；触发或已访问后才揭晓，事件主题预告保留。

责任边界：我们维护核心规则与桌面 UI；账号、联机传输、天机大比由另一位成员维护。服务层增加可选本地启动模式与只读模式探测；默认公网模式和 Tianji Store 业务实现保持原样。

## 去重统计

| 分派归属 | 精确端点 | 查询 | 写操作 | ReplaceGame | Persistence |
| --- | ---: | ---: | ---: | ---: | ---: |
| 核心 RouteInfo 表 | 104 | 26 ReadOnly | 72 Mutation | 3 | 3 |
| 服务层模式/账号/社区（排除转发Tianji） | 8 | 4 | 4 | 0 | 0 |
| Tianji Store | 5 | 2 | 3 | 0 | 0 |
| 总计（路径去重） | **117** | **32** | **79** | **3** | **3** |

核心四类取自真实 `RouteEffect`。服务层/Tianji 的查询与写操作按用途列示，它们没有使用该 enum；查询可维护会话、缓存或天机窗口记录，不能据此推断服务器完全没有 I/O。`/api/tianji/board` 在 web_main 与 Store 均出现，但最终归 Tianji，仅计一次。web_main 的精确分派共9条，其中8条为服务专属。

## 请求、登录与通用返回

- 多人 Web 服务中，全部104个核心端点均需有效 `nz_sid` Cookie。mode、nickinfo、login、logout、leaderboard与tianji/board可不登录；whoami/password/feedback以及其余Tianji端点要求登录。直接调用C++ `api::handle` 的单机/测试会话不处理登录Cookie。
- 显式 `--local` 启动时强制监听 `127.0.0.1`，使用内置本地会话，空档按默认配置开局。账号、反馈、排行榜、导出及全部 `/api/tianji/` 返回404；不初始化或读取 Tianji Store。默认不加此参数，继续使用上述公网鉴权。
- 核心输入从 JSON **请求正文**读取，不从URL查询串读取；需要参数的查询建议使用POST JSON。表中未列默认值的字符串参数缺省为`""`，随后由对应规则校验；“无”表示没有业务输入。非空正文必须是JSON对象。
- 所有26条核心 ReadOnly 端点均支持 **GET和POST**；其余核心端点只支持POST。成功的核心动作通常返回 `{ok:true,state:{...}}`，某些动作附加text/report；可能出现checkpointWarning。state包括玩家、功法、世界、经济、社交、秘境与恢复点视图。
- `/api/state` 的核心处理在GET与POST下都纯只读：不推进时钟或RNG，不更新自动快照，也不写第4槽；未开局返回`{ok:true,hasGame:false}`。多人服务仍有玩家加载/续玩持久化、会话维护与Tianji.observe等外层维护，并在完整state上补充`state.tianji`及可能的tianjiWarning。
- 常见错误为 `{ok:false,error:"中文说明"}`。服务层未登录另带`needLogin:true`；Tianji错误另带字符串`code`。HTTP200仍需检查ok：例如conflict/preview资格失败返回200且ok:false，build-check返回200并以valid表示方案合法性。

| 状态码 | 通用含义与具体边界 |
| --- | --- |
| 200 | 成功，或返回可判断的查询结果/无效建号方案；检查ok和valid |
| 400 | JSON、参数、业务条件或核心动作阻塞不合法 |
| 401 | 未登录/登录密码错误；未认证请求先被服务层拦截 |
| 403 | 修改密码时旧密码错误 |
| 404 | 未知已开局空闲核心POST路由、读取不存在/坏档，或未知Tianji POST操作 |
| 405 | 方法不被当前端点接受；未知核心GET也为405 |
| 409 | 天机挑战中的人生写锁、资格/对手/挑战/幂等ID/版本冲突 |
| 413 | Tianji POST正文超过4096字节（传输层另有总请求限制） |
| 429 | 传输层限频、登录/密码错误限频、反馈限频 |
| 500 | 核心存档/传承、密码、反馈写入失败，或传输层捕获异常 |
| 503 | Tianji存储不可用、容量/写入或窗口记录保存失败 |

核心整数参数days/slot/count/amount/choice/level/roll/batch/expectedCost必须为有限、整数、int范围内数值；seed为0–4294967295整数；allocation各值为0–MAX_POINTS整数。解析校验先于核心方法检查；登录、服务写锁或Tianji存储健康检查也可能先于后续业务检查，因此不要仅依靠URL预测错误码。

## 核心动作阻塞顺序

ReadOnly跳过人生动作阻塞；ReplaceGame（new/import/load）可替换人生；Persistence（save/legacy/settle/checkpoint/restore）需已开局，但跳过一般动作阻塞并执行各自专有校验。Mutation需已开局，随后按下列顺序保护：

1. 秘境进行中：只放行5条已识别dungeon动作与battle/turn。
2. 人生已结束：拒绝一般动作。
3. 普通斗法进行中：只放行battle/turn。
4. 有待处理抉择：只放行choose与battle/turn。
5. 有在途安排：只放行advance、cancel-job、plan、choose与battle/turn。

未知路径按Mutation处理，不能凭dungeon前缀绕过保护。天机留影挑战进行中，Web服务另外把全部核心写动作（包括换档、保存）拦为409；20条核心查询仍可用。Tianji动作操作独立挑战存储，不使用人生的battle/turn。

## 服务层模式、账号与社区（8条）

| 路径 | 方法（当前实现） | 登录 | 主要输入/默认值 | 返回与副作用 |
| --- | --- | --- | --- | --- |
| `/api/mode` | 未限定；通常GET | 否 | 无 | `ok,local,nickname`；只读，本地模式 nickname 为“本地道友”，公网模式为空 |
| `/api/nickinfo` | 未限定；通常GET | 否 | URL query `nickname` | ok/exists/hasPassword；查询账号目录与密码记录 |
| `/api/login` | POST | 否 | JSON nickname/password | 去除道号首尾空格；无密码记录则首次设置；返回nickname/hasGame/passwordSet并设置nz_sid Cookie |
| `/api/logout` | 未限定；建议POST | 否 | Cookie nz_sid（可无） | 删除对应会话，清Cookie；ok:true |
| `/api/whoami` | 未限定；通常GET | 是 | Cookie nz_sid | nickname/hasGame；可能加载玩家会话 |
| `/api/password` | POST | 是 | JSON old/new | 校验旧密码、写新密码并作废同账号其他设备会话；ok:true |
| `/api/leaderboard` | GET | 否 | URL query `limit=20`，钳制1–100 | ok/total/updatedAt/list；名次、道号、角色、境界、修为、声望、年龄与结局标记 |
| `/api/feedback` | POST | 是 | JSON type=bug/suggest/other，text | UTF-8文本最多2000字；追加feedback.jsonl（含上下文/IP/UA）；ok:true |

道号1–16字符，仅汉字/英文字母/数字/下划线；密码至少4字符、最多128字节。登录及密码验证以IP+账号计10分钟内5次失败锁定；反馈对道号和IP分别限制每分钟1条、每24小时窗口30条。当前nickinfo/whoami/logout没有方法检查，此处描述实际实现而非约定强制GET/POST。登录Cookie为HttpOnly、SameSite=Lax、Path=/、Max-Age=2592000。

## Tianji 天机大比（5条）

| 路径 | 方法 | 登录 | 主要输入/默认值 | 返回与副作用 |
| --- | --- | --- | --- | --- |
| `/api/tianji/board` | GET | 否 | 无 | ok/updatedAt/nextRefreshAt/refreshSeconds/total/list/rules；榜单缓存刷新 |
| `/api/tianji/status` | GET | 是 | 无 | window/phase/reason/entry/opponents/canJoin/previewBuild/encounter/rules；observe可能持久化已过期窗口关闭标记 |
| `/api/tianji/join` | POST | 是 | requestId | 冻结服务器当前角色构筑并持久化报名；返回完整status；同账号同届已有报名则返回既有状态 |
| `/api/tianji/challenge` | POST | 是 | requestId/targetId | 挑战同届同组其他真人留影，独立随机种子/战斗；占用本届一次机会并持久化；返回status |
| `/api/tianji/action` | POST | 是 | requestId/encounterId/expectedRevision/id，target可选空 | 执行留影斗法/forfeit、递增revision并结算成绩；返回status；原人生不扣资源、不发物品/修为/灵石 |

requestId为1–64位字母/数字/下划线/短横线/点；POST仅接受表中对应字段，拒绝客户端构筑、归属、成绩及额外字段。action的id非空且≤128字节，target≤128字节；expectedRevision必须是非负int整数。重复action使用相同requestId与相同正文返回既有状态，正文不同或revision过期返回409；写入失败回滚本次操作并允许使用相同requestId重试。

报名/挑战需已有角色、处于本人入道50–59、100–109等十年窗口、人生未结束且无普通斗法/抉择/安排/秘境或当前留影挑战；已关闭窗口不能用回档重开。报名构筑按服务器当前存档冻结，移除临时丹药准备，双方满气血开局。每个同届同组对手一次，胜3分、24回合平1分、败/认输0分。Store健康失败可先返回503。

## 核心领域清单（86条）

下表“只读/动作/换世/存档”分别对应ReadOnly/Mutation/ReplaceGame/Persistence。所有行在多人Web服务中都需要登录；“状态”指上述标准ok+state响应。实际领域校验仍由规则层负责，字段表不会替代可用性预览。

### 建号、状态、存档与传承

| 路径 | 方法 | 类别 | 登录 | 主要输入/默认值 | 返回概要/副作用 |
| --- | --- | --- | --- | --- | --- |
| `/api/state` | GET/POST | 只读 | 是 | 无 | `ok,hasGame,state`；未开局仅 `hasGame:false`；核心层纯只读 |
| `/api/new` | POST | 换世 | 是 | `name="沈知微",gender="女",seed=20261004`；`portraitId`；`traits[]` 或兼容 `talent="sword"`；`allocation{}` | 按与 build-check 相同的校验创建人生、保存立绘并建立恢复点；返回完整状态 |
| `/api/build-options` | GET/POST | 只读 | 是 | 可选 `seed`；省略时按当前种子与事件序号派生，显式0保持0 | `budget,pendingLegacyPoints,maxPoints,talents,stats,innate,portraits`；先天气运、范围与立绘目录 |
| `/api/build-check` | GET/POST | 只读 | 是 | 与 new 相同的角色字段及默认值 | `valid,error,budget,pendingLegacyPoints,spent,allocSpent,remain,traitCount`；合法时另含真实开局 `preview`；方案校验失败HTTP200，通用参数错误可能400 |
| `/api/save` | POST | 存档 | 是 | `slot=1`，范围1–3 | 写账号手动槽；更新最近恢复点；返回状态 |
| `/api/load` | POST | 换世 | 是 | `slot=1`，范围1–4（4是自动档） | 读取并替换当前人生；失败404；返回状态 |
| `/api/import` | POST | 换世 | 是 | 存档字符串 `game`，兼容 `save` / `text`；可选传承字符串 `legacy` | 先校验再替换；可写传承档；返回状态 |
| `/api/export` | GET/POST | 只读 | 是 | 无；需已开局 | `format,version,game,legacy,name,year,realm`；game/legacy为JSON字符串 |
| `/api/slots` | GET/POST | 只读 | 是 | 无 | `slots[]`：slot/automatic/exists/name/realm/year/age |
| `/api/checkpoint/restore` | POST | 存档 | 是 | 无；需意外死亡、未结算且恢复点有效 | 恢复最近快照；返回状态；自然寿终不可恢复 |
| `/api/legacy` | GET/POST | 只读 | 是 | 无 | 传承 earned/runs/budget/milestones，结算资格与当前人生说明 |
| `/api/legacy/draft` | GET/POST | 只读 | 是 | `seed=20261004,roll=0`，roll范围0–2 | `talents[],budget`；建号天赋候选 |
| `/api/legacy/settle` | POST | 存档 | 是 | 无；需有可结算结局 | 写传承档，防重复结算；返回状态及earned/budget |
| `/api/achievements` | GET/POST | 只读 | 是 | 无 | `achievements[]` 保留progress/target/done/points，并提供condition/reward/rewardClaimed及顶层rewardNote；奖励在结算或实际重开时领取 |

### 建角方案与真实开局预览（2026-10-13）

`new` 与 `build-check` 接受同一份角色方案。建议先读取 `build-options`，保留返回的 `innate.seed`，选定天赋、属性与立绘后将相同正文先预检再提交；界面的“锁定方案”是客户端操作，不产生额外令牌或服务端锁定状态。两次请求都按当时账号传承预算重新校验。

| 字段 | 契约 |
| --- | --- |
| `name` / `gender` | 字符串，缺省分别为“沈知微”与“女”；性别仅接受“男”或“女” |
| `seed` | 0–4294967295整数；new/build-check缺省20261004，显式0有效；build-options省略seed才派生种子，因此预检与创建应主动传回 `innate.seed` |
| `traits` / `talent` | `traits` 必须为字符串ID数组；显式 `[]` 表示无天赋；省略时采用兼容字段 `talent`，默认 `sword`；未知、重复、互斥或超预算组合拒绝，不静默删减天赋 |
| `allocation` | 对象，只接受 `apt,luck,insight,body,wealth`；各项为非负整数且受 `stats[].max` 与总预算限制；缺失键补0；显式 `{}` 表示五项全0，省略整个字段才按余点自动分配最多10点资质 |
| `portraitId` | 使用 `portraits[].id`，必须与gender一致；省略或空字符串采用同性别默认立绘；不接受图片URL或任意路径。当前14张立绘复用已有资源，选择不改变门派或属性 |

`build-options.portraits[]` 的每项为 `{id,name,gender,src}`；显示图片使用 `src`，提交使用 `id`。`innate` 包含 `seed,apt,insight` 及两种属性的最终上下限和初始抽取范围。`budget` 与 `pendingLegacyPoints` 已考虑当前人生可结算传承，查询本身不结算或落盘。

`build-check` 合法时返回 `preview.player`、`preview.arts`、`preview.gears`、`preview.seed` 与完整五项 `preview.allocation`。这些视图由局部 `createGame` 经正式出生奖励流程生成；`preview.player.talents[]` 包含 `id,name,tag,cost,desc`，`portraitId` 为稳定选择，`portrait` 为资源路径。它不返回整个人生世界，也不替换当前人生、消耗当前RNG、分配新人生编号或写存档/传承/恢复点。非法方案没有 `preview`；先处理HTTP/`ok`错误，再判断 `valid`。

正式创建、状态查询及后续保存保留 `player.portraitId`；旧档缺字段时显示同性别默认图。导入仍校验立绘ID、性别匹配与字段类型。完整天赋作用入口见 [初始天赋矩阵](INITIAL_TALENTS.md)。

### 时间、计划、修炼与洞府

| 路径 | 方法 | 类别 | 登录 | 主要输入/默认值 | 返回概要/副作用 |
| --- | --- | --- | --- | --- | --- |
| `/api/advance` | POST | 动作 | 是 | `days=本月剩余天数`，1–3600；`autoStop=true` | 推进日历/当前安排；状态及report(daysPassed/stopReason/highlights) |
| `/api/plan` | POST | 动作 | 是 | `plan=当前计划` | 切换日常计划；状态 |
| `/api/choose` | POST | 动作 | 是 | id（当前抉择提供且未禁用的选项） | 处理抉择分支；状态 |
| `/api/cancel-job` | POST | 动作 | 是 | 无；需有安排 | 中止安排；状态、text；按规则处理取消与退款 |
| `/api/breakthrough` | POST | 动作 | 是 | 无；需修为圆满、灵石足、气血≥30，stage2改走zhuji | 消耗突破资源，安排并推进30/90天闭关；状态 |
| `/api/zhuji` | GET/POST | 混合 | 是 | GET查询；POST kind=start/quote/action/bind，含revision；写动作含requestId | 两路筑基准备、报价、运行及本命设置适配（可更换）；只适用于玩家筑基 |
| `/api/supply` | POST | 动作 | 是 | `kind`，`level=0` | 调整灵石/灵气配给；状态 |
| `/api/cave/move` | POST | 动作 | 是 | 无 | 将洞府迁到当前所在地、扣除费用；状态 |
| `/api/cave/upgrade` | POST | 动作 | 是 | 无 | 升级洞府、扣资源；状态 |

### 功法、神通、悟道与宗门

| 路径 | 方法 | 类别 | 登录 | 主要输入/默认值 | 返回概要/副作用 |
| --- | --- | --- | --- | --- | --- |
| `/api/arts/main` | POST | 动作 | 是 | `id` 功法标识 | 设主修并从辅修移除；状态 |
| `/api/arts/aux` | POST | 动作 | 是 | `id` 功法标识 | 切换辅修（最多4部，主修不可兼任）；状态 |
| `/api/arts/focus` | POST | 动作 | 是 | `dao` | 切换修炼大道；状态 |
| `/api/arts/node` | POST | 动作 | 是 | `node`，如大道索引-层级 | 解锁悟道节点；状态 |
| `/api/arts/learn` | POST | 动作 | 是 | `id` 功法标识 | 按权威报价扣一次资粮并推进学习；中途事件可暂停；返回状态，完成时不重复收费 |
| `/api/arts/meditate` | POST | 动作 | 是 | `dao` | 安排并推进30天参悟；状态 |
| `/api/arts/spell` | POST | 动作 | 是 | `id`，兼容 `spell` | 切换神通携带；状态、text、carried |
| `/api/arts/gear` | POST | 动作 | 是 | `id`, `action="equip"` | 兼容装备/卸下/铭刻/分解入口；状态 |
| `/api/arts/spells` | GET/POST | 只读 | 是 | 无 | `spells[],max,manaRegen,carriedCount`；known/learned/equipped/canEquip及权威preview |
| `/api/arts/nodes` | GET/POST | 只读 | 是 | 无 | `nodes[]`：id/dao/daoIndex/level/cost/name/desc/unlocked |
| `/api/join` | POST | 动作 | 是 | `sect` 宗门标识 | 进入宗门试炼流程；状态 |
| `/api/sects` | GET/POST | 只读 | 是 | 无 | `sects[],mentors[]`，试炼、拜师门槛、当前宗门/月差事上限 |

### 秘境与斗法

| 路径 | 方法 | 类别 | 登录 | 主要输入/默认值 | 返回概要/副作用 |
| --- | --- | --- | --- | --- | --- |
| `/api/dungeon/enter` | POST | 动作 | 是 | `id`，可选`cardRewardPreference:{mode,daos}` | 进入秘境并冻结临时卡奖励偏好；状态、targetTab=dungeon |
| `/api/dungeon/sweep` | POST | 动作 | 是 | 仅非空字符串`id` | 至少高于入口一个大境界，复用所在地/有效期/空闲资格；直接进入冻结终奖选择，不自动替选、不发沿途奖励，离境结算1天 |
| `/api/dungeon/choose` | POST | 动作 | 是 | `id` 选项/节点标识 | 选择当前秘境行动；状态、targetTab=dungeon |
| `/api/dungeon/claim` | POST | 动作 | 是 | `id` 奖励选项标识 | 领取秘境奖励；状态、targetTab=dungeon |
| `/api/dungeon/leave` | POST | 动作 | 是 | 无 | 结束/退出秘境探索；状态 |
| `/api/dungeon/intel` | POST | 动作 | 是 | `kind`, `id` | 购买/获取秘境情报；状态 |
| `/api/battle/turn` | POST | 动作 | 是 | `id` 行动/神通/法宝；可选 `target=""` | 出牌/结束回合/脱离；实时状态；奖励由battleFinish统一结算 |
| `/api/conflict` | POST | 动作 | 是 | `npc`, `intent="rob"` | 野外冲突/索财/截杀，可能斗法；状态、text、response、notoriety |
| `/api/conflict/preview` | GET/POST | 只读 | 是 | `npc`, `intent="rob"` | response/name/stones/herbs；资格失败可HTTP200且ok:false |

### 营生、交易、丹药与炼器

| 路径 | 方法 | 类别 | 登录 | 主要输入/默认值 | 返回概要/副作用 |
| --- | --- | --- | --- | --- | --- |
| `/api/task` | POST | 动作 | 是 | `id="errand"` | 安排并推进营生差事；状态 |
| `/api/eco` | POST | 动作 | 是 | `type`, `id`, `batch=1` | 通用营生（如task/commission/hunt/mine/arena/quest/forge）；安排并推进；状态、text |
| `/api/eco-options` | GET/POST | 只读 | 是 | 无 | `options[]`：营生与机缘的耗时/报酬/费用/门槛、完成标记及宗门奖励预览 |
| `/api/buy` | POST | 动作 | 是 | `kind`, `id`, `count=1`，1–1000 | 坊市购买、扣资源；状态 |
| `/api/sell` | POST | 动作 | 是 | `kind`, `id`, `count=1`, `expectedUnitPrice`可选 | 丹药可批量出售，法器count仅允许1；状态、text、gain；过期售价和超过库存/需求/资金的数量拒绝，不部分出售 |
| `/api/trade-stock` | POST | 动作 | 是 | `kind`, `buy=true`, `count=1` | 交易基础凝元丹/灵核等库存；状态、text |
| `/api/book-buy` | POST | 动作 | 是 | `id` 功法标识 | 购买功法书；状态、text |
| `/api/library` | POST | 动作 | 是 | `id` 功法标识 | 贡献兑换功法；状态、text |
| `/api/library/spell` | POST | 动作 | 是 | `id` 神通标识 | 贡献兑换宗门神通；状态、text |
| `/api/craft` | POST | 动作 | 是 | `kind="brew"`, `id`, `batch=1` | 仅炼丹；安排并推进；状态、text；炼器使用forge |
| `/api/use-pill` | POST | 动作 | 是 | `id` 丹药标识 | 服用一枚，按权威规则改变属性/准备/丹毒；状态 |
| `/api/forge` | POST | 动作 | 是 | `main`, `aux`, `core`, `shape="sword"` | 校验配料、安排并推进炼器；状态 |
| `/api/forge-preview` | GET/POST | 只读 | 是 | `main`, `aux`, `core`, `shape="sword"` | `info`：grade/cost/days/chance/材料缺口/blockers及装备/铭刻前后权威预览 |
| `/api/forge-options` | GET/POST | 只读 | 是 | 无 | `materials[],shapes[]`：库存、品阶、属性、来源及可买资格 |
| `/api/equip` | POST | 动作 | 是 | `id` 装备标识 | 装备法器；状态 |
| `/api/gear/unequip` | POST | 动作 | 是 | `id` 装备标识 | 卸下装备；状态 |
| `/api/gear/salvage` | POST | 动作 | 是 | `id` 装备标识 | 分解装备；状态、text |
| `/api/gear/inscribe` | POST | 动作 | 是 | `id` 装备标识 | 铭刻装备、扣费用；状态、text |
| `/api/furnace/buy` | POST | 动作 | 是 | `id` 炼丹炉标识 | 购买/更换炼丹炉；状态、text |
| `/api/furnace/repair` | POST | 动作 | 是 | 无 | 修复当前丹炉、扣费用；状态、text |
| `/api/quest` | POST | 动作 | 是 | `id` 机缘标识 | 执行限时机缘并结算；状态 |

### 人物往来、旅行与世界事件

| 路径 | 方法 | 类别 | 登录 | 主要输入/默认值 | 返回概要/副作用 |
| --- | --- | --- | --- | --- | --- |
| `/api/bond` | POST | 动作 | 是 | `kind`（如lesson/teach/together/part/dismiss） | 师徒/伴侣往来，可能安排并推进；状态 |
| `/api/stipend` | POST | 动作 | 是 | 无 | 领取宗门/师门积累配给；状态 |
| `/api/social` | POST | 动作 | 是 | `npc`, `action` | 与NPC往来，按行为耗时推进；状态、text、relationDelta |
| `/api/social-actions` | GET/POST | 只读 | 是 | `npc` | `actions[]`：可用往来、days/cost/enabled/reason |
| `/api/npc-learn` | POST | 动作 | 是 | `npc`, `kind`, `id`, `begin=false` | 安排向NPC学习；begin=true只开始，默认自动推进；状态 |
| `/api/greet` | POST | 动作 | 是 | `npc` | 打招呼建立交情；状态、text |
| `/api/gift` | POST | 动作 | 是 | `npc`, `kind`, `id` | 赠送物品/装备、改变交情/换装；状态、text |
| `/api/npc-bag` | GET/POST | 只读 | 是 | `npc` | NPC背包与装备借取资格；`items[]`及credit/creditLabel |
| `/api/npc-take` | POST | 动作 | 是 | `npc`, `kind`, `id` | 按交情取用NPC物品；状态、text |
| `/api/roster` | GET/POST | 只读 | 是 | `strangers=false`, `sort="default"` | `roster[]`：通讯录/陌生人、位置、关系、传讯/邀请资格 |
| `/api/message` | POST | 动作 | 是 | `npc` | 远程传讯；状态、text |
| `/api/invite` | POST | 动作 | 是 | `npc` | 邀请NPC到身边；状态、text |
| `/api/story` | POST | 动作 | 是 | `id`, `choice=0`（0或1） | 选择故事分支，安排并推进；状态 |
| `/api/wild` | POST | 动作 | 是 | `id` 野外任务标识 | 安排并推进野外行动；状态 |
| `/api/expedition` | POST | 动作 | 是 | 无 | 后山7天游历；状态 |
| `/api/travel` | POST | 动作 | 是 | `to`, `mode="boat"`, `begin=false` | 扣旅费并启程；begin=true只开始，默认推进；状态、text、arrived、report |
| `/api/location-service` | POST | 动作 | 是 | `id`, `begin=false` | 当前地点服务；begin=true只开始安排，默认推进；状态、targetTab |
| `/api/tide` | GET/POST | 只读 | 是 | 无 | 统一只读兽潮state视图，含daysLeft、battlefields、leaderboard、market；不更新检查点 |
| `/api/tide/fight` | POST | 动作 | 是 | battlefield=本人最高战场，显式值仅整数0–3 | 对应大境界妖兽；四战场每杀1/3/9/27分；状态、text、lethal |
| `/api/tide/market/buy` | POST | 动作 | 是 | eventId,kind=herb/material,id,count=1,expectedUnitPrice可选 | 当前城市且开放期可购有限高阶库存；全部预检成功后一次性扣钱/发货；状态、text、cost |
| `/api/tide/hunker` | POST | 动作 | 是 | 无 | 避险度过兽潮；状态、text |

一键交易复用以上动作：`state.tide.market.items[].maxPurchase` 提供 `{enabled,count,unitPrice,totalCost,reason}`，数量取库存、灵石和行囊剩余容量的交集。`state.pillCatalog[].maxSale` 提供 `{enabled,count,unitPrice,totalGain,reason,limitReason}`，数量取持有量、当月同类需求、收购资金及玩家灵石余额容量的交集。前端提交对应的报价数量和单价；动作按最新活动状态再次检查。查询不补充月资金、不改变库存或随机数。字段是展示快照，不是锁定库存或价格的凭证。

## 展示预览的权威边界

非战斗预览统一标记reference:true并附context，使用真实规则层计算，不能直接视为当前敌方实战结果：

- `state.player.combatReference`给出hp/hpMax、开局mana、manaRegen/manaCap、shield、swordQi、reduction、speed、damageBuff；日常health另为0–100百分制。
- `arts/spells[].preview`与携带spellList/mandatorySpells含damage/shield/heal/healPotential、cost、energy/manaGain、qi/draw/drawNext、hpMaxChange/manaRegenDelta、storedMana、damageBonusPct/reductionPct/guardPct、statuses、conditions。known/learned/equipped/canEquip/canUnequip等用于学习与携带资格。
- `state.gears[].ability.preview`、passive.delta与canEquip来自局部装备投影；actions包含enabled/reason/costStones/returnStones。forge-preview补充材料角色与缺口、灵石缺口、blockers以及inscribedAbility/inscribedPassive。
- `state.pillCatalog[]`覆盖全部丹药，包括effect/effectValue/baseEffect/catalogEffect/usage、owned/used/tolerance、poisonAfter/poisonDelta、requiredStage/requiredRealm和enabled/reason；breakthrough给出真实chance/cost/days/failureLoss及消耗说明。`catalogEffect` 为角色无关的本地标称能力，不受当前境界、属性、天赋、耐药与库存截断；修为丹另提供 `catalogEffectValue`（本地标称修为点）。`effect` / `effectValue` 仍为当前真实服用结果，不能互换；修为丹 `usage` 列出标称值、小境25%上限及距圆满余量。`market[].grade` 来自物品定义，未知为0；丹药市场与 `brewOptions[]` 也提供 `catalogEffect`，后者增加真实配方品阶。
- battle视图的hand/artifacts使用实时battlePreview，enemyMana/enemyMaxMana与intentDescription/intentDamage/intentShield/intentHeal/intentSpells反映当前战斗；Tianji encounter复用公开battleView。enemyActionCount为当前预演实际招式数，enemyActionLimit为境界行动上限，护盾/治疗/抽牌也计作动作；修士没有预演招式时intentName明确显示不行动或斗法已结束，不回退旧攻击标题。妖兽使用独立的beastActionLimit/beastEnergyBudget/beastActions逐段信息，字段与冻结计划见BEAST_ACTIONS.md。

业务标识采用服务端实际id；界面展示名称、中文兼容值与HTTP字段不应被用于推导或复制数值公式。JSON中直接使用上述已存在字段，前端别名如isLearned不构成额外HTTP端点。

## 学习、经济与 NPC 状态补充（2026-10-13）

这些是既有状态视图和存档字段的变化，不是新增HTTP端点。

| 字段位置 | 含义与使用方式 |
| --- | --- |
| `state.arts.manualList[].nextDays/nextCost/maxed` | 已修功法下一层的天数、灵石和满层标记；与开工共用 `manualStudyInfo`，满五层时报价0并拒绝继续学习 |
| `state.manualSources[].learnDays/learnCost/canLearn/learnReason` | 抄本来源页的同一学习报价和资格；`price` 仍是抄本购买价格，不能代替研习费用 |
| `state.brewOptions[].output` | 单炉成功时的实际产量，包含天赋、悟道与功法增产，最多4枚 |
| `state.brewOptions[].sellPrice/expectedOutput/expectedSaleIncome` | 单枚收购价、单炉期望产量 `chance × output`、单炉期望售出收入 `chance × output × sellPrice`；收入未扣原料、炉费、耐久，不表示必然成功或当前能全额售出 |
| `state.marketCash/marketBudget/marketTier` | 本月实际剩余收购资金、按当前条件计算的下次月预算、对应境界的市场品阶；查看、晋阶或读档不会把剩余资金补满 |
| `state.nearby[].loadout` | NPC实际构筑的 `main,spells,manuals,nodes`，包含服丹后已获得的悟道节点；仅查询不会自动服丹 |

功法学习的一阶第一层基准为40天、24灵石；每一已修层数乘1.2，每一功法阶次乘2，悟性及过目不忘只折扣时间。`/api/arts/learn` 与 `/api/eco` 的 `type:"learn"` 都在开工时冻结并扣除报价，自动推进或后续 `/api/advance` 完成时不再次扣费。中途遇到事件可暂停，返回的安排状态为准。已开始的旧档学习保留原费用与时间；详情见 [功法参悟与 NPC 服丹](MANUAL_STUDY_AND_NPC_PILLS.md)。

炼丹批量支持1/3/5/10炉；开工冻结存档中的 `job.craftOutput`，完成时以成功炉数乘该值，中途改变构筑或读档不会重算已开工产量。成药零售与收购沿用服务端统一报价；月资金按境界分层，仍受单月剩余现金与同类需求约束，高阶预算不会开放高阶药材采购。公式、成本口径及代表丹方见 [炼丹经济说明](ALCHEMY_ECONOMY.md)。

NPC新增记录保存在导出/导入JSON中的 `npcs[].pillState`，**不作为 `state.nearby[]` 的完整字段直接暴露**：

- `used` 保存每味耐药次数，`daoExp` 与 `nodes` 保存丹药带来的大道经验和节点；`poison,aptPills,hpBonus,heart,mind,speed,points,repel` 保存丹毒、额度、常驻属性与余量。
- `breakBuff` 保存一次突破准备；`combatPrep` 与 `combatBuff` 保存下一场战斗准备。真实战斗开局将准备转入敌方战斗状态后清空，读档或只读预览不重复扣药。
- 年度开始和真实战前各最多服3枚，赠丹只检查赠送的1枚；实际扣除 `bag["potion:" + id]`，遵守适境、流派、耐药、丹毒和收益上限。旧 `pillsUsed` 迁移为至少一次使用记录，已结算属性不重放。新增字段经类型与范围校验后才允许导入。

## 精确路径核对规则

本表99条路径全部在实际分派中存在，逐行路径无重复，核心表86条全覆盖。服务层的前缀转发、HTML页面、assets文件与JSON字段不是API端点；不同请求方法和同一路径在多层出现也不重复计数。新增端点需同时更新RouteInfo（核心）、真实handler与本表，再复核方法/登录/阻塞和错误契约。

## 工坊整单补料（本版新增）

2026-10-14 便利补充未增加 HTTP 路由：`state.serviceRoutes.market/intel` 和 `quote.travel` 包含 `enabled,alreadyHere,destination,destinationName,mode,distance,days,cost,reason,targetTab,targetSub`。路线由领域选择最近实际供应城市/天机阁并复用 `travelPlan`；前端使用现有 `/api/travel`，到达只打开目标页，不触发购买。制作缺料无法城市直购时，高阶材料可引导情报目录，情报仍是材料位置，尚未实施功法线索改版。

`state.bonds.stipendAction` 包含 `enabled,reason,paid,owed`。`/api/stipend` 保留原协议，允许异地领取；已有工作、斗法、抉择、秘境、月额或账目异常明确拒绝，府库空不消耗领取月额。附近仙友新增 `buildId/buildName`，由真实模板或实际大道/已带核心神通生成，仅作展示分类，不改变装备或决策。

材料购买成功响应的 `quote` 重新计算剩余缺料，备齐后不会继续推荐补料路线；该单实际花费在 `text` 中说明。

| 路径 | 方法 | 效应 | 输入 | 输出 |
| --- | --- | --- | --- | --- |
| `/api/craft/materials/quote` | GET/POST | ReadOnly | kind=brew/forge, id, batch=1；forge也可传main/aux/core/shape | quote.valid/enabled/reason/totalCost/materials；不写入资源 |
| `/api/craft/materials/buy` | POST | Mutation | 同上，expectedCost可选非负整数 | state/text/quote；只购买缺口；失败保留完整状态 |

炼丹支持1/3/5/10炉；炼器支持1至100份备料，保持实际每次开炉1件。报价叠加本地货源、总库存容量、当前灵石和工作阻塞。客户端建议传expectedCost，金额变化时拒绝确认。
state新增herbCatalog/materialCatalog供野外行囊识别名称与品阶；每件gears.salvage含materials、costStones、estimated、sourceNote，与实际gear/salvage共用回收规则。

## 洞府灵田与永久小绿瓶

所有动作必须POST，并要求回到自己的洞府且无进行中的斗法、抉择、工作或秘境。数量和阶位为严格整数，拒绝不改变材料、余额、日期或RNG。规则版本2移除储备与日费，成长和费用由后端统一报价。

| 路径 | 正文 | 成功行为 |
| --- | --- | --- |
| `/api/cave/garden/plant` | `plotId,herbId,count` | 扣已有药材，按独立批次播种，同格同种最多12份 |
| `/api/cave/garden/harvest` | `plotId` | 只收成熟批次，每份种苗产3份草药 |
| `/api/cave/garden/reserve/deposit` | 旧接口 | 机制已移除，明确拒绝且不改状态 |
| `/api/cave/garden/reserve/withdraw` | 旧接口 | 机制已移除，明确拒绝且不改状态；旧余额在真实日或园艺动作自动退回 |
| `/api/cave/garden/clear` | `plotId` | 放弃所有种苗；界面须确认 |
| `/api/cave/green-bottle/upgrade` | `expectedGrade` | 按同一报价永久升一级；阶位0至6，过期/资源不足不扣费 |
| `/api/cave/green-bottle/dungeon` | `grade,expectedGrade` | grade为4/5/6，瓶阶至少3/4/5；洞府内创建并进入一次真实秘境，无额外门票 |

两个绿瓶端点在 `interface/routes.cpp` 登记为 `Mutation`，沿用全部默认忙碌阻断，不开放斗法、抉择、工作或秘境中调用；领域再次校验真实洞府位置和资格。请求拒绝未知键、缺字段、非整数、越界和过期阶位。

`state.cave.greenBottle` 无天赋仅有 `hasTalent:false`；持有者含当前/下阶品阶与附加日增、自然/总日增、`upgrade`统一灵石及材料缺量报价、两组固定6项`{grade,days}`成熟周期对比，以及已解锁`dungeons`入口（品阶、专属材料名、保底10份、最低瓶阶、资格/原因）。`garden.plots`提供`naturalUnitsPerDay/bottleBonusUnitsPerDay/effectiveUnitsPerDay`和各批`estimatedDays`，不再暴露储备、日费或灵气催生计划。

`state.cave.access/training/garden`为纯只读快照；`state.workshopAvailable`仅表示所在城市或自己的洞府有工坊条件，不改变野外坊市资格。`state.*.pricing`给出完整材料价值、1.5/3倍基价和法器逐项额外出售乘数；操作时重新按权威价格检查。`/api/mode.features.resourcePoints`给出requested/available/mutationsAllowed/status。骨架即使请求开启也不可变更游戏。

### 2026-10-16 三项规则接入

- 玩家`health`改为绝对点数，`healthMax`为权威上限，`healthUnit`为“点”；NPC和副本内部仍为百分比。秘境视图保留兼容`health`，新增`healthCurrent/healthMax`供显示。
- `state.zhuji`仅为公开局面，不含随机种子、随机流或未揭示穴位参数。准备期通过advance推进，active期间禁止其他人生写动作，仍可保存/读档。成功后的本命选择面板不替换后续境界突破入口。
- `/api/zhuji`为支持GET的混合路由；表中ReadOnly元数据只表示GET分派资格，POST的start/action/bind仍由独立严格输入校验、revision、requestId和真实领域状态保护并保存。quote不写状态。
- 存档顶层`healthRulesVersion=2`，缺失或1按旧百分比迁移；新永久基础气血、丹药寿元、指定突破境界和战斗丹枚数均单独持久化。既有战斗hp/max/intent不在加载时重算。


## 2026-10-18 玩法与操作改进

以下九个端点按 RouteInfo 统一执行方法和活动守卫。查询可GET/POST；需要参数时使用POST JSON。报价不推进日历、RNG或库存。

| 路径 | 类型 | 输入与返回 |
| --- | --- | --- |
| `/api/pills/use-quote` | ReadOnly | `{id,count}`，count为1—1000整数；返回扁平报价：enabled/reason、ownedCount、maxUsableCount、effect及单位、poisonAfter、usedAfter等 |
| `/api/pills/use` | Mutation | 同上；重验整批，拒绝不部分服用，成功返回新state |
| `/api/alchemy/quote` | ReadOnly | `{id,count}`，count为1/3/5/10炉；返回总费用/天数/耐久/材料需有缺/每炉成功率与产出/整批期望，不代替实际成丹 |
| `/api/tianji-shop` | ReadOnly | 无；返回`tianjiSteward`固定货架、所在分号执事与资格 |
| `/api/tianji-shop/quote` | ReadOnly | `{npc,offer,count}`；返回`{ok:true,quote}`，含fixedUnitPrice/totalCost/enabled/reason |
| `/api/tianji-shop/buy` | Mutation | `{npc,offer,count,expectedCost}`；在当前分号重验固定价及容量，成功返回新state |
| `/api/arts/lifebound` | Mutation | `{spellId}`；筑基后非活动中更换本命，固定攻防亦可；类型由筑基路线决定，不增加携带槽 |
| `/api/ascension-tournament` | ReadOnly | 无；返回`ascensionTournament`届次资格、四战进度、奖励及规则 |
| `/api/ascension-tournament/enter` | Mutation | 空对象；邀请或整备后入口报名，冻结三宗对手并立即进入首战 |

升仙邀请使用原`/api/choose`，id为accept/decline；战斗仍用`/api/battle/turn`。四战之间自动衔接，大会活动中拒绝换装、服丹与其他人生动作。每届参赛一次，前三胜后进入下一战前恢复20%最大气血，末胜只发500灵石与一枚筑基丹。

完整state新增`tianjiSteward`、`ascensionTournament`及`arts.lifebound`；`arts/spells`每项新增本命设置资格。`brewOptions[].batchQuotes`按1/3/5/10返回同一后端炼丹报价。法器视图仅由白名单形状生成appearanceId/illustration，不读取用户资源路径。
