# 法器固定点数修正

## 目标与影响范围

当前新版法器真实结算仍使用人物战力/最大气血/功法倍率，工坊和斗法预览同时复用了错误公式。本次行为修正落实玩家最终指定的三组品阶点数：直伤20/40/120/450/1800，护体20/50/150/500/2000，绵护3/8/25/100/400。

| 项目 | 本次范围与依据 |
| --- | --- |
| 权威入口 | combat/artifact_effects的说明、被动快照及实际动作；artifact.cpp的能力元数据同步固定基数 |
| 调用方 | battleEffect真实/预览共用；普通NPC和留影视角共用法器；工坊forge_api、装备equipment_views及战斗battle_views消费后端说明 |
| 数据契约 | 既有炼器和战斗动作路由/装备ID不变；artifactState冻结字段改为roundShield/roundBurn/roundPoisonDamagePerLayer整数点数，codec/形状/范围一同校验 |
| 运行条件 | 新数值查询无RNG/时钟/存档副作用；动作依原校验提交；不停止现有服务，不接触玩家存档 |
| 规则边界 | 只改ForgeVersion2；旧三料装备入口原样；清浊/破护与抽换牌协议不变；主动灵纹、流派资源兑现、目标防御及通用触发明确保留 |
| 持续伤害 | 法器新增火/毒按固定点数；普通施毒/毒增长只累计新增层，避免重新放大已有法器毒；双方共享镜像路径 |
| 两套前端 | web/forge、inventory-panel、cards及v2/views/inventory/battle直接读取后端点数，无前端数值公式 |
| 同步项 | FORGING_V2、combat/crafting README、持久化校验、固定点数与codec回归、真实浏览器工具；新增测试后同步VS列表 |
| 搜索证据 | mfBattleDamageBasis、EffectPower、mfApplyArtifactAction、RoundShieldRatio/RoundBurnScale、poisonDamage赋值；覆盖src/tests/tools/web/docs |

## 验证记录

RED：旧版EXE在隔离随机端口、临时账号，通过真实Edge与正规导入取得15组工坊报价；仍显示战力百分比。证据位于`tmp/artifact-fixed-browser-20261017/red-8V7Mqe/report.json`。后续检查结果完成后追加，不借用历史通过记录。

### 本轮最终检查

- 当前工作区VS工程已同步；`visual-studio/ningzhou.sln`使用MSBuild Release/x64、输出`build-vs-current/Release/`编译成功。core与Web单独构建亦成功；仅有既有编译警告。
- 隔离CWD及TEMP/TMP执行完整C++测试，23,158项通过，1项测试夹具失败。失败是混合毒预览误传卡牌UID；`battlePreview`要求神通ID，实际`battleTurn`才接受UID。将测试参数改为`corrode`后重新编译测试工程，`--forge`定向回归2,095项通过、0失败。生产代码未因该测试失败再次变更；未重复执行整套全量测试。
- 新EXE真实Edge检查123项通过：15组工坊报价、不同人物境界/增益下的主动结算、入战与下轮供盾、七类资源兑现、桌面与390px手机操作、重复点击、503失败不扣费及恢复。浏览器无JS错误。记录：`tmp/artifact-fixed-browser-20261017/green-hRi6To/report.json`；该测试临时服务已停止。
- `tools/forge-web-check.mjs --dom`33项通过；`tools/check-artifact-status.mjs`23项通过；新增浏览器脚本语法和本轮文件`git diff --check`通过。
- 独立只读代码审查未发现本轮阻塞问题，核对了固定点数、预览共用、混合毒追加、双方快照与14字段codec契约。

### 交付及限制

- 修正版完整服务器包：`dist/宁州一梦-完整服务器版-20261017-r3.zip`，47,040,966字节，默认端口8110。242个归档文件已逐项按长度与SHA256核对，0失败；解压目录及启动器已生成。
- 实测及包内EXE SHA256：`B86125798FF85C04279AC9022FFFD3A5FE5A17CF7379EE333B73FE192A705CDF`；ZIP SHA256：`B76C4223088C8C83E6F35275842AC7BAAA2698956E280D6711B55D4DAE3F018E`。校验记录：`tmp/artifact-fixed-20261017/archive-verification.json`。
- 代启动r3用户预览服务的操作被自动审批以`blocked by policy`拦截，未给出具体原因；未重试启动，8110没有由本轮启动。归档提取与哈希验证单独完成。已有8106/8108服务及玩家存档保持原样。未验证跨机局域网连接及解压版手动启动流程。
- 既有手机体验缺口：网络失败后，法器弹层需关闭重开才能重试；本轮验证失败不扣费且不自动重放，未改该前端恢复行为。
- 独立审查另记录既有留影初始化缺口：`battleStartShadow`初始镜像未复制挑战者入战毒，随后回写会清空该入战供毒；此问题早于本轮，与三组数值表无关，本轮未扩展修复初始化流程。
- 本次限定新版十材法器，不改变旧三料装备数值；按用户要求不增加旧快照格式兼容。灵纹明确加成、资源层数兑现、目标防御、共用护盾容量与通用触发仍按各自机制生效。
