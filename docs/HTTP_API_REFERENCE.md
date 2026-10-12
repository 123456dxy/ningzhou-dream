# HTTP API 参考与统计

核对日期：2026-10-12。来源为 `src/core/interface/routes.cpp`、`src/core/api.cpp`、`src/web_main.cpp` 与 `src/core/tianji.cpp` 的实际分派。统计对象为精确 `/api/...` JSON 端点；相同路径的 GET/POST 计一个端点。`/api/tianji/` 前缀不是端点，静态页面/资源文件不计 API。

责任边界：我们维护核心规则与桌面 UI；账号、联机传输、天机大比由另一位成员维护。服务层和 Tianji 清单仅供对接参考，本轮保持这些实现与原位置不变。

## 去重统计

| 分派归属 | 精确端点 | 查询 | 写操作 | ReplaceGame | Persistence |
| --- | ---: | ---: | ---: | ---: | ---: |
| 核心 RouteInfo 表 | 85 | 20 ReadOnly | 59 Mutation | 3 | 3 |
| 服务层账号/社区（排除转发Tianji） | 7 | 3 | 4 | 0 | 0 |
| Tianji Store | 5 | 2 | 3 | 0 | 0 |
| 总计（路径去重） | **97** | **25** | **66** | **3** | **3** |

核心四类取自真实 `RouteEffect`。服务层/Tianji 的查询与写操作按用途列示，它们没有使用该 enum；查询可维护会话、缓存或天机窗口记录，不能据此推断服务器完全没有 I/O。`/api/tianji/board` 在 web_main 与 Store 均出现，但最终归 Tianji，仅计一次。web_main 的精确拦截共8条，其中7条为服务专属。

## 请求、登录与通用返回

- 多人 Web 服务中，全部85个核心端点均需有效 `nz_sid` Cookie。只有nickinfo、login、logout、leaderboard与tianji/board可不登录；whoami/password/feedback以及其余Tianji端点要求登录。直接调用C++ `api::handle` 的单机/测试会话不处理登录Cookie。
- 核心输入从 JSON **请求正文**读取，不从URL查询串读取；需要参数的查询建议使用POST JSON。表中未列默认值的字符串参数缺省为`""`，随后由对应规则校验；“无”表示没有业务输入。非空正文必须是JSON对象。
- 所有20条核心 ReadOnly 端点均支持 **GET和POST**；其余核心端点只支持POST。成功的核心动作通常返回 `{ok:true,state:{...}}`，某些动作附加text/report；可能出现checkpointWarning。state包括玩家、功法、世界、经济、社交、秘境与恢复点视图。
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

核心整数参数days/slot/count/choice/level/roll/batch/expectedCost必须为有限、整数、int范围内数值；seed为0–4294967295整数；allocation各值为0–MAX_POINTS整数。解析校验先于核心方法检查；登录、服务写锁或Tianji存储健康检查也可能先于后续业务检查，因此不要仅依靠URL预测错误码。

## 核心动作阻塞顺序

ReadOnly跳过人生动作阻塞；ReplaceGame（new/import/load）可替换人生；Persistence（save/legacy/settle/checkpoint/restore）需已开局，但跳过一般动作阻塞并执行各自专有校验。Mutation需已开局，随后按下列顺序保护：

1. 秘境进行中：只放行5条已识别dungeon动作与battle/turn。
2. 人生已结束：拒绝一般动作。
3. 普通斗法进行中：只放行battle/turn。
4. 有待处理抉择：只放行choose与battle/turn。
5. 有在途安排：只放行advance、cancel-job、plan、choose与battle/turn。

未知路径按Mutation处理，不能凭dungeon前缀绕过保护。天机留影挑战进行中，Web服务另外把全部核心写动作（包括换档、保存）拦为409；20条核心查询仍可用。Tianji动作操作独立挑战存储，不使用人生的battle/turn。

## 服务层账号与社区（7条）

