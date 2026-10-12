// 天机大比：独立的真人留影赛事。这里只展示服务端结果，不推演战斗或伪造参赛者。
import { esc } from './shell.mjs';
import { cardKind, cardRule, cardNumbers, battleStatusRows } from './cards.mjs';
import { renderBattleArtifacts, renderArtifactAbility } from './artifacts.mjs';

const phaseNames = { unavailable: '暂不可参赛', closed: '赛期未开放', eligible: '可以报名', waiting: '等待同组道友', ready: '可选择对手', fighting: '斗法进行中', settled: '本场已结算' };
const resultNames = { win: '挑战获胜', loss: '挑战落败', draw: '双方平局', forfeit: '已认输' };
const num = value => Number.isFinite(Number(value)) ? String(Number(value)) : '—';
const visibleHp = value => Number.isFinite(Number(value)) ? String(Math.max(0, Number(value))) : '—';
const stamp = value => {
  if (!value) return '尚未更新';
  const date = new Date(typeof value === 'number' && value < 1e12 ? value * 1000 : value);
  return Number.isNaN(date.getTime()) ? '刚刚' : date.toLocaleTimeString('zh-CN', { hour12: false });
};
const button = (action, label, attrs = '', primary = false) => `<button type="button" class="${primary ? 'primary' : 'subtle'}" data-tianji="${action}" ${attrs}>${label}</button>`;
const groupLabel = entry => `${esc(entry.realm || entry.group || '未分组')} · 第 ${num(entry.eventId)} 届`;
const slotNames = { weapon: '武器', armor: '防具', accessory: '饰品' };
const itemName = item => typeof item === 'string' ? item : item?.name || item?.id || '未命名';

export function renderTianjiBuild(build) {
  if (!build) return '<p class="tj-muted">配装暂未载入，请刷新会场。</p>';
  const list = (title, entries) => `<section class="tj-build-section"><h4>${title}</h4><ul>${(Array.isArray(entries) ? entries : []).map(item => `<li><b>${esc(itemName(item))}</b>${item?.level != null ? ` · ${num(item.level)} 层` : ''}${item?.dao ? ` · ${esc(item.dao)}系` : ''}${item?.grade != null ? ` · ${num(item.grade)}阶` : ''}${item?.slot ? ` · ${esc(slotNames[item.slot] || item.slot)}` : ''}${item?.shapeName || item?.element ? `<span>${esc([item.shapeName, item.element, item.polarity].filter(Boolean).join(' · '))}</span>` : ''}${item?.dimensionText ? `<span>${esc(item.dimensionText)}</span>` : item?.dimensions?.length ? `<span>器材五维：${esc(item.dimensions.join(' / '))}</span>` : ''}${item?.inscribed ? '<span>已铭刻</span>' : ''}${item?.passive?.text ? `<span>${esc(item.passive.text)}</span>` : ''}${item?.ability ? renderArtifactAbility(item.ability, { compact: true }) : ''}</li>`).join('') || '<li class="tj-muted">无</li>'}</ul></section>`;
  const stats = build.stats || {};
  return `<div class="tj-stats">${[['hp', '气血'], ['mana', '灵力'], ['power', '基础威力'], ['body', '体魄'], ['mind', '神识'], ['speed', '遁速']].map(([key, name]) => `<div><span>${name}</span><b>${num(stats[key])}</b></div>`).join('')}</div>`
    + `<div class="tj-build-grid">${list('天赋', build.traits)}${list('主修功法', build.main?.id ? [build.main] : [])}${list('辅修功法', build.aux)}${list('已修功法', build.manuals)}${list('道法节点', build.nodes)}${list('神通牌组', build.spells)}${list('装备', build.gear)}</div>`;
}

