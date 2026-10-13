# 功法插图

当前正式目录的 40 本功法各有一张独立图片，按稳定功法 ID 命名并选择。即使同属木道，青木诀、长生诀、龟息术、鸩羽诀、万木同春经也分别展示不同卷轴形制和主题物件。功法名称、层数、数值与操作按钮继续由游戏文字显示，图片中不烘焙这些内容。

## 资源与接入

- 成品：本目录恰好 40 个 `<manual-id>-v1.webp`，全部为 512 × 512 的不透明方图。
- 目录：`manifest.mjs`。正式功法稳定 `id` 优先选择逐本图片；仅未来未知 ID 或缺少 ID 的条目才使用大道默认图。不匹配中文名称、描述或价格。
- 使用：`web/arts.mjs` 的 `renderManualIllustration` 用于已修功法卡、抄本交易行、流派构筑组件，`compact` 参数控制紧凑列表。各模块无需计算图片路径。
- 降级：完全未知条目显示“卷”字；图片载入失败退回道字底图，不影响修习或交易操作。
- 样式：`web/manuals-art.css`，只限定插图及功法组件。

## 生成与压缩

使用内置 `image_gen`。先独立生成金、木、水、火、土、剑、神、气八张基础图，并分别用于金虹诀、青木诀、星河真法、焚心诀、厚土诀、剑典·人道篇、养魂诀、吐纳功；其余 32 本均通过独立编辑调用，以相应基础图为风格参考重新生成核心图案。变更包括牛首厚册、生命树卷、九珠环流、双盘阵图、炼丹鼎炉、铸器锤砧、雨幕折卷、龟甲毒羽、灯火珠焰、人剑观想、链锁山岳等。没有用 CSS 调色、复制同图、裁剪或重命名冒充独立图片。

每本的实际完整提示、生成方式、参考基础图和原始生成文件名保存在 `generation-spec.json`。原始 PNG 保留于本机 Codex `generated_images` 输出目录，没有放入工作区或版本库。

成品通过 Sharp/libvips 等比例缩放并转换为 WebP（quality 82，effort 6），没有通过 Python 编辑图像。40 张成品总计 **2,255,806 字节（约 2.15 MiB）**，最大 **73,886 字节**，每张低于 90 KiB；40 份原始 PNG 共 109,777,500 字节，减少约 97.9%。未映射的旧 `qi-v1.webp` 已移除，其图正式属于 `tuna-v1.webp`。

## 校验和维护

执行 `node tools/check-manual-art.mjs`，无需启动服务器、安装图片依赖或访问玩家存档。该脚本核对当前 C++ 的 40 本目录、每本路径、真实 WebP 容器和尺寸、体积、内容 SHA256、未知项回退与图片错误隔离。当前 **256 项检查通过，40 个路径和 40 个 SHA256 均唯一**。`asset-check.json` 保存每本的名称、ID、路径、字节、尺寸及哈希；需要更新报告时执行 `node tools/check-manual-art.mjs --write-report`。

新增正式功法时，为新稳定 ID 交付独立图片并更新 `MANUAL_ART` / `MANUAL_ART_BY_ID`。替换现有图片应添加 `v2` 文件并更新目录，避免覆盖浏览器已缓存的旧图。

## 视觉参考

搜索《觅长生》功法界面，仅参考“按流派颜色辨识、图标辅助文字、古卷与修炼意象”的通用设计原则。本目录像素素材全部重新生成，没有下载或复制参考游戏素材。

- [《觅长生》功法配置界面参考](https://www.9game.cn/wzry/4527302.html)
- [土之道的功法配置截图参考](https://www.3dmgame.com/gl/3848513_11.html)

## 正式功法清单

| 稳定 ID | 功法 | 大道 | 独立图片 |
| --- | --- | --- | --- |
| tuna | 吐纳功 | 气 | tuna-v1.webp |
| bull | 蛮牛护体功 | 体 | bull-v1.webp |
| wood | 青木诀 | 木 | wood-v1.webp |
| sword | 剑典·人道篇 | 剑 | sword-v1.webp |
| water | 星河真法 | 水 | water-v1.webp |
| fire | 焚心诀 | 火 | fire-v1.webp |
| earth | 厚土诀 | 土 | earth-v1.webp |
| gold | 金虹诀 | 金 | gold-v1.webp |
| longlife | 长生诀 | 木 | longlife-v1.webp |
| zhou | 大周天吐纳 | 气 | zhou-v1.webp |
| mind | 养魂诀 | 神 | mind-v1.webp |
| array | 两仪阵经 | 阵 | array-v1.webp |
| pill | 丹火诀 | 丹 | pill-v1.webp |
| forge | 百炼真解 | 器 | forge-v1.webp |
| wheel | 重元转轮诀 | 气 | wheel-v1.webp |
| heavy | 重剑诀 | 金 | heavy-v1.webp |
| hidden | 藏剑诀 | 剑 | hidden-v1.webp |
| bladecare | 养刀术 | 金 | bladecare-v1.webp |
| rainform | 化雨诀 | 水 | rainform-v1.webp |
| wave | 浪击诀 | 水 | wave-v1.webp |
| ocean | 沧海诀 | 水 | ocean-v1.webp |
| turtle | 龟息术 | 木 | turtle-v1.webp |
| venom | 鸩羽诀 | 木 | venom-v1.webp |
| flame | 烈焰诀 | 火 | flame-v1.webp |
| cloud | 凌云诀 | 气 | cloud-v1.webp |
| kindle | 引火诀 | 火 | kindle-v1.webp |
| playfire | 弄焰诀 | 火 | playfire-v1.webp |
| mountain | 撼山诀 | 土 | mountain-v1.webp |
| fetal | 胎息经 | 体 | fetal-v1.webp |
| human | 剑典·人剑篇 | 剑 | human-v1.webp |
| swordcraft | 剑术要诀 | 剑 | swordcraft-v1.webp |
| soulfire | 燃魂诀 | 神 | soulfire-v1.webp |
| visual | 五内观想法 | 神 | visual-v1.webp |
| steal | 夺灵诀 | 气 | steal-v1.webp |
| jadecloud | 玉云功 | 土 | jadecloud-v1.webp |
| sunwheel | 离火归元经 | 火 | sunwheel-v1.webp |
| deepwater | 沧溟水剑经 | 水 | deepwater-v1.webp |
| evergreen | 万木同春经 | 木 | evergreen-v1.webp |
| gravity | 玄山镇岳经 | 土 | gravity-v1.webp |
| goldenrain | 金虹剑阵经 | 金 | goldenrain-v1.webp |
