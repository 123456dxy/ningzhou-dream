import { esc, fmt, action, card, empty, tag, tabs, numericPreview, uiButton, legacyLink } from '../ui.mjs';

// data.nodes = /arts/nodes, data.spells = /arts/spells. Refresh both after mutations.
// Display the core's previews and flags; do not reproduce combat or Dao formulas.
const list = value => Array.isArray(value) ? value : [];
const count = value => esc(fmt(Number(value) || 0));
const note = text => text ? `<p class="meta">${esc(text)}</p>` : '';
const blocked = state => Boolean(state.ended || state.pending || state.battle || state.job || state.dungeons?.run);
const refId = value => typeof value === 'string' ? value : value?.id;
const artsTabs = [['loadout', '当前搭配'], ['manuals', '功法'], ['spells', '神通'], ['daos', '悟道'], ['cave', '洞府']];
const kindLabel = value => ({ attack: '攻击', sword: '剑术', human: '剑术', fire: '灼烧', heal: '治疗', shield: '护盾', guard: '防御', mana: '聚气', float: '聚气', drain: '穿盾', charge: '蓄势', water: '蓄水', poison: '施毒', soul: '震荡', wave: '水势爆发', heavy: '重势爆发', poisonBurst: '毒伤爆发', soulBurst: '震荡爆发', bodyBurst: '体道爆发', fireBurst: '引爆', steal: '夺灵', store: '蓄灵', flow: '聚气', burst: '夺灵', draw: '抽牌', foresee: '预谋', exhaust: '本场移除', redraw: '换手', empower: '增伤', ward: '减伤' }[value] || '神通');

const queryOf = value => String(value || '').trim().toLocaleLowerCase();
const matches = (query, ...values) => !query || values.filter(Boolean).join(' ').toLocaleLowerCase().includes(query);
const searchField = (key, value, label) => `<label class="search-label">${esc(label)}<input type="search" data-input="${esc(key)}" value="${esc(value || '')}" placeholder="名称、大道或效果" autocomplete="off"></label>`;
const compactList = (items, render, limit = 8) => `<div class="list">${items.slice(0, limit).map(render).join('')}</div>`
  + (items.length > limit ? `<details><summary>展开其余 ${items.length - limit} 项</summary><div class="list">${items.slice(limit).map(render).join('')}</div></details>` : '');

const selectControl = (id, label, entries, active) => `<label class="player-control"><span>${esc(label)}</span><select data-ui="${esc(id)}">${entries.map(([value, text]) => `<option value="${esc(value)}"${String(value) === String(active) ? ' selected' : ''}>${esc(text)}</option>`).join('')}</select></label>`;
function continuation(state) {
  if (!state.job) return '';
  const days = Math.max(1, Math.min(3600, Number(state.job.remaining) || 1));
  return action(`继续 ${fmt(days)} 天至${days === Number(state.job.remaining) ? '完成' : '下一段'}`, 'advance', { days }, { className: 'primary', disabled: Boolean(state.ended || state.pending || state.battle || state.dungeons?.run) });
}

function outcome(ui, id, paths) {
  const result = ui.playerActionResult;
  if (!result?.text || (result.itemId && id && String(result.itemId) !== String(id)) || (result.path && result.path !== 'advance' && !paths.some(path => result.path === path || result.path.startsWith(path + '/')))) return '<div class="player-result player-result-empty" role="status">操作后，结果会保留在这里。</div>';
  return `<div class="player-result" role="status"><p>${esc(result.text)}</p></div>`;
}

function loadoutView(state, data, showBuilds = false) {
  const arts = state.arts || {};
  const manuals = list(arts.manualList);
  const main = manuals.find(item => item.isMain);
  const aux = manuals.filter(item => item.isAux);
  const carried = list(arts.spellList);
  return card('', `<div class="player-title"><strong>当前搭配</strong>${tag(`${list(arts.spells).length} / ${fmt(arts.spellSlots ?? data.spells?.max ?? '—')} 神通`)}</div>`
    + `<p class="player-context">主修：<strong>${esc(main?.name || '尚未设置')}</strong>${main ? ` · 第 ${count(main.level)} 层` : ''}</p>`
    + `<p class="player-context">辅修：${esc(aux.map(item => item.name).join('、') || '无')}</p>`
    + `<p class="player-effect">${esc(carried.map(item => item.name).join(' · ') || '尚未携带额外神通')}</p>`
    + `<p class="player-context">${esc(arts.focus || '未定')}道 · 每回合基础灵力 ${count(arts.manaRegen)}</p>`
    + `<div class="player-actions">${uiButton('调整功法', 'arts-tab', 'manuals', 'primary')}${uiButton('调整神通', 'arts-tab', 'spells', 'primary')}</div>`
    + `<details><summary>加成与固定招式</summary>${modifiersView(state)}${list(arts.mandatorySpells).map(item => note(typeof item === 'string' ? item : `${item.name} · ${item.copies} 张`)).join('')}</details>`
    + `<details${showBuilds ? ' open' : ''}><summary>流派组合建议</summary>${buildsView(state, data)}</details>`, 'player-workspace');
}