export function tianjiCardNumbers(card, battle = {}) {
  const pieces = [];
  const damageKinds = ['attack', 'sword', 'human', 'wave', 'heavy', 'poisonBurst', 'soulBurst', 'bodyBurst', 'steal', 'drain', 'fireBurst'];
  if (card.damage === 0 && damageKinds.includes(card.kind)) pieces.push('伤 0（护盾 / 减伤后）');
  if (card.heal === 0 && card.kind === 'heal') pieces.push(battle.hp >= battle.max ? '疗 0（气血已满）' : '疗 0（当前预览）');
  const numbers = cardNumbers(card);
  if (numbers) pieces.push(numbers);
  if (Number.isFinite(card.energy) && card.energy !== 0) pieces.push(`灵力 ${card.energy > 0 ? '+' : ''}${card.energy}`);
  if (Number.isFinite(card.draw) && card.draw > 0) pieces.push(`抽 ${card.draw} 张`);
  return pieces.join(' · ') || card.text || cardKind(card);
}

function renderBattle(encounter, entry, busy, compact = false) {
  const b = encounter.battle;
  if (!b) return '<p>战斗数据暂未返回，请刷新会场恢复本场。</p>';
  const active = encounter.state === 'active';
  const disabled = busy || !active;
  const health = (hp, max) => `${Math.max(0, Math.min(100, Number(max) > 0 ? Number(hp) / Number(max) * 100 : 0))}%`;
  const ownStatus = battleStatusRows(b).map(row => `<span class="status-chip ${esc(row.tone)}"><b>${esc(row.label)}：${esc(row.value)}</b><small>${esc(row.scope)}</small></span>`).join('');
  const enemyStatus = [['poison', '中毒'], ['burn', '灼烧'], ['soul', '震荡']].filter(([key]) => b[key] > 0).map(([key, label]) => `<span class="status-chip">${label} ${num(b[key])}</span>`).join('');
  const actor = (name, realm, hp, max, shield, statuses, opponent) => `<section class="combat-actor${opponent ? ' combat-enemy' : ''}"><span class="combat-label">${opponent ? '真人留影' : '你的报名留影'} · ${esc(realm)}</span><h3>${esc(name)}</h3><div class="combat-vital"><b>${visibleHp(hp)}</b><span>/ ${num(max)} 气血</span><em>盾 ${num(shield)}</em></div><div class="combat-health"><i style="width:${health(hp, max)}"></i></div><div class="combat-status">${statuses || '<span>暂无状态</span>'}</div></section>`;
  const hand = (b.hand || []).map(card => `<article class="tj-card-wrap"><button type="button" class="card card-combat${card.playable === false ? ' card-disabled' : ''}" data-tianji="play" data-id="${esc(card.uid)}" ${disabled || card.playable === false ? 'disabled' : ''}><span class="card-top"><span class="card-energy">${num(card.cost)}</span><strong>${esc(card.name)}</strong><span class="card-kind">${esc(cardKind(card))}</span></span><span class="card-school">${esc(card.dao)}系</span><span class="card-effect">${esc(tianjiCardNumbers(card, b))}</span><span class="card-rule">${esc(cardRule(card))}</span><span class="card-foot">${!active ? '本场已结束' : card.playable === false ? esc(card.reason || '当前不可用') : card.requiresTarget || card.kind === 'exhaust' ? '选择移除目标' : '点击出牌'}</span></button><details class="tj-card-rules"><summary>${esc(card.name)} · 规则</summary><p>${esc(cardRule(card)) || '按卡面数值结算。'}</p></details></article>`).join('');
  const logs = encounter.log || b.log || [];
  let artifacts = renderBattleArtifacts(b).replaceAll('data-action="artifact-play"', 'data-tianji="artifact"');
  if (disabled) artifacts = artifacts.replaceAll('<button ', '<button disabled ');
  return `<section class="tj-battle" aria-label="天机大比斗法"><header class="tj-section-head"><div><h3 data-tianji-battle-heading tabindex="-1">${active ? compact ? `对阵 ${esc(encounter.target?.name || b.enemy)}` : '手动挑战真人留影' : resultNames[encounter.result] || '本场已结算'}</h3><p>第 ${num((b.turn || 0) + 1)} / 24 回合 · ${b.playerFirst === false ? '后手' : '先手'}${compact ? '' : ' · 对方离线也可继续'}</p></div>${button('details', compact ? '对手配装' : '查看对手配装', `data-id="${esc(encounter.target?.id)}"`)}</header>`
    + (active ? `<details class="tj-battle-details" ${compact ? '' : 'open'}><summary>双方详细状态与留影意图</summary>` : '')
    + (!active ? `<div class="tj-result" role="status"><b>${esc(resultNames[encounter.result] || '已结算')}</b><span>本场积分 ${Number(encounter.scoreDelta) > 0 ? '+' : ''}${num(encounter.scoreDelta)}</span><p>胜 3 分 · 平 1 分 · 负 / 认输 0 分。战报已保存，每位对手仅计分一次。</p></div>` : '<p class="tj-note">使用报名时冻结的配装。退出页面不会认输；再次进入可继续。本场结束前，日常修行暂时暂停。</p>')
    + `<div class="combat-field tj-combat-field">${actor(entry?.name || entry?.nickname || '你', entry?.realm || '', b.hp, b.max, b.shield, ownStatus, false)}${actor(b.enemy || encounter.target?.name || '对方', encounter.target?.realm || b.enemyRealmName || '', b.enemyHp, b.enemyMax, b.enemyShield, enemyStatus, true)}</div>`
    + (active ? `<div class="tj-intent"><b>留影意图 · ${esc(b.intentName || '下一回合行动')}</b><p>${esc(b.intentDescription || '结束回合后，留影依据冻结配装行动。')}</p><span>预估伤害 ${num(b.intentDamage ?? b.intentRaw ?? 0)}${b.intentShield ? ` · 护盾 ${num(b.intentShield)}` : ''}${b.intentHeal ? ` · 治疗 ${num(b.intentHeal)}` : ''}</span></div></details>${compact ? `<div class="tj-battle-hud" aria-label="本场实时状态"><div class="tj-hud-vitals"><div><span>我方气血</span><b>${visibleHp(b.hp)} / ${num(b.max)}</b><small>盾 ${num(b.shield)}</small></div><div><span>对方气血</span><b>${visibleHp(b.enemyHp)} / ${num(b.enemyMax)}</b><small>盾 ${num(b.enemyShield)}</small></div></div><div class="tj-hud-actions"><div><b>灵力 ${num(b.mana)}</b><span>预计受伤 ${num(b.intentDamage ?? b.intentRaw ?? 0)}</span></div>${button('end', '结束回合', disabled ? 'disabled' : '', true)}</div></div>` : ''}<div class="combat-flow ${compact ? 'tj-desktop-flow' : ''}"><div class="energy-orb"><b>${num(b.mana)}</b><span>灵力</span></div><p>下回合回复 ${num(b.nextMana ?? b.maxMana)} · 上限 ${num(b.manaCap ?? 8)}<br>本回合施术 ${num(b.played)} / 24 次</p>${button('end', '结束回合 →', disabled ? 'disabled' : '', true)}</div>${compact && (b.artifacts || []).length ? `<details class="tj-artifact-drawer"><summary>法宝神通（${b.artifacts.length}）</summary>${artifacts}</details>` : compact ? '' : artifacts}<div class="combat-hand">${hand || '<p class="empty-hand">手牌已用完，结束回合后重新抽牌。</p>'}</div><div class="combat-bottom"><span>抽牌堆 ${num(b.draw)} · 弃牌堆 ${num(b.discard)} · 移除 ${num(b.exhausted)}</span>${button('forfeit', '认输并结束', disabled ? 'disabled' : '')}</div>` : '')
    + `<section class="tj-battle-log" aria-label="本场战报"><h4>最近战报</h4>${logs.slice(-3).map(line => `<p>${esc(line)}</p>`).join('') || '<p>本场尚无出牌记录。</p>'}<details><summary>完整战报（${logs.length} 条）</summary>${logs.map(line => `<p>${esc(line)}</p>`).join('')}</details></section></section>`;
}

