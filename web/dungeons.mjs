// 秘境展示只读引擎状态；路线、奖励和可执行条件均由 C++ 决定。
import { heading, panel, esc, hintBox } from './shell.mjs';
import { locationArt } from './location-art.mjs';
import { renderServiceRoute } from './service-navigation.mjs';

const ROOM = {
  battle: ['⚔', '斗法'], combat: ['⚔', '斗法'], elite: ['✦', '强敌'],
  camp: ['♨', '营地'], rest: ['♨', '营地'], event: ['?', '奇遇'],
  merchant: ['◇', '行商'], shop: ['◇', '行商'], chest: ['▣', '宝箱'],
  treasure: ['▣', '宝箱'], boss: ['♜', '守关者'], start: ['⌾', '入口'],
};
const SITE = { wild: '野外秘境', natural: '野外秘境', intel: '情报秘境', bottle: '灵材秘境', tianmo: '天魔眼', sea: '无尽海' };
// 用途：显示秘境奖励类型中文名。
// 流程：按奖励kind查中文标签，未知类型保留其文字。
// 调用方式：秘境奖励渲染传入kind。
// 返回值：中文类型字符串。
const kindName = kind => ({ herb: '药材', material: '炼器材料', bottleMaterial: '专属灵材', spell: '神通', card: '神通', buyCard: '购买神通',
  learnSpell: '永久神通', manual: '功法抄本',
  relic: '秘境遗物', buyRelic: '购买遗物', heal: '疗伤', rest: '调息休整', buyHeal: '药浴疗伤', remove: '精简牌组',
  cache: '搜寻遗藏', risk: '破开禁制', eventChoice: '事件抉择', stones: '灵石' }[kind] || '奖励');
// 用途：显示秘境节点图标与名称。
// 流程：按节点type从ROOM表查找，未知类型回退通用节点。
// 调用方式：路线和当前节点渲染传入type。
// 返回值：图标与名称二元数组。
const room = type => ROOM[type] || ['○', '去处'];

// 用途：统一读取后端奖励说明；流程：奖励预览和真实选项；调用方式：传奖励快照；返回值：说明文本。
const rewardDescription = reward => reward.description ?? reward.desc ?? '';

// 用途：显示入场时冻结的终点材料数量；流程：地图入口、目录、探索与结算共用。
// 调用方式：传后端site或run快照；返回值：转义后的数量，旧响应缺字段沿用原十份。
const targetRewardCount = source => esc(source.targetRewardCount ?? 10);

// 用途：展示后端扫荡报价；流程：秘境目录与舆图入口；调用方式：传站点和是否附带原因；返回值：按钮与禁用说明HTML。
function renderDungeonSweep(site, { reason = true } = {}) {
  const sweep = site.sweep;
  if (!sweep) return '';
  const duration = Number.isFinite(sweep.days) && sweep.days > 0 ? ` · ${esc(sweep.days)} 天` : '';
  return `<button class="dungeon-sweep" data-action="dungeon-sweep" data-id="${esc(site.id)}" ${sweep.enabled === true ? '' : 'disabled'}>扫荡${duration}</button>`
    + (reason && sweep.reason ? `<small class="dungeon-sweep-reason">${esc(sweep.reason)}</small>` : '');
}

// 用途：显示秘境通关奖励候选。
// 流程：读取bossRewards，按details决定是否显示描述与折叠容器。
// 调用方式：入口或目录传入source和选项。
// 返回值：奖励预览HTML字符串。
export function renderDungeonBossPreview(source, { details = false } = {}) {
  const rewards = source.bossRewards || [];
  if (!rewards.length) return '';
  const content = `<ul>${rewards.map(reward => `<li><span>${esc(kindName(reward.kind))}</span><strong>${esc(reward.name)}</strong>${reward.kind === 'stones' ? `<b>${esc(reward.amount)} 灵石</b>` : ''}${details ? `<p>${esc(rewardDescription(reward))}</p>` : ''}</li>`).join('')}</ul>`;
  return details ? `<details class="dungeon-boss-preview"><summary>通关主奖励 · 三选一</summary>${content}<p>永久神通与功法抄本带回这一世；沿途选到的秘境卡牌与遗物只在本次探索生效。</p></details>`
    : `<section class="dungeon-boss-preview"><strong>通关主奖励 · 三选一</strong>${content}</section>`;
}

