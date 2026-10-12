import {esc,fmt,card,action,meter,tag,empty,uiButton} from '../ui.mjs';
import {battleStatusRows,cardKind,cardRule} from '../../cards.mjs';

const list = value => Array.isArray(value) ? value : [];

// Compact readings only: costs, outcomes and turn recovery remain server-owned.
export function liveStatuses(b, rows = battleStatusRows(b)) {
  const chips = [];
  const add = (value, label, text, bad = false) => {
    if (!(Number(value) > 0)) return;
    const row = rows.find(item => item.label === label);
    chips.push(`<span class="badge${bad ? ' danger' : ''}"${row ? ` title="${esc(`${row.label} ${row.value} · ${row.scope}`)}"` : ''}>${esc(text)}</span>`);
  };
  add(b.swordQi, '剑气', `剑气 ${fmt(b.swordQi)}`);
  add(b.charge, '重势', `重势 ${fmt(b.charge)}`);
  add(b.water, '水势', `水势 ${fmt(b.water)}`);
  add(b.soul, '震荡', `震荡 ${fmt(b.soul)}`);
  add(b.manaPenalty, '夺灵代价', `回灵 −${fmt(b.manaPenalty)}`, true);
  add(b.lock, '锁灵', `锁灵 ${fmt(b.lock)} 回`, true);
  add(b.playerPoison, '受毒', `受毒 ${fmt(b.playerPoison)}/回`, true);
  add(b.weakened ?? b.enemyWeaken, '扰灵', `扰灵 ${fmt(b.weakened ?? b.enemyWeaken)} 层`, true);
  add(b.poison, '敌方中毒', `敌毒 ${fmt(b.poison)} 层`);
  add(b.burn, '敌方灼烧', `敌灼烧 ${fmt(b.burn)}/回`);
  return chips.length ? `<div class="status-inline" aria-label="当前关键战况">${chips.join('')}</div>` : '';
}

export function battleNumbers(item) {
  const fields = [['damage', '伤'], ['shield', '盾'], ['heal', '疗'], ['qi', '势'], ['energy', '灵力净变']];
  const values = fields.filter(([key]) => Number.isFinite(Number(item[key])) && Number(item[key]) !== 0).map(([key, label]) => `${label} ${fmt(item[key])}`);
  if (item.damage === 0 && ['attack','sword','human','wave','heavy','poisonBurst','soulBurst','bodyBurst','drain','steal','fireBurst'].includes(item.kind)) values.unshift('伤 0');
  if (item.heal === 0 && item.kind === 'heal') values.push('疗 0');
  return values.join(' · ') || item.text || '按招式说明结算';
}

export function combatActor(name, hp, max, shield, enemy = false) {
  return `<div class="actor${enemy ? ' enemy' : ''}"><div class="actor-identity"><strong title="${esc(name)}">${esc(name)}</strong><span>盾 ${fmt(shield ?? 0)}</span></div>${meter(Math.max(0, Number(hp) || 0), max, '气血', enemy ? 'danger' : '')}</div>`;
}

export function combatCard(item, play, detailUI = 'combat-detail') {
  return `<article class="spell${item.playable === false ? ' unavailable' : ''}" data-card-uid="${esc(item.uid)}"><div class="combat-card-heading"><span class="energy" aria-label="灵力消耗 ${esc(item.cost)}">${fmt(item.cost)}</span><strong>${esc(item.name)}</strong></div><p class="spell-result">${esc(battleNumbers(item))}</p><p class="combat-card-meta">${esc(item.playable === false ? item.reason || '当前不可施展' : cardKind(item))}</p><p class="combat-card-summary">${esc(cardRule(item) || item.text || '')}</p><div class="combat-card-actions">${play}${uiButton('详情',detailUI,`card:${item.uid}`,'combat-card-detail')}</div></article>`;
}