function manualView(state, ui) {
  const arts = state.arts || {};
  const learned = list(arts.manualList);
  const sources = list(state.manualSources);
  const catalogue = list(state.manualCatalog);
  const definitions = [...new Set([...learned, ...sources, ...catalogue].map(item => item.id))].map(id => {
    const manual = learned.find(item => item.id === id);
    const source = sources.find(item => item.id === id);
    const definition = catalogue.find(item => item.id === id);
    const lessons = list(state.nearby).flatMap(npc => list(npc.learning).filter(lesson => lesson.kind === 'manual' && lesson.id === id && !lesson.learned).map(lesson => ({ ...lesson, npc: npc.id, teacher: npc.name })));
    return { ...definition, ...source, ...manual, id, learned: Boolean(manual), owned: Boolean(manual || source?.owned || list(arts.bookList).includes(id)), source, manual, lessons };
  });
  const groups = { learned: definitions.filter(item => item.learned), books: definitions.filter(item => item.owned && !item.learned), sources: definitions.filter(item => !item.owned && (item.source || item.lessons.length)) };
  const selected = Object.hasOwn(groups, ui.manualFilter) ? ui.manualFilter : 'learned';
  const query = queryOf(ui.manualSearch);
  let filtered = groups[selected].filter(item => matches(query, item.name, item.dao, item.effect, item.how));
  const retained = definitions.find(item => item.id === (ui.manualId || (['arts/learn','arts/main','arts/aux','npc-learn'].includes(ui.playerActionResult?.path) ? ui.playerActionResult.itemId : '')));
  if (retained && ui.playerActionResult?.itemId === retained.id && !filtered.some(item => item.id === retained.id)) filtered = [retained, ...filtered];
  const chosen = filtered.find(item => item.id === (ui.manualId || (['arts/learn','arts/main','arts/aux','npc-learn'].includes(ui.playerActionResult?.path) ? ui.playerActionResult.itemId : ''))) || filtered[0];
  const main = learned.find(item => item.isMain);
  const context = `<p class="player-context">当前主修：<strong>${esc(main?.name || '未设置')}</strong> · 辅修 ${esc(learned.filter(item => item.isAux).map(item => item.name).join('、') || '无')}</p>`;
  const controls = `<div class="player-controls">${selectControl('manual-filter-tab', '范围', [['learned', `已修 ${groups.learned.length}`], ['books', `抄本待修 ${groups.books.length}`], ['sources', `已知来源 ${groups.sources.length}`]], selected)}`
    + selectControl('manual-select', '选择功法', filtered.length ? filtered.map(item => [item.id, `${item.name}${item.isMain ? ' · 主修' : item.isAux ? ' · 辅修' : ''}`]) : [['', '暂无功法']], chosen?.id || '') + '</div>';
  const rows = chosen ? [chosen].map(item => {
    const source = item.source, busy = blocked(state);
    const ready = !item.maxed && source?.canLearn === true && !busy;
    const cost = source?.learnCost ?? item.nextCost, days = source?.learnDays ?? item.nextDays;
    const status = item.isMain ? '主修生效' : item.isAux ? '辅修生效' : item.learned ? '未启用' : item.owned ? '抄本待修' : '未获取';
    const teacher = item.lessons[0];
    let commands = '', requirements = '', reason = '';
    if (item.owned) {
      requirements = item.maxed ? '已修满。' : `${item.learned ? '下一层' : '修习'}：${days == null ? '耗时待查' : `${fmt(days)} 天`} · ${cost == null ? '费用待查' : `${fmt(cost)} 灵石`}；现有 ${fmt(state.player?.stones || 0)}`;
      reason = source?.learnReason || (busy ? '请先完成当前安排或事件。' : !item.maxed && source?.canLearn == null ? '修习条件待查。' : `${item.dao || ''}道${item.learned ? ` · 第 ${item.level} 层` : ''}`);
      commands = (continuation(state) || action(item.maxed ? '已修满' : item.learned ? '修习下一层' : '开始修习', 'arts/learn', { id: item.id }, { className: 'primary', disabled: !ready || cost == null || days == null, title: reason }))
        + action(item.isMain ? '正在主修' : '设为主修', 'arts/main', { id: item.id }, { disabled: busy || !item.learned || item.isMain })
        + action(item.isAux ? '卸下辅修' : '设为辅修', 'arts/aux', { id: item.id }, { disabled: busy || !item.learned || item.isMain });
    } else if (teacher) {
      requirements = `向${teacher.teacher}请教：${fmt(teacher.days)} 天 · ${fmt(teacher.stoneCost)} 灵石 · ${fmt(teacher.creditCost)} 交情；需好感 ${fmt(teacher.affinityNeed)}`;
      reason = teacher.reason || `持有 ${fmt(state.player?.stones || 0)} 灵石。`;
      commands = continuation(state) || action('请教此功法', 'npc-learn', { npc: teacher.npc, kind: 'manual', id: item.id }, { disabled: teacher.enabled !== true || busy, className: 'primary' });
    } else {
      requirements = item.city ? `书坊 ${fmt(item.price)} 灵石；现有 ${fmt(state.player?.stones || 0)}` : item.sect ? `宗门兑换 ${fmt(item.points)} 贡献` : '尚无可用来源。';
      reason = item.how || '前往当地寻找传承。';
      commands = item.city ? uiButton('前往坊市查看', 'navigate', 'inventory/market', 'primary') : uiButton('前往宗门查看', 'navigate', 'social/sects', 'primary');
    }
    const comparison = main && main.id !== item.id ? `当前主修：${main.effect || main.name}；候选：${item.effect || '效果待查'}` : item.effect || '效果说明尚未提供。';
    return `<section class="player-detail" data-selected-manual="${esc(item.id)}"><div class="player-title"><strong>${esc(item.name)}</strong>${tag(status)}</div>`
      + `<p class="player-effect">${esc(comparison)}</p><p class="player-risk">${esc(requirements)}</p><p class="player-condition">${esc(reason)}</p>`
      + `<div class="player-actions">${commands || '<span class="meta">此功法已修满并主修。</span>'}</div>${outcome(ui, item.id, ['arts', 'npc-learn'])}`
      + `<details data-disclosure-key="manual-${esc(item.id)}"><summary>完整效果与当前搭配</summary>${note(item.effect)}${note('当前主修：' + (main?.name || '无'))}${note(main?.effect)}${note('辅修：' + (learned.filter(item => item.isAux).map(item => `${item.name} · ${item.effect}`).join('；') || '无'))}${note(item.how)}</details></section>`;
  }).join('') : empty(query ? '没有符合搜索的功法。' : selected === 'books' ? '暂无待修习抄本。' : selected === 'sources' ? '暂无未获取功法的来源。' : '还没有已修功法。');
  return card('', controls + context + rows + `<details class="player-filter" data-disclosure-key="manual-search"><summary>查找其他功法</summary>${searchField('manual-search', ui.manualSearch, '搜索')}${!chosen ? uiButton('查看已知来源', 'manual-filter-tab', 'sources') : ''}</details>`, 'player-workspace');
}

