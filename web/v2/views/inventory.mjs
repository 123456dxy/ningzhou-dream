import { renderArtifactIllustration } from '../../artifact-illustrations.mjs';
import { renderPillBatchControls } from '../../pill-batch.mjs';
import { renderAlchemyQuote } from '../../alchemy.mjs';
import { renderTianjiSteward } from '../../tianji-steward.mjs';
import { esc, fmt, action, card, empty, tag, tabs, numericPreview, uiButton, legacyLink } from '../ui.mjs';
import { hasNewForge, renderForgeWorkshop, forgeInscriptionName } from '../../forge.mjs';

// Pure views: all quantities and eligibility come from the latest server state.
// data: economy (/eco-options), forge (/forge-options), forgePreview (/forge-preview).
const list = value => Array.isArray(value) ? value : [];
const count = value => esc(fmt(Number(value) || 0));
const percent = value => `${count(Number(value) * 100)}%`;
const note = text => text ? `<p class="meta">${esc(text)}</p>` : '';
const unavailable = state => Boolean(state.ended || state.pending || state.battle || state.job || state.dungeons?.run);
const slotName = slot => ({ weapon: '武器', armor: '防具', accessory: '佩饰' }[slot] || slot || '法器');
const inventoryTabs = [['bag', '我的物品'], ['gear', '装备'], ['market', '坊市'], ['workshop', '工坊'], ['jobs', '差事']];
const confirmGear = (label, kind, id, disabled) => `<button type="button" class="btn" data-ui="gear-confirm-${esc(kind)}" data-value="${esc(id)}"${disabled ? ' disabled' : ''}>${esc(label)}</button>`;
const owned = collection => Object.entries(collection || {}).filter(([, amount]) => Number(amount) > 0);

function abilityView(ability) {
  if (!ability) return '';
  return `<div class="stack"><strong>${esc(ability.name || '法宝神通')}</strong>`
    + note(ability.description) + numericPreview(ability.preview)
    + `<p class="meta">${esc(ability.dao || '')}道 · 灵力 ${count(ability.cost)} · 冷却 ${count(ability.cooldown)} 回合</p></div>`;
}

const matchText = (query, ...values) => !query || values.filter(Boolean).join(' ').toLocaleLowerCase().includes(query);
const searchField = (key, value, label) => `<label class="search-label">${esc(label)}<input type="search" data-input="${esc(key)}" value="${esc(value || '')}" placeholder="名称或已有说明" autocomplete="off"></label>`;
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

