import { renderArtifactIllustration } from './artifact-illustrations.mjs';
import { renderPillBatchControls } from './pill-batch.mjs';
import { esc, heading, panel, layout, asideBlock } from './shell.mjs';
import { renderArtifactAbility, renderEquippedArtifacts } from './artifacts.mjs';
import { forgeInscriptionName } from './forge.mjs';
import { renderPillEffect } from './economy.mjs';
import { renderRenownPanel } from './renown.mjs';
import { renderCraftPricing } from './craft-pricing.mjs';
import { renderPotionSaleActions, renderMarketCash } from './quantity-controls.mjs';

// 用途：从真实库存汇总拥有的物品，排除功法与神通。
// 流程：合并法宝、丹药、材料、草药与突破物品，用权威目录补充名称和品阶。
// 调用方式：行囊页面传入当前 state 与已加载的炼器材料目录。
// 返回值：物品数组；仅收录数量大于零的库存，不修改存档。
export function inventoryItems(state, materials = []) {
  const items = (state.gears || []).map(gear => ({ ...gear, kind: 'gear', count: 1 }));
  for (const [id, count] of Object.entries(state.arts?.potions || {})) {
    if (count <= 0) continue;
    const definition = (state.pillCatalog || []).find(item => item.id === id);
    items.push({ ...definition, id, name: definition?.name || '丹药名称待加载', kind: 'potion', count });
  }
  for (const [kind, stock, catalog] of [
    ['material', state.arts?.materials, state.materialCatalog || materials],
    ['herb', state.arts?.herbs, state.herbCatalog || state.market?.filter(item => item.kind === 'herb') || []],
  ]) {
    for (const [id, count] of Object.entries(stock || {})) {
      if (count <= 0) continue;
      const definition = catalog.find(item => item.id === id || item.name === id);
      items.push({ ...definition, id, name: definition?.name || (kind === 'herb' ? id : '材料名称待加载'), kind, count });
    }
  }
  for (const [id, name, count] of [['cores', '灵核', state.player.cores], ['pills', '凝元丹', state.player.pills]]) {
    if (count > 0) items.push({ id, name, kind: 'other', count, desc: '大境界突破时使用' });
  }
  const furnace = (state.furnaces || []).find(item => item.current);
  if (furnace) items.push({ ...furnace, kind: 'other', count: 1, desc: `丹炉耐久 ${state.arts.durability} / 40；在工坊管理` });
  return items;
}

// 用途：展示人物详细属性及中文天赋。
// 流程：读取 player、talents 与 combatReference 的真实属性，分组列出当前数值。
// 调用方式：行囊与面板的“人物面板”子页调用。
// 返回值：人物属性面板 HTML；不重算战斗或经济公式。
export function renderCharacterPanel(state) {
  const p = state.player, combat = p.combatReference || {};
  const talents = (p.talents || []).map(item => `<article class="talent-detail"><strong>${esc(item.name)}</strong><p>${esc(item.desc)}</p></article>`).join('');
  const rows = [['境界', p.realmName], ['身份', p.rank], ['年龄 / 寿元', `${p.age} / ${p.lifespan} 岁`],
    ['资质', p.apt], ['悟性', p.insight], ['福缘', p.luck], ['体魄', p.body], ['丹术', p.alchemy],
    ['遁速', p.escapeSpeed], ['气血', `${p.health} / 100`], ['声望', p.renown], ['灵石', p.stones],
    ['斗法开局气血', combat.hp != null ? `${combat.hp} / ${combat.hpMax}` : '待查'],
    ['斗法开局灵力', combat.mana], ['每回合基础灵力', combat.manaRegen], ['灵力上限', combat.manaCap]];
  return panel('人物详细属性', p.name, `<div class="character-details"><div class="detail-list">`
    + rows.map(([label, value]) => `<div><label>${esc(label)}</label><strong>${esc(value ?? '—')}</strong></div>`).join('')
    + `</div><p class="panel-note">${esc(combat.context || '')}</p><h4>出身天赋</h4>`
    + (talents || '<p class="panel-note">本世未选择天赋。</p>') + `</div>`);
}

