// 立绘只负责显示；角色性别、宗门和妖兽境界都由引擎提供。
// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = value => String(value ?? '').replace(/[&<>"']/g,
  ch => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));
const SECTS = { 金虹剑派: 'jinhong', 星河剑派: 'xinghe', 竹山宗: 'zhushan', 离火门: 'lihuo', 化尘教: 'huachen' };

// 用途：解析人物或妖兽立绘地址。
// 流程：优先使用权威portrait，再按kind与人物属性选择回退资源。
// 调用方式：头像渲染传入person与kind。
// 返回值：图片资源路径字符串。
export function portraitSource(person = {}, kind = 'npc') {
  if (person.portrait) return person.portrait;
  if (kind === 'player') return `assets/portraits/player-${person.gender === '女' ? 'female' : 'male'}.webp`;
  if (kind === 'beast') return `assets/portraits/beast-${Math.max(0, Math.min(4, Math.floor((Number(person.stage) || 0) / 3)))}.webp`;
  const sect = SECTS[person.sectName] || SECTS[person.sect] || person.sect;
  if (['jinhong', 'xinghe', 'zhushan', 'lihuo', 'huachen'].includes(sect)) {
    const female = person.gender === '女';
    const originalFemale = sect === 'xinghe' || sect === 'zhushan';
    const suffix = female === originalFemale ? '' : female ? '-female' : '-male';
    return `assets/portraits/sect-${sect}${suffix}.webp`;
  }
  return `assets/portraits/rogue${person.gender === '女' ? '-female' : ''}.webp`;
}

// 用途：生成角色立绘元素。
// 流程：读取portraitSource并组合替代文字与样式类。
// 调用方式：人物面板传入person、kind和可选className。
// 返回值：img元素HTML字符串。
export function renderPortrait(person, kind = 'npc', className = '') {
  return `<img class="character-portrait ${esc(className)}" src="${esc(portraitSource(person, kind))}" alt="${esc(person?.name || '修士')}立绘" loading="eager" decoding="async">`;
}

// 仅比较同一场斗法的实际状态；建号、导入、刷新和新战斗不会产生受击动画。
// 用途：整理一次出牌后的视觉反馈。
// 流程：比较出牌前后敌我气血和护盾，提取受击与挡伤变化。
// 调用方式：API响应处理传入before与after。
// 返回值：效果条目数组。
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

// 用途：播放一次战斗数值反馈。
// 流程：查找对应战斗人物，插入数字徽章和短时动画，随后清理。
// 调用方式：状态重绘后传入impacts和可选document。
// 返回值：无返回值；修改短时DOM。
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