function bagView(state, ui, data) {
  const arts = state.arts || {};
  const market = list(state.market);
  const recipes = list(state.brewOptions);
  const pillCatalog = list(state.pillCatalog);
  const materials = list(data.forge?.materials);
  const query = String(ui.inventorySearch || '').trim().toLocaleLowerCase();
  const groups = [
    { id: 'potions', name: '丹药', items: owned(arts.potions).map(([id, amount]) => {
      const marketItem = market.find(item => item.kind === 'potion' && item.id === id) || {};
      const recipe = recipes.find(item => item.id === id) || {};
      const pill = pillCatalog.find(item => item.id === id);
      return { ...marketItem, ...recipe, ...(pill || {}), id, amount: pill?.owned ?? amount, name: pill?.name || recipe.name || marketItem.name || id, description: pill?.effect || recipe.desc || marketItem.desc || marketItem.description || '', preview: pill?.preview || marketItem.preview || recipe.preview, useEnabled: pill?.enabled, useReason: pill?.reason, hasUseInfo: Boolean(pill) };
    }) },
    { id: 'herbs', name: '药材', items: owned(arts.herbs).map(([id, amount]) => {
      const item = market.find(item => item.kind === 'herb' && (item.id === id || item.name === id)) || {};
      return { ...item, id, amount, name: item.name || id, description: item.desc || '炼丹药材；可在工坊查看丹方需求。' };
    }) },
    { id: 'materials', name: '炼器材料', items: owned(arts.materials).map(([id, amount]) => {
      const item = materials.find(item => item.id === id) || market.find(item => item.kind === 'material' && item.id === id) || {};
      return { ...item, id, amount, name: item.name || id, description: item.desc || [item.grade == null ? '' : `${item.grade} 阶`, item.element, item.polarity].filter(Boolean).join(' · ') };
    }) },
    { id: 'breakthrough', name: '突破用品', items: [['cores', '灵核'], ['pills', '凝元丹']].filter(([id]) => Number(state.player?.[id]) > 0).map(([id, name]) => ({ id, name, amount: state.player[id], description: '在修行页的突破安排中使用。' })) },
  ];
  const legacyKind = { potions: 'potions', herbs: 'herbs', items: 'materials' }[ui.inventoryTab];
  const requested = ui.inventoryKind || legacyKind || 'all';
  const category = groups.some(group => group.id === requested) ? requested : 'all';
  const all = groups.flatMap(group => group.items.map(item => ({ ...item, category: group.id, key: `${group.id}:${item.id}` })));
  let options = all.filter(item => (category === 'all' || item.category === category) && matchText(query, item.name, item.description, item.id));
  const selectedKey = ui.inventoryItem || (['use-pill','pills/use','sell'].includes(ui.playerActionResult?.path) && ui.playerActionResult?.itemId ? `potions:${ui.playerActionResult.itemId}` : '');
  let selected = options.find(item => item.key === selectedKey);
  if (!selected && selectedKey.startsWith('potions:')) {
    const id = selectedKey.slice('potions:'.length);
    const pill = pillCatalog.find(item => item.id === id);
    if (pill && Number(pill.owned) === 0 && ui.playerActionResult?.itemId === id && ['use-pill', 'pills/use', 'sell'].includes(ui.playerActionResult?.path)) {
      selected = { ...pill, id, key: selectedKey, category: 'potions', amount: 0, description: pill.effect, useEnabled: false, useReason: pill.reason, hasUseInfo: true };
      options = [selected, ...options];
    }
  }
  selected ||= options[0];
  const controls = `<div class="player-controls">${selectControl('inventory-kind-tab', '类别', [['all', `全部 ${all.length}`], ...groups.map(group => [group.id, `${group.name} ${group.items.length}`])], category)}`
    + selectControl('inventory-item', '物品', options.length ? options.map(item => [item.key, `${item.name} ×${item.amount}`]) : [['', '暂无物品']], selected?.key || '') + '</div>';
  if (!selected) return card('', controls + empty(query ? '没有符合搜索的已持有物品。' : '这一类暂无物品。')
    + `<div class="player-actions">${uiButton('去坊市', 'inventory-tab', 'market')}${uiButton('去工坊', 'inventory-tab', 'workshop')}</div>`
    + `<details><summary>搜索与筛选</summary>${searchField('inventory-search', ui.inventorySearch, '查找我的物品')}</details>`, 'player-workspace');
  const item = selected;
  const isPill = item.category === 'potions';
  const effect = item.preview?.text || item.description || '效果说明尚未提供。';
  const risk = isPill ? `<p class="player-risk">${item.poisonAfter == null ? '服后丹毒未提供' : `服后丹毒 ${count(item.poisonAfter)}`} · ${item.toleranceUnlimited ? '不限耐药次数' : item.toleranceRemaining == null ? '耐药次数未提供' : `耐药剩余 ${count(item.toleranceRemaining)} 次`}</p>` : '<p class="player-risk">用途：按当前持有数量使用。</p>';
  let primary = '', secondary = '';
  if (isPill) {
    primary = renderPillBatchControls(item.id);
    secondary = `<details data-disclosure-key="pill-${esc(item.id)}"><summary>服用记录与出售</summary>${note(item.usage)}<p class="meta">已服 ${count(item.used ?? arts.used?.[item.id])} 次${item.sellPrice == null ? '' : ` · 出售 ${count(item.sellPrice)} 灵石 / 枚`}</p>`
      + action(state.inCity ? '出售一枚' : '城中可售', 'sell', { kind: 'potion', id: item.id }, { disabled: !state.inCity || unavailable(state) || Number(item.amount) <= 0 }) + '</details>';
  } else if (item.category === 'herbs') primary = uiButton('查看炼丹需求', 'inventory-tab', 'brew', 'primary');
  else if (item.category === 'materials') primary = uiButton('用来炼器', 'inventory-tab', 'forge', 'primary');
  else primary = uiButton('前往突破', 'navigate', 'home', 'primary');
  const detail = `<section class="player-detail" data-selected-item="${esc(item.key)}"><div class="player-title"><strong>${esc(item.name)}</strong>${tag(`持有 ${fmt(item.amount)}`)}</div>`
    + `<p class="player-effect">${esc(effect)}</p>${risk}`
    + `<p class="player-condition${isPill ? ' player-caution' : ''}">${esc(item.useReason || (isPill ? '即使实际增益为 0，服用也消耗 1 枚并结算丹毒。' : '可查看用途与所需条件。'))}</p><div class="player-actions">${continuation(state) || primary}</div>`
    + outcome(ui, item.id, ['use-pill', 'pills/use', 'sell']) + `<details><summary>完整效果与说明</summary>${note(effect)}${isPill ? note('即使实际增益为 0，服用仍消耗一枚丹药，并按服后丹毒结算。') : ''}</details>` + secondary
    + (item.source ? `<details><summary>获取来源</summary>${note(item.source)}</details>` : '') + '</section>';
  return card('', controls + detail + `<details class="player-filter" data-disclosure-key="bag-search"><summary>查找其他物品 · ${options.length} 项</summary>${searchField('inventory-search', ui.inventorySearch, '搜索')}`
    + `<div class="player-picker">${options.map(candidate => uiButton(`${candidate.name} ×${candidate.amount}`, 'inventory-item', candidate.key, candidate.key === item.key ? 'selected' : '')).join('')}</div></details>`, 'player-workspace');
}

