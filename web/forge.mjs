import { renderArtifactIllustration } from './artifact-illustrations.mjs';
import { esc } from './shell.mjs';

const list = value => Array.isArray(value) ? value : [];
const categoryNames = { attack: '攻击', defense: '防御', support: '辅助' };
const shapeMarks = { sword: '剑', needle: '针', seal: '印', ruler: '尺', box: '匣', armor: '甲', robe: '袍', bell: '钟', pearl: '珠', ring: '环', token: '令' };
const dimensionNames = ['亲和', '操控', '灵性', '坚固', '韧性'];
export const forgeInscriptionName = id => ({ azure: '青龙', white: '白虎', vermilion: '朱雀', black: '玄武' }[id] || '无灵纹');
const shapePaths = {
  sword: '<path d="M94 26 77 53v91l17 14 17-14V53Z"/><path d="M94 42v108M64 157l30-8 30 8-8 10-22-5-22 5ZM87 164v32h14v-32M83 199h22"/>',
  needle: '<path d="m120 30-51 158-2 21 13-17 51-158ZM118 43l-43 143M128 27l5-12M120 25l-2-10"/>',
  seal: '<path d="M55 111h80v70H55ZM49 181h92v13H49ZM67 111V86c0-21 10-34 28-34s28 13 28 34v25M81 78l14-18 14 18-14 18ZM73 131h44v30H73ZM84 138v16h22v-16"/>',
  ruler: '<path d="m64 35 35-7 32 164-35 7ZM77 56l15-3M81 77l15-3M85 98l15-3M89 119l15-3M93 140l15-3M97 161l15-3M101 182l15-3"/>',
  box: '<path d="M43 79 95 51l52 28v98l-52 28-52-28ZM43 79l52 28 52-28M95 107v98M43 103l52 28 52-28M86 130h18v20H86ZM56 64l39 21 39-21M67 53l28-15 28 15"/>',
  armor: '<path d="m67 51-25 22 15 38 12-10v81l26 20 26-20v-81l12 10 15-38-25-22-28 12ZM69 102l26 15 26-15M69 127l26 15 26-15M69 152l26 15 26-15M80 62l15 26 15-26"/>',
  robe: '<path d="m71 49-17 13-25 76 24 10 17-42-4 96h58l-4-96 17 42 24-10-25-76-17-13-24 24ZM72 50l42 76M118 50l-42 76M68 133h54M91 135l-7 58M101 135l9 58"/>',
  bell: '<path d="M58 156c15-14 9-62 18-78 8-14 30-14 38 0 9 16 3 64 18 78ZM50 156h90v16H50ZM82 177c0 19 26 19 26 0M83 65V52a12 12 0 0 1 24 0v13M83 82c-8 25-3 53-12 66"/>',
  pearl: '<circle cx="95" cy="111" r="48"/><circle cx="95" cy="111" r="38"/><path d="M75 93c11-17 34-14 41 3M71 140l24-18 24 18M65 156l-13 35 43-15 43 15-13-35M85 64l10-19 10 19"/>',
  ring: '<path d="M95 58c-39 0-57 38-45 75s34 53 45 53 33-16 45-53-6-75-45-75ZM95 78c-27 0-37 25-28 50s22 37 28 37 19-12 28-37-1-50-28-50ZM79 61l3-23h26l3 23M82 39l13-13 13 13M84 42l11 14 11-14"/>',
  token: '<path d="m95 35 39 26-7 123-32 22-32-22-7-123ZM75 75h40v87H75ZM83 93h24M83 110h24M83 128h24M87 164v20M103 164v20M88 33V20h14v13"/>'
};

function forgeShapeArt(shape) {
  const illustration = renderArtifactIllustration(shape);
  if (illustration) return illustration;
  return shapePaths[shape?.id] ? `<svg viewBox="0 0 190 230" fill="#eef2de" stroke="currentColor" stroke-width="3" stroke-linejoin="round" stroke-linecap="round">${shapePaths[shape.id]}</svg>`
    : `<span>${esc(shapeMarks[shape?.id] || shape?.name?.[0] || '器')}</span>`;
}

export function hasNewForge(catalog) {
  return Number(catalog?.version) >= 2 || list(catalog?.effects).length > 0;
}

export function forgeRequest(selection = {}, quote) {
  return { version: 2, shape: selection.shape || '', materials: Array.from({ length: 10 }, (_, index) => selection.materials?.[index] || ''),
    main: selection.main || '', effect: selection.effect || '', inscription: selection.inscription || '',
    ...(quote?.quoteToken ? { quoteToken: quote.quoteToken } : {}) };
}

