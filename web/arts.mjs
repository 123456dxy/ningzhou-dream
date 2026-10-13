// 宁州一梦 · C++ 版 — 功法与悟道页
//
// 为什么单独一个文件：
//   十二大道有 60 个节点，每个都有名称（金道入门…）和效果说明（金系伤害 +10%）。
//   之前我只把「节点 id」发到前端，界面上就只有 ○1 ○2 ○3 五个小圆点，
//   玩家根本不知道点下去会发生什么 —— 而 JS 版是把名称和效果都列出来的。
//
// 现在按 JS 版 arts-ui.mjs 的做法：每道一张卡，卡里列出 5 个节点的
// 名称 / 花费 / 效果 / 领悟按钮，另外给一个「参悟 30 天」。

'use strict';

import { cardKind, cardRule } from './cards.mjs';
import { renderNumericPreview } from './numeric-preview.mjs';
import { MANUAL_ART, MANUAL_ART_BY_DAO, MANUAL_ART_BY_ID } from './assets/manuals/manifest.mjs';

// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

export const DAOS = ['金', '木', '水', '火', '土', '神', '体', '剑', '气', '阵', '丹', '器'];
export const DAO_LEVELS = ['未入门', '初窥门径', '略有小成', '融会贯通', '道之真境', '大道已成'];
const THRESHOLDS = [0, 30, 100, 300, 900, 2000];

// 用途：按稳定功法 ID 选择每本独立插图。
// 流程：正式功法 ID 优先；未来未知功法才按大道使用默认图，无映射则文字回退。
// 调用方式：功法卡、书坊和流派组件传入功法目录对象。
// 返回值：插图定义；未知功法返回 null。
export function manualIllustration(manual = {}) {
  const key = (Object.hasOwn(MANUAL_ART_BY_ID, manual?.id) ? MANUAL_ART_BY_ID[manual.id] : null)
    || (Object.hasOwn(MANUAL_ART_BY_DAO, manual?.daoId) ? MANUAL_ART_BY_DAO[manual.daoId] : null)
    || (Object.hasOwn(MANUAL_ART_BY_DAO, manual?.dao) ? MANUAL_ART_BY_DAO[manual.dao] : null);
  return typeof key === 'string' && Object.hasOwn(MANUAL_ART, key) ? { key, ...MANUAL_ART[key] } : null;
}

// 用途：生成可降级的功法插图，避免图片失败影响修习和交易。
// 流程：输出目录内固定地址与独立文字底图；卡片的效果和名称不写进图片。
// 调用方式：传入功法；交易行和流派组件可设 compact=true。
// 返回值：装饰性插图 HTML，未知条目仅显示“卷”字。
export function renderManualIllustration(manual = {}, options = {}) {
  const art = manualIllustration(manual);
  const glyph = typeof manual?.dao === 'string' && manual.dao.length === 1 ? manual.dao : '卷';
  return `<span class="manual-art${options?.compact ? ' manual-art--compact' : ''}" aria-hidden="true">`
    + `<span class="manual-art-fallback">${esc(glyph)}</span>`
    + (art ? `<img data-manual-art="${esc(art.key)}" src="${esc(art.src)}" alt="" loading="lazy" decoding="async" width="512" height="512">` : '')
    + `</span>`;
}

// 用途：处理插图载入失败，让失败图片退回底层道字。
// 流程：只识别本模块的 img，隐藏失败图片，保留同一张卡的文字和按钮。
// 调用方式：文档 capture error 监听器传入事件；不产生 API 请求。
// 返回值：无返回值。
export function handleManualIllustrationError(event) {
  const image = event?.target;
  if (image?.matches?.('img[data-manual-art]')) image.hidden = true;
}

if (typeof document !== 'undefined' && typeof document.addEventListener === 'function') {
  document.addEventListener('error', handleManualIllustrationError, true);
}