| 路径 | 方法（当前实现） | 登录 | 主要输入/默认值 | 返回与副作用 |
| --- | --- | --- | --- | --- |
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

## 核心领域清单（85条）

下表“只读/动作/换世/存档”分别对应ReadOnly/Mutation/ReplaceGame/Persistence。所有行在多人Web服务中都需要登录；“状态”指上述标准ok+state响应。实际领域校验仍由规则层负责，字段表不会替代可用性预览。

### 建号、状态、存档与传承

| 路径 | 方法 | 类别 | 登录 | 主要输入/默认值 | 返回概要/副作用 |
| --- | --- | --- | --- | --- | --- |
| `/api/state` | GET/POST | 只读 | 是 | 无 | `ok,hasGame,state`；未开局仅 `hasGame:false`；核心层纯只读 |
| `/api/new` | POST | 换世 | 是 | `name="沈知微",gender="女",seed=20261004`；`traits[]` 或兼容 `talent="sword"`；`allocation{}` | 替换当前人生，建立恢复点；返回完整状态 |
| `/api/build-options` | GET/POST | 只读 | 是 | `seed=0`；0/缺省按当前种子与事件序号派生 | `budget,maxPoints,talents,stats,innate`；先天气运和范围 |
| `/api/build-check` | GET/POST | 只读 | 是 | `traits=[]`, `allocation={}` | `valid,error,budget,spent,allocSpent,remain,traitCount`；方案不合法仍HTTP200 |
| `/api/save` | POST | 存档 | 是 | `slot=1`，范围1–3 | 写账号手动槽；更新最近恢复点；返回状态 |
| `/api/load` | POST | 换世 | 是 | `slot=1`，范围1–4（4是自动档） | 读取并替换当前人生；失败404；返回状态 |
| `/api/import` | POST | 换世 | 是 | 存档字符串 `game`，兼容 `save` / `text`；可选传承字符串 `legacy` | 先校验再替换；可写传承档；返回状态 |
| `/api/export` | GET/POST | 只读 | 是 | 无；需已开局 | `format,version,game,legacy,name,year,realm`；game/legacy为JSON字符串 |
| `/api/slots` | GET/POST | 只读 | 是 | 无 | `slots[]`：slot/automatic/exists/name/realm/year/age |
| `/api/checkpoint/restore` | POST | 存档 | 是 | 无；需意外死亡、未结算且恢复点有效 | 恢复最近快照；返回状态；自然寿终不可恢复 |
| `/api/legacy` | GET/POST | 只读 | 是 | 无 | 传承 earned/runs/budget/milestones，结算资格与当前人生说明 |
| `/api/legacy/draft` | GET/POST | 只读 | 是 | `seed=20261004,roll=0`，roll范围0–2 | `talents[],budget`；建号天赋候选 |
| `/api/legacy/settle` | POST | 存档 | 是 | 无；需有可结算结局 | 写传承档，防重复结算；返回状态及earned/budget |
| `/api/achievements` | GET/POST | 只读 | 是 | 无 | `achievements[]` 的progress/target/done/points；展示查询 |

### 时间、计划、修炼与洞府