// 用途：渲染行囊类型/品阶筛选、服丹与法宝管理入口。
// 流程：筛选真实拥有物品，显示权威服丹效果和装备状态，并提供服务端动作按钮。
// 调用方式：主视图传入当前状态、筛选参数、材料目录及丹毒面板 HTML。
// 返回值：完整行囊页面 HTML；所有实际变更由 app 的动作处理提交。
export function renderInventoryPanel(state, { kind = 'all', grade = 'all', sub = 'items', materials = [], poisonHTML = '' } = {}) {
  const items = inventoryItems(state, materials);
  const visible = items.filter(item => (kind === 'all' || item.kind === kind)
    && (grade === 'all' || (grade === 'unknown' ? !item.grade : Number(item.grade) === Number(grade))));
  const types = [['all', '全部'], ['gear', '法宝'], ['potion', '丹药'], ['material', '材料'], ['herb', '草药'], ['other', '其他']];
  const grades = [...new Set(items.map(item => item.grade).filter(Boolean))].sort((a, b) => a - b);
  const tabs = `<div class="subtabs"><button class="subtab ${sub === 'items' ? 'on' : ''}" data-subtab="inventory" data-sub="items">随身行囊</button><button class="subtab ${sub === 'character' ? 'on' : ''}" data-subtab="inventory" data-sub="character">人物面板</button></div>`;
  const aside = asideBlock('丹毒与服丹', poisonHTML) + renderEquippedArtifacts(state.gears);
  const head = heading('行囊与面板', '查看随身物品、服用丹药、管理法宝与人物属性。', `${items.length} 种物品`) + tabs;
  if (sub === 'character') return head + layout(renderCharacterPanel(state), aside)
    + `<div style="margin-top:22px">${panel('宁州声望', '议价、洞府与机缘', `<div class="character-details">${renderRenownPanel(state.renownInfo)}</div>`)}</div>`;
  const filters = `<div class="inventory-controls"><div class="shop-tabs">${types.map(([id, name]) => `<button data-inventory-kind="${id}" class="${kind === id ? 'active' : ''}">${name}</button>`).join('')}</div>`
    + `<label>品阶 <select data-inventory-grade><option value="all">全部品阶</option>${grades.map(value => `<option value="${value}" ${String(value) === String(grade) ? 'selected' : ''}>${value} 阶</option>`).join('')}<option value="unknown" ${grade === 'unknown' ? 'selected' : ''}>无品阶 / 待查</option></select></label></div>`;
  const cards = visible.map(item => {
    const label = `${item.grade ? `${item.grade} 阶` : '无品阶 / 待查'} · ×${item.count}${item.equipped ? ' · 已装备' : ''}`;
    let actions = '';
    if (item.kind === 'potion') actions = renderPillBatchControls(item.id)
      + renderPotionSaleActions(item.id, item.maxSale, state);
    if (item.kind === 'gear') {
      const equipAction = item.equipped ? 'gear/unequip' : 'equip';
      const quote = item.actions?.[item.equipped ? 'unequip' : 'equip'];
      actions = `<button data-action="${equipAction}" data-id="${esc(item.id)}" ${quote?.enabled === false ? 'disabled' : ''}>${item.equipped ? '卸下' : '装备'}</button>`
        + (!item.inscribed && Number(item.forgeVersion || 0) < 2 ? `<button data-action="gear/inscribe" data-id="${esc(item.id)}" ${item.actions?.inscribe?.enabled === false ? 'disabled' : ''}>铭刻</button>` : '')
        + `<button data-action="gear-salvage-preview" data-id="${esc(item.id)}">材料回收预览</button>`
        + `<button data-action="sell" data-kind="gear" data-id="${esc(item.id)}" data-count="1" ${item.actions?.sell?.enabled === true ? '' : 'disabled'} title="${esc(item.actions?.sell?.reason || '')}">出售法器</button>`;
    }
    return `<article class="inventory-item" data-inventory-item="${esc(item.id)}"><div class="inventory-item-head">${item.kind === "gear" || item.illustration ? renderArtifactIllustration(item,{compact:true}) : ""}<h4>${esc(item.name)}</h4><span>${esc(label)}</span></div>`
      + (item.kind === 'potion'
        ? `<details class="inventory-pill-details" data-inventory-pill-details="${esc(item.id)}"><summary>查看效果与服用</summary><p>持有 ${item.count} 枚</p>${renderPillEffect(state, item.id)}${renderCraftPricing(item.pricing, { kind: 'potion', compact: true })}<div class="arts-actions">${actions}</div></details>`
        : (item.ability ? renderArtifactAbility(item.ability, { compact: true }) : `<p>${esc(item.effectText || item.passive?.text || item.desc || '')}</p>`)
          + (item.kind === 'gear' && Number(item.forgeVersion || 0) >= 2 ? `<p class="panel-note">${esc(forgeInscriptionName(item.inscription))} · 主效果与灵纹已在成器时确定</p>` : '')
          + (item.kind === 'gear' ? renderCraftPricing(item.pricing, { kind: 'gear', compact: true }) : '')
          + `<div class="arts-actions">${actions}</div>${item.kind === 'gear' ? renderMarketCash(state)
            + (item.actions?.sell?.reason ? `<p class="trade-limit-reason">${esc(item.actions.sell.reason)}</p>` : '') : ''}`)
      + `</article>`;
  }).join('') || '<div class="empty">该分类没有拥有的物品。</div>';
  return head + layout(panel('随身行囊', `${visible.length} 种`, filters + `<div class="inventory-grid">${cards}</div>`), aside);
}
