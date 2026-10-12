import { esc, fmt, action, card, empty, tag, tabs, numericPreview, meter } from '../ui.mjs';
import { battleStatusRows } from '../../cards.mjs';
import { liveStatuses, combatScreen, combatCard, battleNumbers, battleCardDetail, battleIntentDetail, battleStatusDetail } from './battle.mjs';

// Tianji is a separate server-side competition. The caller must use requestId
// (not the normal save's opId) and retain it when retrying an uncertain request.
const PHASES = { unavailable: '暂不可参赛', closed: '赛期未开放', eligible: '可以报名', waiting: '等待同组道友', ready: '可以挑战', fighting: '斗法进行中', settled: '本场已结算' };
const RESULTS = { win: '挑战获胜', loss: '挑战落败', draw: '双方平局', forfeit: '已认输', pending: '斗法中' };
const list = value => Array.isArray(value) ? value : [];
const local = (label, key, value = '', disabled = false) => `<button type="button" class="btn" data-ui="${esc(key)}" data-value="${esc(value)}"${disabled ? ' disabled' : ''}>${esc(label)}</button>`;
const paragraph = text => text ? `<p class="meta">${esc(text)}</p>` : '';
const group = entry => `${entry.realm || entry.group || '未分组'} · 第 ${fmt(entry.eventId)} 届`;
const nameOf = item => typeof item === 'string' ? item : item?.name || item?.id || '未命名';

function buildDetails(build, title = '查看冻结配装') {
  if (!build) return empty('配装尚未返回，请刷新会场。');
  const stats = [['hp', '气血'], ['mana', '灵力'], ['power', '威力'], ['body', '体魄'], ['mind', '神识'], ['speed', '遁速']];
  const section = (name, items) => `<section class="stack"><h4>${esc(name)}</h4>${list(items).map(item => `<div class="list-row"><strong>${esc(nameOf(item))}</strong>${paragraph([item?.level != null ? `${fmt(item.level)} 层` : '', item?.dao ? `${item.dao}道` : '', item?.grade != null ? `${fmt(item.grade)} 阶` : '', item?.shapeName, item?.element, item?.polarity].filter(Boolean).join(' · '))}${paragraph(item?.description || item?.desc)}${paragraph(item?.dimensionText)}${paragraph(item?.passive?.text)}${item?.ability ? `<p>${esc(item.ability.name)} · 灵力 ${fmt(item.ability.cost)}</p>${paragraph(item.ability.description)}${numericPreview(item.ability.preview)}` : ''}</div>`).join('') || empty('暂无')}</section>`;
  return `<details class="stack"><summary>${esc(title)}</summary><div class="grid-2">${stats.map(([key, label]) => `<div class="meta">${label} <strong>${build.stats?.[key] == null ? '—' : fmt(build.stats[key])}</strong></div>`).join('')}</div>${section('天赋', build.traits)}${section('主修功法', build.main?.id ? [build.main] : [])}${section('辅修功法', build.aux)}${section('已修功法', build.manuals)}${section('道法节点', build.nodes)}${section('神通牌组', build.spells)}${section('装备', build.gear)}</details>`;
}

function battleStatus(battle, enemy = false) {
  if (!enemy) return battleStatusRows(battle).map(item => tag(`${item.label} ${item.value}`)).join('') || '<span class="meta">暂无叠层状态</span>';
  return [['poison', '中毒'], ['burn', '灼烧'], ['soul', '震荡']].filter(([key]) => Number(battle[key]) > 0).map(([key, label]) => tag(`${label} ${fmt(battle[key])}`)).join('') || '<span class="meta">暂无叠层状态</span>';
}

function currentNumbers(item, compact = false) {
  const fields = [['damage', '伤害'], ['shield', '护盾'], ['heal', '治疗'], ['energy', '灵力'], ['qi', '势']];
  const pieces = fields.filter(([key]) => Number(item[key]) !== 0 && Number.isFinite(Number(item[key]))).map(([key, label]) => `${label} ${fmt(item[key])}`);
  if (item.damage === 0 && ['attack', 'sword', 'human', 'wave', 'heavy', 'poisonBurst', 'soulBurst', 'bodyBurst', 'drain', 'steal', 'fireBurst'].includes(item.kind)) pieces.unshift('伤害 0');
  if (item.heal === 0 && item.kind === 'heal') pieces.push('治疗 0');
  return `<p>${esc(pieces.join(' · ') || item.text || '按卡面效果结算')}</p>${!compact && item.text && pieces.length ? paragraph(item.text) : ''}`;
}

