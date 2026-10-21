import { esc } from './shell.mjs';

export const isArtifactRedraw = ability => ability?.targetMode === 'hand-many';

export function artifactRedrawSnapshot(battle) {
  return { expectedTurn: battle?.turn, expectedHand: (battle?.hand || []).map(card => card.uid) };
}

export function artifactRedrawIsCurrent(battle, snapshot) {
  return !!battle && snapshot?.expectedTurn === battle.turn
    && JSON.stringify(snapshot.expectedHand) === JSON.stringify((battle.hand || []).map(card => card.uid));
}

export function artifactRedrawCards(battle, ability) {
  const permitted = Array.isArray(ability?.targets) ? new Set(ability.targets.map(item => typeof item === 'string' ? item : item.uid || item.id)) : null;
  return (battle?.hand || []).filter(card => card.uid && (!permitted || permitted.has(card.uid)));
}

export function toggleArtifactRedraw(battle, ability, selected, uid) {
  const cards = artifactRedrawCards(battle, ability);
  const next = [...new Set(selected || [])].filter(id => cards.some(card => card.uid === id));
  if (next.includes(uid)) return next.filter(id => id !== uid);
  if (next.length >= Number(ability?.targetMax || 0) || !cards.some(card => card.uid === uid)) return next;
  return [...next, uid];
}

export function artifactRedrawRequest(battle, ability, selected, snapshot) {
  if (!isArtifactRedraw(ability) || ability.playable !== true || !artifactRedrawIsCurrent(battle, snapshot)) return null;
  const cards = artifactRedrawCards(battle, ability), unique = [...new Set(selected || [])];
  if (unique.length !== (selected || []).length || unique.length < Number(ability.targetMin || 1)
      || unique.length > Number(ability.targetMax || 0) || !unique.every(uid => cards.some(card => card.uid === uid))) return null;
  return { id: ability.id, targets: unique, ...snapshot };
}

export function renderArtifactRedraw(battle, ability, selected = [], { mode = 'desktop', busy = false } = {}) {
  const cards = artifactRedrawCards(battle, ability), maximum = Math.min(Number(ability?.targetMax || 0), cards.length);
  const attrs = (action, value = '') => mode === 'mobile' ? `data-ui="artifact-redraw-${action}" data-value="${esc(value)}"`
    : `data-action="artifact-redraw-${action}" data-id="${esc(value)}"`;
  return `<p class="artifact-redraw-description">选择当前手牌，先弃置，再抽取同样数量。最多换 ${esc(ability?.targetMax || 0)} 张，手牌不足也可使用。确认才消耗 ${esc(ability?.cost || 0)} 灵力${Number(ability?.healthCost) > 0 ? `和 ${esc(ability.healthCost)} 真实气血` : ''}。</p>`
    + `<div class="artifact-redraw-count"><strong>已选 ${selected.length} / ${maximum} 张</strong><span>将弃 ${selected.length} 抽 ${selected.length}</span></div>`
    + `<div class="artifact-redraw-cards">${cards.map((card, index) => {
      const chosen = selected.includes(card.uid), blocked = busy || (!chosen && selected.length >= maximum);
      return `<button type="button" class="artifact-redraw-card${chosen ? ' selected' : ''}" ${attrs('toggle', card.uid)} aria-pressed="${chosen}"${blocked ? ' disabled' : ''}><strong>${chosen ? '✓ ' : ''}${esc(card.name)}</strong><span>${esc(card.dao || '')} · 灵力 ${esc(card.cost ?? '—')}</span><small>第 ${index + 1} 张${card.retained || card.retain || card.lifebound === 'retain' ? ' · 可主动弃置的保留牌' : ''}</small></button>`;
    }).join('') || '<p>当前没有可换的手牌。</p>'}</div>`
    + `<div class="artifact-redraw-actions"><button type="button" ${attrs('cancel')}${busy ? ' disabled' : ''}>取消</button><p>弃牌会参与正常重洗。</p><button type="button" class="primary" ${attrs('confirm')}${busy || selected.length < Number(ability?.targetMin || 1) ? ' disabled' : ''}>确认弃 ${selected.length} 抽 ${selected.length}</button></div>`;
}