function modifiersView(state) {
  const arts = state.arts || {};
  const mods = arts.mods || {};
  const rows = [['修炼效率', mods.cult, '×'], ['斗法伤害', mods.attack, '×'], ['气血上限', mods.hp, '×'], ['每回合基础灵力', arts.manaRegen, '']].filter(([, value]) => value != null);
  return rows.length ? `<div class="list">${rows.map(([label, value, prefix]) => `<div class="row"><span class="meta">${label}</span><strong>${prefix}${count(value)}</strong></div>`).join('')}</div>` : empty('暂无修行加成数据。');
}

function spellView(state, ui, data) {
  const arts = state.arts || {};
  const catalogue = list(data.spells?.spells || data.spells);
  const spells = catalogue.length ? catalogue : list(arts.spellList);
  const mandatory = list(arts.mandatorySpells).map(refId);
  const carried = list(arts.spells).map(refId);
  const known = list(arts.knownSpells).map(refId);
  const isFixed = item => item.mandatory === true || mandatory.includes(item.id);
  const isCarried = item => isFixed(item) || carried.includes(item.id);
  const isKnown = item => isCarried(item) || item.known === true || item.learned === true || known.includes(item.id);
  const sourcesFor = id => [
    ...list(state.librarySpells).filter(item => item.id === id && !item.known).map(item => ({ ...item, key: 'library', type: 'library', label: '宗门藏经阁' })),
    ...list(state.nearby).flatMap(npc => list(npc.learning).filter(item => item.kind === 'spell' && item.id === id && !item.learned).map(item => ({ ...item, key: `teacher:${npc.id}`, type: 'teacher', npc: npc.id, label: `向${npc.name || '当地道友'}请教` }))),
  ];
  const groups = { known: spells.filter(item => isKnown(item) && !isFixed(item)), carried: spells.filter(item => isCarried(item) && !isFixed(item)), learnable: spells.filter(item => !isKnown(item) && sourcesFor(item.id).some(source => source.enabled === true)), all: spells.filter(item => !isFixed(item)) };
  const selected = Object.hasOwn(groups, ui.spellFilter) ? ui.spellFilter : 'known';
  const daos = [...new Set(spells.map(item => item.dao).filter(Boolean))];
  const dao = daos.includes(ui.spellDao) ? ui.spellDao : 'all';
  const query = queryOf(ui.spellSearch);
  let filtered = groups[selected].filter(item => (dao === 'all' || item.dao === dao) && matches(query, item.name, item.dao, item.description, item.preview?.text));
  const retained = spells.find(item => item.id === (ui.spellId || (['arts/spell','library/spell','npc-learn'].includes(ui.playerActionResult?.path) ? ui.playerActionResult.itemId : '')));
  if (retained && ui.playerActionResult?.itemId === retained.id && !filtered.some(item => item.id === retained.id)) filtered = [retained, ...filtered];
  const item = filtered.find(item => item.id === (ui.spellId || (['arts/spell','library/spell','npc-learn'].includes(ui.playerActionResult?.path) ? ui.playerActionResult.itemId : ''))) || filtered[0];
  const context = `<p class="player-context">当前携带 <strong>${carried.length} / ${fmt(arts.spellSlots ?? data.spells?.max ?? '—')}</strong> · 每回合灵力 ${count(arts.manaRegen)}</p>`;
  const controls = `<div class="player-controls">${selectControl('spell-filter-tab', '范围', [['known', `已学会 ${groups.known.length}`], ['carried', `已携带 ${groups.carried.length}`], ['learnable', `现在可学 ${groups.learnable.length}`], ['all', `图鉴 ${groups.all.length}`]], selected)}`
    + selectControl('spell-select', '选择神通', filtered.length ? filtered.map(item => [item.id, `${item.name}${isCarried(item) ? ' · 已携带' : ''}`]) : [['', '暂无神通']], item?.id || '') + '</div>';
  let detail = empty(catalogue.length ? '这个范围内暂无神通，可切换范围或清空筛选。' : data.spells?.error || '正在读取神通…');
  if (item) {
    const equipped = isCarried(item), learned = isKnown(item);
    const eligible = equipped ? item.canUnequip !== false : item.canEquip === true;
    const sources = learned ? [] : sourcesFor(item.id);
    const source = sources.find(source => source.key === ui.spellSource) || sources[0];
    let perform = '', requirements = '', sourceReason = '';
    if (learned) perform = action(equipped ? '卸下此神通' : '携带此神通', 'arts/spell', { id: item.id }, { className: 'primary', disabled: !eligible || blocked(state), title: item.reason || '' });
    else if (source) {
      requirements = `<p class="player-risk">${esc(source.label)}：${source.type === 'library' ? `${count(source.points)} 宗门贡献` : `${count(source.days)} 天 · ${count(source.stoneCost)} 灵石 · ${count(source.creditCost)} 交情 · 好感需 ${count(source.affinityNeed)}`}</p>`;
      sourceReason = source.reason || '';
      perform = action(source.type === 'library' ? '兑换并学会' : '请教此神通', source.type === 'library' ? 'library/spell' : 'npc-learn', source.type === 'library' ? { id: item.id } : { npc: source.npc, kind: 'spell', id: item.id }, { className: 'primary', disabled: source.enabled !== true || blocked(state), title: source.reason || '' });
    } else { requirements = '<p class="player-risk">当前地点没有已知可学来源。</p>'; perform = uiButton('寻找道友或宗门来源', 'navigate', 'social'); }
    detail = `<section class="player-detail" data-selected-spell="${esc(item.id)}"><div class="player-title"><strong>${esc(item.name)}</strong>${tag(equipped ? '已携带' : learned ? '已学会' : '未学会')}</div>`
      + `<p class="player-context">${esc(item.dao)}道 · ${esc(kindLabel(item.kind))} · 灵力 ${count(item.actualCost ?? item.cost)}${item.cooldown ? ` · 冷却 ${count(item.cooldown)}` : ''}</p>`
      + `<p class="player-effect">${esc(item.preview?.text || item.description || '暂无效果说明。')}</p>${requirements || '<p class="player-risk">无需学习费用，可调整携带状态。</p>'}`
      + `<p class="player-condition">${esc(sourceReason || (learned ? item.reason || (equipped ? '已占用一个神通槽。' : '携带后占用一个神通槽。') : '需先获取传承来源。'))}</p>` + `<div class="player-actions">${continuation(state) || perform}</div>` + outcome(ui, item.id, ['arts/spell', 'library/spell', 'npc-learn'])
      + `<details data-disclosure-key="spell-${esc(item.id)}"><summary>完整规则与学习来源</summary>${sources.length > 1 ? selectControl('spell-source', '学习来源', sources.map(source => [source.key, source.label]), source.key) : ''}${note(item.stageName ? `所需境界：${item.stageName}` : '')}${note(item.description)}${numericPreview(item.preview)}</details></section>`;
  }
  return card('', controls + context + detail
    + `<details class="player-filter" data-disclosure-key="spell-search"><summary>搜索 / 当前完整牌组</summary>${searchField('spell-search', ui.spellSearch, '查找神通')}${selectControl('spell-dao', '按大道', [['all', '全部'], ...daos.map(dao => [dao, dao + '道'])], dao)}`
    + note('已携带：' + list(arts.spellList).map(item => item.name).join('、'))
    + note('固定招式：' + spells.filter(isFixed).map(item => item.name).join('、')) + '</details>', 'player-workspace');
}