function gearView(state, ui) {
  const gear = list(state.gears);
  const slots = [['weapon', '武器'], ['armor', '防具'], ['accessory', '佩饰']];
  const requested = ui.gearSlot || gear.find(item => item.id === ui.selectedGear)?.slot || gear[0]?.slot || 'weapon';
  const slot = slots.some(([id]) => id === requested) ? requested : 'weapon';
  const candidates = gear.filter(item => item.slot === slot);
  const equipped = candidates.find(item => item.equipped);
  let item = candidates.find(item => item.id === ui.selectedGear) || candidates[0];
  const receipt = ui.playerActionResult;
  if (ui.selectedGear && !gear.some(item => item.id === ui.selectedGear) && receipt?.itemId === ui.selectedGear && ['sell', 'gear/salvage'].includes(receipt.path)) item = { id: ui.selectedGear, name: '已处置法器', removed: true };
  const choices = item?.removed ? [item, ...candidates] : candidates;
  const controls = `<div class="player-controls">${selectControl('gear-slot-tab', '装备位', slots, slot)}${selectControl('gear-select', '选择法器', choices.length ? choices.map(gear => [gear.id, `${gear.name}${gear.equipped ? ' · 已装备' : ''}`]) : [['', '暂无法器']], item?.id || '')}</div>`;
  const current = `<p class="player-context">当前${esc(slotName(slot))}：<strong>${esc(equipped?.name || '未装备')}</strong></p>`;
  if (!item) return card('', controls + current + empty('这个装备位暂无可用法器。') + `<div class="player-actions">${uiButton('去坊市', 'inventory-tab', 'market')}${uiButton('去炼器', 'inventory-tab', 'forge')}</div>`, 'player-workspace');
  if (item.removed) return card('', controls + current + outcome(ui, item.id, ['sell', 'gear/salvage']) + note('该法器已不在行囊，选择其他法器即可继续。'), 'player-workspace');
  const quotes = item.actions || {};
  const equipQuote = quotes[item.equipped ? 'unequip' : 'equip'];
  const inscription = quotes.inscribe || item.inscribe;
  const cost = inscription?.costStones ?? inscription?.cost;
  const sale = quotes.sell, salvage = quotes.salvage;
  const delta = item.passive?.delta;
  const isNewForge = Number(item.forgeVersion || 0) >= 2;
  const signed = value => `${Number(value) > 0 ? '+' : ''}${fmt(Number(value) || 0)}`;
  const passive = item.passive;
  const primary = passive?.primaryValue == null ? '核心数值待查' : `${passive.primaryStat} +${fmt(passive.primaryValue)}${slot === 'accessory' ? '点' : '个百分点'}`;
  const currentPrimary = equipped?.passive?.primaryValue == null ? equipped ? '核心数值待查' : '未装备' : `+${fmt(equipped.passive.primaryValue)}`;
  const comparison = isNewForge ? `<p class="player-effect player-gear-effect">${esc(item.effectText || item.ability?.description || item.passive?.text || '成器效果待读取。')}</p>`
    : delta ? `<p class="player-effect player-gear-effect">${esc(primary)}（当前 ${esc(currentPrimary)}）<br>换装：气血 ${esc(signed(delta.hpMax))} · 每回合盾 ${esc(signed(delta.roundShieldPct))} 个百分点 · 回灵 ${esc(signed(delta.manaRegen))} · 攻击 ${esc(signed(delta.attackPct))} 个百分点</p>` : `<p class="player-effect player-gear-effect">${esc(item.passive?.text || '当前服务未提供替换对比。')}</p>`;
  const controlsHTML = `<div class="player-actions">${continuation(state) || action(item.equipped ? '卸下' : '装备此法器', item.equipped ? 'gear/unequip' : 'equip', { id: item.id }, { className: 'primary', disabled: unavailable(state) || equipQuote?.enabled !== true, title: equipQuote?.reason || '' })}`
    + (isNewForge ? '' : !item.inscribed ? action(cost == null ? '铭刻 · 待报价' : `铭刻 ${fmt(cost)} 灵石`, 'gear/inscribe', { id: item.id }, { disabled: unavailable(state) || cost == null || inscription?.enabled !== true, title: inscription?.reason || '' }) : action('已铭刻', 'gear/inscribe', { id: item.id }, { disabled: true })) + '</div>';
  return card('', controls + current + `<section class="player-detail" data-selected-gear="${esc(item.id)}">${renderArtifactIllustration(item,{compact:true})}<div class="player-title"><strong>${esc(item.name)}</strong>${tag(item.equipped ? '已装备' : '待装备')}</div>`
    + `<p class="player-context">${count(item.grade)} 阶${item.inscribed ? ' · 已铭刻' : ''} · 持有 ${count(state.player?.stones)} 灵石</p>` + comparison
    + `<p class="player-condition">${esc(equipQuote?.reason || (isNewForge ? `${forgeInscriptionName(item.inscription)} · 主效果与灵纹已在成器时确定。` : !item.inscribed ? inscription?.reason || (cost == null ? '铭刻费用待服务端报价。' : `铭刻需 ${fmt(cost)} 灵石。`) : '当前法器已铭刻。'))}</p>` + (!isNewForge && cost == null ? legacyLink('在经典界面查看铭刻') : '') + controlsHTML
    + outcome(ui, item.id, ['equip', 'gear', 'sell'])
    + `<details data-disclosure-key="gear-${esc(item.id)}"><summary>完整属性与法宝神通</summary>${note(item.passive?.text)}${abilityView(item.ability)}</details>`
    + `<details data-disclosure-key="gear-dispose-${esc(item.id)}"><summary>出售 / 分解</summary>${note('会永久移除此法器，提交前再次确认。')}<div class="player-actions">`
    + confirmGear(salvage?.returnStones == null ? '分解 · 待报价' : `分解 ${fmt(salvage.returnStones)} 灵石`, 'salvage', item.id, unavailable(state) || salvage?.returnStones == null || salvage?.enabled !== true)
    + confirmGear(sale?.returnStones == null ? '出售 · 待报价' : `出售 ${fmt(sale.returnStones)} 灵石`, 'sell', item.id, unavailable(state) || sale?.returnStones == null || sale?.enabled !== true)
    + `</div>${note(salvage?.reason)}${note(sale?.reason)}</details></section>`, 'player-workspace');
}