| 路径 | 方法 | 类别 | 登录 | 主要输入/默认值 | 返回概要/副作用 |
| --- | --- | --- | --- | --- | --- |
| `/api/advance` | POST | 动作 | 是 | `days=本月剩余天数`，1–3600；`autoStop=true` | 推进日历/当前安排；状态及report(daysPassed/stopReason/highlights) |
| `/api/plan` | POST | 动作 | 是 | `plan=当前计划` | 切换日常计划；状态 |
| `/api/choose` | POST | 动作 | 是 | id（当前抉择提供且未禁用的选项） | 处理抉择分支；状态 |
| `/api/cancel-job` | POST | 动作 | 是 | 无；需有安排 | 中止安排；状态、text；按规则处理取消与退款 |
| `/api/breakthrough` | POST | 动作 | 是 | 无；需修为圆满、灵石足、健康≥35 | 消耗突破资源，安排并推进30/90天闭关；状态 |
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
| `/api/arts/learn` | POST | 动作 | 是 | `id` 功法标识 | 安排并推进功法学习；状态 |
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
| `/api/dungeon/enter` | POST | 动作 | 是 | `id` 秘境标识 | 进入秘境；状态、targetTab=dungeon |
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
| `/api/sell` | POST | 动作 | 是 | `kind`, `id` | 出售一次物品/装备；状态、text |
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
| `/api/tide` | GET/POST | 只读 | 是 | 无 | 兽潮完整展示：active/severity/monthsLeft等统计与fallen |
| `/api/tide/fight` | POST | 动作 | 是 | 无 | 参与抵御兽潮，可能斗法；状态、text、lethal |
| `/api/tide/hunker` | POST | 动作 | 是 | 无 | 避险度过兽潮；状态、text |

## 展示预览的权威边界

非战斗预览统一标记reference:true并附context，使用真实规则层计算，不能直接视为当前敌方实战结果：

- `state.player.combatReference`给出hp/hpMax、开局mana、manaRegen/manaCap、shield、swordQi、reduction、speed、damageBuff；日常health另为0–100百分制。
- `arts/spells[].preview`与携带spellList/mandatorySpells含damage/shield/heal/healPotential、cost、energy/manaGain、qi/draw/drawNext、hpMaxChange/manaRegenDelta、storedMana、damageBonusPct/reductionPct/guardPct、statuses、conditions。known/learned/equipped/canEquip/canUnequip等用于学习与携带资格。
- `state.gears[].ability.preview`、passive.delta与canEquip来自局部装备投影；actions包含enabled/reason/costStones/returnStones。forge-preview补充材料角色与缺口、灵石缺口、blockers以及inscribedAbility/inscribedPassive。
- `state.pillCatalog[]`覆盖全部丹药，包括effect/effectValue/baseEffect/usage、owned/used/tolerance、poisonAfter/poisonDelta、requiredStage/requiredRealm和enabled/reason；breakthrough给出真实chance/cost/days/failureLoss及消耗说明。
- battle视图的hand/artifacts使用实时battlePreview，enemyMana/enemyMaxMana与intentDescription/intentDamage/intentShield/intentHeal/intentSpells反映当前战斗；Tianji encounter复用公开battleView。

业务标识采用服务端实际id；界面展示名称、中文兼容值与HTTP字段不应被用于推导或复制数值公式。JSON中直接使用上述已存在字段，前端别名如isLearned不构成额外HTTP端点。

## 精确路径核对规则

本表97条路径全部在实际分派中存在，逐行路径无重复，核心表85条全覆盖。服务层的前缀转发、HTML页面、assets文件与JSON字段不是API端点；不同请求方法和同一路径在多层出现也不重复计数。新增端点需同时更新RouteInfo（核心）、真实handler与本表，再复核方法/登录/阻塞和错误契约。

## 工坊整单补料（本版新增）

| 路径 | 方法 | 效应 | 输入 | 输出 |
| --- | --- | --- | --- | --- |
| `/api/craft/materials/quote` | GET/POST | ReadOnly | kind=brew/forge, id, batch=1；forge也可传main/aux/core/shape | quote.valid/enabled/reason/totalCost/materials；不写入资源 |
| `/api/craft/materials/buy` | POST | Mutation | 同上，expectedCost可选非负整数 | state/text/quote；只购买缺口；失败保留完整状态 |

炼丹支持1/3/5/10炉；炼器支持1至100份备料，保持实际每次开炉1件。报价叠加本地货源、总库存容量、当前灵石和工作阻塞。客户端建议传expectedCost，金额变化时拒绝确认。
state新增herbCatalog/materialCatalog供野外行囊识别名称与品阶；每件gears.salvage含materials、costStones、estimated、sourceNote，与实际gear/salvage共用回收规则。
