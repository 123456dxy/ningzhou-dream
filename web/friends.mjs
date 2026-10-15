// 宁州一梦 · C++ 版 — 道友往来界面
//
// 两块东西：
//   1. 名单 —— 地图页显示「当地仙友」，宗门页只显示本宗门的人
//   2. 交互 —— 见礼、往来、请教与礼物交换
//
// 设计原则：把「为什么加/减好感」如实写在界面上。
// 玩家送礼被扣了好感却不知道为什么，那才是真的糟糕。

'use strict';
import { renderPortrait } from './portraits.mjs';
import { renderNpcTransmission } from './build-transmission.mjs';
import { filterNearbyFriends, renderFriendFilters, friendBuild } from './friend-filters.mjs';
// 兼容旧调用；兽潮展示集中到独立模块，避免道友功能与全州事件耦合。
export { renderTidePanel } from './tide-panel.mjs';

// ------------------------------------------------------------
// 缓存
// ------------------------------------------------------------
let bagCache = new Map();      // npcId -> /api/npc-bag 的结果

// 用途：清除指定道友缓存。
// 流程：按npcId移除缓存与更新时间。
// 调用方式：道友库存或交情改变后调用。
// 返回值：无返回值。
export function invalidateFriendCache(npcId) {
  if (npcId) bagCache.delete(npcId);
  else bagCache.clear();
}

// 用途：读取道友可交换物品。
// 流程：根据npcId与有效期复用缓存，必要时POST npc-bag。
// 调用方式：选中道友或交易后可传force。
// 返回值：Promise，道友背包对象或失败时null。
export async function loadNpcBag(npcId, force) {
  if (!force && bagCache.has(npcId)) return bagCache.get(npcId);
  const r = await fetch('/api/npc-bag', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ npc: npcId }),
  }).then(x => x.json()).catch(() => null);
  bagCache.set(npcId, r);
  return r;
}

// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, c => (
  { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]
));
const STAGE_NAMES = ['炼气初期', '炼气中期', '炼气后期', '筑基初期', '筑基中期', '筑基后期', '金丹初期', '金丹中期', '金丹后期', '元婴初期', '元婴中期', '元婴后期', '化神初期', '化神中期', '化神后期'];

// 用途：展示道友传授功法与神通。
// 流程：读取learning条目，标注境界、交情、花费及学习状态。
// 调用方式：道友详情传入npc。
// 返回值：学习区域HTML字符串。
export function renderNpcLearning(npc) {
  const learning = npc.learning || [];
  if (!learning.length) return '';
  // 用途：生成一类营生清单。
  // 流程：读取groups中的行动并调用actionRow。
  // 调用方式：renderEconomy传入type。
  // 返回值：行动分组HTML；无条目时空字符串。
  const section = (kind, label) => {
    const list = learning.filter(item => item.kind === kind);
    if (!list.length) return '';
    return `<section><h4>${label}</h4><div class="npc-learning-list">${list.map(item =>
      `<div class="npc-learning-item"><div><strong>${esc(item.name)}</strong><span>${esc(item.dao)}系 · 需${esc(STAGE_NAMES[item.stage] || `境界 ${item.stage}`)}</span>`
      + `<small>${item.days || 0} 天 · 所需好感 ${item.affinityNeed || 0} · 消耗交情 ${item.creditCost || 0}${item.stoneCost ? ` · ${item.stoneCost} 灵石` : ''}</small>`
      + `${!item.enabled && !item.learned && item.reason ? `<p>${esc(item.reason)}</p>` : ''}</div>`
      + `<button data-action="npc-learn" data-npc="${esc(npc.id)}" data-kind="${kind}" data-id="${esc(item.id)}" ${item.enabled && !item.learned ? '' : 'disabled'}>`
      + `${item.learned ? kind === 'spell' ? '已领悟' : '已修习' : '请教'}</button></div>`).join('')}</div></section>`;
  };
  return `<details class="npc-learning" open><summary>请教道法 <small>神通与功法</small></summary>`
    + `<p>请教会消耗时间与交情，遇事时暂停。学会后可到洞府道法配装。</p>`
    + section('spell', '对方所知神通') + section('manual', '对方所知功法') + `</details>`;
}

