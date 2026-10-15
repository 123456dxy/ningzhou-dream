import { cardKind, cardRule, cardNumbers, battleStatusRows, cardGenerationHint } from './cards.mjs';
import { battleImpacts, animateBattleImpacts } from './portraits.mjs';
import { renderBeastIntent } from './beast-intent.mjs';

const daoArt = Object.freeze({
  '金': 'gold', '木': 'wood', '水': 'water', '火': 'fire', '土': 'earth',
  '剑': 'sword', '神': 'visual', '体': 'bull', '气': 'zhou', '阵': 'array', '丹': 'pill', '器': 'forge',
});
const ui = {
  config: null, state: null, draft: null, busy: false, requestSequence: 0,
  notice: '', needsRefresh: false, target: null, loadoutOpen: false, logOpen: false,
};
const configElement = document.querySelector('#lab-config');
const battleElement = document.querySelector('#lab-battle');
const detailsElement = document.querySelector('#lab-details');
const noticeElement = document.querySelector('#lab-notice');
const targetDialog = document.querySelector('#lab-target');
targetDialog.className = 'lab-target';

// 用途：转义服务端展示文本；参与所有渲染；传任意文本，返回安全 HTML。
function escapeHtml(value) {
  return String(value ?? '').replace(/[&<>"']/g, character => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[character]));
}

// 用途：规范数组快照；参与构筑与手牌渲染；传协议值，返回数组或空数组。
function list(value) {
  return Array.isArray(value) ? value : [];
}

// 用途：补齐初始传承级别；参与首屏、旧快照及重试；传选择对象，返回不修改原对象的完整草稿。
function selectionDraft(selection) {
  return selection ? { ...selection, tier: selection.tier || 'initial' } : null;
}

// 用途：取得后端允许的传承级别；参与配置与键盘切换；无参数，返回目录，旧接口仅展示初始流派。
function availableTiers() {
  return list(ui.config?.tiers).length ? ui.config.tiers : [{ id: 'initial', name: '初始流派' }];
}

// 用途：取得当前选择的传承名称；参与待选配置和实际战局标签；传选择对象，返回后端目录中的名称。
function tierName(selection) {
  return availableTiers().find(tier => tier.id === (selection?.tier || 'initial'))?.name || '初始流派';
}

// 用途：取得对应传承级别的构筑名称；参与配置展示；传构筑与选择快照，返回后端显示名称。
function buildName(build, selection) {
  return selection?.tier === 'advanced' ? build?.advancedName || build?.name || '' : build?.name || '';
}

// 用途：格式化后端数值；参与状态展示；传数值，返回有限数字的文字。
function numberText(value) {
  return Number.isFinite(Number(value)) ? String(Number(value)) : '0';
}

// 用途：只采用已知图片路径；参与立绘与卡图展示；传路径，返回白名单路径或回退图。
function portraitPath(value, fallback) {
  return typeof value === 'string' && /^\/?assets\/portraits\/[a-z0-9-]+\.webp$/.test(value)
    ? value : fallback;
}

// 用途：匹配待选与已开战构筑；参与切换提示；传两个选择对象，返回是否相同。
function sameSelection(left, right) {
  const leftDraft = selectionDraft(left);
  const rightDraft = selectionDraft(right);
  return !!leftDraft && !!rightDraft && ['buildId', 'stage', 'opponentId', 'seed', 'tier']
    .every(key => String(leftDraft[key]) === String(rightDraft[key]));
}

// 用途：生成流派及参数控件；参与载入和交互重绘；读取草稿，返回 HTML。
function renderConfig() {
  const selection = ui.draft;
  if (!ui.config || !selection) return;
  const selectedBuild = list(ui.config.builds).find(build => build.id === selection.buildId);
  const description = selection.tier === 'advanced'
    ? selectedBuild?.advancedDescription || '完整传承按当前境界装配合法功法、神通与悟道。'
    : selectedBuild?.description || '功法与悟道自动装配，按当前境界携带合法神通。';
  const disabled = ui.busy ? ' disabled' : '';
  configElement.innerHTML = `<div class="tier-options" role="tablist" aria-label="构筑传承">
    ${availableTiers().map(tier => `<button type="button" id="lab-tier-${escapeHtml(tier.id)}" role="tab" class="tier-option${tier.id === selection.tier ? ' selected' : ''}" data-action="tier" data-tier-id="${escapeHtml(tier.id)}" data-testid="tier-choice" aria-selected="${tier.id === selection.tier}" aria-controls="lab-build-options" tabindex="${tier.id === selection.tier ? '0' : '-1'}"${disabled}>${escapeHtml(tier.name)}</button>`).join('')}
    </div><div id="lab-build-options" class="build-options" role="tabpanel" aria-labelledby="lab-tier-${escapeHtml(selection.tier)}">
    ${list(ui.config.builds).map((build, index) => `<button type="button" class="build-option${build.id === selection.buildId ? ' selected' : ''}" data-action="build" data-build-id="${escapeHtml(build.id)}" data-testid="build-choice" aria-pressed="${build.id === selection.buildId}"${disabled}>${escapeHtml(buildName(build, selection))}<span>流派 ${index + 1}</span></button>`).join('')}
    </div><div class="config-controls">
      <label>境界<select id="lab-stage" data-testid="stage-select"${disabled}>${list(ui.config.realms).map(realm => `<option value="${escapeHtml(realm.stage)}"${Number(realm.stage) === Number(selection.stage) ? ' selected' : ''}>${escapeHtml(realm.name)}</option>`).join('')}</select></label>
      <label>对手<select id="lab-opponent" data-testid="opponent-select"${disabled}>${list(ui.config.opponents).map(opponent => `<option value="${escapeHtml(opponent.id)}"${opponent.id === selection.opponentId ? ' selected' : ''}>${escapeHtml(opponent.name)}</option>`).join('')}</select></label>
      <label>种子<input id="lab-seed" data-testid="seed-input" type="number" min="0" max="4294967295" step="1" value="${escapeHtml(selection.seed)}"${disabled}></label>
      <button type="button" data-action="new-seed"${disabled}>换个种子</button>
      <button type="button" class="primary" data-action="start" data-testid="start-battle"${disabled}>${ui.busy ? '结算中…' : ui.state?.battle ? '按此构筑重开' : '开始斗法'}</button>
    </div><p class="build-description" data-testid="build-description"><b>${escapeHtml(tierName(selection))} · ${escapeHtml(buildName(selectedBuild, selection))}</b> ${escapeHtml(description)}</p>
    <p class="draft-note" data-testid="selection-note"${ui.state?.battle && !sameSelection(selection, ui.state.selection) ? '' : ' hidden'}>参数已改变，点击「按此构筑重开」应用；当前战局仍使用原构筑。</p>`;
}

// 用途：只更新参数变更提示；参与表单输入；不替换输入框或按钮，避免失焦使下一次点击丢失。
function renderSelectionNote() {
  const note = configElement.querySelector('[data-testid="selection-note"]');
  if (note) note.hidden = !ui.state?.battle || sameSelection(ui.draft, ui.state.selection);
}

// 用途：生成气血条；参与人物与对手 HUD；传实际气血及上限，返回展示元素。
function renderHealth(hp, maximum) {
  const percent = maximum > 0 ? Math.max(0, Math.min(100, Number(hp) / Number(maximum) * 100)) : 0;
  return `<div class="actor-vital"><b>${numberText(hp)}</b><span>/ ${numberText(maximum)} 气血</span></div><div class="health-track" role="meter" aria-label="气血" aria-valuemin="0" aria-valuemax="${escapeHtml(maximum)}" aria-valuenow="${escapeHtml(Math.max(0, Number(hp) || 0))}"><i style="width:${percent}%"></i></div>`;
}

// 用途：展示已有毒火及蓄积；参与双方状态 HUD；传战况和阵营，返回状态标签。
function renderStatuses(battle, enemy) {
  const rows = enemy ? [] : battleStatusRows(battle);
  if (enemy && battle.enemyFireGrowthRounds > 0) rows.push({ label: '焰势成长', value: `${numberText(battle.enemyFireGrowthRounds)} / ${numberText(battle.fireGrowthMaxRounds)} 轮`, scope: '对方本场已兑现灼烧的成长', tone: 'good' });
  if (enemy && battle.poison > 0) rows.push({ label: '中毒', value: `${numberText(battle.poison)} 层 · 每回合 ${numberText(battle.poisonDamage)} 点`, scope: `剩 ${numberText(battle.poisonTurns)} 回合`, tone: 'bad' });
  if (enemy && battle.burn > 0) rows.push({ label: '灼烧', value: `${numberText(battle.burn)} 点 / 回`, scope: `剩 ${numberText(battle.burnTurns)} 回合`, tone: 'bad' });
  if (enemy && battle.soul > 0) rows.push({ label: '震荡', value: `${numberText(battle.soul)} 层`, scope: '神识爆发消耗', tone: 'bad' });
  if (!enemy && battle.playerBurn > 0) rows.push({ label: '受灼烧', value: `${numberText(battle.playerBurn)} 点 / 回`, scope: `剩 ${numberText(battle.playerBurnTurns)} 回合`, tone: 'bad' });
  return rows.length ? rows.map(row => `<span class="status-chip ${row.tone === 'bad' ? 'bad' : ''}" title="${escapeHtml(row.scope)}">${escapeHtml(row.label)} ${escapeHtml(row.value)}</span>`).join('')
    : '<span class="status-chip no-status">暂无持续状态</span>';
}

// 用途：生成一方立绘与生命状态；参与战斗主场景；传战况及阵营，返回人物 HUD。
function renderActor(battle, enemy) {
  const player = ui.state?.player || {};
  const name = enemy ? battle.enemy : player.name || '试炼修士';
  const portrait = enemy ? portraitPath(battle.enemyPortrait, 'assets/portraits/rogue.webp')
    : portraitPath(player.portrait, 'assets/portraits/player-male.webp');
  return `<article class="actor${enemy ? ' enemy' : ''}" data-combat-actor="${enemy ? 'enemy' : 'player'}">
    <div class="portrait-wrap"><img src="${escapeHtml(portrait)}" alt="${escapeHtml(name)}立绘" decoding="async"></div>
    <div class="actor-info"><p class="actor-label">${enemy ? '对手' : '你的构筑'}</p><h3>${escapeHtml(name)}</h3>
      <p class="actor-realm">${escapeHtml(enemy ? battle.enemyRealmName : player.realmName)}</p>
      ${renderHealth(enemy ? battle.enemyHp : battle.hp, enemy ? battle.enemyMax : battle.max)}
      <p class="shield-label">护盾 <b>${numberText(enemy ? battle.enemyShield : battle.shield)}</b>${enemy && !battle.enemyBeast && battle.enemyMana != null ? ` · 灵力 ${numberText(battle.enemyMana)}` : ''}</p>
      <div class="status-list">${renderStatuses(battle, enemy)}</div></div></article>`;
}

// 用途：提示对手行动后遗留的持续伤害；参与意图展示；传权威预演，返回下一轮毒火摘要。
function renderFutureStatuses(battle) {
  const statuses = list(battle.enemyIntentStatuses).filter(status => Number(status.perTurn) > 0 && Number(status.turns) > 0);
  if (!statuses.length) return '';
  return `<p class="intent-future" data-testid="future-dot"><b>对手行动后持续伤害</b>${statuses.map(status => `<span>${escapeHtml(status.name)} ${numberText(status.perTurn)} 点 / 回 · 剩 ${numberText(status.turns)} 回合</span>`).join('')}<small>将在下一回合结束时结算；本轮伤害为 0 也可能附加毒火。</small></p>`;
}

// 用途：选择现有功法插图作流派卡面；参与手牌展示；传大道 ID，返回固定资源地址。
function cardArt(dao) {
  return `assets/manuals/${daoArt[dao] || 'zhou'}-v1.webp`;
}

// 用途：汇总即时伤盾疗与抽牌回灵；参与手牌预览；传权威卡牌，返回数值摘要，不计算规则。
function cardPreviewNumbers(card) {
  const hasRawDamage = card.rawDamage != null && Number.isFinite(Number(card.rawDamage));
  const isAttack = Number(card.damage) > 0 || (hasRawDamage && Number(card.rawDamage) > 0)
    || ['attack', 'sword', 'human', 'wave', 'heavy', 'poisonBurst', 'fireBurst', 'soulBurst', 'drain', 'fire', 'poison'].includes(card.kind);
  const damage = isAttack ? hasRawDamage
    ? `打出 ${numberText(card.rawDamage)} / 气血 ${numberText(card.damage)}${Number(card.rawDamage) > 0 && Number(card.damage) === 0 ? ' · 护盾/减伤抵消' : ''}`
    : `气血 ${numberText(card.damage)}` : '';
  const numbers = [damage, cardNumbers(card, { showDamage: false })];
  if (Number(card.healPotential) > Number(card.heal || 0)) numbers.push(`满效疗 ${numberText(card.healPotential)}`);
  if (Number(card.manaGain) > 0) numbers.push(`回灵 ${numberText(card.manaGain)}`);
  if (Number(card.draw) > 0) numbers.push(`抽 ${numberText(card.draw)}`);
  if (Number(card.drawNext) > 0) numbers.push(`下回抽 ${numberText(card.drawNext)}`);
  return numbers.filter(Boolean).join(' · ') || '辅助神通';
}

// 用途：展示真实手牌和后端预览；参与每次战况更新；传卡及序号，返回可点击卡牌。
function renderCard(card, index) {
  const unavailable = card.playable === false || ui.busy || ui.needsRefresh;
  const generationHint = cardGenerationHint(card);
  const numbers = cardPreviewNumbers(card);
  const singleRemoval = card.requiresTarget || card.kind === 'exhaust';
  const previewText = singleRemoval ? `只移除选定的 1 张牌${Number(card.draw) > 0 ? `，抽 ${numberText(card.draw)} 张` : ''}。` : card.text;
  const rule = singleRemoval ? '选择一张其它神通牌，本场移除这张牌，其余同名牌保留；下一场恢复。' : cardRule(card);
  return `<button type="button" class="spell-card" data-elemental-generation-ready="${Boolean(generationHint)}" data-action="spell" data-card-uid="${escapeHtml(card.uid)}" data-spell-id="${escapeHtml(card.id)}" data-testid="spell-${escapeHtml(card.uid)}" aria-label="施展 ${escapeHtml(card.name)}，消耗 ${escapeHtml(card.cost)} 灵力${generationHint ? `，${generationHint}` : ''}${card.playable === false ? `，${escapeHtml(card.reason)}` : ''}"${unavailable ? ' disabled' : ''}>
    <span class="card-image"><img src="${cardArt(card.dao)}" alt="" loading="eager"><span class="card-cost">${numberText(card.cost)}</span><span class="card-type">${escapeHtml(card.dao)} · ${escapeHtml(cardKind(card))}</span></span>
    <span class="card-main"><strong>${escapeHtml(card.name)}</strong>${generationHint ? `<span class="elemental-generation-badge">${generationHint}</span>` : ''}<span class="card-numbers">${escapeHtml(numbers)}</span>
      ${previewText ? `<span class="card-preview">${escapeHtml(previewText)}</span>` : ''}<span class="card-rule">${escapeHtml(rule)}</span></span>
    <span class="card-foot">${card.playable === false ? escapeHtml(card.reason || '当前不可施展') : ui.busy ? '正在结算' : card.requiresTarget ? '点击选择移除目标' : `点击出牌 · ${index + 1}`}</span></button>`;
}

// 用途：展示战局或结果；参与启动、施术及结束回合；读取服务端快照并更新主场景。
function renderBattle() {
  const battle = ui.state?.battle;
  const result = ui.state?.result;
  if (!battle) {
    battleElement.innerHTML = result ? `<div class="result-panel${result.won ? '' : ' loss'}" data-testid="battle-result" data-won="${!!result.won}"><h2>${result.won ? '试炼获胜' : '试炼落败'}</h2><p>${escapeHtml(result.text)}</p><p class="result-stats">${escapeHtml(tierName(ui.state?.selection))} · 历时 ${numberText(result.turns)} 回合 · 种子 ${numberText(ui.state?.selection?.seed)}</p><div class="result-actions"><button type="button" class="primary" data-action="retry" data-testid="retry-battle"${ui.busy ? ' disabled' : ''}>同种子再试</button><button type="button" data-action="restart-random"${ui.busy ? ' disabled' : ''}>换种子再试</button></div></div>`
      : '<div class="intro"><h2>选一套流派，开始试炼</h2><p>真实抽牌与敌方构筑。先看敌方意图，再连续施展神通，最后结束回合。</p></div>';
    return;
  }
  const previousScroll = battleElement.querySelector('.combat-hand')?.scrollLeft || 0;
  const beastIntent = renderBeastIntent(battle);
  battleElement.innerHTML = `<div class="round-heading"><span>第 <strong>${numberText(Number(battle.turn) + 1)}</strong> / 24 回合 · ${battle.playerFirst ? '你方先手' : '敌方先手'}</span><span data-testid="active-tier">${escapeHtml(tierName(ui.state.selection))} · 试炼种子 ${numberText(ui.state.selection?.seed)} · 当前出牌 ${numberText(battle.played)} / 24</span></div>
    <div class="combat-board">${renderActor(battle, false)}<article class="intent${beastIntent ? ' has-beast-intent' : ''}" aria-label="敌方意图"><span class="intent-label">预计气血伤害</span><strong class="intent-number" data-testid="intent-damage">${numberText(battle.intentDamage ?? battle.intentRaw)}</strong><h3>${escapeHtml(battle.intentName || '敌方意图')}</h3>${beastIntent || `<p>${escapeHtml(battle.intentDescription || '结束回合后，对手行动。')}</p>`}<small>含已有持续伤害；继续出牌会改变预览${battle.intentShield ? ` · 护盾 ${numberText(battle.intentShield)}` : ''}${battle.intentHeal ? ` · 治疗 ${numberText(battle.intentHeal)}` : ''}</small>${!battle.enemyBeast ? `<small data-testid="npc-action-budget">预计行动 ${numberText(battle.enemyActionCount)} / ${numberText(battle.enemyActionLimit)} · 灵力 ${numberText(battle.enemyMana)}；抽牌、护盾和治疗也占行动</small>` : ''}${list(battle.intentSpells).length ? `<small>${list(battle.intentSpells).map(escapeHtml).join(' · ')}</small>` : ''}${renderFutureStatuses(battle)}</article>${renderActor(battle, true)}</div>
    <div class="mana-toolbar"><div class="mana-orb"><b data-testid="current-mana">${numberText(battle.mana)}</b><span>灵力</span></div><p class="mana-text">下回合灵力恢复到 <b>${numberText(battle.nextMana ?? battle.maxMana)}</b> 点<span> · 上限 ${numberText(battle.manaCap ?? battle.maxMana)}</span><small>剩余灵力默认不保留${battle.carryMana ? ` · 已封存 ${numberText(battle.carryMana)} 点` : ''}${battle.drawNext ? ` · 下回合额外抽 ${numberText(battle.drawNext)} 张` : ''}</small></p><button type="button" class="primary end-turn" data-action="end" data-testid="end-turn"${ui.busy || ui.needsRefresh ? ' disabled' : ''}>${ui.busy ? '结算中…' : '结束回合 →'}</button></div>
    <div class="hand-heading"><b>手中神通 · ${list(battle.hand).length} / ${battle.handLimit ?? '—'} 张</b><span>数字 1～8 出牌，其余点击 · E 结束回合 · 抽牌堆 ${numberText(battle.draw)} / 弃牌堆 ${numberText(battle.discard)} / 本场移除 ${numberText(battle.exhausted)}</span></div>
    <div class="combat-hand" tabindex="0" aria-label="当前手牌，可左右滚动">${list(battle.hand).map(renderCard).join('') || '<p class="empty-hand">手牌已用完，结束回合后重新抽牌。</p>'}</div>`;
  battleElement.querySelector('.combat-hand').scrollLeft = previousScroll;
}

// 用途：折叠展示实际装配与日志；参与每次战况更新；读取快照，更新详情区域。
function renderDetails() {
  if (!ui.state?.loadout) { detailsElement.innerHTML = ''; return; }
  const loadout = ui.state.loadout;
  const logs = list(ui.state.battle?.log).length ? ui.state.battle.log : list(ui.state.log);
  detailsElement.innerHTML = `<details data-detail="loadout"${ui.loadoutOpen ? ' open' : ''}><summary>本场${escapeHtml(tierName(ui.state.selection))} · ${list(loadout.spells).length} 种额外神通</summary>${loadout.notes ? `<p class="loadout-notes">${escapeHtml(loadout.notes)}</p>` : ''}<p class="loadout-title">功法</p><div class="loadout-row">${list(loadout.manuals).map(manual => `<span>${escapeHtml(manual.name)} ${numberText(manual.level)} 层 · ${escapeHtml(manual.role)}</span>`).join('') || '暂无'}</div><p class="loadout-title">神通 · 固定普通攻击、普通防御各 5 张</p><div class="loadout-row">${list(loadout.spells).map(spell => `<span>${escapeHtml(spell.name)}</span>`).join('')}</div><p class="loadout-title">悟道</p><div class="loadout-row">${list(loadout.nodes).map(node => `<span>${escapeHtml(node.name)}</span>`).join('') || '暂无'}</div>${list(loadout.unavailable).length ? `<p class="loadout-unavailable">${list(loadout.unavailable).map(escapeHtml).join('；')}</p>` : ''}</details>
    <details data-detail="log"${ui.logOpen ? ' open' : ''}><summary>战报 · ${logs.length} 条</summary><div class="battle-log">${logs.map(message => `<p>${escapeHtml(message)}</p>`).join('') || '<p>等待开战。</p>'}</div></details><p class="log-recent">${escapeHtml(logs.at(-1) || '')}</p>`;
}

// 用途：展示错误及恢复入口；参与请求失败和状态同步；读取通知，更新提示元素。
function renderNotice() {
  noticeElement.hidden = !ui.notice;
  noticeElement.innerHTML = ui.notice ? `<span>${escapeHtml(ui.notice)}</span>${ui.needsRefresh ? `<button type="button" data-action="refresh"${ui.busy ? ' disabled' : ''}>重新读取战况</button>` : ''}` : '';
}

// 用途：保持移除目标与最新战局一致；参与忘机类选牌；读取选中神通，更新或关闭对话框。
function renderTarget() {
  if (!ui.target) { if (targetDialog.open) targetDialog.close(); return; }
  const card = list(ui.state?.battle?.hand).find(candidate => candidate.uid === ui.target.uid);
  const options = list(ui.state?.battle?.removalOptions).filter(option => option.id !== card?.id && typeof option.uid === 'string' && option.uid && Number(option.copies) > 0);
  if (!card || card.playable === false || !options.length) { ui.target = null; if (targetDialog.open) targetDialog.close(); return; }
  if (!options.some(option => option.uid === ui.target.targetUid)) ui.target.targetUid = null;
  const chosen = options.find(option => option.uid === ui.target.targetUid);
  targetDialog.innerHTML = `<div class="target-heading"><h2 id="target-title">${escapeHtml(card.name)} · 选择移除目标</h2><button type="button" data-action="target-cancel">取消</button></div><p class="target-note">只移除选定的 1 张牌，其余同名牌保留。下一场恢复。确认才会消耗 ${numberText(card.cost)} 点灵力。</p><div class="target-options">${options.map((option, index) => `<button type="button" class="target-option${option.uid === chosen?.uid ? ' selected' : ''}" data-action="target-select" data-target-id="${escapeHtml(option.uid)}" aria-pressed="${option.uid === chosen?.uid}"${ui.busy ? ' disabled' : ''}><b>${escapeHtml(option.name)}</b><span>只移除这 1 张 · ${escapeHtml(option.dao)}系 · 牌 ${index + 1}</span></button>`).join('')}</div><div class="target-confirm"><span>${chosen ? `仅移除「${escapeHtml(chosen.name)}」1 张` : '先选择一张牌'}</span><button type="button" class="primary" data-action="target-confirm"${!chosen || ui.busy ? ' disabled' : ''}>确认移除并出牌</button></div>`;
  if (!targetDialog.open) targetDialog.showModal();
}

// 用途：统一更新页面；参与所有有效交互；无参数，无返回值，保留折叠与手牌滚动状态。
function render() {
  renderConfig(); renderBattle(); renderDetails(); renderNotice(); renderTarget();
}

// 用途：接受新的权威快照；参与请求响应；传状态，忽略旧 revision，返回是否更新成功。
function acceptState(nextState) {
  if (!nextState || !Number.isInteger(nextState.revision)) return false;
  if (Number.isInteger(ui.state?.revision) && nextState.revision < ui.state.revision) return false;
  ui.state = nextState;
  if (!ui.draft && nextState.selection) ui.draft = selectionDraft(nextState.selection);
  return true;
}

// 用途：串行提交真实动作或读取快照；参与启动及出牌；传路径与可选参数，返回响应或 null。
async function request(path, payload) {
  if (ui.busy) return null;
  ui.busy = true;
  const sequence = ++ui.requestSequence;
  const before = ui.state?.battle;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 15000);
  render();
  try {
    const response = await fetch(path, {
      method: payload ? 'POST' : 'GET', signal: controller.signal,
      headers: payload ? { 'Content-Type': 'application/json' } : undefined,
      body: payload ? JSON.stringify(payload) : undefined, cache: 'no-store',
    });
    const data = await response.json();
    if (sequence !== ui.requestSequence) return null;
    if (path === '/api/lab/config') {
      if (response.ok && !data.error) ui.config = data;
    }
    else acceptState(data);
    if (!response.ok || data.error) {
      ui.notice = data.error || `请求失败（${response.status}），请重试。`;
      ui.needsRefresh = !Number.isInteger(data.revision);
    } else { ui.notice = ''; ui.needsRefresh = false; }
    return data;
  } catch (error) {
    if (sequence === ui.requestSequence) {
      ui.notice = error.name === 'AbortError' ? '请求超时，请重新读取战况后再继续。' : '连接中断，请重新读取战况后再继续。';
      ui.needsRefresh = true;
    }
    return null;
  } finally {
    clearTimeout(timeout);
    if (sequence === ui.requestSequence) {
      ui.busy = false; render();
      if (path === '/api/lab/turn') animateBattleImpacts(battleImpacts(before, ui.state?.battle));
    }
  }
}

// 用途：校验玩家输入的试炼参数；参与开战；无参数，返回合法草稿或 null 并显示错误。
function checkedSelection() {
  const selection = selectionDraft(ui.draft);
  if (!selection || !availableTiers().some(tier => tier.id === selection.tier)) {
    ui.notice = '请选择后端支持的传承级别。'; renderNotice(); return null;
  }
  const seed = Number(selection.seed);
  if (String(selection.seed).trim() === '' || !Number.isInteger(seed) || seed < 0 || seed > 4294967295) {
    ui.notice = '种子请填写 0～4294967295 的整数。'; renderNotice(); return null;
  }
  selection.seed = seed;
  selection.stage = Number(selection.stage);
  return selection;
}

// 用途：在独立 RNG 上选取重试种子；参与换种子；无参数，返回 uint32 范围整数，不改变引擎随机状态。
function newSeed() {
  return crypto.getRandomValues(new Uint32Array(1))[0];
}

// 用途：开始或重试一个合法构筑；参与开战按钮；传选择快照，等待后端验证并返回无值。
async function startBattle(selection) {
  if (!selection || ui.busy) return;
  ui.target = null;
  const data = await request('/api/lab/start', { ...selectionDraft(selection), revision: ui.state?.revision });
  if (data && !data.error && data.selection) { ui.draft = selectionDraft(data.selection); renderConfig(); }
}

// 用途：施展当前可用手牌；参与点击或快捷键；传卡 UID，必要时先选目标，无返回值。
async function playCard(uid) {
  if (ui.busy || ui.needsRefresh || !ui.state?.battle) return;
  const card = list(ui.state.battle.hand).find(candidate => candidate.uid === uid);
  if (!card || card.playable === false) return;
  if (card.requiresTarget || card.kind === 'exhaust') {
    if (!list(ui.state.battle.removalOptions).some(option => option.uid && option.id !== card.id)) {
      ui.notice = '当前没有可移除的单张牌；请使用新版试炼场。'; renderNotice(); return;
    }
    ui.target = { uid, targetUid: null }; renderTarget(); return;
  }
  await request('/api/lab/turn', { action: 'spell', spell: uid, revision: ui.state.revision });
}

// 用途：分发明确的按钮动作；参与页面和目标弹窗；传 DOM 事件，等待必要请求，无返回值。
async function handleClick(event) {
  const button = event.target.closest('button[data-action]');
  if (!button || button.disabled || ui.busy) return;
  switch (button.dataset.action) {
    case 'tier':
      if (availableTiers().some(tier => tier.id === button.dataset.tierId)) {
        ui.draft.tier = button.dataset.tierId; renderConfig();
        configElement.querySelector(`[data-tier-id="${button.dataset.tierId}"]`)?.focus();
      }
      break;
    case 'build':
      ui.draft.buildId = button.dataset.buildId; renderConfig(); break;
    case 'new-seed':
      ui.draft.seed = newSeed(); renderConfig(); break;
    case 'start':
      await startBattle(checkedSelection()); break;
    case 'retry':
      await startBattle(ui.state?.selection); break;
    case 'restart-random':
      await startBattle(ui.state?.selection ? { ...ui.state.selection, seed: newSeed() } : null); break;
    case 'spell':
      await playCard(button.dataset.cardUid); break;
    case 'end':
      if (!ui.needsRefresh && ui.state?.battle) await request('/api/lab/turn', { action: 'end', revision: ui.state.revision });
      break;
    case 'refresh':
      if (!ui.config) {
        const config = await request('/api/lab/config');
        if (!config?.defaultSelection || config.error || !ui.config) {
          ui.needsRefresh = true; renderNotice(); return;
        }
        if (config?.defaultSelection && !ui.draft) ui.draft = selectionDraft(config.defaultSelection);
      }
      await request('/api/lab/state'); break;
    case 'target-cancel':
      ui.target = null; renderTarget(); break;
    case 'target-select':
      if (ui.target) { ui.target.targetUid = button.dataset.targetId; renderTarget(); } break;
    case 'target-confirm': {
      if (!ui.target?.targetUid || !ui.state?.battle) break;
      const chosen = { ...ui.target }; ui.target = null;
      await request('/api/lab/turn', { action: 'spell', spell: chosen.uid, target: chosen.targetUid, revision: ui.state.revision });
      break;
    }
  }
}

// 用途：保存控件草稿；参与切换境界、敌人和种子；传输入事件，不请求后端，无返回值。
function handleSelection(event) {
  if (!ui.draft || ui.busy) return;
  if (event.target.id === 'lab-stage') ui.draft.stage = Number(event.target.value);
  if (event.target.id === 'lab-opponent') ui.draft.opponentId = event.target.value;
  if (event.target.id === 'lab-seed') ui.draft.seed = event.target.value;
  renderSelectionNote();
}

// 用途：支持战斗快捷键；参与出牌；输入框和弹窗时不接管，传键盘事件，无返回值。
function handleKey(event) {
  const tierButton = event.target.closest('[data-tier-id]');
  if (tierButton && !ui.busy && ['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) {
    const tiers = availableTiers();
    const current = tiers.findIndex(tier => tier.id === tierButton.dataset.tierId);
    const next = event.key === 'Home' ? 0 : event.key === 'End' ? tiers.length - 1
      : (current + (event.key === 'ArrowLeft' ? -1 : 1) + tiers.length) % tiers.length;
    event.preventDefault();
    ui.draft.tier = tiers[next].id; renderConfig();
    configElement.querySelector(`[data-tier-id="${tiers[next].id}"]`)?.focus();
    return;
  }
  if (event.repeat || event.ctrlKey || event.altKey || event.metaKey || ui.busy || ui.needsRefresh || targetDialog.open
    || event.target.closest('input,select,textarea,[contenteditable="true"]')) return;
  if (/^[1-8]$/.test(event.key)) {
    const card = list(ui.state?.battle?.hand)[Number(event.key) - 1];
    if (card) { event.preventDefault(); void playCard(card.uid); }
  } else if (event.code === 'KeyE' && ui.state?.battle) {
    event.preventDefault(); void request('/api/lab/turn', { action: 'end', revision: ui.state.revision });
  }
}

// 用途：初始化流派目录和独立战况；参与首屏；无参数，读取接口后渲染，不自动开战。
async function initialize() {
  const config = await request('/api/lab/config');
  if (!config?.defaultSelection || config.error) return;
  ui.draft = selectionDraft(config.defaultSelection);
  await request('/api/lab/state');
  if (ui.state?.selection) ui.draft = selectionDraft(ui.state.selection);
  render();
}

document.addEventListener('click', handleClick);
document.addEventListener('change', handleSelection);
document.addEventListener('input', handleSelection);
document.addEventListener('keydown', handleKey);
// 用途：记忆折叠状态；参与详情操作；传 toggle 事件，无返回值。
detailsElement.addEventListener('toggle', event => {
  if (event.target.dataset.detail === 'loadout') ui.loadoutOpen = event.target.open;
  if (event.target.dataset.detail === 'log') ui.logOpen = event.target.open;
}, true);
// 用途：响应 Esc 取消目标；参与移除选择；无参数，清空待选卡，不消耗灵力。
targetDialog.addEventListener('cancel', () => { ui.target = null; });
void initialize();