// 用途：筛选已发现秘境入口。
// 流程：只保留revealed条目，并按可选地点过滤。
// 调用方式：舆图或秘境页传入state与location。
// 返回值：入口数组。
export function visibleDungeonSites(state, location = '') {
  return (state.dungeons?.sites || []).filter(site => site.revealed !== false && (!location || site.location === location));
}

// 用途：显示当地秘境入口。
// 流程：组合可见副本、周期开放说明及情报购买入口。
// 调用方式：地点详情传入state和location。
// 返回值：入口HTML字符串。
export function renderDungeonEntrances(state, location) {
  const sites = visibleDungeonSites(state, location);
  const specials = (state.dungeons?.specials || []).filter(site => site.location === location);
  const intel = location === 'tianji';
  if (!sites.length && !specials.length && !intel) return '';
  return `<section class="location-dungeons" data-location-dungeons><h4>秘境入口</h4>`
    + (sites.some(site => site.sweep) ? `<p class="dungeon-sweep-note">扫荡直接选择通关主奖励，不获得沿途收益；离开秘境时结算所示耗时。</p>` : '')
    + sites.map(site => `<div class="location-service"><div><strong>${esc(site.name)}</strong>`
      + `<p>${esc(site.realmName)} · ${site.targetName ? `${esc(site.targetName)} ×${targetRewardCount(site)}` : esc(SITE[site.type] || '历练秘境')}</p>`
      + renderDungeonBossPreview(site, { details: true })
      + `<small>${esc(site.reason || (site.cleared ? '已通关' : site.available ? `第 ${site.expiresYear} 年前可探索` : site.nextOpenYear != null ? `下次第 ${site.nextOpenYear} 年开放` : '暂未开放'))}</small></div>`
      + `<div class="dungeon-entrance-actions"><button data-action="dungeon-enter" data-id="${esc(site.id)}" ${site.entryEnabled ? '' : 'disabled'}>进入秘境</button>${renderDungeonSweep(site)}</div></div>`).join('')
    + specials.filter(special => !special.open || !sites.some(site => site.type === special.type)).map(special => `<div class="dungeon-special-closed"><strong>${esc(special.name)}</strong><p>每 ${special.period} 年开放 ${special.duration} 年 · ${special.stage >= 6 ? '金丹' : '筑基'}起可入</p><small>下次第 ${special.nextOpenYear} 年开放</small></div>`).join('')
    + (intel ? `<button class="wide-btn" data-action="dungeon-intel-open">购买材料秘境情报</button><p class="dungeon-note">选择需要的药材或炼器材料，取得副本位置；击败守关者取得目标材料 ×10。</p>` : '')
    + `<button class="subtle" data-action="goto-tab" data-tab="dungeon">查看宁州秘境</button></section>`;
}