export function forgeEffects(catalog, selection, preview) {
  if (Array.isArray(preview?.info?.availableEffects)) return preview.info.availableEffects;
  const shape = list(catalog?.shapes).find(item => item.id === selection?.shape);
  const main = list(catalog?.materials).find(item => item.id === selection?.main);
  return list(catalog?.effects).filter(item => (!list(item.allowedShapes).length || item.allowedShapes.includes(shape?.id))
    && (!item.slot || item.slot === shape?.slot || (!shape?.slot && item.category === shape?.category))
    && (!item.category || item.category === shape?.category)
    && (!list(item.categories).length || item.categories.includes(shape?.category))
    && (!list(item.elements).length || !main || item.elements.includes(main.element)));
}

export function normalizeForgeSelection(selection = {}, catalog = {}) {
  const shapes = list(catalog.shapes), materials = list(catalog.materials);
  const next = forgeRequest(selection);
  if (!shapes.some(item => item.id === next.shape)) next.shape = shapes[0]?.id || '';
  next.materials = next.materials.map(id => materials.some(item => item.id === id) ? id : '');
  if (!next.materials.includes(next.main) || !next.main) next.main = next.materials.find(Boolean) || '';
  const effects = forgeEffects(catalog, next);
  if (!effects.some(item => item.id === next.effect)) next.effect = effects[0]?.id || '';
  if (effects.find(item => item.id === next.effect)?.passive) next.inscription = '';
  return next;
}

// These operations only edit the ten selected inventory portions. All crafting rules are quoted by the server.
export function editForgeSelection(selection, catalog, action, value, slot = null) {
  const next = normalizeForgeSelection(selection, catalog);
  if (action === 'clear') { next.materials.fill(''); next.main = ''; }
  if (action === 'remove' && Number.isInteger(Number(value)) && Number(value) >= 0 && Number(value) < 10) next.materials[Number(value)] = '';
  if (action === 'add' || action === 'fill') {
    const material = list(catalog.materials).find(item => item.id === value);
    if (!material) return next;
    const chosenSlot = Number.isInteger(slot) && slot >= 0 && slot < 10 ? slot : next.materials.indexOf('');
    const used = next.materials.filter((id, index) => id === value && index !== chosenSlot).length;
    if (chosenSlot < 0 || used >= Number(material.owned || 0)) return next;
    next.materials[chosenSlot] = value;
    if (action === 'fill') {
      let remaining = Number(material.owned || 0) - next.materials.filter(id => id === value).length;
      for (let index = 0; index < 10 && remaining > 0; index++) if (!next.materials[index]) { next.materials[index] = value; remaining--; }
    }
  }
  if (['shape', 'main', 'effect', 'inscription'].includes(action)) next[action] = value;
  return normalizeForgeSelection(next, catalog);
}

function control(mode, action, value = '', extra = '') {
  return mode === 'mobile' ? `data-ui="forge-${action}" data-value="${esc(value)}" ${extra}`
    : `data-action="forge-${action}" data-id="${esc(value)}" ${extra}`;
}

function selectControl(mode, field, label, values, selected, empty = '') {
  return `<label class="forge-control"><span>${esc(label)}</span><select ${mode === 'mobile' ? 'data-ui="forge-field"' : 'data-forge-field'} data-field="${esc(field)}">`
    + (empty ? `<option value="">${esc(empty)}</option>` : '')
    + values.map(item => `<option value="${esc(item.id)}"${item.id === selected ? ' selected' : ''}>${esc(item.name || item.id)}</option>`).join('') + '</select></label>';
}

