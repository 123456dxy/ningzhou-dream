// 妖兽纯外观白名单，与core/content/beast_portraits的六主题和五境界协议对应。
// 不读取中文名字、存档路径或世界随机数；人物立绘继续由人物注册管理。
export const BEAST_PORTRAIT_THEMES = Object.freeze([
  Object.freeze({ id: 'stone-guard', styles: Object.freeze(['earth', '土', 'water', '水']) }),
  Object.freeze({ id: 'flame-beast', styles: Object.freeze(['fire', '火']) }),
  Object.freeze({ id: 'venom-beast', styles: Object.freeze(['wood', '木']) }),
  Object.freeze({ id: 'mind-beast', styles: Object.freeze(['mind', '神']) }),
  Object.freeze({ id: 'rending-beast', styles: Object.freeze(['metal', '金', 'sword', '剑']) }),
  Object.freeze({ id: 'charging-beast', styles: Object.freeze(['body', '体']) }),
]);

const allowedPaths = new Set();
for (let major = 0; major < 5; major++) {
  allowedPaths.add(`assets/portraits/beast-${major}.webp`);
  for (const theme of BEAST_PORTRAIT_THEMES)
    allowedPaths.add(`assets/portraits/beast-${theme.id}-${major}-v1.webp`);
}

// 用途：检查妖兽专用资源地址；流程：正式页/试炼页选图；用法：传服务端portrait；返回：是否属于固定35条路径。
export function isBeastPortraitPath(value) {
  return typeof value === 'string' && allowedPaths.has(value.startsWith('/') ? value.slice(1) : value);
}

// 用途：缺少后端立绘时复用六主题五境回退；流程：正式斗法与独立试炼；用法：传portrait、stage及真实enemyDao/style/dao。
// 返回值：白名单图片地址；优先后端合法资源，不按显示名猜图，也不接受任意人物图或外部地址。
export function beastPortraitSource(person = {}) {
  if (isBeastPortraitPath(person.portrait)) return person.portrait;
  const stage = Number(person.stage);
  const major = Number.isFinite(stage) ? Math.max(0, Math.min(4, Math.floor(stage / 3))) : 0;
  const style = person.archetype || person.enemyDao || person.style || person.dao;
  const chosen = BEAST_PORTRAIT_THEMES.find(theme => theme.id === style || theme.styles.includes(style));
  return chosen ? `assets/portraits/beast-${chosen.id}-${major}-v1.webp` : `assets/portraits/beast-${major}.webp`;
}