function daoView(state, ui, data) {
  const arts = state.arts || {};
  const nodes = list(data.nodes?.nodes || data.nodes);
  const daos = [...new Set([...nodes.map(item => item.dao), ...Object.keys(arts.exp || {}), arts.focus].filter(Boolean))];
  const dao = daos.includes(ui.artsDao) ? ui.artsDao : daos.includes(arts.focus) ? arts.focus : daos[0];
  const owned = list(arts.nodes).map(refId);
  const options = nodes.filter(item => item.dao === dao).sort((a,b) => Number(a.level) - Number(b.level));
  const item = options.find(item => item.id === (ui.nodeId || (ui.playerActionResult?.path === 'arts/node' ? ui.playerActionResult.itemId : ''))) || options[0];
  const controls = `<div class="player-controls">${selectControl('arts-dao', '大道', daos.map(dao => [dao, dao + '道']), dao)}${selectControl('node-select', '领悟节点', options.length ? options.map(item => [item.id, `${item.name}${owned.includes(item.id) ? ' · 已领悟' : ''}`]) : [['', '读取节点中']], item?.id || '')}</div>`;
  const context = `<p class="player-context">${esc(dao || '')}道经验 ${count(arts.exp?.[dao])} · 持有 ${count(arts.points)} 悟道点</p>`;
  if (!item) return card('', controls + context + empty(data.nodes?.error || '正在读取大道节点…'), 'player-workspace');
  const unlocked = owned.includes(item.id);
  const enough = Number(arts.points || 0) >= Number(item.cost || 0);
  return card('', controls + context + `<section class="player-detail" data-selected-node="${esc(item.id)}"><div class="player-title"><strong>${esc(item.name)}</strong>${tag(unlocked ? '已领悟' : '待领悟')}</div>`
    + `<p class="player-effect">${esc(item.desc)}</p><p class="player-risk">需本大道第 ${count(item.level)} 境 · 消耗 ${count(item.cost)} 悟道点</p>`
    + `<p class="player-condition">${esc(unlocked ? '已经领悟此节点。' : item.reason || (!enough ? '悟道点不足。' : item.canLearn == null ? '当前道境条件由服务端校验。' : '当前可以领悟。'))}</p>`
    + `<div class="player-actions">${continuation(state) || action(unlocked ? '已领悟' : '领悟此节点', 'arts/node', { node: item.id }, { className: 'primary', disabled: unlocked || !enough || item.canLearn === false || blocked(state) })}</div>`
    + outcome(ui, item.id, ['arts/node', 'arts/meditate', 'arts/focus'])
    + `<details><summary>参悟与修行方向</summary><p class="meta">当前方向：${esc(arts.focus || '未设置')}道</p><div class="player-actions">${action('参悟 30 天', 'arts/meditate', { dao }, { disabled: blocked(state) })}${action(arts.focus === dao ? '当前方向' : '设为方向', 'arts/focus', { dao }, { disabled: arts.focus === dao || blocked(state) })}</div></details></section>`, 'player-workspace');
}