function renderSettledBattle(encounter, entry, ui) {
  const battle = encounter.battle;
  const active = encounter.state === 'active';
  if (!battle) return card('天机斗法', empty('战斗数据尚未返回，请刷新会场恢复进度。'));
  const locked = !!ui.busy || !!ui.tianjiPending || !active;
  const bodyFor = (id, target) => ({ encounterId: encounter.id, expectedRevision: encounter.revision, id, ...(target ? { target } : {}) });
  const play = (label, id, disabled = false, target, primary = false) => action(label, 'tianji/action', bodyFor(id, target), { disabled: locked || disabled, className: primary ? 'primary' : '' });
  const actor = (name, hp, max, shield, enemy = false) => `<div class="actor${enemy ? ' enemy' : ''}"><div class="row"><strong>${esc(name)}</strong>${tag(`护盾 ${fmt(shield)}`)}</div>${meter(Math.max(0, Number(hp) || 0), max, '气血', enemy ? 'danger' : '')}</div>`;
  const hand = list(battle.hand).map(item => {
    const requiresTarget = item.requiresTarget || item.kind === 'exhaust';
    const targets = list(battle.removalOptions).filter(target => target.id !== item.id && Number(target.copies) > 0);
    const button = requiresTarget
      ? `<details><summary>选择移除目标</summary><p class="meta">移除所选神通的全部副本，本场不再抽到。确认后才会消耗本牌。</p><div class="stack">${targets.map(target => play(`确认移除 ${target.name} × ${fmt(target.copies)}`, item.uid, item.playable === false, target.id)).join('') || empty('没有其他可移除的神通。')}</div></details>`
      : play('施展', item.uid, item.playable === false, undefined, true);
    return `<article class="spell${item.playable === false ? ' unavailable' : ''}"><div class="row"><span class="energy">${fmt(item.cost)}</span><strong>${esc(item.name)}</strong>${item.dao ? tag(`${item.dao}道`) : ''}</div>${currentNumbers(item, true)}${item.playable === false ? `<p class="meta">${esc(item.reason || '当前不可施展')}</p>` : ''}${button}<details class="card-rule"><summary>招式说明</summary>${paragraph(item.description)}${paragraph(item.text)}</details></article>`;
  }).join('');
  const artifacts = list(battle.artifacts).map(item => `<article class="spell"><div class="row"><strong>${esc(item.name)}</strong>${tag(`灵力 ${fmt(item.cost)}`)}</div>${currentNumbers(item)}${paragraph(item.description)}<p class="meta">冷却 ${fmt(item.cooldown)} 回合${Number(item.cooldownRemaining) > 0 ? ` · 剩余 ${fmt(item.cooldownRemaining)} 回合` : ' · 已就绪'}</p>${item.playable !== true ? paragraph(item.reason || '当前不可施展') : ''}${play('施展法宝', item.id, item.playable !== true)}</article>`).join('');
  const logs = list(encounter.log || battle.log);
  const statusRows = battleStatusRows(battle);
  const heading = `<div class="row"><h2 class="section-title">${esc(active ? '留影斗法' : RESULTS[encounter.result] || '本场已结束')}</h2>${tag(`第 ${fmt((Number(battle.turn) || 0) + 1)} / 24 回合`)}</div>`;
  const result = active ? '' : `<p class="notice" role="status">本场积分 ${Number(encounter.scoreDelta) > 0 ? '+' : ''}${fmt(encounter.scoreDelta)}。每位对手本届只计分一次。</p>`;
  const arena = `<section class="card combat-board"><div class="combat-actors">${actor(entry?.name || entry?.nickname || '你', battle.hp, battle.max, battle.shield)}${actor(battle.enemy || encounter.target?.name || '对方', battle.enemyHp, battle.enemyMax, battle.enemyShield, true)}</div>${active ? `<details class="intent"><summary>${esc(battle.intentName || '留影意图')} · 预计伤害 ${fmt(battle.intentDamage ?? battle.intentRaw ?? 0)}</summary>${paragraph(battle.intentDescription)}<p class="meta">预估护盾 ${fmt(battle.intentShield ?? 0)} · 预估治疗 ${fmt(battle.intentHeal ?? 0)}${list(battle.intentSpells).length ? ` · 神通：${list(battle.intentSpells).map(esc).join('、')}` : ''}</p></details>` : ''}${liveStatuses(battle)}</section>`;
  const toolbar = active ? `<div class="battle-toolbar"><div><strong>${fmt(battle.mana)} / ${fmt(battle.manaCap ?? 8)} 灵力</strong><p class="meta">下回合回复 ${fmt(battle.nextMana ?? battle.maxMana)} · 抽牌堆 ${fmt(battle.draw)}</p></div>${play('结束回合', 'end', false, undefined, true)}</div>` : '';
  const rules = `<details><summary>本场规则与牌堆</summary><p class="meta">${battle.playerFirst === false ? '后手' : '先手'} · 本回合施术 ${fmt(battle.played)} / 24 次</p><p class="meta">抽牌堆 ${fmt(battle.draw)} · 弃牌堆 ${fmt(battle.discard)} · 已移除 ${fmt(battle.exhausted)}</p><p>使用报名时冻结的配装。离开页面可继续本场，不会自动认输；日常修行暂时暂停。</p><p>胜 3 分、平 1 分、负或认输 0 分；每位对手本届仅计分一次。</p></details>`;
  const statuses = `<details><summary>双方状态 · 我方 ${fmt(statusRows.length)} 项</summary>${statusRows.map(item => `<p><strong>${esc(item.label)} · ${esc(item.value)}</strong><br><span class="meta">${esc(item.scope)}</span></p>`).join('') || empty('我方暂无持续增益。')}<p class="meta">对方留影状态</p><div class="row">${battleStatus(battle, true)}</div></details>`;
  const history = `<details><summary>完整战报 · ${fmt(logs.length)} 条</summary><div class="bounded-log">${logs.map(line => `<p class="meta">${esc(line)}</p>`).join('') || empty('尚无出牌记录。')}</div></details>`;
  const forfeit = active ? `<details><summary>认输并结束本场</summary><p>认输计为负场，获得 0 分；本届不能再向同一留影重复计分。</p>${play('确认认输', 'forfeit')}</details>` : '';
  return `<section class="tianji-battle">${heading}${result}${arena}${toolbar}${active ? `<h3 class="section-title">手中神通</h3><div class="spell-grid hand">${hand || empty('手牌已用完。结束回合后重新抽牌。')}</div>${artifacts ? `<details class="card artifact-disclosure"><summary>法宝神通 · ${fmt(list(battle.artifacts).length)} 件</summary><div class="spell-grid">${artifacts}</div></details>` : ''}` : ''}<div class="card stack">${logs.length?`<p class="meta">${esc(logs.at(-1))}</p>`:''}${statuses}${history}${rules}${encounter.target?.build ? buildDetails(encounter.target.build, '对手冻结配装') : ''}${forfeit}</div></section>`;
}