// The phone screen is a bounded action loop. Only its hand scrolls horizontally;
// full rules, target selection, logs and secondary actions use the shared dialog.
export function combatScreen(b, options) {
  const {title,subtitle='',playerName='你',enemyName=b.enemy,hand='',endTurn='',detailUI='combat-detail',artifacts=list(b.artifacts),logs=list(b.log),recovery=''} = options;
  const mana = `${fmt(b.mana)}${b.manaCap != null ? ` / ${fmt(b.manaCap)}` : ''}`;
  return `<section class="combat-screen" data-lethal="${!!b.lethal}" aria-label="${esc(title)}"><header class="combat-heading"><div><h1>${esc(title)}</h1><p>第 ${fmt((Number(b.turn) || 0) + 1)} 回合 · ${esc(subtitle || '月历暂停')}</p></div>${uiButton('战况',detailUI,'more')}</header><section class="combat-board" aria-label="双方气血与护盾"><div class="combat-actors">${combatActor(playerName,b.hp,b.max,b.shield)}${combatActor(enemyName,b.enemyHp,b.enemyMax,b.enemyShield,true)}</div><button type="button" class="combat-intent" data-ui="${esc(detailUI)}" data-value="intent" aria-label="查看对手意图详情"><span><b>${esc(b.intentName || '对手意图')}</b><span>预计伤害 <strong>${fmt(b.intentDamage ?? b.intentRaw ?? 0)}</strong></span></span><span aria-hidden="true">›</span></button><div class="combat-status-slot">${liveStatuses(b) || '<span class="meta">暂无叠层状态</span>'}</div></section><button type="button" class="combat-feedback" data-ui="${esc(detailUI)}" data-value="log" aria-label="查看完整战报"><span class="combat-feedback-label">最新</span><span role="status" aria-live="polite">${esc(logs.at(-1) || '斗法开始。选择神通，查看即时战果。')}</span><span aria-hidden="true">›</span></button><section class="combat-hand-region" aria-label="手中神通"><div class="combat-hand-heading"><h2>手中神通 <span>${fmt(list(b.hand).length)} 张</span></h2><span>左右滑动选牌</span></div><div class="spell-grid hand" tabindex="0" aria-label="手牌横向列表，可左右滑动或用方向键滚动">${hand || empty('手牌已用完，结束回合后重新抽牌。')}</div></section><div class="battle-toolbar"><div class="combat-mana"><strong>${mana} <span>灵力</span></strong><span>下回合回复 ${fmt(b.nextMana ?? b.maxMana)}</span></div>${artifacts.length ? uiButton(`法宝 ${fmt(artifacts.length)}`,detailUI,'artifacts','combat-artifacts') : ''}${endTurn}</div>${recovery ? `<div class="combat-recovery">${recovery}</div>` : ''}</section>`;
}

export function battleStatusDetail(b) {
  const rows = battleStatusRows(b);
  return `<div class="stack">${rows.map(x => `<p><strong>${esc(x.label)} · ${esc(x.value)}</strong><br><span class="meta">${esc(x.scope)}</span></p>`).join('') || empty('我方暂无持续增益。')}<p>敌方中毒 ${fmt(b.poison || 0)} 层 · 剩 ${fmt(b.poisonTurns || 0)} 回合<br>敌方灼烧 ${fmt(b.burn || 0)}/回 · 剩 ${fmt(b.burnTurns || 0)} 回合<br>震荡 ${fmt(b.soul || 0)} 层</p></div>`;
}

export function battleIntentDetail(b) {
  return `<div class="stack"><p><strong>${esc(b.intentName || '对手意图')}</strong></p><p>${esc(b.intentDescription || '结束回合后，对手行动。')}</p><p>预计伤害 ${fmt(b.intentDamage ?? b.intentRaw ?? 0)} · 护盾 ${fmt(b.intentShield ?? 0)} · 治疗 ${fmt(b.intentHeal ?? 0)}</p>${list(b.intentSpells).length ? `<p>神通：${list(b.intentSpells).map(esc).join('、')}</p>` : ''}</div>`;
}

export function battleCardDetail(item, play) {
  return `<div class="stack"><div class="row">${tag(cardKind(item))}${tag(`灵力 ${fmt(item.cost)}`)}</div><strong class="spell-result">${esc(battleNumbers(item))}</strong><p>${esc(cardRule(item) || item.text || '暂无额外说明。')}</p>${item.text && item.text !== item.description ? `<p>${esc(item.text)}</p>` : ''}${item.playable === false ? `<p class="notice">${esc(item.reason || '当前不可施展')}</p>` : ''}${play}</div>`;
}

const ordinaryPlay = (item) => item.requiresTarget || item.kind === 'exhaust'
  ? `<button type="button" class="btn primary" data-ui="battle-target" data-value="${esc(item.uid)}"${item.playable === false ? ' disabled' : ''}>选目标</button>`
  : action('施展','battle/turn',{id:item.uid},{disabled:item.playable === false,className:'primary'});