function caveView(state, ui) {
  const player = state.player || {}, cave = state.cave || {}, supply = player.supply || {}, cultivation = player.cultivation || {};
  const stoneRows = Array.from({ length: Math.max(0, Math.min(20, Number(supply.maxLevel) || 0)) + 1 }, (_, level) => ({ kind: 'stone', level, name: `灵石 ${level} 档` }));
  const qiRows = list(cave.supplies).map(item => ({ ...item, kind: 'qi' }));
  const options = [...stoneRows, ...qiRows];
  const selected = options.find(item => `${item.kind}:${item.level}` === ui.caveSupply) || stoneRows.find(item => item.level === Number(supply.stoneLevel || 0)) || options[0];
  const kind = selected?.kind || 'stone', currentLevel = Number(supply[kind + 'Level'] || 0), current = Number(selected?.level) === currentLevel;
  // Candidate monthly costs are absent in older contracts. Do not reproduce supply formulas.
  const monthlyCost = current ? supply[kind + 'Cost'] : selected?.costStones ?? selected?.monthlyCost ?? selected?.cost;
  const desc = current ? supply[kind + 'Desc'] : selected?.desc;
  const reason = current ? '当前正在使用此档位。' : selected?.reason || (monthlyCost == null ? '候选月费待查，请用经典界面查看完整配给。' : '调整后按此档位结算月费。');
  const theoryRows = [['功法基础 / 年', 'manual', ''], ['资质乘数', 'apt', '×'], ['悟性乘数', 'insight', '×'], ['境界系数', 'realm', '×'], ['传承加成', 'legacy', '×'], ['天赋加成', 'talentMul', '×'], ['灵石配给', 'stone', '×'], ['灵气配给', 'qi', '×'], ['洞府加成', 'cave', '×']];
  const upgrade = cave.upgrade;
  return card('', selectControl('cave-supply', '选择配给', options.map(item => [`${item.kind}:${item.level}`, item.kind === 'qi' ? `灵气 · ${item.name}` : item.name]), `${kind}:${selected?.level ?? 0}`)
    + `<p class="player-context">${esc(player.caveName || '尚未安府')} · 现有 ${count(player.stones)} 灵石</p><section class="player-detail">`
    + `<div class="player-title"><strong>${esc(selected?.name || '配给')}</strong>${tag(current ? '当前档位' : '候选档位')}</div>`
    + `<p class="player-effect">当前${kind === 'qi' ? '灵气' : '灵石'}：${esc(supply[kind + 'Name'] || '未设置')} · 每月 ${count(supply[kind + 'Cost'])} 灵石<br>${esc(desc || '候选效果说明待服务端提供。')}</p>`
    + `<p class="player-risk">候选月费 ${monthlyCost == null ? '待查' : `${count(monthlyCost)} 灵石`} · 理论合计 ${cultivation.total == null ? '待查' : `${count(cultivation.total)} / 年`}</p>`
    + `<p class="player-condition">${esc(reason)}</p><div class="player-actions">${continuation(state) || action(current ? '当前配给' : '设为此配给', 'supply', { kind, level: Number(selected?.level || 0) }, { className: 'primary', disabled: current || monthlyCost == null || selected?.enabled === false || selected?.locked || blocked(state), title: reason })}${legacyLink('完整配给')}</div>`
    + outcome(ui, null, ['supply','cave'])
    + `<details><summary>理论修炼速度拆解</summary><div class="list">${theoryRows.filter(([, key]) => cultivation[key] != null).map(([label,key,prefix]) => `<div class="row"><span>${label}</span><strong>${prefix}${count(cultivation[key])}</strong></div>`).join('')}</div>${note('理论速度不是保证收益，实际推进受资源与事件影响。')}${note(list(state.plans).find(item => item.id === 'cultivate')?.preview)}</details></section>`
    + `<details><summary>洞府安置与升级</summary>${note(cultivation.note)}${!player.atCave ? `<p class="meta">安府 / 迁府 ${count(supply.moveCost)} 灵石</p>${action(player.cave ? '迁府至此' : '在此安府', 'cave/move', {}, { disabled: blocked(state) || supply.moveCost == null })}` : note('你正在洞府所在地点。')}${upgrade ? `<p class="meta">${esc(upgrade.name)}：${count(upgrade.cost)} 灵石 · 声望 ${count(upgrade.minRenown)}</p>${note(upgrade.reason)}${action('升级洞府', 'cave/upgrade', {}, { disabled: upgrade.enabled !== true || blocked(state) })}` : ''}</details>`, 'player-workspace');
}