export function renderTianjiBoard(board, { loading = false, error = '' } = {}) {
  return `<section class="tj-board" aria-label="全服天机榜"><header class="tj-section-head"><div><h3>全服天机榜</h3><p>独立赛事积分榜 · 仅查看时约每 30 秒更新</p></div>${button('refresh-board', loading ? '更新中…' : '刷新榜单', loading ? 'disabled' : '')}</header><p class="tj-muted" role="status">${board ? `更新于 ${esc(stamp(board.updatedAt))} · ${num(board.total ?? board.list?.length)} 条参赛记录` : '正在读取全服榜单…'}</p>${error ? `<p class="tj-error" role="alert">${esc(error)}</p>` : ''}`
    + (board?.list?.length ? `<ol class="tj-board-list">${board.list.map((row, index) => `<li${row.isMe ? ' class="tj-me"' : ''}><span class="tj-rank">${num(row.rank ?? index + 1)}</span><div><b>${esc(row.nickname || row.name)}</b><p>${esc(row.name)} · ${groupLabel(row)}</p><span>${num(row.wins)} 胜 · ${num(row.draws)} 平 · ${num(row.losses)} 负</span></div><strong>${num(row.score)}<small>积分</small></strong></li>`).join('')}</ol>` : board ? '<div class="tj-empty"><h4>尚无真人报名记录</h4><p>有玩家报名后，会显示在这里。会场不会用 NPC 补位。</p></div>' : '')
    + '<p class="tj-note">按各自世界年份参加对应届次，同届、同大境界的真人留影可互相挑战。不同届次与组别请结合标签查看；此榜与天骄榜分开。</p>'
    + (board?.rules?.description ? `<details class="tj-rules"><summary>榜单规则与说明</summary><p>${esc(board.rules.description)}</p>${board.rules.integrityNotice ? `<p>${esc(board.rules.integrityNotice)}</p>` : ''}</details>` : '') + '</section>';
}