export function renderForgePicker(catalog, selection, view = {}, mode = 'desktop') {
  const materials = list(catalog?.materials), filters = view.filters || {}, counts = new Map();
  for (const id of selection.materials || []) if (id) counts.set(id, (counts.get(id) || 0) + 1);
  const choices = materials.filter(item => ['grade', 'element', 'polarity', 'family', 'part'].every(key => !filters[key] || String(item[key]) === filters[key])
    && (!filters.owned || Number(item.owned) > 0));
  const pageSize = 48, pages = Math.max(1, Math.ceil(choices.length / pageSize));
  const page = Math.max(0, Math.min(pages - 1, Number(view.page) || 0));
  const visible = choices.slice(page * pageSize, (page + 1) * pageSize);
  return `<section class="forge-picker" aria-label="选择炼器材料"><div class="forge-picker-heading"><strong>选材 · 第 ${Number(view.slot ?? 0) + 1} 份</strong>`
    + (selection.materials?.[view.slot] ? `<button type="button" ${control(mode, 'remove', view.slot)}>取回这一份</button>` : '')
    + `<button type="button" ${control(mode, 'picker-close')}>收起</button></div><div class="forge-filters">`
    + ['grade', 'element', 'polarity', 'family', 'part'].map((key, index) => {
      const values = [...new Set(materials.map(item => item[key]).filter(value => value != null && value !== ''))];
      return `<label><span class="sr-only">${['品阶', '属性', '阴阳', '材类', '部位'][index]}</span><select ${mode === 'mobile' ? 'data-ui="forge-filter"' : 'data-forge-filter'} data-field="${key}"><option value="">全部${['品阶', '属性', '阴阳', '材类', '部位'][index]}</option>`
        + values.map(value => { const sample = materials.find(item => item[key] === value); const label = key === 'family' ? sample.familyName || value : key === 'part' ? sample.partName || value : value;
          return `<option value="${esc(value)}"${filters[key] === String(value) ? ' selected' : ''}>${esc(label)}${key === 'grade' ? '阶' : ''}</option>`; }).join('') + '</select></label>';
    }).join('') + `<button type="button" class="${filters.owned ? 'selected' : ''}" ${control(mode, 'owned')} aria-pressed="${!!filters.owned}">只看持有</button></div>`
    + `<div class="forge-material-grid">${visible.map(item => {
      const inSlot = selection.materials?.[view.slot] === item.id ? 1 : 0;
      const available = Number(item.owned || 0) - (counts.get(item.id) || 0) + inSlot;
      return `<article class="forge-material${available > 0 ? '' : ' unavailable'}"><div><strong>${esc(item.name)}</strong><b>${esc(item.grade)}阶</b></div>`
        + `<small>${esc([item.element, item.polarity, item.familyName || item.family, item.partName || item.part].filter(Boolean).join(' · '))}</small>`
        + `<p>持有 ${esc(item.owned || 0)} · 已投 ${counts.get(item.id) || 0}</p><small>${esc(item.acquisitionPending ? '获取后续开放' : item.source || '现有材料')}</small>`
        + `<div class="forge-material-actions"><button type="button" ${control(mode, 'add', item.id)}${available > 0 ? '' : ' disabled'}>投一份</button>`
        + `<button type="button" ${control(mode, 'fill', item.id)}${available > 0 && selection.materials.includes('') ? '' : ' disabled'} title="用同种材料填入空位，最多使用已有数量">填空位</button></div></article>`;
    }).join('') || '<p class="forge-empty">没有符合筛选的材料。</p>'}</div>`
    + `<div class="forge-pagination"><span>${choices.length} 种材料${pages > 1 ? ` · 第 ${page + 1} / ${pages} 页` : ''}</span>${pages > 1 ? `<div><button type="button" ${control(mode, 'page', page - 1)}${page ? '' : ' disabled'}>上一页</button><button type="button" ${control(mode, 'page', page + 1)}${page < pages - 1 ? '' : ' disabled'}>下一页</button></div>` : ''}</div></section>`;
}