// 用途：展示全部已知秘境。
// 流程：读取地点、周期与入口条件，为可见副本生成定位和进入操作。
// 调用方式：秘境页传入state。
// 返回值：秘境目录HTML字符串。
export function renderDungeonDirectory(state) {
  const d = state.dungeons || {};
  const sites = visibleDungeonSites(state);
  return `<div class="dungeon-directory" data-dungeon-directory>`
    + `<div class="dungeon-cycle"><b>山河秘境</b><span>炼气至金丹每 ${d.earlyPeriod || 3} 年出现新秘境；元婴起每 ${d.latePeriod || 50} 年更新。</span>`
    + `${d.nextSpawnYear != null ? `<small>下次发现：第 ${esc(d.nextSpawnYear)} 年</small>` : ''}</div>`
    + (sites.some(site => site.sweep) ? `<p class="dungeon-sweep-note" data-dungeon-sweep-note>扫荡直接选择通关主奖励，不获得沿途收益；耗时在离开秘境时结算。可否扫荡及耗时以各入口报价为准。</p>` : '')
    + (d.specials?.length ? `<section class="dungeon-specials">${d.specials.map(special => `<article class="dungeon-special-card"><span>${special.open ? '本期开放' : '尚未开放'} · ${special.stage >= 6 ? '金丹' : '筑基'}起</span><h3>${esc(special.name)}</h3><p>每 ${special.period} 年开放 ${special.duration} 年</p><small>下次第 ${special.nextOpenYear} 年开放</small><button data-action="dungeon-locate" data-id="${esc(special.location)}">在舆图定位</button></article>`).join('')}</section>` : '')
    + `<div class="dungeon-sites">${sites.map(site => {
      const art = locationArt(site.location);
      const here = state.player.location === site.location;
      return `<article class="dungeon-site${site.cleared ? ' cleared' : ''}" data-dungeon-site="${esc(site.id)}">`
        + (art ? `<img src="${esc(art.src)}" alt="${esc(site.locationName || art.alt)}" width="1600" height="900" loading="lazy">` : '')
        + `<div class="dungeon-site-body"><div class="dungeon-site-head"><span>${esc(SITE[site.type] || '野外秘境')}</span><b>准入 · ${esc(site.realmName)}</b></div>`
        + `<h3>${esc(site.name)}</h3><p>${esc(site.locationName)}${site.available ? ` · 开放至第 ${esc(site.expiresYear)} 年` : ''}</p>`
        + (site.rewardHint ? `<p class="dungeon-reward-hint" data-dungeon-reward-hint>${esc(site.rewardHint)}</p>` : '')
        + (site.targetName ? `<div class="dungeon-target">终点保底 <b>${esc(site.targetName)} ×${targetRewardCount(site)}</b><small>${esc(site.targetGrade)}阶${kindName(site.targetKind)}</small></div>` : '')
        + renderDungeonBossPreview(site, { details: true })
        + `<p class="dungeon-site-reason">${esc(site.reason || (site.cleared ? '本处秘境已通关。' : !site.available && site.nextOpenYear != null ? `下次第 ${site.nextOpenYear} 年开放` : here ? '已抵达入口，可开始探索。' : '先前往入口所在地点。'))}</p>`
        + `<div class="dungeon-site-actions"><button class="primary" data-action="dungeon-enter" data-id="${esc(site.id)}" ${site.entryEnabled ? '' : 'disabled'}>${site.entered ? '继续探索' : '进入秘境'}</button>`
        + renderDungeonSweep(site, { reason: false })
        + (!here ? `<button data-action="dungeon-locate" data-id="${esc(site.location)}">在舆图定位</button>` : '') + `</div>`
        + (site.sweep?.reason ? `<small class="dungeon-sweep-reason">扫荡：${esc(site.sweep.reason)}</small>` : '') + `</div></article>`;
    }).join('') || `<div class="empty">尚未发现可探索的秘境。前往天机阁购买材料秘境情报，或等待山河中的新机缘。</div>`}</div></div>`;
}