function furnaceView(state, ui) {
  const furnaces = list(state.furnaces);
  const current = furnaces.find(item => item.current);
  const repair = state.furnaceRepair || state.furnace?.repair;
  const repairCost = repair?.costStones ?? repair?.cost;
  return card('丹炉', `<div class="row"><strong>${esc(current?.name || state.arts?.furnace || '当前丹炉')}</strong>${tag(`耐久 ${fmt(state.arts?.durability || 0)}`)}</div>`
    + `<div class="row">${continuation(state) || action(repairCost == null ? '修缮 · 待报价' : `修缮 · ${fmt(repairCost)} 灵石`, 'furnace/repair', {}, { disabled: unavailable(state) || repairCost == null || repair?.enabled !== true })}</div>${note(repair?.reason || (repairCost == null ? '修缮费用与可用条件待服务端报价。' : ''))}`
    + outcome(ui, null, ['furnace']) + (repairCost == null ? legacyLink('在经典界面查看丹炉维护') : '')
    + (furnaces.length ? `<details><summary>查看可更换丹炉</summary><div class="list">${furnaces.map(item => `<div class="stack"><div class="row"><strong>${esc(item.name)}</strong>${item.current ? tag('使用中') : ''}</div>`
      + `<p class="meta">${count(item.grade)} 品 · 容量 ${count(item.capacity)} · ${count(item.price)} 灵石</p>`
      + action(item.current ? '当前丹炉' : '购入丹炉', 'furnace/buy', { id: item.id }, { disabled: Boolean(item.current) || unavailable(state) || Number(state.player?.stones || 0) < Number(item.price || 0) }) + '</div>').join('')}</div></details>` : ''));
}

