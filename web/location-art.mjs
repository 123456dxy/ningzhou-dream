// 每处地点独立景观；主题对应 locations/prompts.json 中的插画构思。
// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = value => String(value ?? '').replace(/[&<>"']/g,
  ch => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));

// 用途：登记地点插画信息。
// 流程：根据id拼接资源路径并冻结名称、副标题等条目。
// 调用方式：地点插画映射表初始化时调用。
// 返回值：不可变插画对象。
const artwork = (id, name, subtitle) => Object.freeze({
  src: `assets/map/locations/${id}.webp`,
  alt: `${name}景观插画：${subtitle}`,
  subtitle,
});

const ORIGINAL_ART = {
  wuling: artwork('wuling', '武陵城', '白帝楼 · 升仙台'),
  yifeng: artwork('yifeng', '逸风城', '斗法台 · 风过旗楼'),
  yunxi: artwork('yunxi', '云汐城', '药阁 · 水巷烟雨'),
  tianxing: artwork('tianxing', '天星城', '问道楼 · 灯火书市'),
  guangling: artwork('guangling', '广陵城', '百宝行 · 临海商埠'),
  jinhong: artwork('jinhong', '金虹剑派', '金虹山门 · 剑壁凌空'),
  xinghe: artwork('xinghe', '星河剑派', '水榭剑台 · 星河倒映'),
  zhushan: artwork('zhushan', '竹山宗', '竹海药圃 · 木灵山门'),
  lihuo: artwork('lihuo', '离火门', '赤岩丹炉 · 地火长明'),
  huachen: artwork('huachen', '化尘教', '厚土石殿 · 砂岩层台'),
  fanghu: artwork('fanghu', '方壶山', '碧幽深潭 · 灵药险峰'),
  qingshi: artwork('qingshi', '青石灵脉', '苍青矿谷 · 灵核晶光'),
  sea: artwork('sea', '无尽海', '雷云孤帆 · 海上仙山'),
};

export const LOCATION_ART = Object.freeze({
  ...ORIGINAL_ART,
  fenglei: artwork('fenglei', '风雷谷', '寒月幽潭 · 水道机缘'),
  wojiao: artwork('wojiao', '沃焦山', '火山药田 · 赤岩火脉'),
  lianshi: artwork('lianshi', '连石山', '剑壁矿山 · 金系灵材'),
  zhengyang: artwork('zhengyang', '正阳山', '厚土山川 · 灵矿药田'),
  xiuluo: artwork('xiuluo', '岫络谷', '幽谷古迹 · 各宗寻踪'),
  yishan: artwork('yishan', '沂山', '峡谷药田 · 山派驻地'),
  heshan: artwork('heshan', '禾山', '山林药草 · 灵猴幽谷'),
  wushan: artwork('wushan', '巫山', '山脚洞窟 · 林间药草'),
  tianji: artwork('tianji', '天机阁', '消息珍品 · 高阶拍卖'),
  dongshi: artwork('dongshi', '东石谷', '仙师集会 · 伴月拍卖'),
  jiuyi: artwork('jiuyi', '九嶷山', '偏僻破庙 · 药山奇遇'),
  yongan: artwork('yongan', '永安镇', '镇中学堂 · 人间烟火'),
  daiyu: artwork('daiyu', '岱舆山', '临海药山 · 草木灵机'),
  xuandao: artwork('xuandao', '玄道宗', '东南山门 · 调查集结'),
  tianmo: artwork('tianmo', '天魔眼', '魔物灵核 · 险地探查'),
});

// 用途：读取地点插画条目。
// 流程：接受地点对象或id，在映射表中查找对应插画。
// 调用方式：地点渲染传入locationOrId。
// 返回值：插画对象或空值。
export function locationArt(locationOrId) {
  const id = typeof locationOrId === 'string' ? locationOrId : locationOrId?.id;
  return Object.hasOwn(LOCATION_ART, id) ? LOCATION_ART[id] : null;
}

// 用途：显示地点专属插画。
// 流程：读取插画与地点信息，生成可放大的图片和说明。
// 调用方式：地图地点详情传入location。
// 返回值：插画HTML字符串。
export function renderLocationArt(location) {
  const art = locationArt(location);
  if (!art) return '';
  return `<div class="location-art-viewer"><header class="location-art-heading"><div>`
    + `<span class="travel-eyebrow">宁州景观</span><h2 id="location-art-title">${esc(location.name)}</h2>`
    + `<p>${esc(art.subtitle)}</p></div>`
    + `<button class="subtle" data-action="close-location-art" aria-label="关闭景观大图" autofocus>返回舆图 ×</button></header>`
    + `<img class="location-art-full" src="${esc(art.src)}" alt="${esc(art.alt)}">`
    + `<p class="location-art-description">${esc(location.desc)}</p></div>`;
}