export function renderBattleDetail(s, key) {
  const b = s?.battle;
  if (!b) return null;
  if (key.startsWith('card:')) {
    const item = list(b.hand).find(c => c.uid === key.slice(5));
    return item ? {title:item.name,content:battleCardDetail(item,ordinaryPlay(item))} : null;
  }
  if (key === 'intent') return {title:'对手下回合意图',content:battleIntentDetail(b)};
  if (key === 'status') return {title:'双方状态',content:battleStatusDetail(b)};
  if (key === 'log') return {title:'完整战报',content:`<div class="combat-log">${list(b.log).map(t=>`<p>${esc(t)}</p>`).join('') || empty('暂无出牌记录。')}</div>`};
  if (key === 'artifacts') return {title:'法宝神通',content:`<div class="stack">${list(b.artifacts).map(a=>`<article class="spell"><div class="row"><strong>${esc(a.name)}</strong>${tag(`灵力 ${fmt(a.cost)}`)}</div><strong class="spell-result">${esc(battleNumbers(a))}</strong><p>${esc(a.description || a.text || '')}</p><p class="meta">冷却 ${fmt(a.cooldown)} 回合 · ${Number(a.cooldownRemaining)>0 ? `剩余 ${fmt(a.cooldownRemaining)} 回合` : '已就绪'}</p>${a.playable === false ? `<p class="meta">${esc(a.reason || '当前不可用')}</p>` : ''}${action('施展法宝','battle/turn',{id:a.id},{disabled:a.playable === false,className:'primary wide'})}</article>`).join('') || empty('本场没有法宝神通。')}</div>`};
  if (key === 'more') return {title:'战局与退路',content:`<div class="stack">${b.lethal ? '<p class="notice danger">战败可能结束这一世。请留意对手意图与气血。</p>' : '<p>切磋期间月历暂停，退出后回到日常修行。</p>'}${s.dungeons?.run ? `<p>${esc(s.dungeons.run.siteName)} · 秘境中的斗法</p>` : ''}<div class="grid-2">${uiButton('双方状态','combat-detail','status')}${uiButton('完整战报','combat-detail','log')}</div><p>抽牌堆 ${fmt(b.draw)} · 弃牌堆 ${fmt(b.discard)} · 已移除 ${fmt(b.exhausted)}<br>本回合已施术 ${fmt(b.played)} 次</p>${battleStatusDetail(b)}<div class="divider"></div>${action(b.lethal ? `尝试逃离 · ${fmt((b.escapeChance || 0) * 100)}%` : '退出切磋','battle/turn',{id:'escape'},{className:'subtle wide'})}</div>`};
  return null;
}

export function renderBattle(s) {
  const b = s.battle;
  if (!b) return '';
  return combatScreen(b,{
    title:b.lethal ? '生死斗法' : '切磋历练',
    subtitle:b.lethal ? '战败可能结束此生' : s.dungeons?.run?.siteName || '月历暂停',
    playerName:s.player?.name || '你',
    hand:list(b.hand).map(c=>combatCard(c,ordinaryPlay(c))).join(''),
    endTurn:action('结束回合','battle/turn',{id:'end'},{className:'primary combat-end-turn'})
  });
}
export function renderDungeon(s){const r=s.dungeons?.run;if(!r)return '';if(s.battle)return renderBattle(s);
  const reward=['reward','camp','event','merchant','chest','choice'].includes(r.phase);
  return `<div class="page-heading"><div><p class="eyebrow">${esc(r.realmName)} · 秘境历练</p><h1>${esc(r.siteName)}</h1></div>${tag(`气血 ${fmt(r.health)}%`)}</div><p class="notice">提前离开后入口消失，主动退出可带回已得物资。</p>
    ${reward?card(r.cleared&&!r.bossRewardClaimed?'通关传承 · 选择主奖励':'眼前的抉择',`<p>${esc(r.text||'选择一项，继续向前。')}</p><div class="stack">${(r.rewards||[]).map(x=>`<article class="list-item"><div><h3>${esc(x.name)}</h3><p>${esc(x.desc)}</p><p class="meta">${esc(x.reason||'')}${x.cost?' · '+fmt(x.cost)+' 灵石':''}</p></div>${action('选择','dungeon/claim',{id:x.id},{disabled:x.enabled===false,className:'primary'})}</article>`).join('')}${r.canSkip!==false&&!(r.cleared&&!r.bossRewardClaimed)?action('略过并继续','dungeon/claim',{id:'skip'}):''}</div>`):''}
    ${r.phase==='route'?card('下一段路',`<div class="grid-2">${(r.nodes||[]).filter(n=>n.available).map(n=>`<div class="route-node"><span class="eyebrow">第 ${fmt(n.row+1)} 层</span><h3>${esc(n.name)}</h3><p>${esc(n.text)}</p>${action('前往','dungeon/choose',{id:n.id},{className:'primary wide'})}</div>`).join('')||empty('暂无可选路线。')}</div>`):''}
    ${r.phase==='complete'?card('秘境已通关',`<p>已取得${esc(r.bossRewardTaken?.name||'通关奖励')}，离开时结算战利品。</p>${action('结算并离开','dungeon/leave',{}, {className:'primary'})}`):''}
    ${card('本次行囊',`<p>秘境灵石 ${fmt(r.stones)} · 历时 ${fmt(r.elapsedDays)} 天</p><details><summary>路线与构筑</summary><p>${(r.nodes||[]).filter(n=>n.visited).map(n=>esc(n.name)).join(' → ')}</p><p>${(r.relics||[]).map(x=>esc(x.name)+'：'+esc(x.desc)).join('<br>')||'暂无遗物'}</p><p>${(r.deck||[]).map(x=>esc(x.name)).join(' · ')}</p></details><details><summary>待结算战利品</summary>${(r.lootItems||[]).map(x=>`<p>${esc(x.name)} × ${fmt(x.count)}</p>`).join('')}<p>灵石 ${fmt(r.loot?.stones||0)} · 灵核 ${fmt(r.loot?.cores||0)}</p></details>${r.cleared&&!r.bossRewardClaimed?'<p class="meta">先领取通关主奖励，才能离开。</p><button disabled>尚未领取主奖励</button>':'<button class="btn" data-ui="dungeon-leave">提前离开秘境</button>'}`)}`;
}