export function renderTianji(status, { section = 'event', board = null, busy = false, loading = false, error = '', boardError = '', pending = false, notice = '', compact = false } = {}) {
  const w = status?.window;
  const entry = status?.entry;
  const encounter = status?.encounter;
  const opponents = status?.opponents || [];
  const compactFight = compact && encounter?.state === 'active';
  const title = `<header class="tj-heading"><div class="eyebrow">宁州同道 · 留影论道</div><h2>天机大比</h2><p>你亲自出牌，挑战其他真人玩家的报名留影。</p></header><nav class="tj-tabs" aria-label="天机大比页面">${button('section', '参赛与斗法', `data-section="event" aria-current="${section === 'event' ? 'page' : 'false'}"`)}${button('section', '全服天机榜', `data-section="board" aria-current="${section === 'board' ? 'page' : 'false'}"`)}</nav>`;
  if (section === 'board') return `<div class="tianji-hub">${title}${renderTianjiBoard(board, { loading, error: boardError })}</div>`;
  return `<div class="tianji-hub${compactFight ? ' tj-fighting' : ''}" aria-busy="${busy}">${title}${notice ? `<p class="tj-notice" role="status">${esc(notice)}</p>` : ''}${error ? `<div class="tj-error" role="alert">${esc(error)}${pending ? button('retry', '重试上次操作', busy ? 'disabled' : '') : ''}</div>` : ''}`
    + (compactFight ? renderBattle(encounter, entry, busy || pending, true) + '<details class="tj-match-meta"><summary>赛期、我的配装与其他参赛者</summary>' : '')
    + (!status ? `<section class="tj-empty"><p>${loading ? '正在读取会场…' : '会场暂未载入。'}</p>${button('refresh', loading ? '读取中…' : '重新读取', loading ? 'disabled' : '')}</section>` : `<section class="tj-window"><div><b>${esc(phaseNames[status.phase] || '会场状态')}</b><p>${w ? `你的世界第 ${num(w.year)} 年 · ${w.open ? `第 ${num(w.eventId)} 届：${num(w.startYear)}–${num(w.endYear)} 年` : `下次赛期：第 ${num(w.nextStartYear)} 年开始`}` : '尚无可参赛的存档'}</p><p>${esc(status.reason || '每 50 年开赛，报名与挑战窗口持续 10 年。')}</p></div>${button('refresh', loading ? '刷新中…' : '刷新会场', loading || busy ? 'disabled' : '')}</section>`
    + (!entry ? `<section class="tj-signup"><h3>留下这一刻的配装</h3><p>报名将冻结你的天赋、功法、道法、神通与装备。本届改装或重启一世不会改写已经留下的留影。</p><p>赛期为世界第 50–59 年、100–109 年，以此类推。对手必须是同届同组的另一位真人玩家。</p>${button('join-review', '查看配装并报名', !status.canJoin || busy ? 'disabled' : '', true)}${!status.canJoin ? `<p class="tj-muted">${esc(status.reason || '当前不满足报名条件')}</p>` : ''}</section>` : `<section class="tj-entry"><div><h3>${esc(entry.name || entry.nickname)} · 已冻结</h3><p>${groupLabel(entry)} · 第 ${num(entry.registeredYear)} 年留影</p><span>${num(entry.score)} 分 · ${num(entry.wins)} 胜 ${num(entry.draws)} 平 ${num(entry.losses)} 负 · 已挑战 ${num(entry.attempts)} 人</span></div>${button('details', '我的冻结配装', `data-id="${esc(entry.id)}"`)}</section>`)
    + (encounter && !compactFight ? renderBattle(encounter, entry, busy || pending, compact) : '')
    + (entry ? `<section class="tj-opponents"><header class="tj-section-head"><div><h3>选择真人留影</h3><p>每位对手仅可计分一次。留影自动应战，无须对方在线。</p></div><span>${opponents.length} 位</span></header>${opponents.length ? `<div class="tj-opponent-list">${opponents.map(opponent => `<article><div><h4>${esc(opponent.nickname || opponent.name)}</h4><p>${esc(opponent.name)} · ${esc(opponent.realm)}</p><span>${num(opponent.score)} 分 · ${num(opponent.wins)} 胜 ${num(opponent.draws)} 平 ${num(opponent.losses)} 负</span></div><div class="tj-actions">${button('details', '查看配装', `data-id="${esc(opponent.id)}"`)}${button('challenge-review', '挑战留影', `data-id="${esc(opponent.id)}" ${busy || pending || encounter?.state === 'active' || !opponent.canChallenge ? 'disabled' : ''}`, true)}</div>${!opponent.canChallenge && opponent.reason ? `<p class="tj-muted">${esc(opponent.reason)}</p>` : ''}</article>`).join('')}</div>` : `<div class="tj-empty"><h4>正在等待其他真人参赛</h4><p>同届、同大境界暂时没有可见的其他玩家留影。你已报名，无须重复操作；稍后刷新会场查看。</p></div>`}</section>` : ''))
    + (compactFight ? '</details>' : '')
    + `<details class="tj-rules"><summary>参赛规则与积分</summary><p>${esc(status?.rules?.description || '每 50 年开放 10 年；按各自世界年份判定，同届与同大境界分组。报名留影后配装固定，只能挑战其他真人玩家。胜 3 分、平 1 分、负或认输 0 分；每位对手只计分一次。斗法可中途离开并恢复，战报与成绩由服务器保存。')}</p>${status?.rules?.integrityNotice ? `<p>${esc(status.rules.integrityNotice)}</p>` : ''}</details></div>`;
}