function marketView(state, ui) {
  if (!state.inCity) return empty('此地没有坊市。前往城池后即可买卖。');
  const kinds = [['potion', '丹药'], ['herb', '药材'], ['material', '材料'], ['steward', '天机阁执事']];
  const kind = kinds.some(([key]) => key === ui.marketKind) ? ui.marketKind : 'potion';
  if (kind === 'steward') return `<div class="player-controls">${selectControl('market-kind-tab', '类别', kinds, kind)}</div>`
    + renderTianjiSteward(state.tianjiSteward, { kind: ui.stewardKind || 'potion' });
  const availability = ui.marketAvailability === 'all' ? 'all' : 'affordable';
  const query = String(ui.marketSearch || '').trim().toLocaleLowerCase();
  const stones = Number(state.player?.stones) || 0;
  const describe = item => (item.kind === 'potion' ? list(state.pillCatalog).find(pill => pill.id === item.id)?.effect : '') || item.desc || item.description || item.effect || '';
  const source = list(state.market).filter(item => item.kind === kind);
  let goods = source.filter(item => (availability === 'all' || (item.price != null && Number.isFinite(Number(item.price)) && Number(item.price) <= stones)) && matchText(query, item.name, describe(item), item.id));
  const selectedKey = ui.marketItem || (ui.playerActionResult?.path === 'buy' ? `${kind}:${ui.playerActionResult.itemId}` : '');
  const retained = source.find(item => `${item.kind}:${item.id}` === selectedKey);
  // An affordable filter must never replace the selected purchase after spending stones.
  if (retained && !goods.some(item => item.id === retained.id)) goods = [retained, ...goods];
  const item = goods.find(item => `${item.kind}:${item.id}` === selectedKey) || goods[0];
  const controls = `<div class="player-controls">${selectControl('market-kind-tab', '类别', kinds, kind)}${selectControl('market-item', '商品', goods.length ? goods.map(item => [`${item.kind}:${item.id}`, `${item.name} · ${fmt(item.price)} 灵石`]) : [['', '暂无符合条件的商品']], item ? `${item.kind}:${item.id}` : '')}</div>`;
  let detail = empty('暂无符合条件的商品。可切换下方筛选为“全部”。');
  if (item) {
    const price = Number(item.price), hasPrice = item.price != null && Number.isFinite(price);
    const shortfall = hasPrice ? Math.max(0, price - stones) : 0;
    const reason = item.reason || (!hasPrice ? '价格待查，暂不可购买。' : shortfall > 0 ? `灵石不足，还差 ${fmt(shortfall)} 枚。` : item.locked === true ? '尚未开放。' : item.enabled === false ? '当前不可购买。' : unavailable(state) ? '请先完成当前安排或事件。' : '每次购买 1 件。');
    const disabled = !hasPrice || shortfall > 0 || item.enabled === false || item.locked === true || unavailable(state);
    detail = `<section class="player-detail" data-selected-market="${esc(item.kind + ':' + item.id)}"><div class="player-title"><strong>${esc(item.name)}</strong>${tag(`持有 ${fmt(item.owned || 0)}`)}</div>`
      + `<p class="player-effect">${esc(describe(item) || item.preview?.text || '效果说明尚未提供。')}</p>`
      + `<p class="player-risk">单价 ${hasPrice ? count(price) : '待查'} 灵石 · 现有 ${count(stones)} 灵石</p><p class="player-condition">${esc(reason)}</p>`
      + `<div class="player-actions">${continuation(state) || action('买入一件', 'buy', { kind: item.kind, id: item.id, count: 1 }, { className: 'primary', disabled, title: reason })}</div>`
      + outcome(ui, item.id, ['buy']) + `<details><summary>完整商品说明</summary>${note(describe(item))}${numericPreview(item.preview)}</details></section>`;
  }
  const stocks = [['cores', '灵核', state.coreBuyPrice, state.world?.coreStock], ['pills', '凝元丹', state.pillBuyPrice, state.world?.pillStock]].map(([id, label, price, stock]) => card(label,
    `<div class="row"><span>持有 ${count(state.player?.[id])}</span><span class="meta">坊市库存 ${stock == null ? '待查' : count(stock)}</span></div><p>${count(price)} 灵石 / 枚</p>`
    + `<div class="row">${action('买入一枚', 'trade-stock', { kind: id, buy: true, count: 1 }, { disabled: unavailable(state) || stock === 0 || price == null || Number(state.player?.stones || 0) < Number(price) })}`
    + action('卖出一枚', 'trade-stock', { kind: id, buy: false, count: 1 }, { disabled: unavailable(state) || !(Number(state.player?.[id]) > 0) }) + '</div>')).join('');
  const books = list(state.manualSources).filter(item => item.city);
  const bookshop = books.length ? card('功法书坊', `<div class="list">${books.map(item => `<div class="stack"><div class="row"><strong>${esc(item.name)}</strong>${item.owned ? tag('已得抄本') : tag(`${fmt(item.price)} 灵石`)}</div>${note(item.how)}`
    + action(item.owned ? '已得抄本' : '购入抄本', 'book-buy', { id: item.id }, { disabled: Boolean(item.owned) || unavailable(state) || item.city !== state.player?.location || Number(state.player?.stones || 0) < Number(item.price || 0) }) + '</div>').join('')}</div>`) : '';
  return card('', controls + detail + `<details class="player-filter" data-disclosure-key="market-search"><summary>查找 / 筛选 · ${goods.length} 件</summary>${searchField('market-search', ui.marketSearch, '搜索商品')}${selectControl('market-availability-tab', '显示范围', [['affordable', '买得起'], ['all', '全部']], availability)}</details>`, 'player-workspace')
    + `<details class="card"><summary>突破物品与功法书坊</summary>${stocks}${bookshop}</details>`;
}

