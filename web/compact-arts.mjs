import { esc } from './shell.mjs';

// 用途：提取用途短句；流程：非战斗功法卡；传后端说明，返回首句的有限文字，不重算或修改属性。
export function effectPurpose(effect) {
  const sentence = String(effect || '').split(/[。；\n]/u).find(part => part.trim())?.trim();
  return sentence ? sentence.slice(0, 42) + (sentence.length > 42 ? '…' : '') : '展开查看修行效果';
}

// 用途：将完整效果折叠；流程：功法、神通目录；传用途和已转义详情HTML，返回可键盘展开的原生details，默认关闭。
export function renderArtsDetails(purpose, detailsHtml) {
  return `<details class="compact-art-details"><summary>${esc(purpose)}<span>详细属性</span></summary><div>${detailsHtml}</div></details>`;
}
