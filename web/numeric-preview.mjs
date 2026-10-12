import { esc } from './shell.mjs';

// 数字与条件均由核心的真实结算预览提供，界面不另算一套公式。
// 用途：展示核心提供的真实数值。
// 流程：只在valid时显示text、条件和计算参考环境。
// 调用方式：神通与法宝渲染传入preview和compact选项。
// 返回值：已转义HTML；无有效预览时空字符串。
export function renderNumericPreview(preview, { compact = false } = {}) {
  if (!preview?.valid) return '';
  return `<div class="numeric-preview${compact ? ' compact' : ''}" data-numeric-preview>`
    + `<b>当前数值</b><p>${esc(preview.text || '暂无直接数值变化')}</p>`
    + (preview.conditions ? `<p class="numeric-conditions">${esc(preview.conditions)}</p>` : '')
    + (preview.context ? `<small>${esc(preview.context)}</small>` : '')
    + `</div>`;
}