function buildsView(state, data) {
  const builds = list(state.artBuilds);
  const arts = state.arts || {};
  const nodes = list(data.nodes?.nodes || data.nodes);
  const spells = list(data.spells?.spells || data.spells);
  const resolve = (kind, ref) => {
    const id = refId(ref);
    const named = typeof ref === 'object' ? ref?.name : '';
    if (kind === 'manuals') {
      const item = list(arts.manualList).find(item => item.id === id);
      const source = list(state.manualSources).find(item => item.id === id);
      return { name: named || item?.name || source?.name || list(state.manualCatalog).find(item => item.id === id)?.name || id, status: item?.isMain || item?.isAux ? '已生效' : item ? '已修习 · 待设置' : source?.owned || list(arts.bookList).includes(id) ? '已得抄本 · 待修习' : '未获取', ready: Boolean(item?.isMain || item?.isAux) };
    }
    if (kind === 'spells') {
      const item = spells.find(item => item.id === id) || list(arts.spellList).find(item => item.id === id);
      const ready = list(arts.spells).map(refId).includes(id) || list(arts.mandatorySpells).map(refId).includes(id);
      return { name: named || item?.name || id, status: ready ? '已携带' : list(arts.knownSpells).includes(id) || item?.known ? '已学会 · 待携带' : '未学会', ready };
    }
    const item = nodes.find(item => item.id === id);
    const ready = list(arts.nodes).includes(id);
    return { name: named || item?.name || id, status: ready ? '已领悟' : '待领悟', ready };
  };
  return builds.length ? builds.map(build => {
    const groups = [['manuals', '功法'], ['spells', '神通'], ['nodes', '大道节点']].map(([key, label]) => ({ label, items: list(build[key]).map(ref => resolve(key, ref)) }));
    const items = groups.flatMap(group => group.items);
    return card(build.name, note(build.desc) + tag(`${items.filter(item => item.ready).length} / ${items.length} 已具备`)
      + groups.filter(group => group.items.length).map(group => `<h3 class="section-title">${group.label}</h3><div class="list">${group.items.map(item => `<div class="row"><strong>${esc(item.name)}</strong><span class="meta">${esc(item.status)}</span></div>`).join('')}</div>`).join(''));
  }).join('') : empty('暂无流派组合建议。');
}

export function renderArts(state = {}, ui = {}, data = {}) {
  const requested = ui.artsTab === 'builds' ? 'loadout' : ui.artsTab;
  const active = artsTabs.some(([key]) => key === requested) ? requested : 'loadout';
  const renderers = { loadout: () => loadoutView(state, data, ui.artsTab === 'builds'), manuals: () => manualView(state, ui), spells: () => spellView(state, ui, data), daos: () => daoView(state, ui, data), cave: () => caveView(state, ui) };
  return `<section class="stack player-page" aria-label="道法与洞府"><div class="player-page-heading"><h1 class="section-title">道法</h1>${tag(`${fmt(state.arts?.points || 0)} 悟道点`)}</div>`
    + tabs('arts', artsTabs, active)
    + renderers[active]() + '</section>';
}
