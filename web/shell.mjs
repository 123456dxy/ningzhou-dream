// 宁州一梦 · C++ 版 — 页面骨架
//
// 这个文件存在的理由：之前我把原版的布局类用错了，导致界面看起来别扭。
// 具体错在：
//   1. .chronicle-top 本来是「面板内的标题栏」（flex + border-bottom），
//      我拿它当两个面板的外层容器用了
//   2. .chronicle-layout 是 grid-template-columns: 1fr 254px（主区 + 窄侧栏），
//      我往里塞了三个子元素，第三个被挤成 254px 宽
//   3. 原版每页都以 .section-head 开头（标题 + 说明），我漏了
//   4. 原版已经有 .plan-panel / .plan-options / .year-controls / .hint-box
//      这套右侧栏，我自己另写了一套
//
// 所以这里把骨架抽出来，所有页面统一走这几个函数，
// 保证和原版的 HTML 结构一致。

'use strict';

// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
export const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// 页面标题（对应原版 heading()）
// 用途：生成页面标题。
// 流程：组合标题、说明与可选标记。
// 调用方式：各页面传入title、description与tagText。
// 返回值：标题HTML字符串。
export function heading(title, description, tagText = '') {
  return `<div class="section-head">`
       + `<div><h2>${esc(title)}</h2><p>${esc(description)}</p></div>`
       + (tagText ? `<span class="tag">${esc(tagText)}</span>` : '')
       + `</div>`;
}

// 面板 + 面板内标题栏（对应原版 section.chronicle-panel > .chronicle-top）
// 用途：生成标准主面板。
// 流程：组合面板标题、数量摘要与调用者提供的正文。
// 调用方式：各页面传入title、countText与body。
// 返回值：面板HTML字符串。
export function panel(title, countText, body) {
  return `<section class="chronicle-panel">`
       + `<div class="chronicle-top"><h3>${esc(title)}</h3>`
       + (countText ? `<span class="count">${esc(countText)}</span>` : '')
       + `</div>${body}</section>`;
}

// 主区 + 窄侧栏（对应原版 .chronicle-layout）
// 用途：生成主区与侧栏布局。
// 流程：拼接主区域，存在aside时加侧栏容器。
// 调用方式：各页面传入main与aside。
// 返回值：布局HTML字符串。
export function layout(main, aside) {
  return `<div class="chronicle-layout">${main}`
       + (aside ? `<aside class="plan-panel">${aside}</aside>` : '')
       + `</div>`;
}

// 侧栏里的一小节。
// 注意：不要再往 .plan-panel 里塞 .chronicle-panel ——
// 两者都有自己的边框和内边距，套起来是「盒中盒」，看着很别扭。
// 原版侧栏就是 <h3> + 内容，这里照做。
// 用途：生成侧栏小节。
// 流程：拼接标题、可选说明和正文片段。
// 调用方式：页面侧栏传入title、content与note。
// 返回值：侧栏HTML字符串。
export function asideBlock(title, content, note = '') {
  return `<h3>${esc(title)}</h3>`
       + (note ? `<p class="intro">${esc(note)}</p>` : '')
       + content;
}

// 纪事时间线（对应原版 .timeline > .log-entry）
const CATEGORY_TAG = {
  self: '此身修行', npc: '同道往来', sect: '山门因缘',
  world: '天下大事', sim: '规则', ending: '落笔',
};

// 用途：生成纪事时间线。
// 流程：逐项读取日期、类别、正文与变化，委托ageOf显示年龄。
// 调用方式：纪事页传入logs和年龄回调。
// 返回值：时间线HTML；无日志时空状态HTML。
export function timeline(logs, ageOf) {
  if (!logs.length) return `<div class="empty">还没有纪事。</div>`;
  return logs.map(e => {
    const cat = e.category || 'self';
    return `<article class="log-entry ${esc(cat)}${e.important ? ' important' : ''}">`
      + `<div class="log-date">${ageOf(e)} 岁`
      + `<small>第 ${e.year + 1} 年</small></div>`
      + `<div class="log-content">`
      + `<span class="entry-tag">${esc(CATEGORY_TAG[cat] || '纪事')}</span>`
      + `<p>${esc(e.text)}</p>`
      + (e.delta ? `<div class="entry-delta">${esc(e.delta)}</div>` : '')
      + `</div></article>`;
  }).join('');
}

// 时间推进控件（对应原版 .year-controls）
// 用途：生成时间推进按钮。
// 流程：提供一月、一年、十年与一天的固定动作参数。
// 调用方式：纪事或计划面板无参调用。
// 返回值：推进控件HTML字符串。
export function yearControls() {
  return `<div class="year-controls">`
    + `<button class="primary" data-action="advance" data-days="30">`
    + `过一月 →</button>`
    + `<button data-action="advance" data-days="360">过一年</button>`
    + `</div>`
    + `<button data-action="advance" data-days="3600" style="width:100%;margin-top:7px">`
    + `过十年 · 遇事暂停</button>`
    + `<button data-action="advance-step" data-days="1" style="width:100%;margin-top:7px">`
    + `过一天</button>`;
}

// 侧栏提示（对应原版 .hint-box）
// 用途：生成多行提示框。
// 流程：将提示逐条转义并用换行连接。
// 调用方式：侧栏传入lines数组。
// 返回值：提示框HTML字符串。
export function hintBox(lines) {
  return `<div class="hint-box">${lines.map(esc).join('<br>')}</div>`;
}

// 通用的小卡片列表（用于支线、悬赏、地点等）
// 用途：生成通用行动卡片。
// 流程：根据disabled决定动作属性，再插入图标、标题与说明。
// 调用方式：页面传入卡片配置对象。
// 返回值：卡片HTML字符串。
export function cardButton({ action, id, glyph, title, body, foot, disabled, extra }) {
  return `<button class="card" `
    + (disabled ? 'disabled' : `data-action="${action}" data-id="${id}"`)
    + (extra || '') + `>`
    + `<div class="card-top"><span class="card-glyph">${glyph}</span>`
    + `<strong>${esc(title)}</strong></div>`
    + (body ? `<p class="card-rule">${esc(body)}</p>` : '')
    + (foot ? `<div class="card-foot">${esc(foot)}</div>` : '')
    + `</button>`;
}