// 用途：展示材料秘境情报。
// 流程：按类型与关键词筛选服务端情报，显示费用和可购条件。
// 调用方式：情报子页传入state与筛选选项。
// 返回值：情报列表HTML字符串。
export function renderDungeonIntel(state, { kind = 'all', search = '', grade = '3' } = {}) {
  const d = state.dungeons || {};
  const grades = [...new Set([3, 4, 5, ...(d.intel || []).map(item => Number(item.grade))])].filter(value => Number.isFinite(value) && value >= 3).sort((a, b) => a - b);
  const entries = (d.intel || []).filter(item => Number(item.grade) >= 3 && (grade === 'all' || Number(item.grade) === Number(grade)) && (kind === 'all' || item.kind === kind)
    && (!search.trim() || `${item.name} ${item.desc || ''} ${item.stageName || ''}`.includes(search.trim())));
  const here = state.player.location === 'tianji';
  return panel('天机阁 · 秘境情报', '每月限购一次', `<div class="dungeon-intel-body">`
    + `<p class="dungeon-intel-lead">购买三阶及以上材料所在的秘境位置。探索分支路线，击败终点守关者，保底取得指定材料 ×10，并选择一份通关主奖励。</p>`
    + (!here ? `<div class="dungeon-intel-away">需在天机阁购买情报。`
      + renderServiceRoute(state.serviceRoutes?.intel, { tab: 'dungeon', sub: 'intel', routeKey: 'intel' })
      + `<button class="subtle" data-action="dungeon-locate" data-id="tianji">在舆图定位天机阁</button></div>` : '')
    + `<div class="dungeon-intel-filters"><div class="shop-tabs">${[['all','全部'],['herb','炼丹药材'],['material','炼器材料']].map(([id,label]) => `<button class="${kind === id ? 'active' : ''}" data-dungeon-intel-kind="${id}">${label}</button>`).join('')}</div>`
    + `<label class="shop-grade-control">品阶 <select data-dungeon-intel-grade><option value="all" ${grade === 'all' ? 'selected' : ''}>全部三阶以上</option>${grades.map(value => `<option value="${value}" ${String(value) === String(grade) ? 'selected' : ''}>${value} 阶</option>`).join('')}</select></label>`
    + `<input type="search" data-dungeon-intel-search value="${esc(search)}" placeholder="搜索材料或境界" aria-label="搜索材料秘境情报"></div>`
    + `<div class="dungeon-intel-list">${entries.map(item => {
      const enabled = here && item.enabled;
      return `<article class="dungeon-intel-item"><div><span class="dungeon-item-kind">${esc(item.grade)}阶 · ${esc(kindName(item.kind))} · ${esc(item.stageName)}</span>`
        + `<h4>${esc(item.name)}秘境</h4><p>${esc(item.desc)}</p><small>${esc(!here ? '需先抵达天机阁' : item.reason || '通关必得目标材料 ×10')}</small></div>`
        + `<div class="dungeon-intel-price"><strong>${esc(item.cost)} 灵石</strong><button class="primary" data-action="dungeon-intel" data-kind="${esc(item.kind)}" data-id="${esc(item.id)}" ${enabled ? '' : 'disabled'}>购买位置</button></div></article>`;
    }).join('') || `<div class="empty">没有符合条件的材料情报。</div>`}</div></div>`);
}

// 用途：计算秘境路线绘图位置。
// 流程：按节点行列及总行数映射到路线画布坐标。
// 调用方式：路线绘制传入node和rows。
// 返回值：含x与y的坐标对象。
function nodePosition(node, rows) {
  return { x: 120 + (Number(node.lane) || 0) * 240, y: 64 + (rows - 1 - (Number(node.row) || 0)) * 112 };
}

// 用途：读取节点的遭遇预告；流程：路线选择与当前房间；调用方式：传节点；返回值：后端名称和机制说明，不计算战斗数值。
function dungeonNodePreview(node) {
  return { name: node.encounterName || node.eventName || node.name || room(node.type)[1],
    hint: node.encounterHint || node.eventHint || node.text || '' };
}