function encounterPlay(encounter, ui, label, id, disabled = false, target, primary = false) {
  const locked = !!ui.busy || !!ui.tianjiPending || encounter.state !== 'active';
  return action(label,'tianji/action',{encounterId:encounter.id,expectedRevision:encounter.revision,id,...(target ? {target} : {})},{disabled:locked || disabled,className:primary ? 'primary' : ''});
}

function handPlay(encounter, ui, item) {
  return item.requiresTarget || item.kind === 'exhaust'
    ? local('选目标','tianji-detail',`target:${item.uid}`,!!ui.busy || !!ui.tianjiPending || item.playable === false || encounter.state !== 'active')
    : encounterPlay(encounter,ui,'施展',item.uid,item.playable === false,undefined,true);
}

export function renderTianjiBattleDetail(status, ui = {}, key = '') {
  const encounter = status?.encounter;
  const b = encounter?.battle;
  if (!b) return null;
  if (key.startsWith('card:')) {
    const item = list(b.hand).find(c=>c.uid === key.slice(5));
    return item ? {title:item.name,content:battleCardDetail(item,handPlay(encounter,ui,item))} : null;
  }
  if (key.startsWith('target:')) {
    const item = list(b.hand).find(c=>c.uid === key.slice(7));
    if (!item || !(item.requiresTarget || item.kind === 'exhaust')) return null;
    const targets = list(b.removalOptions).filter(target=>target.id !== item.id && Number(target.copies)>0);
    return {title:'移除哪种神通？',content:`<div class="stack"><p>确认后消耗本牌与 ${fmt(item.cost)} 灵力，将所选神通的全部副本移出本场。</p>${targets.map(target=>encounterPlay(encounter,ui,`确认移除 ${target.name} × ${fmt(target.copies)}`,item.uid,item.playable === false,target.id)).join('') || empty('没有其他可移除的神通。')}</div>`};
  }
  if (key === 'intent') return {title:'留影下回合意图',content:battleIntentDetail(b)};
  if (key === 'status') return {title:'双方状态',content:battleStatusDetail(b)};
  if (key === 'log') return {title:'完整战报',content:`<div class="combat-log">${list(encounter.log || b.log).map(line=>`<p>${esc(line)}</p>`).join('') || empty('尚无出牌记录。')}</div>`};
  if (key === 'artifacts') return {title:'法宝神通',content:`<div class="stack">${list(b.artifacts).map(item=>`<article class="spell"><div class="row"><strong>${esc(item.name)}</strong>${tag(`灵力 ${fmt(item.cost)}`)}</div><strong class="spell-result">${esc(battleNumbers(item))}</strong>${paragraph(item.description)}${paragraph(item.text)}<p class="meta">冷却 ${fmt(item.cooldown)} 回合${Number(item.cooldownRemaining)>0 ? ` · 剩余 ${fmt(item.cooldownRemaining)} 回合` : ' · 已就绪'}</p>${item.playable !== true ? paragraph(item.reason || '当前不可施展') : ''}${encounterPlay(encounter,ui,'施展法宝',item.id,item.playable !== true,undefined,true)}</article>`).join('') || empty('本场没有法宝神通。')}</div>`};
  if (key === 'forfeit') return {title:'认输并结束本场',content:`<div class="stack"><p>认输计为负场，获得 0 分；本届不能再向同一留影重复计分。</p>${encounterPlay(encounter,ui,'确认认输','forfeit')}</div>`};
  if (key === 'more') return {title:'留影战局',content:`<div class="stack"><div class="grid-2">${local('双方状态','tianji-detail','status')}${local('完整战报','tianji-detail','log')}</div><p>第 ${fmt((Number(b.turn)||0)+1)} / 24 回合 · ${b.playerFirst === false ? '后手' : '先手'}<br>本回合施术 ${fmt(b.played)} / 24 次<br>抽牌堆 ${fmt(b.draw)} · 弃牌堆 ${fmt(b.discard)} · 已移除 ${fmt(b.exhausted)}</p><p>使用报名时冻结的配装。离开页面可继续本场，不会自动认输；日常修行暂时暂停。</p><p>胜 3 分、平 1 分、负或认输 0 分；每位对手本届仅计分一次。</p>${battleStatusDetail(b)}${status.entry?.build ? buildDetails(status.entry.build,'我的冻结配装') : ''}${encounter.target?.build ? buildDetails(encounter.target.build,'对手冻结配装') : ''}${local('刷新会场','refresh-tianji','',!!ui.busy)}${local('查看全服天机榜','tianji-tab','board')}${local('认输并结束本场','tianji-detail','forfeit')}</div>`};
  return null;
}