function brewView(state, ui, data) {
  const batch = [1, 3, 5, 10].includes(Number(ui.brewBatch)) ? Number(ui.brewBatch) : 1;
  const query = String(ui.brewSearch || '').trim().toLocaleLowerCase();
  const recipes = list(state.brewOptions).filter(item => matchText(query, item.name, item.desc, item.reason));
  const item = recipes.find(item => item.id === (ui.recipeId || (ui.playerActionResult?.path === 'craft' ? ui.playerActionResult.itemId : ''))) || recipes[0];
  const controls = `<div class="player-controls">${selectControl('brew-batch', '炉数', [1,3,5,10].map(value => [value, value + ' 炉']), batch)}${selectControl('brew-select', '丹方', recipes.length ? recipes.map(item => [item.id, `${item.name}${item.enabled ? ' · 可炼' : ''}`]) : [['', '暂无丹方']], item?.id || '')}</div>`;
  if (!item) return card('', controls + empty('没有符合条件的丹方。') + searchField('brew-search', ui.brewSearch, '搜索丹方'), 'player-workspace');
  const quote = item.batchQuotes?.[batch];
  const supplyKey = JSON.stringify({kind:'brew',id:item.id,batch});
  const supply = data.alchemySupply?.key === supplyKey ? data.alchemySupply.quote : null;
  const supplyCost = supply?.totalCost ?? supply?.cost;
  const supplyHTML = `<details data-disclosure-key="alchemy-supply"><summary>一键补料 · 与开炉费分列</summary>`
    + (supply ? (supply.rows || supply.materials || []).map(row => note(`${row.name}：需 ${row.required} / 有 ${row.owned} / 缺 ${row.missing ?? row.buyCount ?? 0}`)).join('') + note(supply.reason || supply.error) + note(Number.isFinite(supplyCost) ? `补料费用 ${supplyCost} 灵石` : '') : note('先核对当前丹方、本批炉数与当地货源。'))
    + uiButton(supply ? '刷新补料报价' : '查看补料报价','alchemy-supply-quote',supplyKey)
    + (supply?.enabled && Number.isFinite(supplyCost) ? action('确认购买缺料','craft/materials/buy',{kind:'brew',id:item.id,batch,expectedCost:supplyCost},{disabled:unavailable(state)}) : '') + '</details>';
  return card('', controls + `<section class="player-detail" data-selected-recipe="${esc(item.id)}"><div class="player-title"><strong>${esc(item.name)}</strong>${tag(`${item.grade} 阶`)}</div>`
    + `<p class="player-effect">${esc(item.catalogEffect || item.baseEffect || item.desc || '药效说明尚未提供。')}</p>`
    + note(item.usage) + `<p class="player-risk">服用丹毒 ${count(item.poison)} 点；按逐枚耐药规则服用。</p>`
    + renderAlchemyQuote(quote)
    + `<p class="player-context">开炉时一次投入整批材料与费用。完成时逐炉判定实际成丹；期望产量不保证成丹或出售收入，失败炉次投入不返还。</p>`
    + `<div class="player-actions">${continuation(state) || action(`开炉 ${batch} 炉`, 'craft', { kind: 'brew', id: item.id, batch }, { className: 'primary', disabled: quote?.enabled !== true || unavailable(state), title: quote?.reason || '' })}</div>`
    + outcome(ui, item.id, ['craft','craft/materials/buy']) + supplyHTML + '</section>'
    + `<details data-disclosure-key="brew-search"><summary>查找其他丹方 · ${recipes.length} 张</summary>${searchField('brew-search', ui.brewSearch, '名称或缺少的材料')}</details>`, 'player-workspace');
}