// 用途：按大道经验显示境界。
// 流程：用既有阈值表计算已跨过的等级数。
// 调用方式：道法节点渲染传入经验。
// 返回值：0至5的等级整数。
export function artLevelOf(exp) {
  return Math.max(0, THRESHOLDS.filter(t => exp >= t).length - 1);
}

// 节点定义缓存（一次拉取，全局复用）
let nodeCache = null;

// 用途：读取大道节点目录。
// 流程：优先复用缓存，必要时POST arts/nodes并缓存nodes。
// 调用方式：功法与悟道页面可传force强制刷新。
// 返回值：Promise，节点数组。
export async function loadNodes(force) {
  if (nodeCache && !force) return nodeCache;
  const r = await fetch('/api/arts/nodes', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(x => x.json()).catch(() => null);
  nodeCache = r?.nodes || [];
  return nodeCache;
}

// 用途：清除节点目录缓存。
// 流程：把nodeCache置空，供下次加载使用。
// 调用方式：需要刷新定义时无参调用。
// 返回值：无返回值。
export function invalidateNodes() { nodeCache = null; }

// 神通全表缓存（携带 / 卸下用）
let spellCache = null;

// 用途：读取神通目录与权威预览。
// 流程：优先复用缓存，必要时POST arts/spells并保存结果。
// 调用方式：构筑页面可传force强制刷新。
// 返回值：Promise，神通目录对象。
export async function loadSpells(force) {
  if (spellCache && !force) return spellCache;
  const r = await fetch('/api/arts/spells', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(x => x.json()).catch(() => null);
  spellCache = r || { spells: [], max: 4, carriedCount: 0 };
  return spellCache;
}

// 用途：清除神通目录缓存。
// 流程：把spellCache置空使下次读取重新核算。
// 调用方式：角色状态变化时无参调用。
// 返回值：无返回值。
export function invalidateSpells() { spellCache = null; }

// 神通种类的中文标签（与 Core 的 kind 对应）
// 用途：显示神通用途标签。
// 流程：根据现有kind分类选择中文称呼。
// 调用方式：神通面板传入神通对象。
// 返回值：中文标签字符串。
export function spellKindLabel(sp) {
  const k = sp.kind;
  if (k === 'attack') return '攻击';
  if (k === 'sword') return '剑击';
  if (k === 'fire') return '火法';
  if (k === 'heal') return '治疗';
  if (k === 'shield') return '护盾';
  if (k === 'guard') return '守阵';
  if (k === 'mana') return '回灵';
  if (k === 'drain') return '神识';
  return '其它';
}

// 渲染十二大道。
// state 用于取经验、悟道点、主修方向；busy 表示当前不能操作。
// 用途：展示十二大道与节点条件。
// 流程：读取经验、悟道点与已领悟节点，为每道生成状态及按钮。
// 调用方式：洞府页传入state和nodes。
// 返回值：道法卡片HTML字符串。
export function renderDaos(state, nodes) {
  const a = state.arts;
  const byDao = {};
  for (const n of (nodes || [])) (byDao[n.dao] ||= []).push(n);

  return DAOS.map(dao => {
    const exp = a.exp[dao] || 0;
    const lv = artLevelOf(exp);
    const list = (byDao[dao] || []).slice().sort((x, y) => x.level - y.level);

    const nodeHTML = list.map(n => {
      const owned = a.nodes.includes(n.id);
      const enoughLevel = lv >= n.level;
      const enoughPoints = a.points >= n.cost;
      const can = !owned && enoughLevel && enoughPoints;
      const why = owned ? '已领悟'
        : !enoughLevel ? `需「${DAO_LEVELS[n.level]}」`
        : !enoughPoints ? `需 ${n.cost} 悟道点，你有 ${a.points}`
        : '';
      return `<div class="dao-node${owned ? ' owned' : ''}">`
        + `<div class="dn-head"><strong>${esc(n.name)}</strong>`
        + `<span class="dn-cost">${n.cost} 点</span></div>`
        + `<p class="dn-desc">${esc(n.desc)}</p>`
        + `<button class="dn-btn${owned ? ' owned' : ''}" `
        + `${can ? `data-action="node" data-id="${n.id}"` : 'disabled'}>`
        + `${owned ? '已领悟' : why || '领悟'}</button>`
        + `</div>`;
    }).join('');

    return `<section class="card dao-card${a.focus === dao ? ' focused' : ''}">`
      + `<div class="dao-card-top">`
      + `<h3>${dao}之大道</h3>`
      + `${a.focus === dao ? '<span class="tag">当前修行方向</span>' : ''}`
      + `</div>`
      + `<p class="dao-meta">${DAO_LEVELS[lv]} · 经验 ${exp}`
      + `${lv < 5 ? ` / ${THRESHOLDS[lv + 1]}` : ''}</p>`
      + `<div class="dao-bar"><i style="width:${lv >= 5 ? 100
          : Math.min(100, exp / THRESHOLDS[lv + 1] * 100)}%"></i></div>`
      + `<div class="arts-actions">`
      + `<button data-action="meditate" data-id="${dao}">参悟 30 天</button>`
      + `<button data-action="focus" data-id="${dao}" `
      + `${a.focus === dao ? 'disabled' : ''}>设为修行方向</button>`
      + `</div>`
      + `<div class="dao-nodes">${nodeHTML}</div>`
      + `</section>`;
  }).join('');
}

// 洞府页的侧栏
// 用途：展示修炼和斗法属性摘要。
// 流程：读取mods和基础灵力等权威字段，组合说明。
// 调用方式：洞府页传入state。
// 返回值：侧栏HTML字符串。
export function renderArtsAside(state) {
  const a = state.arts;
  const m = a.mods;
  return `<h3>修行概览</h3>`
    + `<p class="intro">悟道点 ${a.points} · 主修 ${esc(a.focus)}道</p>`
    + `<div class="detail-list" style="border-top:0;padding-top:0">`
    + `<div><label>修炼效率</label><strong>×${m.cult.toFixed(3)}</strong></div>`
    + `<div><label>斗法伤害</label><strong>×${m.attack.toFixed(3)}</strong></div>`
    + `<div><label>气血上限</label><strong>×${m.hp.toFixed(3)}</strong></div>`
    + `<div><label>${a.manaRegen != null ? '每回合基础灵力' : '回灵加成'}</label><strong>${a.manaRegen ?? m.mana}</strong></div>`
    + `<div><label>丹炉耐久</label><strong>${a.durability}</strong></div>`
    + `</div>`
    + `<div class="hint-box">`
    + `每道五境，经验阈值 30 / 100 / 300 / 900 / 2000，<br>`
    + `升境得 1 / 1 / 2 / 2 / 2 点悟道点。<br>`
    + `参悟 30 天可以专门推进某一道的经验。<br>`
    + `节点永久生效，境界到位才能领悟。<br>`
    + `每升一个大境界，额外神通槽 +2、基础回灵 +1。`
    + `</div>`;
}

// 携带神通面板：列出全部神通，逐张切换。
// JS 版有「携带神通 / 卸下神通」，C++ 版此前完全缺失，
// 玩家只能一直用开局那四个初始神通。
// 用途：区分神通学习与携带状态。
// 流程：按流派筛选，核实已学、境界、槽位和权威条件，显示真实预览。
// 调用方式：构筑页传入目录、state和筛选选项。
// 返回值：可操作或禁用神通卡片HTML。
export function renderSpellPicker(data, state, options = {}) {
  const max = state.arts.spellSlots ?? data?.max ?? 4;
  const mandatoryIds = (state.arts.mandatorySpells || []).map(s => typeof s === 'string' ? s : s.id);
  const list = (data?.spells || []).filter(sp => sp.mandatory || mandatoryIds.includes(sp.id)
    || ((!options.dao || options.dao === 'all' || options.dao === sp.dao)
      && (!options.spells || options.spells.includes(sp.id))));
  const stage = state.player.stage;
  const carriedIds = state.arts.spells || [];
  const carried = carriedIds.filter(id => !mandatoryIds.includes(id)).length;

  if (!list.length) {
    return `<div class="empty">神通表为空。</div>`;
  }

  return `<p class="intro">额外神通 ${carried} / ${max} 槽。`
    + `普通攻击与防御固定常备，各五张，不占额外槽。每种额外神通入牌组两张。`
    + `${state.arts.manaRegen != null ? `当前每回合基础灵力 ${state.arts.manaRegen} 点。` : ''}</p>`
    + `<div class="arts-grid spell-picker" style="margin:0">`
    + list.map(sp => {
        const mandatory = sp.mandatory || mandatoryIds.includes(sp.id);
        const isCarried = mandatory || carriedIds.includes(sp.id);
        const learned = isCarried || sp.learned === true || sp.known === true
          || (state.arts.knownSpells || []).includes(sp.id);
        const locked = stage < sp.stage;
        const full = !isCarried && carried >= max;
        const realms = ['炼气初期', '炼气中期', '炼气后期', '筑基初期', '筑基中期', '筑基后期', '金丹初期', '金丹中期', '金丹后期', '元婴初期', '元婴中期', '元婴后期', '化神初期', '化神中期', '化神后期'];
        const stageName = sp.stageName || realms[sp.stage] || '更高境界';
        const why = mandatory ? '固定常备，不占槽位'
          : locked ? `尚未达到${stageName}`
          : !learned ? '尚未学会，可向熟悉此术的道友请教或在本门藏经阁兑换'
          : full ? `最多携带 ${max} 个，请先卸下一个`
          : sp.reason || '';
        const canEquip = learned && !locked && !full && sp.canEquip !== false && sp.enabled !== false;
        return `<button class="card${isCarried ? ' selected' : ''}${mandatory ? ' mandatory' : ''}${learned ? ' learned' : ' unlearned'}" `
          + `data-spell-id="${esc(sp.id)}" `
          + `${!mandatory && (isCarried || canEquip)
              ? `data-action="spell" data-id="${sp.id}"` : 'disabled'}>`
          + `<div class="card-top"><span class="card-glyph">✦</span>`
          + `<strong>${esc(sp.name)}</strong>`
          + `<span class="count">${mandatory ? '固定常备' : isCarried ? '已携带 · 点击卸下' : learned ? '已学会 · 未携带' : '未学会'}</span></div>`
          + `<p class="card-rule">${esc(cardRule(sp))}</p>`
          + renderNumericPreview(sp.preview)
          + `<div class="card-foot">${esc(sp.dao)}系 · `
          + `${esc(cardKind(sp))} · 灵力 ${sp.actualCost ?? sp.cost}`
          + `${sp.cooldown ? ` · 冷却 ${sp.cooldown} 回合` : ''} · 所需境界：${esc(stageName)}`
          + `${why ? ` · ${why}` : ''}</div></button>`;
      }).join('')
    + `</div>`;
}

// 用途：显示流派所需构筑条件。
// 流程：解析功法、神通和节点引用，标注获得与生效状态。
// 调用方式：流派页面传入state、神通目录、节点和筛选id。
// 返回值：流派HTML；无建议时空字符串。
export function renderArtBuilds(state, data, nodes, selected = 'all') {
  const builds = state.artBuilds || [];
  if (!builds.length) return '';
  const spells = data?.spells || [];
  // 用途：解释流派构筑引用。
  // 流程：分别按功法、神通或道法查目录并核实获得与生效状态。
  // 调用方式：renderArtBuilds传入kind和ref。
  // 返回值：名称、ready、known及说明对象。
  const resolve = (kind, ref) => {
    const id = typeof ref === 'string' ? ref : ref.id;
    const definition = typeof ref === 'string' ? null : ref;
    if (kind === 'manual') {
      const m = (state.arts.manualList || []).find(x => x.id === id);
      const source = (state.manualSources || []).find(x => x.id === id);
      const copied = source?.owned || (state.arts.bookList || []).includes(id);
      const name = definition?.name || m?.name || (state.manualCatalog || []).find(x => x.id === id)?.name || source?.name || '未知功法';
      return { name, ready: !!(m?.isMain || m?.isAux), known: !!m || !!copied,
        note: m ? m.isMain || m.isAux ? '已生效' : '已修习 · 未设主辅修' : copied ? '抄本已得 · 待修习' : '尚未获取抄本' };
    }
    if (kind === 'spell') {
      const sp = spells.find(x => x.id === id);
      const ready = (state.arts.spells || []).includes(id) || (state.arts.mandatorySpells || []).some(x => (typeof x === 'string' ? x : x.id) === id);
      const known = ready || sp?.known || sp?.learned || (state.arts.knownSpells || []).includes(id);
      return { name: definition?.name || sp?.name || (state.arts.spellList || []).find(x => x.id === id)?.name || id,
        ready, known, note: ready ? '已携带' : `${known ? '已学会 · 未携带' : '未学会'}${sp && state.player.stage < sp.stage ? ` · 需${sp.stageName || '更高境界'}` : ''}` };
    }
    const n = (nodes || []).find(x => x.id === id);
    const ready = (state.arts.nodes || []).includes(id);
    return { name: definition?.name || n?.name || id, ready, known: ready, note: ready ? '已领悟' : '待领悟' };
  };
  // 用途：生成流派一类组件。
  // 流程：解析每个引用并按已生效、已获得或缺少生成标记。
  // 调用方式：renderArtBuilds传入label、kind和refs。
  // 返回值：组件组HTML；无条目时空字符串。
  const group = (label, kind, refs) => {
    if (!refs?.length) return '';
    return `<div class="build-components"><b>${label}</b>` + refs.map(ref => {
      const item = resolve(kind, ref);
      const manualId = typeof ref === 'string' ? ref : ref.id;
      const manual = kind === 'manual'
        ? (state.manualCatalog || state.arts.manualList || []).find(entry => entry.id === manualId) || { id: manualId }
        : null;
      const art = manual ? renderManualIllustration(manual, { compact: true }) : '';
      return `<span class="build-component ${item.ready ? 'ready' : item.known ? 'known' : 'missing'}">${art}<span class="build-component-copy">${esc(item.name)}<small>${esc(item.note)}</small></span></span>`;
    }).join('') + `</div>`;
  };
  return `<div class="art-build-controls"><label>流派组合 <select data-art-build-filter><option value="all">全部流派</option>`
    + builds.map(b => `<option value="${esc(b.id)}" ${b.id === selected ? 'selected' : ''}>${esc(b.name)}</option>`).join('')
    + `</select></label><span>绿色表示已生效的功法、已携带的神通或已领悟的道法。选择一组后，神通列表只显示它需要的神通与固定常备牌。</span></div>`
    + `<div class="art-builds">` + builds.filter(b => selected === 'all' || b.id === selected).map(b => {
      const refs = [...(b.manuals || []).map(x => resolve('manual', x)), ...(b.spells || []).map(x => resolve('spell', x)), ...(b.nodes || []).map(x => resolve('node', x))];
      const ready = refs.filter(x => x.ready).length;
      return `<article class="art-build"><h4>${esc(b.name)} <span>${ready} / ${refs.length} 已具备</span></h4>`
        + `<p>${esc(b.desc)}</p>` + group('功法', 'manual', b.manuals) + group('神通', 'spell', b.spells) + group('道法', 'node', b.nodes) + `</article>`;
    }).join('') + `</div>`;
}