// A controller owns its own transport/revisions. It never calls /api/battle/turn or replaces the base save.
export function createTianji({ onChange = () => {}, onSettled = () => {}, fetcher = (...args) => fetch(...args), doc = globalThis.document, scheduler = globalThis, requestId = () => globalThis.crypto?.randomUUID?.() || `tj-${Date.now()}-${Math.random().toString(36).slice(2)}` } = {}) {
  let status = null, board = null, section = 'event', visible = false, busy = false, loading = false;
  let error = '', boardError = '', notice = '', pending = null, timer = null, statusPromise = null, boardPromise = null, statusGeneration = 0;
  let dialog = null, dialogAction = null, returnFocus = null, historyClosing = false;
  let focusEncounterId = null, focusOnStatus = false, focusScheduled = false;
  const media = globalThis.matchMedia?.('(max-width: 690px)');
  const options = () => ({ section, board, busy, loading, error, boardError, notice, pending: !!pending, compact: media?.matches || false });
  const notify = () => onChange();
  const isVisible = () => visible && !doc?.hidden;
  const paint = () => { notify(); };
  function scheduleBattleFocus() {
    if (!focusEncounterId || historyClosing || focusScheduled) return;
    focusScheduled = true;
    const frame = globalThis.requestAnimationFrame || (callback => globalThis.setTimeout(callback, 0));
    frame(() => {
      focusScheduled = false;
      if (historyClosing) return;
      const expected = focusEncounterId; focusEncounterId = null;
      if (!isVisible() || section !== 'event' || status?.encounter?.id !== expected || status.encounter.state !== 'active') return;
      const heading = doc?.querySelector?.('[data-tianji-battle-heading]');
      heading?.focus?.({ preventScroll: true });
      heading?.scrollIntoView?.({ block: 'start', behavior: 'auto' });
    });
  }
  function focusBattle() {
    if (status?.encounter?.state !== 'active') return;
    focusEncounterId = status.encounter.id;
    scheduleBattleFocus();
  }
  async function request(path, body) {
    // 本地测试版关闭了天机大比：不发请求，交给上层的容错分支显示提示
    if (window.__NZ_LOCAL__) { const failure = new Error('本地测试版已关闭天机大比'); failure.local = true; throw failure; }
    const abort = new AbortController();
    const timeout = globalThis.setTimeout(() => abort.abort(), 15000);
    try {
      const response = await fetcher('/api/tianji/' + path, body === undefined ? { cache: 'no-store', signal: abort.signal } : { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal: abort.signal });
      const data = await response.json();
      if (!response.ok || data.ok === false) { const failure = new Error(data.error || '会场读取失败，请稍后再试'); failure.code = data.code; failure.status = response.status; throw failure; }
      return data;
    } finally { globalThis.clearTimeout(timeout); }
  }
  function stopPoll() { if (timer !== null) scheduler.clearTimeout(timer); timer = null; }
  function schedulePoll() {
    stopPoll();
    if (isVisible() && section === 'board') timer = scheduler.setTimeout(() => { timer = null; refreshBoard(); }, 30000);
  }
  function accepted(command, next) {
    if (!command) return false;
    if (command.path === 'join') return !!next.entry;
    if (command.path === 'challenge') return next.encounter?.target?.id === command.body.targetId;
    return next.encounter?.id === command.body.encounterId && next.encounter.revision > command.body.expectedRevision;
  }
  async function refreshStatus() {
    if (statusPromise) return statusPromise;
    const generation = statusGeneration;
    loading = true; paint();
    statusPromise = (async () => {
      try { const next = await request('status'); if (generation !== statusGeneration) return status; const wasActive = status?.encounter?.state === 'active'; status = next; if (wasActive && status?.encounter?.state === 'settled') onSettled(); if (accepted(pending, status)) { pending = null; error = ''; notice = '已恢复服务器最新进度，请核对后继续。'; } else if (!pending) error = ''; return status; }
      catch (failure) { if (generation === statusGeneration) error = failure.message || '连接失败，请重新读取会场'; return null; }
      finally { loading = false; statusPromise = null; paint(); if (focusOnStatus) { focusOnStatus = false; focusBattle(); } }
    })();
    return statusPromise;
  }
  async function refreshBoard() {
    if (boardPromise || !isVisible() || section !== 'board') return boardPromise;
    loading = true; paint();
    boardPromise = (async () => {
      try { board = await request('board'); boardError = ''; }
      catch (failure) { boardError = '榜单暂时无法更新：' + failure.message; }
      finally { loading = false; boardPromise = null; paint(); schedulePoll(); }
    })();
    return boardPromise;
  }
  async function mutate(path, values = {}, retry = false) {
    if (busy || (pending && !retry)) return;
    const command = retry ? pending : { path, body: { ...values, requestId: requestId() } };
    if (!command) return;
    statusGeneration++; // Ignore any older GET that arrives after this mutation.
    busy = true; error = ''; notice = ''; pending = command; paint();
    const wasActive = status?.encounter?.state === 'active';
    try {
      const next = await request(command.path, command.body); statusGeneration++; status = next; pending = null;
      notice = command.path === 'join' ? '报名成功，配装已冻结。' : command.path === 'challenge' ? '留影已就位，请亲自出牌。' : '';
      if (wasActive && status?.encounter?.state === 'settled') onSettled();
    } catch (failure) {
      statusGeneration++;
      if (statusPromise) await statusPromise; // An earlier read cannot reconcile the new command.
      if (failure.code === 'storage' || failure.status === 409 || (failure.status >= 400 && failure.status < 500)) {
        pending = null; const message = failure.message;
        await refreshStatus(); error = failure.status === 409 ? `${message} 已同步服务器进度，请核对后继续。` : message;
      } else {
        error = '连接中断，正在核对服务器进度。';
        await refreshStatus();
        if (pending) error = '未能确认上次操作结果。请重试同一操作；服务器会避免重复出牌或计分。';
      }
    } finally { busy = false; paint(); if (command.path === 'challenge' && !pending && status?.encounter?.target?.id === command.body.targetId) focusBattle(); }
  }
  function closeDialog(fromHistory = false) {
    dialogAction = null;
    if (dialog?.open) dialog.close();
    returnFocus?.focus?.();
    if (!fromHistory && globalThis.history?.state?.nzTianjiDialog) { historyClosing = true; globalThis.history.back(); }
  }
  function openDialog(title, body, action = null, label = '') {
    if (!doc || historyClosing || dialog?.open) return;
    returnFocus = doc.activeElement;
    if (!dialog) {
      dialog = doc.createElement('dialog'); dialog.id = 'tianji-modal'; dialog.setAttribute('aria-labelledby', 'tj-dialog-title');
      dialog.addEventListener('cancel', event => { event.preventDefault(); closeDialog(); });
      dialog.addEventListener('click', event => { if (event.target === dialog) closeDialog(); });
      doc.body.append(dialog);
    }
    dialogAction = action;
    dialog.innerHTML = `<div class="modal-inner tj-dialog"><header class="modal-heading"><h2 id="tj-dialog-title">${esc(title)}</h2>${button('close-dialog', '关闭', 'aria-label="关闭天机大比弹窗"')}</header>${body}${action ? `<div class="tj-dialog-actions">${button('close-dialog', '取消')}${button('confirm-dialog', esc(label), '', true)}</div>` : ''}</div>`;
    globalThis.history?.pushState({ ...globalThis.history.state, nzTianjiDialog: true }, '', globalThis.location?.href);
    dialog.showModal();
  }
  function getEntry(id) { return [status?.entry, ...(status?.opponents || []), status?.encounter?.target].find(entry => entry && String(entry.id) === String(id)); }
  async function handleClick(event) {
    const target = event.target.closest?.('[data-tianji]');
    if (!target || target.disabled) return false;
    const action = target.dataset.tianji, id = target.dataset.id;
    if (action === 'close-dialog') { closeDialog(); return true; }
    if (action === 'confirm-dialog') { const commit = dialogAction; closeDialog(); if (!busy) await commit?.(); return true; }
    if (action === 'section') { section = target.dataset.section === 'board' ? 'board' : 'event'; stopPoll(); paint(); if (section === 'board') await refreshBoard(); else { focusOnStatus = true; await refreshStatus(); } return true; }
    if (action === 'refresh') { await refreshStatus(); return true; }
    if (action === 'refresh-board') { await refreshBoard(); return true; }
    if (action === 'details') { const entry = getEntry(id); if (entry) openDialog(`${entry.name || entry.nickname} · 冻结配装`, `<p>${groupLabel(entry)} · 第 ${num(entry.registeredYear)} 年留影 · 配装版本 ${num(entry.snapshotVersion)}</p>${renderTianjiBuild(entry.build)}`); return true; }
    if (busy || pending && action !== 'retry') return true;
    if (action === 'retry') { await mutate('', {}, true); return true; }
    if (action === 'join-review' && status?.canJoin) { openDialog('确认报名并冻结配装', `<p>本届只留影一次，报名后无法替换。以下是当前配装预览，以确认时的服务器配装为准；请勿同时在其他页面改装。此后的修炼、换装和重启一世不会改写这份留影。</p>${renderTianjiBuild(status.previewBuild)}`, () => mutate('join'), '确认冻结并报名'); return true; }
    if (action === 'challenge-review') { const opponent = getEntry(id); if (opponent?.canChallenge && status?.encounter?.state !== 'active') openDialog(`挑战 ${opponent.nickname || opponent.name}`, `<p>你将手动操控自己的报名配装，挑战这位真人的冻结留影。对方无须在线。本场结算后不能重复向同一留影计分。</p>${renderTianjiBuild(opponent.build)}`, () => mutate('challenge', { targetId: opponent.id }), '开始挑战'); return true; }
    const encounter = status?.encounter;
    if (encounter?.state !== 'active') return true;
    const act = (cardId, targetId) => mutate('action', { encounterId: encounter.id, expectedRevision: encounter.revision, id: cardId, ...(targetId ? { target: targetId } : {}) });
    if (action === 'forfeit') { openDialog('确认认输？', '<p>认输将结束本场，计为负场，获得 0 分；本届不能再向这位留影重复计分。</p>', () => act('forfeit'), '确认认输'); return true; }
    if (action === 'end') { await act('end'); return true; }
    if (action === 'artifact') { if (encounter.battle?.artifacts?.some(ability => ability.id === id && ability.playable)) await act(id); return true; }
    if (action === 'play') {
      const card = encounter.battle?.hand?.find(item => String(item.uid) === String(id));
      if (!card || card.playable === false) return true;
      if (card.requiresTarget || card.kind === 'exhaust') {
        const choices = (encounter.battle.removalOptions || []).filter(item => item.id !== card.id && item.copies > 0);
        if (!choices.length) { notice = '当前没有其它可移除的神通。'; paint(); return true; }
        openDialog('选择本场移除的神通', `<p>将移除所选神通的全部副本，本场不再抽到。确认才会消耗「${esc(card.name)}」。</p><label class="tj-target-label">移除目标<select id="tj-removal-target">${choices.map(item => `<option value="${esc(item.id)}">${esc(item.name)} × ${num(item.copies)}</option>`).join('')}</select></label>`, () => act(card.uid, dialog.querySelector('#tj-removal-target').value), '确认移除并出牌');
      } else await act(card.uid);
    }
    return true;
  }
  const visibilityChanged = () => { stopPoll(); if (isVisible()) section === 'board' ? refreshBoard() : refreshStatus(); };
  const popstate = () => {
    historyClosing = false;
    if (dialog?.open) closeDialog(true);
    // Forward navigation must not leave a stale modal marker after the dialog was dismissed.
    if (globalThis.history?.state?.nzTianjiDialog) {
      const { nzTianjiDialog, ...rest } = globalThis.history.state;
      globalThis.history.replaceState(rest, '', globalThis.location?.href);
    }
    scheduleBattleFocus();
  };
  media?.addEventListener('change', paint);
  doc?.addEventListener('visibilitychange', visibilityChanged);
  globalThis.addEventListener?.('popstate', popstate);
  return {
    render: () => renderTianji(status, options()), handleClick,
    get status() { return status; }, get busy() { return busy; },
    refresh: refreshStatus,
    setVisible(next) { if (next === visible) return; visible = next; stopPoll(); if (!next) { focusEncounterId = null; focusOnStatus = false; closeDialog(); return; } focusOnStatus = section === 'event'; section === 'board' ? refreshBoard() : refreshStatus(); },
    destroy() { media?.removeEventListener('change', paint); visible = false; stopPoll(); closeDialog(); dialog?.remove(); doc?.removeEventListener('visibilitychange', visibilityChanged); globalThis.removeEventListener?.('popstate', popstate); },
  };
}

export function renderTianjiReminder(tianji) {
  if (!tianji || (!tianji.window?.open && !tianji.hasEncounter)) return '';
  return `<button class="tj-reminder" data-action="goto-tab" data-tab="tianji">${tianji.hasEncounter ? '天机大比斗法尚未结束 · 继续 →' : '天机大比期已开放 · 前往会场 →'}</button>`;
}
