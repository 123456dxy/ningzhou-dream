// 立绘只负责显示；角色性别、宗门和妖兽境界都由引擎提供。
const esc = value => String(value ?? '').replace(/[&<>"']/g,
  ch => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));
const SECTS = { 金虹剑派: 'jinhong', 星河剑派: 'xinghe', 竹山宗: 'zhushan', 离火门: 'lihuo', 化尘教: 'huachen' };

export function portraitSource(person = {}, kind = 'npc') {
  if (person.portrait) return person.portrait;
  if (kind === 'player') return `assets/portraits/player-${person.gender === '女' ? 'female' : 'male'}.png`;
  if (kind === 'beast') return `assets/portraits/beast-${Math.max(0, Math.min(4, Math.floor((Number(person.stage) || 0) / 3)))}.png`;
  const sect = SECTS[person.sectName] || SECTS[person.sect] || person.sect;
  if (['jinhong', 'xinghe', 'zhushan', 'lihuo', 'huachen'].includes(sect)) {
    const female = person.gender === '女';
    const originalFemale = sect === 'xinghe' || sect === 'zhushan';
    const suffix = female === originalFemale ? '' : female ? '-female' : '-male';
    return `assets/portraits/sect-${sect}${suffix}.png`;
  }
  return `assets/portraits/rogue${person.gender === '女' ? '-female' : ''}.png`;
}

export function renderPortrait(person, kind = 'npc', className = '') {
  return `<img class="character-portrait ${esc(className)}" src="${esc(portraitSource(person, kind))}" alt="${esc(person?.name || '修士')}立绘" loading="eager" decoding="async">`;
}

// 仅比较同一场斗法的实际状态；建号、导入、刷新和新战斗不会产生受击动画。
export function battleImpacts(before, after) {
  if (!before || !after || before.enemy !== after.enemy || before.npcId !== after.npcId
    || Number(after.turn) < Number(before.turn)) return [];
  const impacts = [];
  for (const [side, hp, shield] of [['player', 'hp', 'shield'], ['enemy', 'enemyHp', 'enemyShield']]) {
    const damage = Math.max(0, (Number(before[hp]) || 0) - (Number(after[hp]) || 0));
    const absorbed = Math.max(0, (Number(before[shield]) || 0) - (Number(after[shield]) || 0));
    if (damage) impacts.push({ side, damage, absorbed });
  }
  return impacts;
}

export function animateBattleImpacts(impacts, doc = document) {
  for (const impact of impacts) {
    const actor = doc.querySelector(`[data-combat-actor="${impact.side}"]`);
    if (!actor) continue;
    const badge = doc.createElement('span');
    badge.className = `combat-impact${impact.damage ? '' : ' blocked'}`;
    badge.textContent = impact.damage ? `−${impact.damage}` : `护盾 −${impact.absorbed}`;
    badge.setAttribute('aria-hidden', 'true');
    actor.append(badge);
    actor.classList.add(impact.damage ? 'combat-hit' : 'combat-block');
    setTimeout(() => { badge.remove(); actor.classList.remove('combat-hit', 'combat-block'); }, 850);
  }
}