function forgeView(state, ui, data) {
  const materials = list(data.forge?.materials), shapes = list(data.forge?.shapes);
  if (!materials.length || !shapes.length) return empty(data.forge?.error || '正在读取炼器材料与器型…');
  if (hasNewForge(data.forge)) return renderForgeWorkshop(state, data.forge, ui.forge, data.forgePreview,
    { mode: 'mobile', view: ui.forgeView || { filters: { owned: true } }, continuation: continuation(state) }) + outcome(ui, null, ['forge']);
  const selected = ui.forge || {};
  const inputs = [['main', '主料'], ['aux', '辅料'], ['core', '灵核'], ['shape', '器型']].map(([key, label]) => {
    const options = key === 'shape' ? shapes : materials;
    return `<label class="player-control"><span>${label}</span><select data-ui="forge-select" data-field="${key}">${options.map(item => `<option value="${esc(item.id)}"${item.id === selected[key] ? ' selected' : ''}>${esc(item.name)}${key === 'shape' ? '' : ` · 有${fmt(item.owned || 0)}`}</option>`).join('')}</select></label>`;
  }).join('');
  const info = data.forgePreview?.info;
  const ready = info?.valid === true && info.enabled === true && !unavailable(state) && ['main','aux','core','shape'].every(key => selected[key]);
  const requirements = list(info?.materials).map(item => `${item.name} ${fmt(item.owned)} / ${fmt(item.required)}${Number(item.missing) > 0 ? `（缺${fmt(item.missing)}）` : ''}`).join(' · ');
  return card('', `<div class="player-controls player-forge-controls">${inputs}</div><section class="player-detail player-forge-detail">`
    + `<div class="player-title"><strong>${esc(info?.valid ? `${info.mainName}${info.shapeName}` : '成器推演')}</strong>${info?.valid ? tag(`${fmt(info.grade)} 阶`) : ''}</div>`
    + `<p class="player-risk">${info?.valid ? `成功 ${percent(info.chance)} · ${count(info.days)} 天 · 需 ${count(info.cost)} / 有 ${count(state.player?.stones)} 灵石` : '费用与成功率待服务端报价。'}</p>`
    + `<p class="player-materials">${esc(requirements || '材料所需 / 持有数量待服务端报价。')}</p>`
    + `<p class="player-condition">${esc(list(info?.blockers).join('；') || info?.reason || data.forgePreview?.error || (ready ? '材料与条件齐备，开炉后投入。' : '选择配方后读取服务端推演。'))}</p>`
    + `<div class="player-actions">${continuation(state) || action('开炉铸炼', 'forge', { main: selected.main || '', aux: selected.aux || '', core: selected.core || '', shape: selected.shape || '' }, { className: 'primary', disabled: !ready, title: info?.reason || '' })}</div>`
    + outcome(ui, null, ['forge'])
    + `<details><summary>成器属性与法宝神通</summary>${note(info?.passive?.text)}${abilityView(info?.ability)}${info?.inscribedAbility ? note(info.inscribeRequirement) + note(info.inscribedPassive?.text) + abilityView(info.inscribedAbility) : ''}</details></section>`, 'player-workspace');
}