function renderBattle(encounter, entry, ui) {
  if (encounter.state !== 'active') return renderSettledBattle(encounter,entry,ui);
  const b = encounter.battle;
  if (!b) return card('天机斗法',empty('战斗数据尚未返回，请刷新会场恢复进度。'));
  const recovery = ui.tianjiPending ? `<p>操作结果待确认</p>${local('重试同一操作','retry-tianji','',!!ui.busy)}` : '';
  return combatScreen(b,{
    title:'留影斗法',subtitle:'天机大比 · 最多 24 回合',
    playerName:entry?.name || entry?.nickname || '你',enemyName:b.enemy || encounter.target?.name || encounter.target?.nickname || '对方',
    detailUI:'tianji-detail',
    hand:list(b.hand).map(item=>combatCard(item,handPlay(encounter,ui,item),'tianji-detail')).join(''),
    endTurn:encounterPlay(encounter,ui,'结束回合','end',false,undefined,true),
    logs:list(encounter.log || b.log),recovery
  });
}

function renderBoard(board, ui) {
  return card('全服天机榜', `<div class="stack"><p class="meta">真人留影赛事积分榜。不同届次与境界分组请结合标签查看。</p>${local('刷新榜单', 'refresh-tianji-board', '', !!ui.busy)}${board?.error ? `<p class="notice" role="alert">${esc(board.error)}</p>` : ''}${!board ? empty('榜单尚未载入。') : list(board.list).length ? `<ol class="list">${list(board.list).map((entry, index) => `<li class="list-row"><div class="row"><strong>${fmt(entry.rank ?? index + 1)}. ${esc(entry.nickname || entry.name)}</strong>${tag(`${fmt(entry.score)} 分`)}</div><p class="meta">${esc(group(entry))}</p><p class="meta">${fmt(entry.wins)} 胜 · ${fmt(entry.draws)} 平 · ${fmt(entry.losses)} 负</p></li>`).join('')}</ol>` : empty('尚无真人报名记录。会场不会用 NPC 补位。')}${paragraph(board?.rules?.description)}</div>`);
}

