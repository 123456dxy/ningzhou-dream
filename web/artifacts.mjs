import { esc } from './shell.mjs';
import { renderNumericPreview } from './numeric-preview.mjs';

const kindLabel = { attack: '攻击', shield: '护盾', guard: '护盾', heal: '治疗', mana: '回灵',
  water: '蓄水', charge: '蓄势', poison: '施毒', fire: '灼烧', soul: '震荡', wave: '水势爆发',
  heavy: '重势爆发', poisonBurst: '毒伤爆发', fireBurst: '灼烧爆发', soulBurst: '神识爆发', empower: '增伤', ward: '减伤' };

// 用途：显示法宝神通与数值。
// 流程：组合中文名称、类型、权威preview、费用和冷却。
// 调用方式：行囊、侧栏与炼器传入ability。
// 返回值：HTML字符串；无神通时空字符串。
export function renderArtifactAbility(ability, { compact = false } = {}) {
  if (!ability) return '';
  return `<div class="artifact-ability${compact ? ' compact' : ''}" data-artifact-ability="${esc(ability.id)}">`
    + `<div><strong>${esc(ability.name)}</strong><span>${esc(ability.dao)}道 · ${esc(kindLabel[ability.kind] || '法宝神通')}</span></div>`
    + `<p>${esc(ability.description)}</p>` + renderNumericPreview(ability.preview, { compact })
    + `<small>耗灵力 ${esc(ability.cost)} · 冷却 ${esc(ability.cooldown)} 回合${ability.stageName ? ` · 需${esc(ability.stageName)}` : ''}</small></div>`;
}

// 用途：显示当前装备法宝神通。
// 流程：筛选已装备且有神通的法宝并提供行囊入口。
// 调用方式：侧栏或行囊传入gears。
// 返回值：法宝摘要HTML字符串。
export function renderEquippedArtifacts(gears = []) {
  const equipped = gears.filter(gear => gear.equipped && gear.ability);
  return `<details class="sidebar-artifacts"${equipped.length ? ' open' : ''}><summary>装备法宝神通 <b>${equipped.length}</b></summary>`
    + (equipped.length ? equipped.map(gear => `<article><small>${esc(gear.name)}</small>${renderArtifactAbility(gear.ability, { compact: true })}</article>`).join('')
      : '<p>装备带有神通的法宝后，可在战斗中直接施展。</p>')
    + `<button data-action="goto-equipment">查看行囊装备</button></details>`;
}

// 用途：显示可直接施展的法宝。
// 流程：读取实时法宝数值、可用原因和冷却，生成施展按钮。
// 调用方式：斗法视图传入battle。
// 返回值：法宝操作区HTML字符串。
export function renderBattleArtifacts(battle) {
  const abilities = battle.artifacts || [];
  return `<section class="combat-artifacts" data-combat-artifacts><div class="combat-artifacts-heading"><h3>法宝神通 <span>${abilities.length}</span></h3>`
    + `<p>装备后常驻，直接施展；无需抽牌，不占额外神通槽。消耗灵力与本回合施术次数，冷却后可再次使用。</p></div>`
    + (abilities.length ? `<div class="combat-artifact-list">${abilities.map(ability => {
      const effects = [['damage', '伤害'], ['shield', '护盾'], ['heal', '治疗'], ['energy', '灵力']]
        .filter(([key]) => ability[key] > 0).map(([key, label]) => `${label} ${ability[key]}`).join(' · ');
      const playable = ability.playable === true;
      return `<button class="combat-artifact${playable ? ' available' : ''}" data-action="artifact-play" data-id="${esc(ability.id)}" ${playable ? '' : 'disabled'}>`
        + `<div class="combat-artifact-top"><span>灵力 ${esc(ability.cost)}</span><strong>${esc(ability.name)}</strong><em>${esc(ability.dao)}道</em></div>`
        + (effects ? `<p class="combat-artifact-effect">${esc(effects)}</p>` : '')
        + (ability.text ? `<p class="combat-artifact-preview">${esc(ability.text)}</p>` : !effects ? '<p class="combat-artifact-effect">按法宝神通效果结算</p>' : '')
        + `<p>${esc(ability.description)}</p><small>冷却 ${esc(ability.cooldown)} 回合${ability.cooldownRemaining > 0 ? ` · 尚余 ${esc(ability.cooldownRemaining)} 回合` : ' · 已就绪'}</small>`
        + `<b class="combat-artifact-action">${esc(playable ? '施展法宝神通' : ability.reason || '当前无法施展')}</b></button>`;
    }).join('')}</div>` : '<p class="combat-artifact-empty">尚未装备带有神通的法宝。可在任务与工坊铸炼，再到行囊装备。</p>')
    + `</section>`;
}

// 用途：展示权威成器效果。
// 流程：检查推演有效性，列出被动、神通、铭刻后数值与配方条件。
// 调用方式：炼器子页传入forge-preview响应。
// 返回值：预览或错误占位HTML字符串。
export function renderForgePreview(preview) {
  if (!preview) return '<div class="forge-preview"><p>正在推演当前配方…</p></div>';
  const info = preview.info;
  if (!info || !info.valid) return `<div class="forge-preview"><p>${esc(info?.reason || preview.error || '当前配方无法推演。')}</p></div>`;
  return `<div class="forge-preview"><div class="forge-preview-heading"><strong>${esc(info.shapeName || '成器预览')}</strong><span>${esc(info.grade)}阶 · 成功率 ${Math.round(info.chance * 100)}%</span></div>`
    + `<p>${esc(info.mainName)} · ${esc(info.mainElement)}${esc(info.mainPolarity)} · 铸炼 ${esc(info.days)} 天 · ${esc(info.cost)} 灵石</p>`
    + (info.passive?.text ? `<div class="forge-passive"><strong>装备数值变化</strong><p>${esc(info.passive.text)}</p></div>` : '')
    + renderArtifactAbility(info.ability)
    + (info.inscribedAbility ? `<details class="forge-inscribed"><summary>铭刻后的数值</summary>`
      + (info.inscribedPassive?.text ? `<p>${esc(info.inscribedPassive.text)}</p>` : '')
      + renderArtifactAbility(info.inscribedAbility) + `</details>` : '')
    + (info.enabled === false ? `<p class="forge-preview-reason">${esc(info.reason)}</p>` : '')
    + `<p class="forge-preview-note">只推演配方；开炉后才消耗材料。成器装备后获得此法宝神通。</p></div>`;
}