// 用途：展示道友当前构筑。
// 流程：列出道友携带神通与修习功法及权威详情。
// 调用方式：道友详情传入npc。
// 返回值：构筑区域HTML字符串。
export function renderNpcLoadout(npc) {
  const loadout = npc.loadout;
  if (!loadout) return '';
  const manuals = (loadout.manuals || []).map(item =>
    `${item.main ? '主修 ' : ''}${esc(item.name)} ${item.level} 层`).join(' · ');
  const spells = (loadout.spells || []).map(item => `<span>${esc(item.name)}</span>`).join('');
  const nodes = (loadout.nodes || []).map(item => esc(item.name)).join(' · ');
  return `<details class="npc-loadout" data-npc-loadout="${esc(npc.id)}"><summary>实际道法 <small>${esc(npc.dao || '')}道配装</small></summary>`
    + `<div class="npc-loadout-body"><p><b>功法</b> ${manuals || '暂无功法'}</p>`
    + `<p><b>悟道</b> ${nodes || '暂无悟道'}</p><b>斗法神通</b><div class="npc-spell-tags">${spells || '暂无额外神通'}</div>`
    + `<small>切磋和生死斗会使用这套道法抽牌出招。</small></div></details>`;
}

// ------------------------------------------------------------
// 好感度 / 交情 的小条
// ------------------------------------------------------------
// 用途：显示道友好感刻度。
// 流程：将好感映射为可见进度并选择状态样式。
// 调用方式：知交面板传入rel。
// 返回值：好感条HTML字符串。
export function affinityBar(rel) {
  // 好感 -100..100 → 0..100%
  const pct = Math.max(0, Math.min(100, (rel + 100) / 2));
  const tone = rel < 0 ? 'bad' : (rel >= 60 ? 'good' : '');
  return `<span class="aff-bar ${tone}"><i style="width:${pct}%"></i></span>`;
}

// ------------------------------------------------------------
// 名单：当地仙友 / 本宗门
// ------------------------------------------------------------
/**
 * @param {object} state 完整 state
 * @param {object} opts  { scope: 'local' | 'sect', selected, emptyHint }
 */
// 用途：显示附近同道名单。
// 流程：按选中人物与筛选参数列出姓名、境界和交情。
// 调用方式：地图或宗门页传入state与opts。
// 返回值：人物列表HTML字符串。
export function renderFriendList(state, opts = {}) {
  const scope = opts.scope || 'local';
  const p = state.player || {};
  const all = state.nearby || [];

  let list = all;
  if (scope === 'sect') {
    // 宗门页：只列本宗门的人
    list = p.sect ? all.filter(n => n.sect === p.sect) : [];
  }

  const original = list;
  if (scope === 'local') list = filterNearbyFriends(original, opts.filters);
  const filters = scope === 'local' ? renderFriendFilters(original, opts.filters, list.length) : '';
  if (!list.length) {
    const hint = scope === 'sect'
      ? (p.sect
          ? '此地没有同门。换个地方，或去「好友通讯录」看看。'
          : '你尚未拜入宗门。去「宗门知交」择一宗，或在地图上看当地仙友。')
      : (original.length ? '没有符合条件的仙友。可调整筛选，或重置后查看当地全部仙友。' : opts.emptyHint || '此处没有别的修士。换个地方看看。');
    return filters + `<div class="empty">${esc(hint)}</div>`;
  }

  const sorted = scope === 'local' ? list : [...list].sort((a, b) => (b.relation ?? 0) - (a.relation ?? 0));

  return filters + `<div class="friend-grid">` + sorted.map(n => {
    const sel = opts.selected === n.id;
    const rel = n.relation ?? 0;
    const tags = [];
    if (n.isChampion === true) tags.push('<span class="tag champion">流派高手</span>');
    if (n.isMaster) tags.push('<span class="tag master">师尊</span>');
    if (n.isPartner) tags.push('<span class="tag partner">道侣</span>');
    if (n.isDisciple) tags.push('<span class="tag disciple">弟子</span>');
    const credit = n.credit ?? 0;
    return `<button class="friend-card ${sel ? 'selected' : ''}"
                    data-person="${esc(n.id)}">
      <div class="fc-head">${renderPortrait(n, 'npc', 'friend-card-portrait')}<strong>${esc(n.name)}</strong>
        <span class="tag">${esc(n.realmName)}</span>${tags.join('')}</div>
      <div class="fc-meta">${esc(n.gender)} · ${n.age} 岁 · `
    + `${esc(n.sectName || '散修')} · ${esc(n.personality)} · ${esc(friendBuild(n).name)}</div>
      <div class="fc-bars">
        <span class="fc-lab">好感</span>${affinityBar(rel)}
        <span class="fc-num">${rel}</span>
        <span class="fc-tone">${esc(n.relationLabel || '')}</span>
      </div>
      <div class="fc-bars">
        <span class="fc-lab">交情</span>
        <span class="fc-credit">${credit}</span>
        <span class="fc-tone">${esc(n.creditLabel || '素无往来')}</span>
      </div>
    </button>`;
  }).join('') + `</div>`;
}

