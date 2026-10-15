import { esc } from './shell.mjs';

// 用途：格式化后端的意图数值；流程：妖兽逐段预告；传权威数值，返回数字文字，未知值不伪造为零。
function intentNumber(value) {
  return typeof value === 'number' && Number.isFinite(value) ? String(value) : '—';
}

// 用途：判断是否需要显示一个正效果；流程：妖兽逐段预告；传权威数值，返回布尔值，不计算效果。
function hasAmount(value) {
  return typeof value === 'number' && Number.isFinite(value) && value > 0;
}

// 用途：列出一段妖兽行动的权威结果；流程：真实回合副本预演展示；传结果对象，返回标签HTML，不合成或推导伤害。
function renderActionEffects(action) {
  const effects = [];
  if (hasAmount(action.rawDamage) || hasAmount(action.damage) || hasAmount(action.blocked)) {
    effects.push(`打出 ${intentNumber(action.rawDamage)} · 气血伤害 ${intentNumber(action.damage)}`);
  }
  if (hasAmount(action.blocked)) effects.push(`盾挡 ${intentNumber(action.blocked)}`);
  if (hasAmount(action.counterDamage)) effects.push(`受到反震 ${intentNumber(action.counterDamage)}`);
  if (hasAmount(action.piercePercent)) effects.push(`穿盾 ${intentNumber(action.piercePercent)}%`);
  if (hasAmount(action.shieldGain)) effects.push(`计划凝盾 ${intentNumber(action.shieldGain)}`);
  if (hasAmount(action.healGain)) effects.push(`最多回复 ${intentNumber(action.healGain)} 气血`);
  if (hasAmount(action.poisonDamage)) effects.push(`毒息 ${intentNumber(action.poisonDamage)} 点 / 回合`);
  if (hasAmount(action.burnDamage)) effects.push(`施加灼烧 ${intentNumber(action.burnDamage)} 点 / 回合`);
  if (hasAmount(action.weakenLayers)) effects.push(`计划虚弱 ${intentNumber(action.weakenLayers)} 层`);
  if (hasAmount(action.chargeGain)) effects.push(`计划蓄势 ${intentNumber(action.chargeGain)} 层`);
  return effects.map(effect => `<span>${esc(effect)}</span>`).join('') || '<span>本段无直接伤害</span>';
}

// 用途：展示冻结的妖兽整轮行动与真实分段预演；流程：正式斗法和独立试炼意图；传战况快照，返回HTML，修士或旧单意图返回空串。
export function renderBeastIntent(battle) {
  if (battle?.enemyBeast !== true || !Array.isArray(battle.beastActions) || !battle.beastActions.length) return '';
  return `<div class="beast-intent" data-beast-intent><div class="beast-intent-budget">`
    + `妖力预算 <b>${esc(intentNumber(battle.beastEnergyBudget))}</b> · 最多 <b>${esc(intentNumber(battle.beastActionLimit))}</b> 次行动</div>`
    + '<ol class="beast-intent-actions" aria-label="妖兽本轮逐段行动">'
    + battle.beastActions.map((action, index) => {
      const executes = action.executed !== false;
      return `<li class="beast-intent-action${executes ? '' : ' skipped'}" data-beast-action="${esc(action.id)}" data-executed="${executes}">`
        + `<div class="beast-intent-action-title"><b>${index + 1}. ${esc(action.name)}</b><span>妖力 ${esc(intentNumber(action.cost))}</span></div>`
        + (executes ? `<div class="beast-intent-effects">${renderActionEffects(action)}</div>`
          : '<div class="beast-intent-skipped">不执行 · 预计战局将提前结束</div>') + '</li>';
    }).join('') + '</ol><small class="beast-intent-note">按顺序预演；继续出牌会改变伤害与后续行动。护盾和回复受实际容量限制。</small></div>';
}
