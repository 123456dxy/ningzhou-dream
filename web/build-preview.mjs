// Shared character-plan presentation. Numeric values come from build-check only.
const escape = value => String(value ?? '').replace(/[&<>"']/g, character =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[character]));
const number = value => Number.isFinite(value) ? new Intl.NumberFormat('zh-CN', {
  maximumFractionDigits: 2,
}).format(value) : '—';

// Copy the complete draft, including the seed, so preview and creation use one input.
export function buildRequest(draft) {
  return {
    name: String(draft.name || '').trim(),
    gender: draft.gender || '女',
    seed: draft.seed,
    portraitId: draft.portraitId || '',
    traits: [...(draft.traits || [])],
    allocation: { ...(draft.allocation || {}) },
  };
}

export function buildKey(draft) {
  const request = buildRequest(draft);
  request.traits.sort();
  request.allocation = Object.fromEntries(Object.entries(request.allocation).sort(([a], [b]) => a.localeCompare(b)));
  return JSON.stringify(request);
}

export function lockBuildPlan(draft, check) {
  if (!check?.valid || !check.preview?.player || !draft.name?.trim() || !draft.seed
      || check.preview.seed !== draft.seed) return null;
  const request = buildRequest(draft);
  Object.freeze(request.traits);
  Object.freeze(request.allocation);
  return Object.freeze({ request: Object.freeze(request), key: buildKey(request), preview: check.preview });
}

export function matchesBuildPlan(plan, draft) {
  return !!plan && plan.key === buildKey(draft);
}

export function defaultBuildPortrait(options, gender) {
  return (options?.portraits || []).find(portrait => portrait.gender === gender)?.id || '';
}

export function renderBuildPortraits(options, selectedId, gender, disabled = false) {
  const portraits = (options?.portraits || []).filter(portrait => !portrait.gender || portrait.gender === gender);
  if (!portraits.length) return '<p class="build-note">立绘目录正在加载。</p>';
  return `<div class="build-portraits" role="group" aria-label="选择角色立绘">${portraits.map(portrait =>
    `<button type="button" class="build-portrait-choice ${portrait.id === selectedId ? 'selected' : ''}"`
      + ` data-build-portrait="${escape(portrait.id)}" aria-pressed="${portrait.id === selectedId}"`
      + ` aria-label="选择立绘：${escape(portrait.name)}" ${disabled ? 'disabled' : ''}>`
      + `<img src="${escape(portrait.src)}" alt="${escape(portrait.name)}" loading="lazy" decoding="async">`
      + `<span>${escape(portrait.name)}</span></button>`).join('')}</div>`;
}

export function renderOpeningPreview(preview) {
  if (!preview?.player) return '<p class="build-note">锁定方案后查看实际开局属性与天赋效果。</p>';
  const player = preview.player;
  const combat = player.combatReference || {};
  const stats = [
    ['资质', player.apt], ['悟性', player.insight], ['福缘', player.luck],
    ['体魄', player.body], ['丹术', player.alchemy], ['灵石', player.stones],
    ['寿元', player.lifespan], ['遁速', player.escapeSpeed],
    ['斗法气血', combat.hpMax], ['开局灵力', combat.mana], ['每回合灵力', combat.manaRegen],
  ];
  const talents = (player.talents || []).map(talent =>
    `<li><strong>${escape(talent.name)}</strong><span>${escape(talent.desc)}</span></li>`).join('');
  const gears = (preview.gears || []).map(gear => `<li>${escape(gear.name)}${gear.equipped ? '（已装备）' : ''}</li>`).join('');
  const arts = preview.arts || {};
  const modifiers = arts.mods || {};
  const details = [
    ['修炼倍率', modifiers.cult == null ? null : `${number(modifiers.cult)} 倍`],
    ['成丹数量加成', modifiers.brewYield == null ? null : `+${number(modifiers.brewYield)}`],
  ].filter(([, value]) => value != null);
  return `<section class="opening-preview" aria-label="锁定方案开局预览">
    <div class="opening-preview-title">${player.portrait ? `<img src="${escape(player.portrait)}" alt="已选角色立绘">` : ''}
      <div><h3>${escape(player.name)} · 开局效果</h3><p>方案已锁定，种子 ${escape(preview.seed)}。确认后按此方案开局。</p></div></div>
    <dl class="opening-stats">${stats.map(([label, value]) => `<div><dt>${label}</dt><dd>${number(value)}</dd></div>`).join('')}</dl>
    ${details.length ? `<p class="build-note">${details.map(([label, value]) => `${label} ${value}`).join(' · ')}</p>` : ''}
    <h4>已选天赋</h4><ul class="opening-talents">${talents || '<li>未选择额外天赋。</li>'}</ul>
    ${gears ? `<h4>开局法器</h4><ul>${gears}</ul>` : ''}
    <p class="build-note">成长、月度与斗法天赋在对应时机触发；以上属性已包含立即生效的加成。</p>
  </section>`;
}
