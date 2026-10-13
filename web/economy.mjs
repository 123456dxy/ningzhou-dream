// 宁州一梦 · C++ 版 — 任务与工坊页
//
// JS 版有独立的 economy 页签（ecoRender + ecoJobHTML），C++ 之前完全没有。
// 结果商会委托、猎妖、寻矿、演武、机缘这些功能规则层都写好了，
// 玩家却一个都点不到。
//
// 这个页面把 /api/eco-options 返回的行动清单直接列出来，
// 另加炼器的配料选择。

'use strict';

// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// 行动清单缓存
let optCache = null;
let forgeCache = null;

// 用途：读取营生活动目录。
// 流程：优先使用缓存，必要时POST eco-options并缓存options。
// 调用方式：任务与宗门页面可传force。
// 返回值：Promise，行动数组。
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

// 用途：读取炼器材料与器型目录。
// 流程：优先使用缓存，必要时POST forge-options并缓存结果。
// 调用方式：工坊和行囊可传force。
// 返回值：Promise，材料与器型对象。
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

// 用途：清除营生和炼器目录缓存。
// 流程：同时置空optCache与forgeCache。
// 调用方式：游戏动作改变状态后调用。
// 返回值：无返回值。
export function invalidateEco() { optCache = null; forgeCache = null; }

export function marketGrade(state, item) {
  const direct = Number(item.grade);
  if (Number.isInteger(direct) && direct > 0) return direct;
  const catalog = item.kind === 'potion' ? state.pillCatalog : item.kind === 'herb' ? state.herbCatalog
    : item.kind === 'material' ? state.materialCatalog : state.gears;
  const known = Number((catalog || []).find(entry => entry.id === item.id)?.grade);
  if (Number.isInteger(known) && known > 0) return known;
  if (item.kind === 'gear') {
    const model = /^(sword|seal|armor|bell|pearl)-([1-5])$/.exec(item.id || '');
    if (model) return Number(model[2]);
  }
  return 0;
}

// 用途：按已选炉数预检服务端提供的单炉材料、灵石与耐久条件。
// 流程：将权威单炉用量乘以炉数，对照当前库存，收集全部缺口并合并原有禁用原因。
// 调用方式：炼丹页与开炉点击处理传入当前状态、丹方选项和所选炉数。
// 返回值：包含整批材料明细、投入、耗时、缺口文案和 enabled 的对象；不修改状态。
export function brewRequirements(state, recipe, batch = 1) {
  const count = Math.max(1, Math.floor(Number(batch) || 1));
  const rows = (recipe.ingredients || []).map(item => {
    const required = Number(item.required) * count;
    const owned = Number(item.owned ?? state.arts?.herbs?.[item.name]) || 0;
    return { ...item, required, owned, missing: Math.max(0, required - owned) };
  });
  const cost = Number(recipe.stoneCost) * count;
  const days = Number(recipe.days) * count;
  const stonesMissing = Math.max(0, cost - (Number(state.player?.stones) || 0));
  const durabilityRequired = Number(recipe.durabilityCost || 0) * count;
  const durabilityMissing = Math.max(0, durabilityRequired - (Number(state.arts?.durability) || 0));
  const blockers = rows.filter(item => item.missing > 0).map(item => `${item.name}需 ${item.required} 份，已有 ${item.owned}，缺 ${item.missing} 份`);
  if (stonesMissing) blockers.push(`灵石需 ${cost} 枚，缺 ${stonesMissing} 枚`);
  if (durabilityMissing) blockers.push(`丹炉耐久需 ${durabilityRequired}，缺 ${durabilityMissing}`);
  if (!recipe.enabled && recipe.reason) blockers.push(recipe.reason);
  if (state.job) blockers.push('请先完成或中止当前安排');
  return { rows, cost, days, count, durabilityRequired, blockers,
    reason: [...new Set(blockers)].join('；'), enabled: recipe.enabled === true && !state.job && !blockers.length };
}

// 用途：显示后端单炉成功产量、收购价和期望毛收入，不推算利润或整批保底收益。
export function renderBrewIncome(recipe = {}) {
  const quoted = value => Number.isFinite(value) ? esc(Number(value.toFixed(2))) : '待报价';
  return `<div class="craft-cost">成功每炉 ${quoted(recipe.output)} 枚 · 收购 ${quoted(recipe.sellPrice)} 灵石/枚 · 每炉期望毛收入 ${quoted(recipe.expectedSaleIncome)} 灵石</div>`
    + '<p class="panel-note">期望不是保底；未扣材料与炉费，出售受本月剩余收购资金限制。</p>';
}
// 用途：展示炼成丹药在当前角色身上的真实服用效果。
// 流程：按丹方编号读取服务端 pillCatalog，依次显示实际效果、使用限制和丹毒变化。
// 调用方式：炼丹卡片、进行中的炼丹安排或丹药行囊调用此纯渲染函数。
// 返回值：已转义的效果 HTML；没有权威条目时返回空字符串。
export function renderPillEffect(state, id, { catalog = false } = {}) {
  const pill = (state.pillCatalog || []).find(item => item.id === id);
  if (!pill) return '';
  if (catalog) return `<div class="pill-effect" data-pill-effect="${esc(id)}" data-pill-catalog-effect><strong>丹药完整效果</strong><p>${esc(pill.catalogEffect || pill.baseEffect || '完整效果暂未加载')}</p>`
    + (pill.usage ? `<small>${esc(pill.usage)}</small>` : '')
    + (pill.basePoison != null ? `<small>基础丹毒 ${esc(pill.basePoison)}</small>` : '') + `</div>`;
  return `<div class="pill-effect" data-pill-effect="${esc(id)}"><strong>当前服用效果</strong><p>${esc(pill.effect || pill.baseEffect)}</p>`
    + (pill.usage ? `<small>${esc(pill.usage)}</small>` : '')
    + `<small>服用后丹毒 ${esc(pill.poisonAfter)}（本次 ${pill.poisonDelta >= 0 ? '+' : ''}${esc(pill.poisonDelta)}）</small>`
    + (pill.reason ? `<small>${esc(pill.reason)}</small>` : '') + `</div>`;
}