// ------------------------------------------------------------
// 交互面板：打招呼 / 送礼 / 讨要
// ------------------------------------------------------------
// 用途：显示道友详情和赠礼。
// 流程：核对附近人物，列出中文库存、交换条件、学习、往来和冲突宿主。
// 调用方式：地图或宗门页传入state、npcId、bag与opts。
// 返回值：人物详情HTML；无人物时空状态HTML。
export function renderFriendPanel(state, npcId, bag, opts = {}) {
  const npc = (state.nearby || []).find(n => n.id === npcId);
  if (!npc) return `<div class="empty">请先选一个人。</div>`;

  const rel = npc.relation ?? 0;
  const credit = npc.credit ?? bag?.credit ?? 0;
  const greeted = npc.greeted ?? false;

  // 能送的礼：玩家自己的丹药 / 药材
  const potions = Object.entries(state.arts?.potions || {}).filter(([, v]) => v > 0);
  const herbs = Object.entries(state.arts?.herbs || {}).filter(([, v]) => v > 0);
  const materials = Object.entries(state.arts?.materials || {}).filter(([, v]) => v > 0);
  const gears = (state.gears || []).filter(g => !g.equipped);

  // 用途：生成一项实际赠礼按钮。
  // 流程：把中文物品名、库存和NPC标识附在按钮上。
  // 调用方式：道友面板传入kind、id、label、count和disabled。
  // 返回值：赠礼按钮HTML字符串。
  const giftBtn = (kind, id, label, count, disabled = false) =>
    `<button class="gift-item" data-faction="gift" data-kind="${kind}" `
  + `data-id="${esc(id)}" data-npc="${esc(npcId)}" ${disabled ? 'disabled' : ''}>`
  + `<strong>${esc(label)}</strong><span>×${count}</span></button>`;

  const giftList = [
    ...potions.map(([id, c]) => {
      const m = (state.pillCatalog || []).find(x => x.id === id)
        || (state.arts?.potionList || []).find(x => x.id === id)
        || (state.brewOptions || []).find(x => x.id === id);
      return giftBtn('potion', id, m?.name || '丹药名称待加载', c, !m?.name);
    }),
    ...herbs.map(([id, c]) => giftBtn('herb', id, id, c)),
    ...materials.map(([id, c]) => {
      const name = (opts.materials || []).find(item => item.id === id)?.name
        || (state.market || []).find(item => item.kind === 'material' && item.id === id)?.name;
      return giftBtn('material', id, name || '正在读取材料名称……', c, !name);
    }),
    ...gears.map(g => giftBtn('gear', g.id, g.name, 1)),
  ].join('') || `<div class="empty">你身上没有可送的东西。</div>`;

  const bagItems = (bag?.items || []).map(it => `
    <button class="bag-item ${it.affordable ? '' : 'off'}"
            data-faction="take" data-kind="${esc(it.kind)}"
            data-id="${esc(it.id)}" data-npc="${esc(npcId)}"
            ${it.affordable ? '' : 'disabled'}>
      <div class="bi-head"><strong>${esc(it.name)}</strong>
        <span class="bi-cost">${it.creditCost} 交情</span></div>
      <div class="bi-desc">${esc(it.desc)} · 价值 ${it.value}</div>
      ${it.affordable ? '' : `<div class="bi-why">${esc(it.reason)}</div>`}
    </button>`).join('') || `<div class="empty">对方身上没什么可换的。</div>`;

  return `<div class="friend-panel">
    <div class="friend-character">${renderPortrait(npc, 'npc', 'friend-detail-portrait')}<div class="friend-character-copy"><div class="fp-head">
      <h3>${esc(npc.name)}</h3>
      ${npc.isChampion === true ? '<span class="tag champion">流派高手</span>' : ''}
      <span class="tag">${esc(npc.realmName)}</span>
      <span class="fp-meta">${esc(npc.sectName || '散修')} · ${esc(npc.personality)}</span>
      <span class="fp-meta">${esc(friendBuild(npc).name)}</span>
    </div>

    <div class="fp-bars">
      <div><span>好感</span>${affinityBar(rel)}
        <b>${rel}</b><em>${esc(npc.relationLabel || '')}</em></div>
      <div><span>交情</span><b class="credit">${credit}</b>
        <em>${esc(npc.creditLabel || bag?.creditLabel || '素无往来')}</em></div>
    </div></div></div>

    <div class="fp-note">
      ${greeted ? '你已与这位道友见过礼。' : '上前见礼，与这位道友结识。'}
      每项往来会列出时间与花费；好感与交情决定可做的事情。
    </div>

    <div class="fp-actions">
      <button class="primary" data-faction="greet" data-npc="${esc(npcId)}"
              ${greeted ? 'disabled' : ''}>
        ${greeted ? '已见过礼' : '上前见礼'}
      </button>
    </div>

    <div class="fp-section">
      <h4>当面往来 <span>闲谈、切磋与结交按条件解锁</span></h4>
      <div class="social-actions">正在读取可做的往来……</div>
    </div>

    ${renderBondActions(state, [
      ...(npc.isMaster ? ['lesson', 'dismiss'] : []),
      ...(npc.isPartner ? ['together', 'part'] : []),
      ...(npc.isDisciple ? ['teach'] : []),
    ])}

    ${renderNpcLoadout(npc)}
    ${renderNpcTransmission(npc, state)}
    ${state.job?.type === 'npcLearn' ? `<div class="npc-learning-progress" data-npc-learning-progress><strong>${esc(state.job.name)}</strong>`
      + `<p>尚余 ${state.job.remaining} / ${state.job.total} 天 · 已付学费与交情，中止不返还。</p>`
      + `<div><button class="primary" data-action="npc-learn-resume" ${state.pending || state.battle || state.ended ? 'disabled' : ''}>继续授业</button>`
      + `<button data-action="advance-step" data-days="1" ${state.pending || state.battle || state.ended ? 'disabled' : ''}>研习一天</button>`
      + `<button class="subtle" data-action="cancel-job">中止授业</button></div></div>` : ''}
    ${renderNpcLearning(npc)}

    <div class="fp-section">
      <h4>送礼 <span>按价值、修行偏好与性格回应，月内多次赠礼谢意递减</span></h4>
      <div class="gift-list">${giftList}</div>
    </div>

    <div class="fp-section">
      <h4>以人情相求 <span>花交情换对方背包里的东西</span></h4>
      <div class="bag-list">${bag ? bagItems : '<div class="empty">正在读取对方行囊……</div>'}</div>
    </div>
  </div>`;
}

// 用途：显示师徒道侣日常动作。
// 流程：从权威bondActions筛选指定类别并生成按钮。
// 调用方式：宗门知交传入state和kinds。
// 返回值：动作区HTML字符串。
export function renderBondActions(state, kinds) {
  const list = (state.bondActions || []).filter(a => !kinds || kinds.includes(a.id));
  if (!list.length) return '';
  return `<div class="bond-actions"><h4>师门与道侣</h4>`
    + list.map(a => `<div class="pill-row"><span>${esc(a.name)}`
      + `<span class="item-sub">${a.desc ? `${esc(a.desc)} · ` : ''}${a.days || 0} 天${a.cost ? ` · ${a.cost} 灵石` : ''}`
      + `${!a.enabled && a.reason ? ` · ${esc(a.reason)}` : ''}</span></span>`
      + `<button data-action="bond" data-id="${esc(a.id)}" ${a.enabled ? '' : 'disabled'}>${a.enabled ? esc(a.name) : '条件未满足'}</button></div>`).join('')
    + `</div>`;
}