// 用途：比较下一步可达路线；流程：仅在可选路线阶段显示；调用方式：传当前run；返回值：节点机制与真实后续连接HTML。
function renderDungeonRouteOptions(run) {
  if (run.phase !== 'route') return '';
  const nodes = run.nodes || [], byId = new Map(nodes.map(node => [node.id, node]));
  const available = nodes.filter(node => node.available);
  if (!available.length) return '';
  return `<section class="dungeon-route-options" data-dungeon-route-options aria-label="下一步路线对照"><h4>下一步去哪里</h4><div>`
    + available.map(node => {
      const [icon, label] = room(node.type), preview = dungeonNodePreview(node);
      const destinations = [...new Set((node.next || []).map(id => byId.get(id)).filter(Boolean)
        .map(next => `${room(next.type)[1]} · ${dungeonNodePreview(next).name}`))];
      return `<article class="dungeon-route-option${node.type === 'elite' ? ' elite' : ''}" data-dungeon-option="${esc(node.id)}">`
        + `<span class="dungeon-option-kind">${icon} ${label} · 第 ${esc(Number(node.row) + 1)} 层</span><h5>${esc(preview.name)}</h5>`
        + (node.name && node.name !== preview.name ? `<small>${esc(node.name)}</small>` : '')
        + (preview.hint ? `<p>${esc(preview.hint)}</p>` : '')
        + (node.type === 'elite' ? `<b class="dungeon-elite-warning">高风险 · 高奖励</b>` : '')
        + (destinations.length ? `<p class="dungeon-option-next">后续可达：${destinations.map(esc).join(' / ')}</p>` : '')
        + `<button data-action="dungeon-choose" data-id="${esc(node.id)}">前往${esc(label)}</button></article>`;
    }).join('') + `</div></section>`;
}

// 用途：展示可走的秘境路线。
// 流程：按真实节点连线绘制路线并区分已走、当前和可选节点。
// 调用方式：探索页传入run。
// 返回值：路线HTML字符串。
export function renderDungeonRoute(run) {
  const nodes = run.nodes || [];
  const rows = Math.max(1, ...nodes.map(node => (Number(node.row) || 0) + 1));
  const height = 128 + (rows - 1) * 112;
  const edges = nodes.flatMap(node => (node.next || []).map(id => {
    const target = nodes.find(next => next.id === id);
    if (!target) return '';
    const a = nodePosition(node, rows), b = nodePosition(target, rows);
    return `<path class="dungeon-edge${node.visited && (target.visited || target.available) ? ' reached' : ''}" d="M${a.x},${a.y} L${b.x},${b.y}"/>`;
  })).join('');
  return `<section class="dungeon-route-panel"><div class="dungeon-route-heading"><h3>向秘境深处</h3><span>由下至上 · 选择相连的下一处</span></div>`
    + (typeof run.routeSummary === 'string' && run.routeSummary ? `<p class="dungeon-route-summary" data-dungeon-route-summary>${esc(run.routeSummary)}</p>` : '')
    + renderDungeonRouteOptions(run)
    + `<div class="dungeon-route-viewport" data-dungeon-route-viewport tabindex="0" aria-label="秘境分支路线，由下至上探索">`
    + `<div class="dungeon-route-map" style="height:${height}px" data-dungeon-route-map><svg viewBox="0 0 720 ${height}" preserveAspectRatio="none" aria-hidden="true">${edges}</svg>`
    + nodes.map(node => {
      const pos = nodePosition(node, rows), [icon,label] = room(node.type), preview = dungeonNodePreview(node);
      const current = node.id === run.current;
      const enabled = node.available && run.phase === 'route';
      return `<button class="dungeon-node ${esc(node.type)}${current ? ' current' : ''}${node.visited ? ' visited' : ''}${enabled ? ' available' : ''}" `
        + `style="left:${pos.x / 7.2}%;top:${pos.y}px" data-action="dungeon-choose" data-id="${esc(node.id)}" data-dungeon-row="${esc(node.row)}" `
        + `${enabled ? '' : 'disabled'} aria-label="第 ${Number(node.row) + 1} 层，${esc(preview.name)}${current ? '，当前所在' : node.visited ? '，已探索' : enabled ? '，可前往' : '，不可前往'}" title="${esc([preview.name, preview.hint].filter(Boolean).join(' · '))}">`
        + `<span class="dungeon-node-icon" aria-hidden="true">${icon}</span><strong>${esc(node.name || label)}</strong><small>${current ? '当前所在' : node.visited ? '已探索' : enabled ? '可前往' : `第 ${Number(node.row) + 1} 层`}</small></button>`;
    }).join('') + `</div></div><div class="dungeon-room-legend" aria-label="节点类型图例">${[['battle','斗法','战胜后选择奖励'],['elite','强敌','高风险、高奖励'],['camp','营地','休整或调整构筑'],['event','奇遇','按选项承担代价与风险'],['merchant','行商','以秘境灵石交易'],['chest','宝箱','获取物资'],['boss','守关者','通关后选择主奖励']].map(([type,label,description]) => `<span title="${description}">${room(type)[0]} ${label}</span>`).join('')}</div></section>`;
}

