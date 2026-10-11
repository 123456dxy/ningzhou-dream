// 宁州一梦 · C++ 版 — 任务与工坊页
//
// JS 版有独立的 economy 页签（ecoRender + ecoJobHTML），C++ 之前完全没有。
// 结果商会委托、猎妖、寻矿、演武、机缘这些功能规则层都写好了，
// 玩家却一个都点不到。
//
// 这个页面把 /api/eco-options 返回的行动清单直接列出来，
// 另加炼器的配料选择。

'use strict';

const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// 行动清单缓存
let optCache = null;
let forgeCache = null;

export async function loadEcoOptions(force) {
  if (optCache && !force) return optCache;
  const r = await fetch('/api/eco-options', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(x => x.json()).catch(() => null);
  optCache = r?.options || [];
  return optCache;
}

export async function loadForgeOptions(force) {
  if (forgeCache && !force) return forgeCache;
  const r = await fetch('/api/forge-options', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(x => x.json()).catch(() => null);
  forgeCache = r || { materials: [], shapes: [] };
  return forgeCache;
}

export function invalidateEco() { optCache = null; forgeCache = null; }

export const TYPE_LABEL = {
  task: '城中差事', commission: '商会委托', hunt: '猎妖委托',
  mine: '开采矿材', arena: '演武切磋', quest: '限时机缘',
};

// 一行行动
export function actionRow(o) {
  const pay = o.pay ? ` · 报酬 ${o.pay}` : '';
  const cost = o.cost ? ` · 投入 ${o.cost}` : '';
  const chance = o.chance ? ` · 成功率 ${Math.round(o.chance * 100)}%` : '';
  const days = o.days ? ` · ${o.days} 天` : '';
  return `<div class="pill-row">
    <span>${esc(o.name)}
      <span class="item-sub">${esc(o.desc || '')}${days}${cost}${pay}${chance}</span></span>
    <button data-action="eco-run" data-type="${esc(o.type)}" data-id="${esc(o.id)}"
            ${o.enabled ? '' : 'disabled'}>
      ${o.enabled ? '进行' : esc(o.reason || '不可用')}
    </button></div>`;
}

export function forgeRequirements(state, materials, selected, preview) {
  const info = preview?.info;
  const amounts = new Map();
  for (const [role, key, fallback, required] of [['主料', 'main', 'iron', 4], ['辅料', 'aux', 'jade', 3], ['灵核', 'core', 'wood', 3]]) {
    const id = selected[key] || fallback;
    const material = materials.find(item => item.id === id);
    if (!material) continue;
    const item = amounts.get(id) || { id, name: material.name, roles: [], required: 0, owned: Number(material.owned) || 0 };
    item.roles.push(role);
    item.required += required;
    amounts.set(id, item);
  }
  const rows = Array.isArray(info?.materials) ? info.materials
    : [...amounts.values()].map(item => ({ ...item, missing: Math.max(0, item.required - item.owned) }));
  const projectedCost = Number(info?.stonesRequired ?? info?.cost);
  const cost = info?.valid && Number.isFinite(projectedCost) ? projectedCost : null;
  const stones = Number(info?.stonesOwned ?? state.player.stones) || 0;
  const stonesMissing = cost === null ? 0 : Math.max(0, cost - stones);
  const missing = rows.filter(item => item.missing > 0).map(item => `${item.name}缺${item.missing}份`);
  if (stonesMissing) missing.push(`灵石缺${stonesMissing}枚`);
  const blockers = Array.isArray(info?.blockers) ? info.blockers.slice()
    : missing.length ? [`材料或灵石不足：${missing.join('、')}。`] : [];
  if (info?.reason && !blockers.includes(info.reason) && (!missing.length || !/(材料|灵石)不足/.test(info.reason))) blockers.push(info.reason);
  const reason = state.job ? '请先完成或中止当前安排。'
    : !preview ? '正在推演配方，请稍候。'
    : preview.error || blockers.join(' ')
      || (!info?.valid ? '请选择有效的配料与器型。' : info?.enabled !== true ? '当前配方无法开炉。' : '材料与灵石已备齐，可以开炉。');
  return { rows, cost, stones, stonesMissing, reason, enabled: !state.job && info?.valid === true && info.enabled === true && !missing.length };
}

function renderForgeRequirements(requirements) {
  const roleLabels = { main: '主料', aux: '辅料', core: '灵核' };
  const amount = (missing, unit) => `<td class="${missing > 0 ? 'missing' : 'sufficient'}">${esc(missing)}${unit}${missing > 0 ? '' : '<small>已备齐</small>'}</td>`;
  return `<table class="forge-costs" aria-label="本次炼器所需材料与灵石"><thead><tr><th>材料 / 用途</th><th>所需</th><th>已有</th><th>缺少</th></tr></thead><tbody>`
    + requirements.rows.map(item => `<tr data-forge-material="${esc(item.id)}"><td>${esc(item.name)}<small>${esc((item.roles || []).map(role => roleLabels[role] || role).join('、'))}</small></td><td>${esc(item.required)}份</td><td>${esc(item.owned)}份</td>${amount(item.missing, '份')}</tr>`).join('')
    + `<tr data-forge-stones><td>灵石</td><td>${requirements.cost === null ? '待推演' : esc(requirements.cost) + '枚'}</td><td>${esc(requirements.stones)}枚</td>${requirements.cost === null ? '<td>待推演</td>' : amount(requirements.stonesMissing, '枚')}</tr></tbody></table>`;
}

export function renderEconomy(state, opts, forge, selected = {}, preview = null) {
  const p = state.player;

  // 按类型分组
  const groups = {};
  for (const o of (opts || [])) (groups[o.type] ||= []).push(o);

  const section = (type) => {
    const list = groups[type] || [];
    if (!list.length) return '';
    return `<h3 style="margin-top:16px">${TYPE_LABEL[type] || type}</h3>`
         + list.map(actionRow).join('');
  };

  // 炼器配料
  const mats = (forge?.materials || []);
  const shapes = (forge?.shapes || []);
  const requirements = forgeRequirements(state, mats, selected, preview);
  const matOptions = (sel) => mats.map(m =>
    `<option value="${esc(m.id)}" ${sel === m.id ? 'selected' : ''}>`
  + `${esc(m.name)}（${m.grade} 阶 · ${esc(m.element)}${m.polarity} · 有 ${m.owned}）`
  + `</option>`).join('');

  const forgePanel = mats.length && shapes.length
    ? `<div class="forge-grid">
        <label>主料<select data-forge="main">${matOptions(selected.main || 'iron')}</select></label>
        <label>辅料<select data-forge="aux">${matOptions(selected.aux || 'jade')}</select></label>
        <label>灵核<select data-forge="core">${matOptions(selected.core || 'wood')}</select></label>
        <label>器型<select data-forge="shape">
          ${shapes.map(s => `<option value="${esc(s.id)}" ${s.id === selected.shape ? 'selected' : ''}>${esc(s.name)}（${esc(s.slot)}）</option>`).join('')}
        </select></label>
      </div>
      ${renderForgeRequirements(requirements)}
      <div class="forge-actions"><button class="primary" data-action="forge-run" ${requirements.enabled ? '' : 'disabled'}>开炉铸炼</button>
        <p class="forge-readiness${requirements.enabled ? '' : ' blocked'}" role="status">${esc(requirements.reason)}</p></div>
      <p class="panel-note">主料 4 份、辅料 3 份、灵核 3 份。品阶越高，成器属性越好。</p>`
    : `<div class="empty">材料数据未加载。</div>`;

  const job = state.job
    ? `<div class="detail-list" style="border-top:0;padding-top:0">
         <div><label>进行中</label><strong>${esc(state.job.name)}</strong></div>
         <div><label>剩余</label><strong>${state.job.remaining} / ${state.job.total} 天</strong></div>
       </div>
       <div class="year-controls" style="margin-top:10px">
         <button data-action="cancel-job">中止当前安排</button>
       </div>`
    : `<div class="empty">当前没有进行中的安排。</div>`;

  return {
    body: section('task') + section('commission') + section('hunt')
        + section('mine') + section('arena'),
    quests: section('quest'),
    forge: forgePanel,
    job,
  };
}