function jobsView(state, data) {
  const options = list(data.economy?.options || data.economy);
  const labels = { task: '差事', commission: '商会委托', hunt: '猎妖', mine: '采矿', arena: '演武', quest: '机缘' };
  if (!options.length) return empty(data.economy?.error || '正在读取可用差事…');
  const row = item => `<article class="list-row"><div class="row"><div><strong>${esc(item.name || labels[item.type] || '行动')}</strong> ${tag(item.done ? '已完成' : labels[item.type] || item.type)}</div>`
    + action(item.done ? '已完成' : item.type === 'quest' ? '探寻' : '开始', item.type === 'quest' ? 'quest' : 'eco', item.type === 'quest' ? { id: item.id } : { type: item.type, id: item.id }, { className: item.enabled ? 'primary' : '', disabled: item.enabled !== true || unavailable(state), title: item.reason || '' }) + '</div>'
    + note([item.days ? `${item.days} 天` : '', item.cost ? `投入 ${item.cost} 灵石` : '', item.pay ? `报酬 ${item.pay} 灵石` : '', item.chance == null ? '' : `成功率 ${fmt(Number(item.chance) * 100)}%`].filter(Boolean).join(' · '))
    + note(item.desc) + note(item.reason) + note(item.reward) + '</article>';
  const ready = options.filter(item => item.enabled === true);
  const other = options.filter(item => item.enabled !== true);
  return card(`现在可进行 · ${ready.length} 项`, ready.length ? compactList(ready, row) : empty('当前没有可以进行的差事。'))
    + `<details class="card"><summary>其他差事与机缘 · ${other.length} 项</summary>${Object.entries(labels).map(([type, label]) => {
      const items = other.filter(item => item.type === type);
      return items.length ? `<h3 class="section-title">${label}</h3>${compactList(items, row)}` : '';
    }).join('')}</details>`;
}

export function renderInventory(state = {}, ui = {}, data = {}) {
  const aliases = { potions: 'bag', herbs: 'bag', items: 'bag', brew: 'workshop', forge: 'workshop', furnace: 'workshop' };
  const requested = aliases[ui.inventoryTab] || ui.inventoryTab;
  const active = inventoryTabs.some(([key]) => key === requested) ? requested : 'bag';
  const workshop = () => {
    const requestedWorkshop = ['brew', 'forge', 'furnace'].includes(ui.inventoryTab) ? ui.inventoryTab : ui.workshopTab;
    const selected = ['brew', 'forge', 'furnace'].includes(requestedWorkshop) ? requestedWorkshop : 'brew';
    return tabs('workshop', [['brew', '炼丹'], ['forge', '炼器'], ['furnace', '丹炉维护']], selected)
      + (selected === 'forge' ? forgeView(state, ui, data) : selected === 'furnace' ? furnaceView(state, ui) : brewView(state, ui, data));
  };
  const renderers = { bag: () => bagView(state, ui, data), gear: () => gearView(state, ui), market: () => marketView(state, ui), workshop, jobs: () => jobsView(state, data) };
  return `<section class="stack player-page" aria-label="行囊与工坊"><div class="player-page-heading"><h1 class="section-title">行囊</h1>${tag(`${fmt(state.player?.stones || 0)} 灵石`)}</div>`
    + tabs('inventory', inventoryTabs, active)
    + renderers[active]() + '</section>';
}