export function renderForgeWorkshop(state, catalog, selection = {}, preview, { mode = 'desktop', view = {}, continuation = '' } = {}) {
  const chosen = normalizeForgeSelection(selection, catalog), info = preview?.info, materials = list(catalog?.materials), shapes = list(catalog?.shapes);
  const shape = shapes.find(item => item.id === chosen.shape), count = chosen.materials.filter(Boolean).length;
  const mainMaterials = [...new Set(chosen.materials.filter(Boolean))].map(id => materials.find(item => item.id === id)).filter(Boolean);
  const effects = forgeEffects(catalog, chosen, preview);
  const selectedEffect = effects.find(item => item.id === chosen.effect);
  const inscriptions = selectedEffect?.passive ? [] : list(catalog?.inscriptions).filter(item => item.id);
  const ready = info?.valid === true && info.enabled === true && !!info.quoteToken && !state?.job;
  const dims = list(info?.dimensions), dimension = index => Number(dims[index] ?? 0);
  const reason = list(info?.blockers).join('；') || info?.reason || preview?.error || (preview ? '' : '正在读取本炉推演…');
  return `<section class="forge-workshop ${mode === 'mobile' ? 'forge-mobile' : ''}" data-forge-workshop><header class="forge-workshop-heading"><div><strong>十材铸器</strong><span>自由配料 · 成器前选定主效果</span></div><span>${count} / 10 份</span></header>`
    + `<div class="forge-configuration">${selectControl(mode, 'shape', '器型', shapes.map(item => ({ ...item, name: `${categoryNames[item.category] || ''} · ${item.name}` })), chosen.shape)}`
    + selectControl(mode, 'main', '引灵主材', mainMaterials, chosen.main, mainMaterials.length ? '' : '先投入材料')
    + selectControl(mode, 'effect', '主效果', effects, chosen.effect, effects.length ? '' : '等待合法效果')
    + selectControl(mode, 'inscription', '灵纹', inscriptions, chosen.inscription, '无灵纹') + '</div>'
    + `<div class="forge-body"><aside class="forge-qualities"><div class="forge-energy"><small>可用 / 所需器能</small><strong>${info?.energy != null ? esc(info.energy) : '—'}<small> / ${info?.requiredEnergy != null ? esc(info.requiredEnergy) : '—'}</small></strong>`
    + `<span>材料利用率 ${info?.utilization != null ? `${Math.round(info.utilization * 100)}%` : '待推演'}</span></div><div class="forge-dimensions">`
    + dimensionNames.map((name, index) => `<div><span>${name}</span><meter min="0" max="500" value="${dimension(index)}" aria-label="${name}"></meter><b>${info?.dimensions ? esc(dimension(index)) : '—'}<small> / 500</small></b></div>`).join('') + '</div>'
    + `<small class="forge-dimension-total">五维合计 ${info?.dimensionScore != null ? esc(info.dimensionScore) : '—'}</small></aside>`
    + `<div class="forge-vessel"><div class="forge-vessel-art" aria-hidden="true">${forgeShapeArt(shape)}</div><div class="forge-portions">`
    + chosen.materials.map((id, index) => { const item = materials.find(material => material.id === id); const shortName = item?.partName && item.part !== 'legacy' ? `${item.element}${item.partName}` : item?.name;
      return `<article class="forge-portion${item ? ' filled' : ''}${view.slot === index ? ' active' : ''}"><button type="button" ${control(mode, 'slot', index)} aria-label="第${index + 1}份：${esc(item?.name || '选择材料')}"><small>${index + 1}</small><strong class="forge-material-full">${esc(item?.name || '＋')}</strong><strong class="forge-material-short" aria-hidden="true">${esc(shortName || '＋')}</strong><span class="forge-portion-meta">${item ? `${esc(item.grade)}阶 · ${esc(item.familyName || item.family || item.element)}` : '投料'}</span><span class="forge-portion-grade" aria-hidden="true">${item ? `${esc(item.grade)}阶` : '投料'}</span></button>${item ? `<button type="button" class="forge-remove" ${control(mode, 'remove', index)} aria-label="取回第${index + 1}份${esc(item.name)}">×</button>` : ''}</article>`; }).join('')
    + `</div><div class="forge-vessel-caption"><strong>${esc(shape?.name || '法器')}</strong><span>${esc(categoryNames[shape?.category] || '')} · ${esc(info?.mainElement || '待引灵')}${esc(info?.mainPolarity || '')}</span><button type="button" ${control(mode, 'clear')}${count ? '' : ' disabled'}>全部取回</button></div></div>`
    + `<section class="forge-result"><div><small>成器效果</small><h3>${esc(info?.effectName || effects.find(item => item.id === chosen.effect)?.name || '选择主效果')}</h3><p>${esc(count ? info?.effectText || info?.ability?.description || info?.passive?.text || '投入材料后查看本炉实际效果。' : '投入材料后查看本炉实际效果。')}</p>`
    + (info?.ability ? `<p class="forge-battle-cost" data-forge-battle-cost>战斗消耗：${info.ability.cost != null ? `${esc(info.ability.cost)} 灵力` : '灵力待报价'}${Number(info.ability.healthCost) > 0 ? ` · ${esc(info.ability.healthCost)} 真实气血` : ''}${info.ability.cooldown != null ? ` · 冷却 ${esc(info.ability.cooldown)} 回合` : ''}</p>` : '')
    + (info?.inscriptionText ? `<p class="forge-inscription">${esc(info.inscriptionText)}</p>` : '') + `</div><dl><div><dt>品阶</dt><dd>${info?.grade ? `${esc(info.grade)}阶` : '—'}</dd></div><div><dt>成功率</dt><dd>${info?.chance != null ? `${Math.round(info.chance * 100)}%` : '—'}</dd></div><div><dt>耗时</dt><dd>${info?.days != null ? `${esc(info.days)}天` : '—'}</dd></div><div><dt>炉费</dt><dd>${info?.cost != null ? `${esc(info.cost)}灵石` : '—'}</dd></div></dl>`
    + `<p class="forge-condition" role="status">${esc(reason || '材料与条件齐备，确认后才投入。')}</p>${continuation || `<button type="button" class="primary forge-submit" ${control(mode, 'run-v2')}${ready ? '' : ' disabled'}>铭刻成器</button>`}`
    + `<small class="forge-note">十份材料可同名重复。引灵主材包含在投料内。</small></section></div>`
    + (view.slot != null ? renderForgePicker(catalog, chosen, view, mode) : '')
    + (list(info?.materials).length ? `<details class="forge-requirements"><summary>本炉材料 · 所需 / 持有</summary><div>${info.materials.map(item => `<span>${esc(item.name || item.id)} ${esc(item.required)} / ${esc(item.owned)}${Number(item.missing) > 0 ? ` · 缺${esc(item.missing)}` : ''}</span>`).join('')}</div></details>` : '') + '</section>';
}
