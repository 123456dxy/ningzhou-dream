// 宁州一梦 · C++ 版 — 声望面板
//
// 原版把「宁州声望」只当计分项，玩家刷一辈子也换不到任何东西。
// 现在它是一套信誉资本，这个面板负责把「当前档位」和
// 「每一项实际加成」如实列出来 —— 不然玩家还是不知道声望有什么用。

'use strict';

// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// 用途：显示百分比。
// 流程：按函数参数指定的小数精度将比例乘100。
// 调用方式：各数值面板传入比例。
// 返回值：带百分号的字符串。
const pct = v => `${Math.round((v || 0) * 100)}%`;

// 声望档位条
// 用途：显示当前声望阶位。
// 流程：依据服务端tiers和当前声望生成阶位摘要。
// 调用方式：声望面板传入info。
// 返回值：声望条HTML字符串。
export function renderRenownBar(info) {
  if (!info) return '';
  const tiers = info.tiers || [];
  return `<div class="renown-bar">
    ${tiers.map(t => `
      <div class="renown-step ${t.reached ? 'reached' : ''}">
        <div class="renown-dot"></div>
        <div class="renown-name">${esc(t.name)}</div>
        <div class="renown-at">${t.at}</div>
      </div>`).join('')}
  </div>
  <div class="renown-progress"><i style="width:${Math.round((info.progress || 0) * 100)}%"></i></div>`;
}

// 声望的实际作用
// 用途：说明可获取声望的途径。
// 流程：逐项列出实际来源、收益、条件与动作入口。
// 调用方式：人物面板传入sources。
// 返回值：来源卡片HTML字符串。
export function renderRenownSources(sources = []) {
  if (!sources.length) return '<div class="empty">声望来源暂未加载。</div>';
  return `<div class="renown-source-grid">${sources.map(source => {
    if (typeof source === 'string') return `<div class="renown-source-card"><p>${esc(source)}</p></div>`;
    const conditions = Array.isArray(source.conditions) ? source.conditions : source.conditions ? [source.conditions] : [];
    const status = source.done ? '本世已完成' : source.enabled === true ? '当前可进行' : source.enabled === false ? '暂不满足条件' : '';
    return `<article class="renown-source-card${source.done ? ' done' : ''}" data-renown-source="${esc(source.id)}">
      <div class="renown-source-heading"><strong>${esc(source.name)}</strong><b>${esc(source.gainText || '')}</b></div>
      ${conditions.length ? `<ul>${conditions.map(condition => `<li>${esc(condition)}</li>`).join('')}</ul>` : ''}
      ${source.limit ? `<p class="renown-source-limit">${esc(source.limit)}</p>` : ''}
      <div class="renown-source-footer"><small>${esc(source.reason || status)}</small>
        ${source.entry ? `<button data-action="renown-source-open" data-id="${esc(source.id)}">${esc(source.entry.label || '查看入口')}</button>` : ''}
      </div>
    </article>`;
  }).join('')}</div>`;
}

// 用途：展示声望用途与获取说明。
// 流程：读取议价、待遇与来源数据并组合阶位和详细面板。
// 调用方式：人物面板传入info及可选sources。
// 返回值：声望面板HTML字符串。
export function renderRenownPanel(info, { sources = info?.sources || [] } = {}) {
  if (!info) return '<div class="empty">声望信息未加载。</div>';
  const rows = [
    ['坊市买入折扣', pct(info.buyDiscount), '名声大，商家愿意让利'],
    ['坊市卖出溢价', pct(info.sellBonus), '名声大，货品卖得更贵'],
    ['每月同类收购上限', `${info.sellLimit} 件`, '基础 8 件，每 20 点声望 +1'],
    ['当前层级月度收购预算', `${info.marketCashCap}`, info.marketBudgetRule || '基础 400，每点声望 +8'],
    ['初见交情', `${info.firstImpression}`, '陌生人一开始就对你有所耳闻'],
    ['藏经阁贡献折扣', pct(info.libraryDiscount), '兑换功法更省贡献'],
    ['山门差事工钱', `+${pct(info.stipendBonus)}`, '接差事的报酬加成'],
    ['拜师门槛', `声望 ${info.masterRequirement}`, '高境修士才肯收有名声的人'],
  ];

  return `<div class="renown-head">
      <div class="renown-tier">${esc(info.tier)}</div>
      <div class="renown-value">声望 <b>${info.value}</b></div>
    </div>
    <p class="panel-note">${esc(info.tierDesc || '')}
      ${info.toNext > 0 ? `距下一档还差 ${info.toNext} 点。` : '已至顶峰。'}</p>
    ${renderRenownBar(info)}
    <div class="renown-rows">
      ${rows.map(([label, value, note]) => `
        <div class="renown-row">
          <div><div class="rn-label">${label}</div>
            <div class="rn-note">${note}</div></div>
          <div class="rn-value">${value}</div>
        </div>`).join('')}
    </div>
    <h3 style="margin-top:16px">声望从哪来</h3>
    <p class="panel-note">按实际收益与限制选择安排。下方按钮只查看对应入口，抵达后再开始行动。</p>
    ${renderRenownSources(sources)}
    <p class="panel-note">野外拦路索财声望−4，截杀声望−10，最低归零；另会累加恶名。普通城中差事、商会委托、演武切磋与提升交情目前不增加声望。</p>`;
}

// 侧栏里的一行摘要（其他页面用）
// 用途：生成声望简短徽章。
// 流程：读取当前阶位与数值并组合标签。
// 调用方式：相关页面传入info。
// 返回值：徽章HTML字符串。
export function renownChip(info) {
  if (!info) return '';
  return `<div><label>宁州声望</label><strong>${info.value}
    <span class="rn-tier">${esc(info.tier)}</span></strong></div>`;
}