export function renderTianji(state = {}, ui = {}, data = {}) {
  const status = data.tianji;
  const navigation = tabs('tianji', [['event', '参赛与斗法'], ['board', '全服天机榜']], ui.tianjiTab || 'event');
  if (ui.tianjiTab === 'board') return navigation + renderBoard(data.tianjiBoard, ui);
  if (!status) return navigation + card('天机大比', empty('会场尚未载入。') + local('读取会场', 'refresh-tianji', '', !!ui.busy) + (ui.tianjiPending ? local('重试同一操作', 'retry-tianji', '', !!ui.busy) : ''));
  const window = status.window;
  const entry = status.entry;
  const encounter = status.encounter;
  const active = encounter?.state === 'active';
  const disabled = !!ui.busy || !!ui.tianjiPending;
  const phase = card('天机大比', `<div class="stack"><div class="row">${tag(PHASES[status.phase] || '会场状态')}${local('刷新会场', 'refresh-tianji', '', !!ui.busy)}</div>${window ? `<p>世界第 ${fmt(window.year)} 年 · ${window.open ? `第 ${fmt(window.eventId)} 届，${fmt(window.startYear)}–${fmt(window.endYear)} 年` : `下次赛期：第 ${fmt(window.nextStartYear)} 年`}</p>` : ''}${paragraph(status.reason)}${status.error ? `<p class="notice" role="alert">${esc(status.error)}</p>` : ''}${ui.tianjiPending ? '<p class="notice">上次操作结果仍待确认，请同步服务器进度后再操作。</p>' + local('重试同一操作', 'retry-tianji', '', !!ui.busy) : ''}</div>`);
  const registration = entry
    ? card('我的报名留影', `<div class="stack"><strong>${esc(entry.name || entry.nickname)}</strong><p class="meta">${esc(group(entry))} · 第 ${fmt(entry.registeredYear)} 年冻结</p><div class="row">${tag(`${fmt(entry.score)} 积分`)}${tag(`${fmt(entry.wins)} 胜 ${fmt(entry.draws)} 平 ${fmt(entry.losses)} 负`)}</div>${paragraph(status.entryNotice)}${buildDetails(entry.build)}</div>`)
    : card('留下这一刻的配装', `<div class="stack"><p>本届只报名一次。天赋、功法、道法、神通和装备会冻结，后续改装或重启一世都不会替换这份留影。</p>${status.previewBuild ? buildDetails(status.previewBuild, '报名前核对当前配装') : ''}${action('确认冻结配装并报名', 'tianji/join', {}, { disabled: disabled || !status.canJoin, className: 'primary' })}${!status.canJoin ? paragraph(status.reason || '当前不满足报名条件') : ''}</div>`);
  const opponents = entry ? card('同届同组真人留影', `<div class="stack"><p class="meta">每位对手仅计分一次。对方无须在线。</p>${list(status.opponents).map(opponent => `<article class="list-row stack"><div class="row"><strong>${esc(opponent.nickname || opponent.name)}</strong>${tag(opponent.realm || opponent.group || '未分组')}${tag(`${fmt(opponent.score)} 分`)}</div><p class="meta">${fmt(opponent.wins)} 胜 · ${fmt(opponent.draws)} 平 · ${fmt(opponent.losses)} 负${opponent.result ? ` · ${esc(RESULTS[opponent.result] || opponent.result)}` : ''}</p>${buildDetails(opponent.build, '核对对手配装')}<details><summary>挑战这位留影</summary><p class="meta">开始后使用你的冻结配装手动出牌；本届不能重复向此留影计分。</p>${action('确认开始挑战', 'tianji/challenge', { targetId: opponent.id }, { disabled: disabled || active || !opponent.canChallenge, className: 'primary' })}</details>${!opponent.canChallenge ? paragraph(opponent.reason) : ''}</article>`).join('') || empty('同届同境界尚无其他真人报名。你已报名，无须重复操作。')}</div>`) : '';
  const rules = `<details class="card"><summary>赛期与积分规则</summary>${paragraph(status.rules?.description || '每 50 年开放 10 年，同届同大境界分组。胜 3 分、平 1 分、负或认输 0 分，每位对手仅计分一次。')}${paragraph(status.rules?.integrityNotice)}</details>`;
  if (active) return `<div class="tianji-live">${renderBattle(encounter, entry, ui)}</div>`;
  return `<div class="stack">${navigation}${active ? renderBattle(encounter, entry, ui) : ''}${phase}${registration}${encounter && !active ? renderBattle(encounter, entry, ui) : ''}${opponents}${rules}</div>`;
}