// 用途：整理秘境战利品摘要。
// 流程：读取已得物资、名称和数量并转为可读条目。
// 调用方式：探索页传入run。
// 返回值：战利品文字数组。
function lootEntries(run) {
  const loot = run.loot || {}, names = run.itemNames || {};
  const entries = Array.isArray(run.lootItems) ? run.lootItems.map(item => `${item.name} ×${item.amount ?? item.count}`) : [];
  if (!Array.isArray(run.lootItems))
    for (const [type,label] of [['herbs','药材'],['materials','材料'],['potions','丹药']])
      for (const [id,amount] of Object.entries(loot[type] || {})) if (amount > 0) entries.push(`${names[id] || id} ×${amount}${names[id] ? '' : `（${label}）`}`);
  for (const [id,label] of [['stones','灵石'],['cores','灵核'],['insight','悟性']]) if (loot[id] > 0) entries.push(`${label} +${loot[id]}`);
  return entries;
}

// 用途：展示当前秘境探索。
// 流程：组合路线、战斗或节点选项、临时牌、遗物与战利品。
// 调用方式：秘境页面传入state与可选battleHTML。
// 返回值：探索HTML字符串。
export function renderDungeonRun(state, battleHTML = '') {
  const run = state.dungeons?.run;
  if (!run) return '';
  const loot = lootEntries(run);
  const current = (run.nodes || []).find(node => node.id === run.current);
  const health = state.battle?.max ? Math.max(0, Math.round(state.battle.hp / state.battle.max * 100)) : run.health ?? state.player.health;
  const groupedDeck = new Map();
  for (const card of run.deck || []) {
    const key = `${card.spell}:${card.cost}`;
    if (groupedDeck.has(key)) groupedDeck.get(key).copies++;
    else groupedDeck.set(key, {...card,copies:1});
  }
  const removalNames = new Set();
  // 同种神通的副本效果相同，营地只展示一张可删代表；仍发送后端给出的具体牌 UID。
  const rewards = (run.rewards || run.choices || []).filter(reward => {
    if (reward.kind !== 'remove') return true;
    if (removalNames.has(reward.name)) return false;
    removalNames.add(reward.name); return true;
  });
  const isReward = run.phase === 'reward';
  const isChoice = ['camp','event','merchant','chest','choice'].includes(run.phase);
  const isEventChoice = rewards.some(reward => reward.kind === 'eventChoice');
  const isBossReward = isReward && run.cleared && !run.bossRewardClaimed;
  const taken = run.bossRewardTaken;
  const currentPreview = current ? dungeonNodePreview(current) : null;
  return `<div class="dungeon-run" data-dungeon-run="${esc(run.siteId)}">`
    + `<header class="dungeon-run-heading"><div><span class="dungeon-eyebrow">${esc((run.type === 'bottle' ? run.difficultyRealmName : '') || run.entryRealmName || run.realmName)} · ${esc(SITE[run.type] || '秘境历练')}</span><h2>${esc(run.siteName)}</h2>`
    + `<p>${esc(current?.name || '秘境入口')} · 已探索 ${(run.nodes || []).filter(node => node.visited).length} 处 · 入境 ${run.elapsedDays || 0} 天</p>`
    + (run.rewardHint ? `<p class="dungeon-reward-hint" data-dungeon-reward-hint>${esc(run.rewardHint)}</p>` : '') + `</div>`
    + `<div class="dungeon-run-vitals"><span>气血 <b>${health}</b> / 100 · 秘境灵石 ${run.stones || 0}</span><button class="subtle" data-action="save">保存进度</button>`
    + `<button data-action="dungeon-leave" ${state.battle || isBossReward ? 'disabled' : ''} ${isBossReward ? 'title="请先选择通关主奖励"' : ''}>${run.phase === 'complete' ? '带着战利品离开' : '离开秘境'}</button></div></header>`
    + (run.phase !== 'complete' ? `<p class="dungeon-exit-note">提前退出后入口消失，无法再次进入；主动退出带回已得物资，战斗中逃离只带回一半。</p>` : '')
    + (current && (current.encounterName || current.eventName || current.encounterHint || current.eventHint) && !isBossReward && run.phase !== 'route' && run.phase !== 'complete'
      ? `<div class="dungeon-current-preview" data-dungeon-current-preview><strong>${esc(currentPreview.name)}</strong><p>${esc(currentPreview.hint)}</p></div>` : '')
    + (run.targetName ? `<div class="dungeon-run-target">终点保底 <strong>${esc(run.targetName)} ×${targetRewardCount(run)}</strong><span>${run.targetGranted ? '已取得目标材料' : '击败守关者后取得，另选通关主奖'}</span></div>` : '')
    + (!isBossReward && run.phase !== 'complete' ? renderDungeonBossPreview(run, { details: true }) : '')
    + (state.battle ? `<div class="dungeon-combat">${battleHTML}</div>` : '')
    + (run.phase === 'complete' ? `<section class="dungeon-complete"><span>♜</span><h3>秘境已通关</h3><p>${run.targetName ? `已取得${esc(run.targetName)} ×${targetRewardCount(run)}。` : ''}${taken ? `通关主奖：${esc(taken.name)}${taken.kind === 'stones' ? ` · ${esc(taken.amount)} 灵石` : ' · 永久获得'}。` : ''}点击离开，结算本次战利品。</p><button class="primary" data-action="dungeon-leave">结算战利品并离开</button></section>` : '')
    + (isReward || isChoice ? `<section class="dungeon-reward-panel${isBossReward ? ' permanent' : ''}" data-dungeon-rewards><div class="dungeon-reward-heading"><span>${esc(current?.name || '本层已完成')}</span>`
      + `<h3>${isBossReward ? '通关主奖励 · 三选一' : ['camp','rest'].includes(current?.type) ? '营地休整' : ['merchant','shop'].includes(current?.type) ? '秘境行商' : current?.type === 'event' ? '眼前的奇遇' : isReward ? '选择一项奖励' : '眼前的抉择'}</h3><p>${esc(isBossReward ? '选择一份永久传承或大量灵石。神通学会后自行携带，功法抄本带回后可修习；这些主奖励会留下。' : run.text || (isEventChoice ? currentPreview?.hint || '选择一项；代价与风险见各选项。' : ['merchant','shop'].includes(current?.type) ? '用本次行囊中的灵石购买一项，或略过行商继续探索。' : isReward ? '沿途神通与遗物只在本次秘境生效，构筑会带入后续斗法。' : current?.text || '做出选择后继续前进。'))}</p></div>`
      + `<div class="dungeon-reward-list">${rewards.map(reward => `<button class="dungeon-reward-card ${esc(reward.kind === 'buyRelic' ? 'relic' : reward.kind || 'choice')}" data-action="dungeon-claim" data-id="${esc(reward.id)}" ${reward.enabled === false ? 'disabled' : ''}>`
        + `<span class="dungeon-item-kind">${esc(kindName(reward.kind))}${reward.cost ? ` · ${esc(reward.cost)} 灵石` : ''}${reward.healthCost ? ` · 气血 -${esc(reward.healthCost)}` : ''}</span><strong>${esc(reward.name)}</strong><p>${esc(rewardDescription(reward))}</p>`
        + `<small>${esc(reward.reason || reward.disabledReason || (isReward && !isEventChoice ? '点击领取' : '选择此项'))}</small></button>`).join('')}</div>`
      + `${run.canSkip === false ? '' : `<button class="subtle" data-action="dungeon-claim" data-id="skip">${isBossReward ? '领取灵石奖励' : isReward && !isEventChoice ? '放弃本次奖励' : '继续前行'}</button>`}</section>` : '')
    + `<div class="dungeon-run-layout">${renderDungeonRoute(run)}<aside class="dungeon-expedition-bag">`
    + `<h3>本次构筑</h3><p class="dungeon-note">带伤继续探索。临时卡牌与遗物在离开秘境后消失，通关得到的永久神通与功法抄本留下；原有功法与悟道持续生效。</p>`
    + `<details class="dungeon-deck" open><summary>秘境牌组 <b>${(run.deck || []).length} 张</b></summary><div>${[...groupedDeck.values()].map(card => `<span class="dungeon-deck-card"><b>${esc(card.name || card.spell)} <em>×${card.copies}</em></b><small>${esc(card.dao)} · 灵力 ${esc(card.cost)}</small></span>`).join('')}</div></details>`
    + `<section class="dungeon-relics"><h4>遗物 ${(run.relics || []).length}</h4>${(run.relics || []).map(relic => `<article><strong>${esc(relic.name)}</strong><p>${esc(relic.desc)}</p></article>`).join('') || `<p class="dungeon-note">尚未取得秘境遗物。</p>`}</section>`
    + `<section class="dungeon-loot"><h4>战利品包</h4><ul>${loot.map(text => `<li>${esc(text)}</li>`).join('') || '<li>尚无战利品</li>'}</ul><p class="dungeon-note">离开秘境时结算战利品。</p></section>`
    + `</aside></div></div>`;
}

