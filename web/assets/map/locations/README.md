# 宁州二十八处地点插画

2026-10-11 使用内置 image_gen 工具分别生成，每个地点一张独立、不透明 PNG；成品全部保存在此目录，游戏离线使用。首批13处与新增15处的完整提示词、生成源路径等元数据仅保留在开发者本地，未纳入公开仓库，生成模式为 built-in generate。游戏运行只需成品 PNG。

新增风雷谷、沃焦山、连石山、正阳山、岫络谷、沂山、禾山、巫山、天机阁、东石谷、九嶷山、永安镇、岱舆山、玄道宗、天魔眼。地点身份及原作参考见 [location-reference.json](../location-reference.json)。本地相对坐标已按用户截图重建；原作完整剧情、竞价拍卖和所有子场景并未照搬，游戏功能以引擎下发的当地事务为准。

## 参考资料与创作边界

联网参考[宁州大地图与武陵城截图](https://www.3dmgame.com/score/3742939.html)、[城市地标和商店功能](https://www.gamersky.com/handbook/202301/1556623.shtml)、[宁州主城介绍](https://forum.gamer.com.tw/C.php?bsn=72729&snA=47)，并核对以下宗门及野外资料。采用的是地点身份与地标线索，镜头、建筑外形、色彩、气候和装饰均为本项目原创视觉表现。天星城的天文装饰、金虹的金色光弧、青石矿道设施等是绘画设计，不表示新增游戏功能。本地坐标与赶路数值使用本项目实现。

| 文件 / 地点 | 原作线索 | 本次独立画面 |
| --- | --- | --- |
| wuling.png / 武陵城 | 白帝楼、升仙台、灵药堂 | 白石高楼、远处升仙石台、晨雾河桥与药市 |
| yifeng.png / 逸风城 | 斗法台、宝器轩 | 圆形斗法台、风中旗楼、旅舍与剑器摊 |
| yunxi.png / 云汐城 | 中草阁、药材交易 | 雨中水巷、草药铺、晾药与廊桥 |
| tianxing.png / 天星城 | 问道楼、功法神通交易 | 灯火书楼、卷轴书市与暮色广场 |
| guangling.png / 广陵城 | 材料交易、码头出海 | 临海商楼、帆舟、货箱和山湾码头 |
| jinhong.png / 金虹剑派 | 金系剑修，见[宗门资料](https://wiki.biligame.com/mcs/金虹剑派) | 金铜山门、剑形石壁、金虹晨光 |
| xinghe.png / 星河剑派 | 水系剑修，见[宗门资料](https://wiki.biligame.com/mcs/星河剑派) | 湖中剑台、水榭石桥、月夜星河倒影 |
| zhushan.png / 竹山宗 | 木系宗门、药田，见[宗门资料](https://wiki.biligame.com/mcs/竹山宗) | 竹海山门、层叠药圃、林间清泉 |
| lihuo.png / 离火门 | 火系宗门、环境炎热，见[宗门资料](https://wiki.biligame.com/mcs/离火门) | 赤岩黑石、铜鼎丹炉、地火与烟岚 |
| huachen.png / 化尘教 | 土系宗门，见[宗门资料](https://wiki.biligame.com/mcs/化尘教) | 厚重石殿、砂岩层台、土色法坛 |
| fanghu.png / 方壶山 | 草药采集、碧幽潭，见[地点攻略](https://www.scribd.com/document/843621381/觅长生全能攻略V1-7) | 玉色深潭、瀑布险峰、幽谷灵药 |
| qingshi.png / 青石灵脉 | 宁州西部、门派共管、采集灵核，见[官方更新](https://store.steampowered.com/news/posts/?appids=1189490&enddate=1629862005&feed=steam_community_announcements) | 苍青矿谷、木撑矿口、小型灵核晶脉 |
| sea.png / 无尽海 | 灵舟、海兽、岛城和灵气乱流，见[官方海域更新](https://store.steampowered.com/news/posts/?appids=1189490&enddate=1603445698&feed=steam_community_announcements) | 雷云孤帆、远岛仙山、深色浪涛 |

## 使用

`web/location-art.mjs` 按地点 id 选择唯一插画，地图详情显示景观主题，点击图片可放大查看。源图片保持生成原尺寸，不进行图像裁切或重绘；缩略卡片由 CSS 排版。

广陵城材料商行在[地点实录](https://wiki.biligame.com/mcs/【觅长生】星河剑派普通难度流程)和[玩家采买说明](https://steamcommunity.com/app/1189490/discussions/0/3779119905193291773/)中写作“百宝行”，界面使用这个名称。生成当时采用了旧攻略的 Wanbao Lou 称呼，本地生成记录保留实际完整文本；画面没有楼名文字，仍是原创材料商楼与港口。