export const TYPE_LABEL = {
  task: '城中差事', commission: '商会委托', hunt: '猎妖委托',
  mine: '开采矿材', arena: '演武切磋', quest: '限时机缘',
};

// 一行行动
// 用途：显示单项营生条件。
// 流程：组合说明、实际报酬成本及耗时，按enabled生成按钮。
// 调用方式：任务列表传入行动对象。
// 返回值：行动行HTML字符串。
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

// 用途：显示炼器材料与灵石缺口。
// 流程：优先消费预览材料及blockers，缺少预览时整理所选材料，综合安排条件。
// 调用方式：工坊传入state、目录、配料与预览。
// 返回值：资源明细及enabled/reason对象。
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

// 用途：生成炼器资源条件表。
// 流程：按明细显示所需、已有、缺少，足料与缺料采用不同样式。
// 调用方式：renderEconomy传入资源预检对象。
// 返回值：材料及灵石表格HTML字符串。
function renderForgeRequirements(requirements) {
  const roleLabels = { main: '主料', aux: '辅料', core: '灵核' };
  // 用途：显示资源缺口单元格。
  // 流程：根据missing选择缺料或足料样式，并追加单位说明。
  // 调用方式：renderForgeRequirements传入missing与unit。
  // 返回值：td元素HTML字符串。
  const amount = (missing, unit) => `<td class="${missing > 0 ? 'missing' : 'sufficient'}">${esc(missing)}${unit}${missing > 0 ? '' : '<small>已备齐</small>'}</td>`;
  return `<table class="forge-costs" aria-label="本次炼器所需材料与灵石"><thead><tr><th>材料 / 用途</th><th>所需</th><th>已有</th><th>缺少</th></tr></thead><tbody>`
    + requirements.rows.map(item => `<tr data-forge-material="${esc(item.id)}"><td>${esc(item.name)}<small>${esc((item.roles || []).map(role => roleLabels[role] || role).join('、'))}</small></td><td>${esc(item.required)}份</td><td>${esc(item.owned)}份</td>${amount(item.missing, '份')}</tr>`).join('')
    + `<tr data-forge-stones><td>灵石</td><td>${requirements.cost === null ? '待推演' : esc(requirements.cost) + '枚'}</td><td>${esc(requirements.stones)}枚</td>${requirements.cost === null ? '<td>待推演</td>' : amount(requirements.stonesMissing, '枚')}</tr></tbody></table>`;
}

// 用途：组织营生与炼器子区。
// 流程：按type分组行动，生成炼器选料、资源表、开炉和当前安排。
// 调用方式：桌面传入state、行动目录、材料目录、配料与预览。
// 返回值：含body、quests、forge、job的HTML片段对象。
export function renderEconomy(state, opts, forge, selected = {}, preview = null) {
  // 按类型分组
  const groups = {};
  for (const o of (opts || [])) (groups[o.type] ||= []).push(o);

  // 用途：生成一类营生清单。
  // 流程：读取groups中的行动并调用actionRow。
  // 调用方式：renderEconomy传入type。
  // 返回值：行动分组HTML；无条目时空字符串。
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
  // 用途：生成炼器材料下拉项。
  // 流程：遍历权威材料目录并显示品阶、属性和库存。
  // 调用方式：工坊为每个选料框传入sel。
  // 返回值：option元素HTML字符串。
  const matOptions = (sel) => mats.map(m =>
    `<option value="${esc(m.id)}" ${sel === m.id ? 'selected' : ''}>`
  + `${esc(m.name)}（${m.grade} 阶 · ${esc(m.element)}${m.polarity} · 有 ${m.owned}）`
  + `</option>`).join('');

  const forgePanel = mats.length && shapes.length
    ? `<div class="forge-grid">
        <label>主料<select data-forge="main" ${state.job?.type === 'forge' ? 'disabled' : ''}>${matOptions(selected.main || 'iron')}</select></label>
        <label>辅料<select data-forge="aux" ${state.job?.type === 'forge' ? 'disabled' : ''}>${matOptions(selected.aux || 'jade')}</select></label>
        <label>灵核<select data-forge="core" ${state.job?.type === 'forge' ? 'disabled' : ''}>${matOptions(selected.core || 'wood')}</select></label>
        <label>器型<select data-forge="shape" ${state.job?.type === 'forge' ? 'disabled' : ''}>
          ${shapes.map(s => `<option value="${esc(s.id)}" ${s.id === selected.shape ? 'selected' : ''}>${esc(s.name)}（${esc({ weapon: '武器', armor: '防具', accessory: '佩饰' }[s.slot] || s.slot)}）</option>`).join('')}
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