// 用途：选择秘境视图。
// 流程：有run显示探索，否则按mode显示目录或材料情报。
// 调用方式：主渲染传入state与模式选项。
// 返回值：秘境页HTML字符串。
export function renderDungeons(state, { mode = 'sites', intelKind = 'all', intelSearch = '', intelGrade = '3', battleHTML = '' } = {}) {
  if (state.dungeons?.run) return renderDungeonRun(state, battleHTML);
  const unfinishedElapsed = state.job?.type === 'socialElapsed' && state.job.remaining > 0;
  const continuation = unfinishedElapsed
    ? `<div class="choice-banner" data-dungeon-elapsed-continuation><p>当前安排「${esc(state.job.name)}」尚余 ${state.job.remaining} 天。请先完成剩余耗时，再开始其他活动。</p>`
      + `<button class="primary" data-action="advance" data-days="${state.job.remaining}" ${state.pending || state.battle ? 'disabled' : ''}>继续当前安排 · ${state.job.remaining} 天</button>`
      + `<button data-action="goto-tab" data-tab="chronicle">返回岁月纪事</button></div>`
    : '';
  return heading('宁州秘境', '寻访秘境，选择路线，以神通与遗物应对层层斗法。', '带伤续战 · 临时构筑')
    + continuation
    + `<div class="subtabs"><button class="subtab ${mode === 'sites' ? 'on' : ''}" data-dungeon-view="sites">山河秘境</button>`
    + `<button class="subtab ${mode === 'intel' ? 'on' : ''}" data-dungeon-view="intel">天机阁情报</button></div>`
    + (mode === 'intel' ? renderDungeonIntel(state, { kind: intelKind, search: intelSearch, grade: intelGrade }) : renderDungeonDirectory(state))
    + hintBox(['敌人难度与奖励以入口说明为准；小绿瓶灵材秘境有固定境界难度，探索后续房间会沿用当前气血。', '天机阁可购买指定材料的秘境位置，通关终点固定获得材料 ×10。', '天魔眼每 50 年开放 5 年；无尽海每 10 年开放 2 年。']);
}
