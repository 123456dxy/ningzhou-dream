// 宁州一梦 · C++ 版前端
//
// 设计原则（针对"体验糟糕"的反馈重写过）：
//   1. 一次点击 = 一个明确结果，绝不逼玩家反复点
//   2. 不能做的事，必须说清「缺什么、差多少」，不能只说"不可以"
//   3. 好感度、危险度这些数值要看得见，且真的影响结果
//   4. 每个动作都要有即时反馈（右侧动作流），不用去翻纪事
//
// 这一层是纯展示 + 交互：所有数值都来自 C++ 的 /api/，前端不算规则。

'use strict';

import { createBuildPanel } from './build-panel.mjs';
import { createTianji, renderTianjiReminder } from './tianji.mjs';
import { ensureLogin } from './login.mjs';
import { loadRoster, invalidateRoster, renderRoster, bindRoster } from './roster.mjs';
import { heading, panel, layout, asideBlock, timeline, yearControls, hintBox } from './shell.mjs';
import { renderDaos, renderArtsAside, loadNodes, invalidateNodes,
         loadSpells, invalidateSpells, renderSpellPicker, renderArtBuilds } from './arts.mjs';
import { loadEcoOptions, loadForgeOptions, invalidateEco, renderEconomy, actionRow, TYPE_LABEL } from './economy.mjs';
import { loadLegacy, invalidateLegacy, draftTalents, renderEnding, renderLegacyPanel, renderDraftPanel } from './legacy.mjs';
import { loadSects, loadAchievements, invalidateSects, renderSectPicker, renderSectSpellLibrary, renderSectMentors, renderSectTasks, renderAchievements } from './sects.mjs';
import { renderRenownPanel, renderRenownBar, renownChip } from './renown.mjs';
import { cardKind, cardRule, cardNumbers, battleStatusRows } from './cards.mjs';
import { renderNumericPreview } from './numeric-preview.mjs';
import { renderAtlas, selectedTravelMode, travelFeedback, isTravelJob } from './map.mjs';
import { renderLocationArt } from './location-art.mjs';
import { renderPortrait, battleImpacts, animateBattleImpacts } from './portraits.mjs';
import { renderDungeons } from './dungeons.mjs';
import { caveSupplyAccess, renderCaveUpgrade, renderCaveSupplyLimits } from './cave-ui.mjs';
import { renderArtifactAbility, renderEquippedArtifacts, renderBattleArtifacts, renderForgePreview } from './artifacts.mjs';
import {
  renderFriendList, renderFriendPanel, loadNpcBag, invalidateFriendCache,
  renderTidePanel, renderBondActions,
} from './friends.mjs';

let S = null;            // 游戏状态（来自 C++）
let tab = location.hash === '#tianji' ? 'tianji' : 'chronicle';   // 当前页签
let selectedNpc = null;  // 选中的人
let friendBag = null;    // 选中的人的可换物品（/api/npc-bag 结果）
let mapFocus = null;     // 舆图上选中的地点
let mapSearch = '';
let mapFilter = 'all';
let mapModes = {};
let mapZoom = 1;
let mapPanFocus = false;
// 子页签：把「一页拉到底」拆成几段，玩家不用滚半天。
// key 是页签名，值是当前子页签 id。
let subTab = {};
function subOf(tabKey, def) { return subTab[tabKey] || def; }
function setSub(tabKey, id) { subTab[tabKey] = id; }
let shopKind = 'herb';   // 坊市当前分类
let shopQty = {};        // 各货品的购买数量
let feed = [];           // 动作流（本会话的操作反馈）
let lastReport = null;   // 上次推进的报告
let requestBusy = false;
let economyReady = false;
let economyLoading = null;
let nodesLoading = null;
let spellsLoading = null;
let brewSearch = '';
let brewBatch = 1;
let forgeSelection = {};
let forgePreview = null;
let forgePreviewSerial = 0;
let artBuildFilter = 'all';
let spellDaoFilter = 'all';
let removalSelection = null;
let dungeonView = 'sites';
let dungeonIntelKind = 'all';
let dungeonIntelSearch = '';
let checkpointNotice = '';
const tianji = createTianji({
  onChange: () => {
    if (S && tab === 'tianji' && !S.battle && !S.dungeons?.run && !S.ended) render();
    else if (S) updateTianjiReminder();
  },
  onSettled: () => { call('state'); },
});
function navigateTab(next) {
  if (next === 'tianji' && location.hash !== '#tianji') history.pushState({ nzTab: next }, '', '#tianji');
  else if (next !== 'tianji' && location.hash === '#tianji') history.pushState({ nzTab: next }, '', location.pathname + location.search);
  tab = next;
  render();
}
window.addEventListener('popstate', () => {
  tab = location.hash === '#tianji' ? 'tianji' : history.state?.nzTab || 'chronicle';
  if (S) render();
});

// ------------------------------------------------------------
// 基础工具
// ------------------------------------------------------------
const $ = sel => document.querySelector(sel);

function escape(s) {
  return String(s ?? '').replace(/[&<>"']/g,
    c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}
const fmt = n => (n ?? 0).toLocaleString('zh-CN');
const pct = (v, d = 0) => `${(v * 100).toFixed(d)}%`;

function toast(msg, kind) {
  const el = $('#toast');
  el.textContent = msg;
  el.className = 'toast' + (kind ? ' ' + kind : '');
  el.hidden = false;
  clearTimeout(el._t);
  el._t = setTimeout(() => { el.hidden = true; }, 2600);
}

// 往动作流里推一条
function pushFeed(text, delta, kind) {
  feed.unshift({ text, delta, kind: kind || 'info', t: Date.now() });
  if (feed.length > 40) feed.pop();
}

// 所有请求都带上反馈
async function call(path, body) {
  if (requestBusy) return null;
  const battleBefore = path === 'battle/turn' && S?.battle ? { ...S.battle } : null;
  const restoresBefore = S?.checkpoint?.restores;
  requestBusy = true;
  const status = $('#save-status');
  const statusBefore = status.textContent;
  status.textContent = '正在处理……';
  document.body.setAttribute('aria-busy', 'true');
  try {
    const res = await fetch('/api/' + path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body || {}),
    });
    const data = await res.json().catch(() => ({ error: '返回内容不是 JSON' }));
    if (!res.ok || data.ok === false || data.error) {
      data.error ||= '操作失败，请稍后重试';
      toast(data.error, 'bad');
      pushFeed(data.error, '', 'bad');
      return data;
    }
    if (data.text) {
      const delta = data.relationDelta;
      pushFeed(data.text, delta ? `好感 ${delta > 0 ? '+' : ''}${delta}` : '', 'good');
      toast(data.text, 'good');
    }
    if (data.checkpointWarning) {
      pushFeed(data.checkpointWarning, '自动存档未写入磁盘', 'warn');
      toast(data.checkpointWarning, 'warn');
    }
    if (data.report) lastReport = data.report;
    if (data.state) {
      const wasReady = S?.breakthrough?.ready, wasStage = S?.player?.stage;
      S = data.state;
      if (S.breakthrough?.ready && wasReady === false && wasStage === S.player?.stage) {
        pushFeed(`修为圆满，可叩关「${S.breakthrough.next}」`, '点左侧「叩关突破 →」', 'good');
        setTimeout(() => toast(`修为已圆满！点左侧金色「叩关突破 →」冲击「${S.breakthrough.next}」。`, 'good'), 2700);
      }
      if (Number.isFinite(restoresBefore) && S.checkpoint?.restores > restoresBefore) {
        checkpointNotice = S.checkpoint.lastReason || '斗法败北，已恢复至最近存档。';
        pushFeed(checkpointNotice, '已回档', 'warn');
        toast(checkpointNotice, 'warn');
      }
      // 实际费用会随功法、道法与配装变化；重新取定义，拥有状态仍以 S 为准。
      invalidateSpells(); spellsReady = false; spellsRevision++;
      invalidateEco(); economyReady = false;
      forgePreview = null; forgePreviewSerial++;
      invalidateRoster(); rosterReady = false;
      invalidateSects(); sectData = null; achData = null;
      legCache = null;
      if (['new', 'load', 'import'].includes(path)) {
        selectedNpc = null; friendBag = null; mapFocus = null;
        mapSearch = ''; mapFilter = 'all'; mapModes = {};
        mapZoom = 1; mapPanFocus = true;
        dungeonView = 'sites'; dungeonIntelKind = 'all'; dungeonIntelSearch = '';
        checkpointNotice = '';
        feed = []; lastReport = null;
      }
      if (tab === 'economy' || tab === 'society') await refreshEconomy();
      render();
      animateBattleImpacts(battleImpacts(battleBefore, S.battle));
    }
    return data;
  } catch (e) {
    toast('连接 C++ 服务失败，请确认服务还在运行', 'bad');
    return null;
  } finally {
    requestBusy = false;
    document.body.removeAttribute('aria-busy');
    status.textContent = statusBefore;
  }
}

// ------------------------------------------------------------
// 子页签组件
// ------------------------------------------------------------
// items: [[id, 标签, 角标], ...]
function subTabs(tabKey, items, current) {
  return `<div class="subtabs">` + items.map(([id, label, badge]) =>
    `<button class="subtab ${id === current ? 'on' : ''}"
             data-subtab="${tabKey}" data-sub="${id}">${escape(label)}${
       badge ? `<span class="st-badge">${escape(badge)}</span>` : ''
     }</button>`).join('') + `</div>`;
}

// ------------------------------------------------------------
// 侧边栏

// ------------------------------------------------------------
function stat(label, value, suffix, cls) {
  return `<div class="stat ${cls || ''}"><label>${escape(label)}</label>`
       + `<strong>${escape(value)}</strong>`
       + (suffix ? `<span>${escape(suffix)}</span>` : '') + `</div>`;
}

// 突破按钮不可用的原因（与后端 /api/breakthrough 的检查一致，只用于显示）
function breakthroughBlock() {
  const bt = S.breakthrough, p = S.player;
  if (!bt.ready) return '修为尚未圆满';
  if (p.stones < bt.cost) return `灵石不足（需 ${bt.cost}，有 ${p.stones}）`;
  if (p.health < 35) return `气血不足（需 35，现 ${p.health}）`;
  return '';
}

function updateTianjiReminder() {
  $('#tianji-reminder').innerHTML = renderTianjiReminder(S.tianji ? { ...S.tianji, hasEncounter: tianji.status ? tianji.status.encounter?.state === 'active' : S.tianji.hasEncounter } : null);
}
function renderSidebar() {
  updateTianjiReminder();
  const p = S.player;
  $('#player-card').innerHTML =
    `<div class="portrait player-portrait">${renderPortrait(p, 'player')}</div>`
  + `<h2 class="player-name">${escape(p.name)}</h2>`
  + `<p class="player-identity">${escape(p.gender)} · `
  + `${escape(p.sectName || '宁州散修')} · ${escape(p.realmName)}</p>`
  + `<p class="location-line"><span>⌾</span> 现居 ${escape(p.locationName)}</p>`
  // 还没拜入宗门时给一条明确指引 —— 之前「怎么入宗」完全没有入口提示，
  // 五宗一览埋在宗门页第 4 屏，玩家根本找不到
  + (!p.sect
      ? `<button class="sect-cta" data-action="goto-tab" data-tab="society">`
        + `去择一宗拜入山门`
        + `<small>可得俸禄、差事与藏经阁</small></button>`
      : '')
  + `<div class="player-stats">`
  + stat('境界', p.realmName)
  + stat('年龄', p.age, '岁')
  + stat('余寿', Math.max(0, p.lifespan - p.age), '年')
  + stat('灵石', fmt(p.stones), '枚')
  + stat('气血', p.health, '/ 100', 'health')
  + stat('遁速', p.escapeSpeed ?? '—', '赶路 / 先手 / 脱身')
  + `</div>` + renderEquippedArtifacts(S.gears);

  const npc = (S.nearby || []).find(n => n.id === selectedNpc);
  const npcCard = $('#npc-card');
  npcCard.hidden = !npc;
  npcCard.innerHTML = npc ? `<div class="sidebar-npc-heading"><span>眼前道友</span><b>${escape(npc.realmName)}</b></div>`
    + renderPortrait(npc, 'npc', 'sidebar-npc-portrait')
    + `<h3>${escape(npc.name)}</h3><p>${escape(npc.sectName || '宁州散修')} · ${escape(npc.gender)}</p>` : '';

  const pc = p.xpNeed ? (p.xp / p.xpNeed * 100) : 0;
  $('#progress-card').innerHTML =
    `<div class="progress-box"><h4>修行进境<span>${pc.toFixed(1)}%</span></h4>`
  + `<div class="progress-track"><i style="width:${Math.min(100, pc)}%"></i></div>`
  + `<p class="progress-note">修为 ${fmt(p.xp)} / ${fmt(p.xpNeed)}</p>`
  + `<div class="progress-meta">`
  + `<span>丹毒 ${S.arts.poison}</span><span>悟道点 ${S.arts.points}</span>`
  + `</div>`
  + (S.breakthrough?.ready
      ? `<button class="bt-cta" data-action="goto-breakthrough">叩关突破 →`
        + `<small>${escape(breakthroughBlock() || '目标「' + S.breakthrough.next + '」· 成功率 ' + Math.round(S.breakthrough.chance * 100) + '%')}</small></button>`
      : '')
  + `</div>`;

  const m = S.arts.mods;
  $('#hero-time').innerHTML =
    `<span class="year-chip">入道第 ${S.clock.year + 1} 年</span>`
  + `<span>${p.age} 岁</span>`
  + `<span>${S.clock.month} 月 ${S.clock.day} 日</span>`
  + `<span>修炼 ×${m.cult.toFixed(2)}</span>`
  + (S.breakthrough?.ready ? `<button class="bt-chip" data-action="goto-breakthrough">修为圆满 · 叩关突破 →</button>` : '');

  $('#world-strip').innerHTML =
    `<div class="world-stat"><span class="symbol">山</span><div>`
  + `<label>天下同道</label><strong>${fmt(S.world.npcCount)} 人</strong></div></div>`
  + `<div class="world-stat"><span class="symbol">市</span><div>`
  + `<label>坊市行情</label><strong>×${S.world.market.toFixed(2)}</strong></div></div>`
  + `<div class="world-stat"><span class="symbol">天</span><div>`
  + `<label>宁州气候</label><strong>${escape(S.world.weather)}</strong></div></div>`
  + `<div class="world-stat"><span class="symbol">脉</span><div>`
  + `<label>青石灵脉</label><strong>${escape((S.locations || []).find(location => location.id === S.world.mineOwner)?.name || S.world.mineOwner)}</strong></div></div>`;
}

// ------------------------------------------------------------
// 页签：岁月纪事
// ------------------------------------------------------------
// ------------------------------------------------------------
// 页签：岁月纪事
// 结构照原版：section-head → chronicle-layout(section + aside.plan-panel)
// ------------------------------------------------------------
function viewChronicle() {
  const p = S.player;
  const m = S.arts.mods;

  // 左侧：纪事时间线
  const logs = (S.logs || []).slice(0, 90);
  const logBody =
    `<div class="filters">`
  + [['all', '全部'], ['important', '大事'], ['self', '自身'],
     ['npc', '同道'], ['sect', '宗门'], ['world', '天下']]
      .map(([k, n]) =>
        `<button data-logfilter="${k}" class="${logFilter === k ? 'active' : ''}">${n}</button>`)
      .join('')
  + `</div>`
  + `<div class="timeline">${timeline(filteredLogs(logs), e => e.age ?? Math.max(16, p.age - S.clock.year + e.year))}</div>`
  + `<div class="timeline-bottom"><span>共 ${logs.length} 则</span>`
  + `<span>第 ${S.clock.year + 1} 年 · ${S.clock.month} 月</span></div>`;

  // 右侧：本月打算
  const planOpts = S.plans.map(pl =>
    `<button class="plan-option ${pl.id === S.plan ? 'selected' : ''}" `
  + `data-action="plan" data-id="${pl.id}">`
  + `<span class="plan-symbol">${escape(pl.icon || '◈')}</span>`
  + `<span><strong>${escape(pl.name)}</strong>`
  + `<small>${escape(pl.desc)}</small>`
  + (pl.preview ? `<small class="plan-preview">${escape(pl.preview)}</small>` : '')
  + `</span></button>`).join('');

  const jobLine = S.job
    ? `<p class="intro">在途：${escape(S.job.name)}（剩 ${S.job.remaining}/${S.job.total} 天）</p>`
    : `<p class="intro">本月尚余 ${S.clock.left} 天；短工作结束后可另作安排。</p>`;

  const bt = S.breakthrough;
  const aside =
    `<h3>本月的打算</h3>${jobLine}`
  + yearControls()
  + `<div class="plan-options" style="margin-top:14px">${planOpts}</div>`
  + hintBox([
      bt.ready ? `修为已圆满，可叩关「${bt.next}」。`
               : `修为 ${p.xp}/${p.xpNeed}，圆满后可叩关。`,
      `推进遇事会自动暂停。`,
      `修炼效率 ×${m.cult.toFixed(2)}　丹毒 ${S.poison?.current ?? 0}`,
    ]);

  // 推进报告 + 动作流 + 丹毒，放在主区上方/下方
  const reportHTML = lastReportHTML();
  const feedHTML = feed.length
    ? feed.slice(0, 12).map(f =>
        `<div class="feed-item ${f.kind}">${escape(f.text)}`
      + (f.delta ? `<span class="feed-delta">${escape(f.delta)}</span>` : '')
      + `</div>`).join('')
    : `<div class="empty">这一世的动作会记在这里。</div>`;

  const btBlock = breakthroughBlock();
  const btPanel = `<div id="bt-panel">` + panel('突破境界 · 闭关叩关', `${pct(bt.chance, 0)} 成功率`,
    `<div style="padding:14px 22px 18px">`
  + `<div class="detail-list" style="border-top:0;padding-top:0">`
  + `<div><label>目标</label><strong>${escape(bt.next)}</strong></div>`
  + `<div><label>需灵石</label><strong>${bt.cost}</strong></div>`
  + `<div><label>修为</label><strong>${fmt(p.xp)} / ${fmt(p.xpNeed)}</strong></div>`
  + `</div>`
  + `<div class="arts-actions" style="margin:0">`
  + `<button class="primary" data-action="breakthrough" ${btBlock ? 'disabled' : ''} title="${escape(btBlock)}">`
  + `${btBlock ? escape(btBlock) : '开始闭关突破'}</button>`
  + `<button data-action="stipend">领俸禄（${S.bonds.stipendBalance}）</button>`
  + `<button data-action="save">保存</button>`
  + `<button data-action="export-save">导出行卷</button>`
  + `<button data-action="import-save">导回行卷</button>`
  + `</div></div>`) + `</div>`;

  const feedPanel = asideBlock('当前动作',
    `<div class="action-feed">${feedHTML}</div>${poisonBox()}`);

  // 兽潮是全图大事：起了就摆在最上面，玩家一眼看到
  const tideBlock = (S.tide && S.tide.active)
    ? `<div style="padding:0 0 16px">${renderTidePanel(S.tide, S.checkpoint)}</div>` : '';

  return tideBlock
       + heading('这一世的山河与因缘',
                 '选一个计划，点推进即可；遇事会自动停下让你拿主意。',
                 `${S.clock.label || ''}`)
       + reportHTML
       + layout(panel('岁月行卷', '按最近发生排序', logBody), aside)
       + `<div class="chronicle-layout" style="margin-top:22px">`
       + btPanel + `<aside class="plan-panel">${feedPanel}</aside></div>`;
}

// 纪事筛选
let logFilter = 'all';
function filteredLogs(logs) {
  if (logFilter === 'all') return logs;
  if (logFilter === 'important') return logs.filter(e => e.important);
  return logs.filter(e => e.category === logFilter);
}

// 推进报告
function lastReportHTML() {
  if (!lastReport) return '';
  const hl = lastReport.highlights || [];
  const stopText = {
    event: '途中遇上事情，需要你拿主意。',
    battle: '遇上了一场斗法。',
    ended: '这一世走到了尽头。',
    done: '安然度过了这段时间。',
    error: '推进中断了。',
  }[lastReport.stopReason] || '';
  return `<div class="choice-banner" style="display:block">`
    + `<strong>推进了 ${lastReport.daysPassed} 天</strong>`
    + (hl.length ? `<ul style="margin:8px 0 0;padding-left:18px">`
        + hl.map(h => `<li>${escape(h)}</li>`).join('') + `</ul>` : '')
    + `<p style="margin:8px 0 0;font-size:12px">${escape(stopText)}</p></div>`;
}

// 丹毒面板
function poisonBox() {
  const pi = S.poison || { current: 0, threshold: 20, affecting: false,
                           cultPenalty: 1, summary: '' };
  return `<div class="poison-box ${pi.affecting ? 'warn' : ''}" style="margin-top:12px">`
    + `<div class="pb-head"><span>丹毒</span><strong>${pi.current}</strong></div>`
    + `<div class="poison-meter"><i style="width:${Math.min(100, pi.current / 2)}%"></i>`
    + `<span class="mark" style="left:${Math.min(100, pi.threshold / 2)}%"></span></div>`
    + `<div class="pb-sum">${escape(pi.summary || '')}</div>`
    + `<div class="pb-sum" style="font-size:12px;margin-top:5px">`
    + `服丹会积毒，超过 ${pi.threshold} 开始拖慢修炼。当前效率 `
    + `<b>×${pi.cultPenalty.toFixed(2)}</b>。</div></div>`;
}

// ------------------------------------------------------------
// 页签：宁州舆图
// ------------------------------------------------------------
function viewMap() {
  const p = S.player;
  if (!S.locations.some(location => location.id === mapFocus))
    mapFocus = isTravelJob(S.job) ? S.job.to : p.location;

  // 子页签：舆图 / 仙友。
  // 之前「当地仙友」接在地图下面，92 个人把交互面板挤到页面最底部 ——
  // 点了人之后要滚很久才看得到，感觉就是「点了没反应」。
  const sub = subOf('map', 'atlas');
  const tabs = subTabs('map', [
    ['atlas', '山川形势', `${S.locations.length} 处`],
    ['friends', '当地仙友', `${(S.nearby || []).length} 人`],
  ], sub);

  const head = heading('宁州舆图',
                       '山门、城池与险地各有因缘；当地仙友就在此处。',
                       `当前：${p.locationName}`);

  if (sub === 'friends') {
    // 左边名单（自己滚），右边交互面板（粘住不动）
    const right = selectedNpc
      ? renderFriendPanel(S, selectedNpc, friendBag, { materials: forgeOpts?.materials })
      : `<div class="empty">点左边任意一位仙友，即可当面往来。<br><br>
           见礼只有第一次给好感；<br>
           往后靠送礼与切磋、请教、游历。</div>`;
    return head + tabs
      + `<div class="friend-split">
           <div class="fs-list">${
             renderFriendList(S, { scope: 'local', selected: selectedNpc })
           }</div>
           <div class="fs-panel">${right}</div>
         </div>`;
  }

  return head + tabs + renderAtlas(S, { focus: mapFocus, search: mapSearch, filter: mapFilter, mode: mapModes[mapFocus], zoom: mapZoom });
}

function showTravelResult(result, to) {
  if (!result || result.error) return;
  const feedback = travelFeedback(S, to, result);
  if (feedback.arrived) { selectedNpc = null; friendBag = null; mapFocus = to; render(); }
  toast(feedback.text, feedback.kind);
}

// ------------------------------------------------------------
// ------------------------------------------------------------
// 页签：洞府道法
// 每道一张卡，列出 5 个节点的名称 / 花费 / 效果，另有「参悟 30 天」
// ------------------------------------------------------------
let artsReady = false;
let artsNodes = [];       // 十二大道的节点定义（异步拉一次）
let spellsReady = false;
let spellData = null;     // 神通全表（携带 / 卸下用）
let spellsRevision = 0;

// ------------------------------------------------------------
// 已修功法：名称 / 大道 / 层数 / 效果，可设主修与辅修
// 之前这里只显示 "tuna Lv.1" 这种原始 id，
// 玩家根本看不出自己会什么，还以为开局什么都没给。
// ------------------------------------------------------------
function manualCards() {
  const list = S.arts.manualList || [];
  if (!list.length) return `<div class="empty">还没有修习任何功法。</div>`;
  return `<div class="manual-list">` + list.map(m => `
    <div class="manual-card ${m.isMain ? 'is-main' : ''} ${m.isAux ? 'is-aux' : ''}">
      <div class="mc-top">
        <strong>${escape(m.name)}</strong>
        <span class="mc-dao">${escape(m.dao)}</span>
        <span class="mc-lv">Lv.${m.level} / 5</span>
        ${m.isMain ? '<span class="tag">主修</span>' : ''}
        ${m.isAux ? '<span class="tag aux">辅修</span>' : ''}
      </div>
      <div class="mc-effect">${escape(m.effect || '')}</div>
      <div class="mc-foot">
        ${m.maxed
          ? '<span class="mc-note">已修满五层</span>'
          : `<span class="mc-note">下一层：${m.nextDays} 天 · ${m.nextCost} 灵石</span>`}
        <span class="mc-btns">
          ${m.maxed ? '' : `<button data-action="learn" data-id="${escape(m.id)}" ${S.job ? 'disabled' : ''}>修习下一层</button>`}
          ${m.isMain ? ''
            : `<button data-action="set-main" data-id="${escape(m.id)}">设为主修</button>`}
          ${m.isMain ? '' : `<button data-action="set-aux" data-id="${escape(m.id)}">${m.isAux ? '卸下辅修' : '设为辅修'}</button>`}
        </span>
      </div>
    </div>`).join('') + `</div>`;
}

function manualSourceRows(list, action) {
  return list.map(m => {
    const owned = !!m.owned;
    const level = S.arts.manuals?.[m.id] || 0;
    const learnWhy = level >= 5 ? '已修满五层' : m.learnReason || (S.job ? '请先完成当前安排' : '');
    const why = owned ? learnWhy
      : S.player.stage < m.stage ? '境界未达获取要求'
      : action === 'library' && S.player.sect !== m.sect ? '需加入所属宗门'
      : action === 'library' && (S.contribution || 0) < m.points ? `还差 ${m.points - (S.contribution || 0)} 贡献`
      : action === 'book-buy' && S.player.location !== m.city ? '需前往武陵城书坊'
      : action === 'book-buy' && S.player.stones < m.price ? `还差 ${m.price - S.player.stones} 灵石`
      : '';
    const canLearn = owned && !S.job && level < 5 && m.canLearn !== false;
    const disabled = owned ? !canLearn : !!why;
    return `<div class="pill-row"><span>${escape(m.name)}${owned ? ` · 抄本已得${level ? ` · 已修 ${level} 层` : ''}` : ''}`
      + `<span class="item-sub">${escape(owned ? `${m.learnDays || 20 + level * 10} 天 · ${m.learnCost || 8 + (level + 1) * 4} 灵石` : m.how)}</span></span>`
      + `<button data-action="${owned ? 'learn' : action}" data-id="${escape(m.id)}" ${disabled ? 'disabled' : ''}>`
      + `${escape(disabled ? why || '暂不可修习' : owned ? level ? '修习下一层' : '开始修习' : action === 'library' ? '兑换抄本' : '购入抄本')}</button></div>`;
  }).join('');
}

// ------------------------------------------------------------
// 携带神通：名称 / 灵力 / 类型 / 规则说明
// ------------------------------------------------------------
function spellCards() {
  const list = S.arts.spellList || [];
  if (!list.length) return `<div class="empty">还没有携带任何神通。</div>`;
  return `<div class="spell-list">` + list.map(sp => `
    <div class="spell-card">
      <div class="sc-top">
        <span class="card-energy">${sp.actualCost ?? sp.cost}</span>
        <strong>${escape(sp.name)}</strong>
        <span class="card-kind">${escape(cardKind(sp))}</span>
      </div>
      <div class="sc-dao">${escape(sp.dao)}系${sp.cooldown ? ` · 冷却 ${sp.cooldown} 回合` : ''} · 所需境界：${escape(sp.stageName || '炼气初期')}</div>
      <div class="sc-rule">${escape(cardRule(sp))}</div>
      ${renderNumericPreview(sp.preview, { compact: true })}
    </div>`).join('') + `</div>`;
}

function viewArts() {
  const a = S.arts;

  // 节点定义是异步拉的，第一次先给占位
  if (!artsReady) {
    nodesLoading ||= loadNodes().then(list => {
      artsNodes = list;
      artsReady = true;
      nodesLoading = null;
      if (tab === 'arts') render();
    });
    return heading('洞府道法', '十二大道各有点亮的节点；境界到位才能往上走。',
                   `悟道点 ${a.points}`)
         + layout(panel('十二大道', '正在读取……',
             `<div class="empty">正在读取节点定义……</div>`),
             renderArtsAside(S));
  }

  const body = `<div style="padding:16px 22px 22px">`
    + `<div class="arts-grid dao-grid">${renderDaos(S, artsNodes)}</div>`
    + `</div>`;

  // 用带名称与说明的卡片，而不是原始 id
  const manuals = manualCards();
  const spells = spellCards();

  // 神通全表也是异步拉一次，用来渲染「携带 / 卸下」面板
  if (!spellsReady) {
    const revision = spellsRevision;
    spellsLoading ||= loadSpells().then(d => {
      if (revision !== spellsRevision) {
        invalidateSpells(); spellsLoading = null;
        if (tab === 'arts') render();
        return;
      }
      spellData = d;
      spellsReady = true;
      spellsLoading = null;
      if (tab === 'arts') render();
    });
  }
  const selectedBuild = (S.artBuilds || []).find(b => b.id === artBuildFilter);
  const picker = spellsReady
    ? renderSpellPicker(spellData, S, { dao: spellDaoFilter, spells: selectedBuild?.spells?.map(s => typeof s === 'string' ? s : s.id) })
    : `<div class="empty">正在读取神通表……</div>`;
  const slots = a.spellSlots ?? spellData?.max ?? 4;
  const mandatoryIds = (a.mandatorySpells || []).map(s => typeof s === 'string' ? s : s.id);
  const extraCount = (a.spells || []).filter(id => !mandatoryIds.includes(id)).length;
  const builds = renderArtBuilds(S, spellData, artsNodes, artBuildFilter);

  const sub = subOf('arts', 'practice');
  const top = heading('洞府道法',
                 '安排修炼与配给，搭配功法神通，再参悟十二大道。',
                 `悟道点 ${a.points} · 已点亮 ${a.nodes.length} 个节点`)
    + subTabs('arts', [
        ['practice', '功法与洞府', `${(a.manualList || []).length} 部`],
        ['spells', '神通搭配', `${extraCount} / ${slots}`],
        ['builds', '流派组合', `${(S.artBuilds || []).length} 组`],
        ['daos', '十二大道', `${a.nodes.length} / 60`],
      ], sub);
  if (sub === 'daos') return top + layout(panel('十二大道', `已点亮 ${a.nodes.length} / 60`, body), renderArtsAside(S));
  if (sub === 'builds') return top + panel('流派组合', '看触发条件，搭配功法、神通与道法', `<div style="padding:14px 22px 18px">${builds || '<div class="empty">暂无组合建议。</div>'}</div>`);
  if (sub === 'spells') return top
    + layout(panel('神通总览', `点击携带或卸下 · ${slots} 个额外槽`, `<div style="padding:14px 22px 18px">`
      + `<div class="spell-filters"><label>大道 <select data-spell-dao>${['all', '金', '木', '水', '火', '土', '神', '体', '剑', '气', '阵'].map(dao => `<option value="${dao}" ${dao === spellDaoFilter ? 'selected' : ''}>${dao === 'all' ? '全部大道' : `${dao}道`}</option>`).join('')}</select></label>`
      + `<button data-action="goto-art-builds">查看流派组合${selectedBuild ? `：${escape(selectedBuild.name)}` : ''}</button>`
      + (selectedBuild ? `<button data-action="clear-build-filter">显示全部神通</button>` : '') + `</div>${picker}</div>`),
        asideBlock('已携带神通', spells)
        + hintBox(['普通攻击与防御固定常备，各五张，不占额外槽。', `额外槽当前 ${slots} 个，每种神通进入牌组两张。`, '换神通后下一场斗法生效。']));
  return top + layout(panel('已修功法', '选择主修、辅修或继续修习', `<div style="padding:14px 22px 18px">${manuals}</div>`), renderArtsAside(S))
    + panel('已得功法抄本', '事件、请教或兑换获得的抄本，修习后才能设置主辅修',
      `<div style="padding:14px 22px 18px">${manualSourceRows((S.manualSources || []).filter(m => m.owned && !(S.arts.manuals?.[m.id] > 0)), 'learn') || '<div class="empty">暂无待修习的功法抄本。</div>'}</div>`)
    + renderSupplyPanel(S);
}

// ------------------------------------------------------------
// 洞府与配给：修炼速度是怎么来的，以及每月要花多少灵石
// ------------------------------------------------------------
function renderSupplyPanel(st) {
  const sup = (st.player && st.player.supply) || {};
  const cul = (st.player && st.player.cultivation) || {};
  const maxLv = sup.maxLevel || 3;

  // 没安府就不能设灵气配给：灵气只存在于洞府，府都没有谈何引气
  const hasCave = !!(st.player && st.player.cave);
  const row = (kind, label, level, cost, name, desc, note) => {
    const locked = (kind === 'qi' && !hasCave);
    const btns = [];
    for (let i = 0; i <= maxLv; i++) {
      const access = caveSupplyAccess(st, kind, i);
      const dis = (locked && i > 0) || !access.enabled;
      btns.push(`<button class="supply-btn ${i === level ? 'on' : ''}" `
        + `${dis ? 'disabled ' : ''}`
        + `title="${escape(access.reason || `${i} 档配给`)}" `
        + `data-action="supply" data-kind="${kind}" data-level="${i}">${i}</button>`);
    }
    return `<div class="supply-row">
      <div class="sr-head"><strong>${label}</strong>
        <span class="sr-name">${escape(name || '')}</span>
        <span class="sr-cost">${cost > 0 ? `${cost} 灵石 / 月` : '不耗灵石'}</span></div>
      <div class="sr-btns">${btns.join('')}</div>
      <div class="sr-desc">${escape(desc || '')}</div>
      ${note ? `<div class="sr-note">${escape(note)}</div>` : ''}
    </div>`;
  };

  const f = (v) => (v == null ? '—' : (Math.round(v * 100) / 100));

  return `<div class="chronicle-layout" style="margin-top:22px">`
    + panel('洞府与配给',
            '灵气只在洞府闭关时生效；两者都按月扣灵石，境界越高越贵',
            `<div style="padding:14px 22px 18px">
              <div class="supply-rows">
                ${row('stone', '灵石配给', sup.stoneLevel || 0, sup.stoneCost || 0,
                       sup.stoneName, sup.stoneDesc, '任何地方闭关都算。')}
                ${row('qi', '灵气配给', sup.qiLevel || 0, sup.qiCost || 0,
                       sup.qiName, sup.qiDesc,
                       hasCave ? (cul.note || '')
                               : '尚未安府：先在右侧「在此安府」才能引灵气。')}
              </div>${renderCaveSupplyLimits(st)}
            </div>`)
    + `<aside class="plan-panel">`
    + asideBlock('洞府', `<div class="detail-list" style="border-top:0;padding-top:0">
        <div><label>当前所在</label><strong>${escape((st.player && st.player.locationName) || '—')}</strong></div>
        <div><label>洞府</label><strong>${escape((st.player && st.player.caveName) || '尚未安府')}</strong></div>
        <div><label>灵气</label><strong>${st.player && st.player.qiActive ? '正在生效' : '未生效'}</strong></div>
      </div>`
      + (st.player && st.player.atCave ? ''
          : `<button class="wide-btn" data-action="cave-move">${
              st.player && st.player.cave ? '迁府至此' : '在此安府'
            }（${sup.moveCost || 0} 灵石）</button>`)
      + renderCaveUpgrade(st))
    + asideBlock('修炼速度', `<div class="speed-breakdown">
        <div><span>功法基础</span><strong>${f(cul.manual)}</strong></div>
        <div><span>资质乘数</span><strong>×${f(cul.apt)}</strong></div>
        <div><span>悟性乘数</span><strong>×${f(cul.insight)}</strong></div>
        <div><span>境界系数</span><strong>×${f(cul.realm)}</strong></div>
        <div><span>灵石配给</span><strong>×${f(cul.stone)}</strong></div>
        <div><span>灵气配给</span><strong>×${f(cul.qi)}</strong></div>
        <div class="total"><span>合计</span><strong>${f(cul.total)} / 年</strong></div>
      </div>`)
    + `</aside></div>`;
}
// 页签：宗门知交
// ------------------------------------------------------------
function viewSociety() {
  const b = S.bonds;
  // 宗门页只列本宗门的人 —— 当地仙友属于「地点」，已挪到地图页
  const all = S.nearby || [];
  const people = S.player.sect ? all.filter(n => n.sect === S.player.sect) : [];

  const cards = people.map(n => {
    const tags = [];
    if (n.isMaster) tags.push('<span class="tag master">师尊</span>');
    if (n.isPartner) tags.push('<span class="tag partner">道侣</span>');
    if (n.isDisciple) tags.push('<span class="tag disciple">弟子</span>');
    return `<button class="person-card ${selectedNpc === n.id ? 'selected' : ''}" `
      + `data-person="${n.id}">`
      + `<div class="person-head"><strong>${escape(n.name)}</strong>`
      + `<span class="tag">${escape(n.realmName)}</span>${tags.join('')}</div>`
      + `<div class="person-meta">${escape(n.gender)} · ${n.age} 岁 · `
      + `${escape(n.sectName)} · ${escape(n.personality)}</div>`
      + `<div class="person-rel">${relBar(n.relation)}`
      + `<span class="rel-num">${n.relation}</span>`
      + `<span class="rel-label">${escape(n.relationLabel)}</span></div>`
      + `</button>`;
  }).join('') || `<div class="empty">${
    S.player.sect
      ? '此地没有同门。当地仙友已挪到「宁州舆图」页，去那里找人。'
      : '你尚未拜入宗门。当地仙友在「宁州舆图」页；想入宗请看上方山门一览。'
  }</div>`;

  const sel = selectedNpc ? people.find(n => n.id === selectedNpc) : null;

  // 原创支线与野外悬赏已挪到「机缘成就」—— 它们和宗门无关，散修一样能做。

  const aside =
    `<h3>宗门与俸禄</h3>`
  + `<div class="detail-list" style="border-top:0;padding-top:0">`
  + `<div><label>宗门</label><strong>${escape(S.player.sectName || '散修')}</strong></div>`
  + `<div><label>身份</label><strong>${escape(S.player.rank || '—')}</strong></div>`
  + `<div><label>待领俸禄</label><strong>${b.stipendBalance}</strong></div>`
  + `<div><label>恶名</label><strong>${b.notoriety}</strong></div>`
  + `</div>`
  + `<div class="year-controls">`
  + `<button class="primary" data-action="stipend">领取俸禄</button>`
  + `<button data-action="expedition">探访后山</button></div>`
  + hintBox(['好感度决定能做什么：', '30 解锁彻夜论道，40 可拜师，',
             '60 可结为道侣。拜师条件与不足项可在「拜师与受业」查看。']);

  // 五宗一览：拜入山门后可做差事、以贡献兑换藏经阁功法
  const sectPanel = `<div class="chronicle-layout" style="margin-top:22px">`
    + panel('五宗一览', '拜入山门后可做差事、以贡献兑换藏经阁功法',
            `<div style="padding:16px 22px 20px">${renderSectPicker(sectData, S.player)}</div>`)
    + `<aside class="plan-panel">`
    + asideBlock('宗门', `<div class="detail-list" style="border-top:0;padding-top:0">
        <div><label>当前宗门</label><strong>${escape(S.player.sectName || '散修')}</strong></div>
        <div><label>身份</label><strong>${escape(S.rank || '—')}</strong></div>
        <div><label>贡献</label><strong>${S.contribution ?? 0}</strong></div>
        <div><label>恶名</label><strong>${S.notoriety ?? 0}</strong></div>
      </div>`
      + (S.player && S.player.bodyHp ? `<div class="detail-list" style="border-top:0">
        <div><label>体魄</label><strong>${S.player.bodyHp.points} 点</strong></div>
        <div><label>气血上限</label><strong>+${Math.round(S.player.bodyHp.bonus * 100)}%</strong></div>
      </div>${S.player.bodyHp.atSoftCap
        ? `<p class="panel-note">体魄已过软上限（${S.player.bodyHp.softCap} 点），再加每点只 +2%，封顶 +${Math.round(S.player.bodyHp.cap * 100)}%。</p>`
        : ''}` : '')
      + `<div class="detail-list" style="border-top:0">
        <div><label>寿元上限</label><strong>${S.player.lifespan} 岁${S.player.lifeBonus ? `（天赋 ${S.player.lifeBonus > 0 ? '+' : ''}${S.player.lifeBonus}）` : ''}</strong></div>
        <div><label>最高境界</label><strong>${escape(S.player.realmName)}${S.player.highestStage > S.player.stage ? `（曾至第 ${S.player.highestStage + 1} 境）` : ''}</strong></div>
      </div>`
      + (S.notorietyNote ? `<p class="panel-note">${escape(S.notorietyNote)}</p>` : ''))
    + `</aside></div>`;

  const inSect = !!(S.player && S.player.sect);

  // 子页签只列「你现在真的有的东西」。
  // 散修不该看到本门弟子 / 山门差事 / 藏经阁 —— 那都是宗门的东西，
  // 没入宗就不存在。野外悬赏与原创支线也不是宗门的事，已挪到「机缘成就」。
  const sub = inSect ? subOf('society', 'members') : 'sect';
  const tabItems = inSect
    ? [
        ['sect', '本门概况'],
        ['members', '本门弟子', `${people.length} 人`],
        ['mentors', '拜师与受业'],
        ['jobs', '山门差事'],
        ['library', '藏经阁', `${S.contribution ?? 0} 贡献`],
      ]
    : [['sect', '拜入山门']];
  const tabs = subTabs('society', tabItems, sub);

  const head = heading('宗门知交',
                       inSect ? '本门事务、弟子往来与藏经阁都在这里。'
                              : '尚未拜入宗门：先在山门一览里择一宗。',
                       inSect ? `${escape(S.player.sectName)}` : '散修');

  // 未入宗时的「拜入山门」面板（入宗后就不需要了）
  const joinPanel = `<div class="chronicle-layout">`
    + panel('拜入山门',
            '抵达山门报名考核，战胜试炼弟子后拜入本门',
            `<div style="padding:16px 22px 20px">${renderSectPicker(sectData, S.player)}</div>`
            + `<p class="panel-note" style="padding:0 22px 16px">
                 入门考核是一场非致命切磋，可体验本门流派。部分宗门还有声望或身份要求，报名条件在各宗卡片上列明。
               </p>`)
    + `</div>`;

  // ---- 本门概况 / 拜入山门 ----
  if (sub === 'sect') {
    const bonds = renderBondActions(S);
    return head + tabs + (inSect ? sectPanel : joinPanel)
      + (bonds ? `<div style="margin-top:18px">${panel('师门日常与道侣', '授业、共修与关系往来', `<div style="padding:14px 22px 18px">${bonds}</div>`)}</div>` : '');
  }

  // ---- 本门弟子 ----
  if (sub === 'mentors') return head + tabs
    + panel('同门师长', '查看拜师条件，增进好感后行礼；拜师后回山门受业',
      `<div style="padding:14px 22px 18px">${sectData ? renderSectMentors(sectData, S) : '<div class="empty">正在读取师门信息……</div>'}</div>`);

  if (sub === 'members') {
    const right = sel
      ? renderFriendPanel(S, sel.id, friendBag, { materials: forgeOpts?.materials })
      : `<div class="empty">点左边任意一位同门，即可当面往来。<br><br>
           见礼只有第一次给好感；<br>
           好感 30 解锁彻夜论道；拜师还需同宗高境界、声望与拜师礼，可在「拜师与受业」查看。</div>`;
    return head + tabs
      + `<div class="friend-split">
           <div class="fs-list"><div class="people-grid">${cards}</div></div>
           <div class="fs-panel">${right}</div>
         </div>`;
  }

  // ---- 山门差事 ----
  if (sub === 'jobs') {
    return head + tabs
      + panel('宗门任务板', '完成差事可得灵石、贡献与本宗大道经验',
              `<div style="padding:14px 22px 18px">${renderSectTasks(S, ecoOpts, sectData)}</div>`);
  }

  // ---- 藏经阁 ----
  if (sub === 'library') {
    const manuals = (S.manualSources || []);
    const sectBooks = manuals.filter(m => m.sect === S.player.sect);
    const cityBooks = manuals.filter(m => m.city);
    const rows = (list, act, label) => list.length
      ? manualSourceRows(list, act)
      : `<div class="empty">暂无可兑换的功法。</div>`;
    return head + tabs
      + `<div class="chronicle-layout">`
      + panel('藏经阁', '用宗门贡献兑换 · 仅限本门',
              `<div style="padding:14px 22px 18px">
                 <div class="detail-list" style="border-top:0;padding-top:0">
                   <div><label>当前宗门</label><strong>${escape(S.player.sectName || '散修')}</strong></div>
                   <div><label>宗门贡献</label><strong>${S.contribution ?? 0}</strong></div>
                 </div>
                 <h3 class="sect-library-heading">本门神通</h3>
                 ${renderSectSpellLibrary(S)}
                 <h3 class="sect-library-heading">功法抄本</h3>
                 ${rows(sectBooks, 'library', '兑换')}
               </div>`)
      + `<aside class="plan-panel">`
      + asideBlock('书坊（用灵石）',
                   rows(cityBooks, 'book-buy', '购入'),
                   '书坊在城中，不占宗门贡献')
      + `</aside></div>`;
  }

  // 入宗后只剩宗门自己的事。野外悬赏与原创支线已挪到「机缘成就」——
  // 它们和宗门无关，散修一样能做。
  return head + tabs
    + panel('本门事务', '差事与藏经阁在各自的页签里',
            `<div style="padding:14px 22px 18px">
               <div class="empty">用上面的页签切换：差事、藏经阁、弟子往来。</div>
             </div>`);
}

// ------------------------------------------------------------
// 页签：机缘成就
// ------------------------------------------------------------
// ------------------------------------------------------------
// 页签：好友通讯录
// 名单数据由 roster.mjs 管理（筛选/排序/缓存都在那边），
// 这里只负责取数、出 HTML，并把交互绑回去。
// ------------------------------------------------------------
function viewRoster() {
  // 名单还没拉到就先拉一次，拉完重绘
  if (!rosterReady) {
    loadRoster(true).then(() => {
      rosterReady = true;
      if (tab === 'roster') render();
    });
  }
  return renderRoster(S);
}

// 绑定名单页的筛选、排序与传讯/邀请/当面往来
function bindRosterTab() {
  const root = $('#workspace');
  if (!root) return;
  bindRoster(root, {
    onRerender: () => render(),
    onAction: async (action, npcId) => {
      if (action === 'message') {
        const r = await call('message', { npc: npcId });
        if (r && !r.error) toast('已传讯', 'good');
      } else if (action === 'invite') {
        const r = await call('invite', { npc: npcId });
        if (r && !r.error) toast('已送出邀请', 'good');
      } else if (action === 'meet') {
        // 当面往来：切到知交页并选中这个人
        selectedNpc = npcId;
        tab = 'map'; setSub('map', 'friends');
        friendBag = null;
        const bag = await loadNpcBag(npcId, true);
        if (selectedNpc === npcId) friendBag = bag;
        render();
        return;
      }
      invalidateRoster();
      await loadRoster(true);
      render();
    },
  });
}
function viewQuests() {
  const adv = S.adventure;
  const p = S.player;

  const aside =
    `<h3>怎么攒机缘</h3>`
  + hintBox(['把月度计划设为「游历寻缘」，', '累计满 20 天开始有遇事机会，',
             '满 60 天必遇一段。', '很多支线还要求你在特定地点。']);

  const advPanel = panel('游历', `${adv.events} 次遇事`, `<div style="padding:16px 22px 20px">
    <div class="detail-list" style="border-top:0;padding-top:0">
      <div><label>当前累计</label><strong>${adv.since} 天</strong></div>
      <div><label>总游历</label><strong>${adv.total} 天</strong></div>
      <div><label>遇事次数</label><strong>${adv.events}</strong></div>
      <div><label>游历胜场</label><strong>${adv.wins}</strong></div>
    </div>
    <div class="arts-actions" style="margin:0">
      <button data-action="plan" data-id="explore">切到游历寻缘</button>
      <button data-action="advance" data-days="60">游历 60 天</button>
    </div></div>`);

  const speedPanel = panel('遁法与游历', '赶路、先手与脱身', `<div style="padding:16px 22px 20px">
    <div class="detail-list" style="border-top:0;padding-top:0">
      <div><label>当前遁速</label><strong>${escape(p.escapeSpeed ?? '—')}</strong></div>
      <div><label>境界</label><strong>${escape(p.realmName)}</strong></div>
    </div>
    <p class="panel-note">遁速影响赶路耗时、斗法先手和逃脱机会。学习遁法、修行境界与装备带来的变化，以当前实际遁速为准。</p></div>`);

  const filePanel = panel('本世档案', '', `<div style="padding:16px 22px 20px">
    <div class="detail-list" style="border-top:0;padding-top:0">
      <div><label>出身天赋</label><strong>${escape(p.traits.join('、'))}</strong></div>
      <div><label>灵根资质</label><strong>${p.apt}</strong></div>
      <div><label>福缘</label><strong>${p.luck}</strong></div>
      <div><label>悟性</label><strong>${p.insight}</strong></div>
      <div><label>体魄</label><strong>${p.body}</strong></div>
      <div><label>丹术</label><strong>${p.alchemy}</strong></div>
      <div><label>声望</label><strong>${p.renown}</strong></div>
    </div></div>`);

  // 成就（14 项）：规则层一直有，之前既没面板也从不发放
  // 声望：把「有什么用」直接摊开给玩家看
  const renownPanel = panel('宁州声望', '名声是你在宁州的信誉资本',
    `<div style="padding:16px 22px 20px">${renderRenownPanel(S.renownInfo)}</div>`);

  const achPanel = panel('成就', '达成后计入传承点',
                         `<div style="padding:16px 22px 20px">${renderAchievements(achData)}</div>`);

  // 野外悬赏与原创支线挪到这里 —— 它们是「出门闯荡」的事，与宗门无关。
  const wildPanel = panel('野外悬赏', '战败会陨落',
    `<div style="padding:14px 22px 18px">
       <p class="panel-note">战败将结束这一世。${S.checkpoint?.enabled ? '意外陨落后可选择接受结局，或回到最近存档；回档会撤回保存之后的进度。' : '请留意双方气血与敌方意图，及时判断能否继续迎战。'}</p>
       <div class="arts-grid" style="margin:0">${
         (S.wildTasks || []).map(t =>
           `<button class="card" data-action="wild" data-id="${escape(t.id)}" ${t.enabled === false ? 'disabled' : ''}>
              <div class="card-top"><span class="card-glyph">⚔</span>
                <strong>${escape(t.name)}</strong></div>
              <p class="card-rule">${escape(t.desc)}</p>
              <div class="card-foot">${t.days} 天 · 3 灵石 · 赏金 ${t.fee}${t.reason ? ` · ${escape(t.reason)}` : ''}</div>
            </button>`).join('')
         || '<div class="empty">此地没有悬赏。</div>'
       }</div></div>`);

  const storyPanel = panel('原创支线',
    `${Object.keys(S.bonds.stories).length} / ${S.stories.length} 已落笔`,
    `<div style="padding:14px 22px 18px">
       <div class="arts-grid" style="margin:0">${
         S.stories.map(s =>
           `<button class="card ${s.available ? '' : 'dead'}"
                    ${s.available ? `data-action="story" data-id="${s.id}"` : 'disabled'}>
              <div class="card-top"><span class="card-glyph">✦</span>
                <strong>${escape(s.name)}</strong>${
                  s.done ? '<span class="count">已落笔</span>'
                         : s.hidden ? '<span class="count">隐藏</span>' : ''
                }</div>
              <p class="card-rule">${escape(s.desc)}</p>
              <div class="card-foot">${s.available
                ? `${s.days} 天 · 点击阅读`
                : escape(s.reason)}</div>
            </button>`).join('')
       }</div></div>`);

  const sub = subOf('quests', 'travel');
  const tabs = subTabs('quests', [
    ['travel', '游历与剑道'],
    ['wild', '悬赏与支线', `${(S.wildTasks || []).length} 项`],
    ['renown', '声望与成就'],
  ], sub);
  const head = heading('机缘成就', '游历、悬赏、剑道、成就与本世档案。', '');

  if (sub === 'wild') {
    return head + tabs
      + `<div style="display:grid;gap:22px">${wildPanel}${storyPanel}</div>`;
  }
  if (sub === 'renown') {
    return head + tabs
      + `<div class="chronicle-layout">`
      + filePanel + `<aside class="plan-panel">${achPanel}</aside></div>`
      + `<div style="margin-top:22px">${renownPanel}</div>`;
  }
  return head + tabs
       + layout(`<div style="display:grid;gap:22px">${advPanel}${speedPanel}</div>`,
                aside);
}

// ------------------------------------------------------------
// 页签：任务与工坊
// 商会委托 / 猎妖 / 寻矿 / 演武 / 机缘 / 炼器
// ------------------------------------------------------------
let ecoOpts = [];
let forgeOpts = { materials: [], shapes: [] };

function viewEconomy() {
  const p = S.player;
  const r = renderEconomy(S, ecoOpts, forgeOpts, forgeSelection, forgePreview);

  const aside =
    `<h3>营生</h3>`
  + r.job
  + `<div class="detail-list" style="border-top:0;padding-top:0">`
  + `<div><label>灵石</label><strong>${fmt(p.stones)}</strong></div>`
  + `<div><label>宗门贡献</label><strong>${S.contribution ?? 0}</strong></div>`
  + `<div><label>气血</label><strong>${p.health} / 100</strong></div>`
  + `</div>`
  + hintBox(['猎妖与演武需要气血充足。',
             '商会委托每月同类最多三件，做满三件有额外授业。',
             '限时机缘有时间窗，错过就要等下一年。']);

  return heading('任务与工坊', '接差事、领委托、开炉铸炼。',
                 S.inCity ? '在城中' : '不在城中')
       + layout(panel('可接行动', '', `<div style="padding:14px 22px 18px">`
           + (r.body || '<div class="empty">此地没有可接的行动。</div>')
           + `</div>`), aside)
       + `<div class="chronicle-layout" style="margin-top:22px">`
       + panel('铸炼法器', '主料 4 · 辅料 3 · 灵核 3',
               `<div style="padding:14px 22px 18px">${r.forge}${renderForgePreview(forgePreview)}</div>`)
       + `<aside class="plan-panel">`
       + asideBlock('限时机缘', r.quests || '<div class="empty">今年没有可参与的机缘。</div>')
       + `</aside></div>`;
}

// 切到该页时把行动清单拉回来
async function refreshEconomy() {
  if (economyLoading) return economyLoading;
  economyLoading = (async () => {
    const [o, f] = await Promise.all([loadEcoOptions(true), loadForgeOptions(true)]);
    ecoOpts = o;
    forgeOpts = f;
    const defaults = { main: 'iron', aux: 'jade', core: 'wood', shape: f.shapes?.[0]?.id };
    for (const key of ['main', 'aux', 'core', 'shape']) {
      const options = key === 'shape' ? f.shapes : f.materials;
      if (!(options || []).some(option => option.id === forgeSelection[key]))
        forgeSelection[key] = (options || []).find(option => option.id === defaults[key])?.id || options?.[0]?.id || '';
    }
    economyReady = true;
    await refreshForgePreview();
  })();
  try { await economyLoading; }
  finally { economyLoading = null; }
}
async function refreshForgePreview() {
  const serial = ++forgePreviewSerial;
  forgePreview = null;
  if (tab === 'economy' && economyReady) render();
  const response = await fetch('/api/forge-preview', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(forgeSelection),
  }).then(response => response.json()).catch(() => ({ error: '配方推演暂未加载。' }));
  if (serial !== forgePreviewSerial) return;
  forgePreview = response;
  if (tab === 'economy') render();
}
// ------------------------------------------------------------
// 页签：坊市行囊
// 买卖 / 炼丹 / 服用 / 装备 / 出售 / 修炉换炉 / 书坊藏经阁
// ------------------------------------------------------------
function viewMarket() {
  const p = S.player;
  const a = S.arts;

  // ---- 坊市买卖 ----
  const KINDS = [['herb', '药材'], ['material', '炼器材料'], ['potion', '丹药'], ['gear', '法器']];
  const tabs = KINDS.map(([k, name]) =>
    `<button class="${shopKind === k ? 'active' : ''}" data-shop="${k}">${name}</button>`).join('');

  let shopBody;
  if (!S.inCity) {
    shopBody = `<div class="empty">此地没有坊市。去「宁州舆图」前往城池即可买卖。</div>`;
  } else {
    const items = (S.market || []).filter(i => i.kind === shopKind);
    shopBody = items.length
      ? `<div class="shop-list" style="padding:14px 22px 18px">` + items.map(i => {
          const qty = i.kind === 'gear' ? 1 : (shopQty[i.id] ?? 1);
          const total = i.price * qty;
          const afford = p.stones >= total;
          const available = afford && i.enabled !== false && i.locked !== true;
          return `<div class="shop-row">
            <div><div class="item-name">${escape(i.name)}</div>
            <div class="item-sub">${escape(i.desc || '')}`
            + `${i.owned ? ` · 已有 ${i.owned}` : ''}`
            + `${i.stageName ? ` · 需${escape(i.stageName)}` : ''}`
            + `${i.reason ? `<span class="market-item-reason">${escape(i.reason)}</span>` : ''}`
            + `</div></div>
            <div class="price">${i.price} 灵石</div>
            <div class="qty">
              <button data-qty="${i.id}" data-delta="-1" ${i.kind === 'gear' || qty === 1 ? 'disabled' : ''}>−</button>
              <input type="number" min="1" max="99" value="${qty}"
                     data-qtyinput="${i.id}" ${i.kind === 'gear' ? 'disabled' : ''}>
              <button data-qty="${i.id}" data-delta="1" ${i.kind === 'gear' || qty === 99 ? 'disabled' : ''}>+</button>
            </div>
            <button class="buy" ${available ? '' : 'disabled'}
                    data-action="buy" data-kind="${i.kind}" data-id="${i.id}"
                    data-count="${qty}">
              ${i.enabled === false || i.locked === true ? '未开放' : afford ? `买入 ${i.kind === 'gear' ? '' : total}` : `差 ${total - p.stones}`}
            </button></div>`;
        }).join('') + `</div>`
      : `<div class="empty">这一类暂时没有货。${['herb','material'].includes(shopKind) ? '高阶材料需探索秘境、委托或其他稀有来源。' : ''}</div>`;
  }

  // ---- 灵核 / 凝元丹 ----
  const stock = S.inCity ? `<div style="padding:12px 22px 4px">
    <div class="shop-row">
      <div><div class="item-name">灵核</div>
      <div class="item-sub">大境界突破消耗 1 枚，成功率 +10% · 坊市存 ${S.world.coreStock ?? '待查'}</div></div>
      <div class="price">${S.coreBuyPrice ?? 0} 灵石</div>
      <div class="qty">已有 ${p.cores ?? 0}</div>
      <button class="buy" data-action="trade-stock" data-kind="cores" data-buy="1" ${p.stones >= S.coreBuyPrice && S.world.coreStock !== 0 ? '' : 'disabled'}>买入</button>
      <button class="buy" data-action="trade-stock" data-kind="cores" data-buy="0"
              ${(p.cores ?? 0) > 0 ? '' : 'disabled'}>卖出</button>
    </div>
    <div class="shop-row">
      <div><div class="item-name">凝元丹</div>
      <div class="item-sub">大境界突破消耗 1 枚，成功率 +18% · 坊市存 ${S.world.pillStock ?? '待查'}</div></div>
      <div class="price">${S.pillBuyPrice ?? 0} 灵石</div>
      <div class="qty">已有 ${p.pills ?? 0}</div>
      <button class="buy" data-action="trade-stock" data-kind="pills" data-buy="1" ${p.stones >= S.pillBuyPrice && S.world.pillStock !== 0 ? '' : 'disabled'}>买入</button>
      <button class="buy" data-action="trade-stock" data-kind="pills" data-buy="0"
              ${(p.pills ?? 0) > 0 ? '' : 'disabled'}>卖出</button>
    </div></div>` : '';

  // ---- 炼丹 ----
  const recipes = (S.brewOptions || []).filter(o => !brewSearch || `${o.name} ${o.desc} ${o.reason}`.includes(brewSearch.trim()));
  const brew = recipes.filter(o => o.enabled);
  const brewCards = brew.map(o =>
    `<button class="craft-card" data-action="craft" data-id="${o.id}">`
  + `<div class="craft-top"><strong>${escape(o.name)}</strong>`
  + `<span class="rate ${o.chance < 0.4 ? 'low' : ''}">成丹 ${pct(o.chance)}</span></div>`
  + `<div class="craft-cost">开炉 ${o.stoneCost} 灵石 · ${o.days} 天</div>`
  + `<p class="craft-desc">${escape(o.desc)}</p>`
  + `</button>`).join('')
    || `<div class="empty">材料或灵石不足，暂时无法开炉。</div>`;

  const blocked = recipes.filter(o => !o.enabled);
  const blockedList = blocked.map(o =>
    `<div class="feed-item bad">${escape(o.name)}：${escape(o.reason)}</div>`).join('');

  // ---- 丹药（服用 / 出售）----
  const potionRows = Object.entries(a.potions || {}).filter(([, n]) => n > 0).map(([id, n]) => {
    const opt = (S.market || []).find(m => m.kind === 'potion' && m.id === id)
      || (S.brewOptions || []).find(m => m.id === id);
    const used = (a.used || {})[id] || 0;
    return `<div class="pill-row">
      <span>${escape(opt ? opt.name : id)} ×${n}`
    + ` <span class="item-sub">已服 ${used}</span></span>
      <span style="display:flex;gap:6px">
      <button data-action="use-pill" data-id="${id}">服用</button>
      <button data-action="sell" data-kind="potion" data-id="${id}" ${S.inCity ? '' : 'disabled'}>${S.inCity ? '出售' : '城中可售'}</button>
      </span></div>`;
  }).join('') || `<div class="empty">还没有炼成丹药。</div>`;

  // ---- 法器（装备 / 卸下 / 分解 / 铭刻 / 出售）----
  const gearRows = (S.gears || []).map(g =>
    `<div class="pill-row">
      <span>${escape(g.name)}
      <span class="item-sub">${escape({ weapon: '武器', armor: '防具', accessory: '佩饰' }[g.slot] || g.slot)} · 加成 +${pct(g.bonus)}`
    + `${g.inscribed ? ' · 已铭刻' : ''}${g.shapeName ? ` · ${escape(g.shapeName)}` : ''}${g.element ? ` · ${escape(g.element)}${escape(g.polarity || '')}` : ''}</span>${renderArtifactAbility(g.ability)}</span>
      <span style="display:flex;gap:5px;flex-wrap:wrap">
      ${g.equipped
        ? `<button data-action="gear/unequip" data-id="${g.id}">卸下</button>`
        : `<button data-action="equip" data-id="${g.id}">装备</button>`}
      ${g.inscribed ? '' : `<button data-action="gear/inscribe" data-id="${g.id}">铭刻</button>`}
      <button data-action="gear/salvage" data-id="${g.id}">分解</button>
      ${g.equipped ? '' : `<button data-action="sell" data-kind="gear" data-id="${g.id}" ${S.inCity ? '' : 'disabled'}>${S.inCity ? '出售' : '城中可售'}</button>`}
      </span></div>`).join('')
    || `<div class="empty">还没有法器。</div>`;

  // ---- 丹炉 ----
  const furnaces = (S.furnaces || []).map(f =>
    `<div class="pill-row"><span>${escape(f.name)}`
  + ` <span class="item-sub">${f.grade} 品 · 容量 ${f.capacity}</span>`
  + `${f.current ? ' <span class="tag">使用中</span>' : ''}</span>`
  + `<button data-action="furnace/buy" data-id="${f.id}" `
  + `${f.current ? 'disabled' : ''}>${f.current ? '使用中' : `${f.price} 灵石`}</button></div>`
  ).join('') || '（无）';

  // ---- 功法获取 ----
  const manuals = (S.manualSources || []);
  // 藏经阁（宗门贡献兑换）已挪到「宗门知交 → 藏经阁」页 ——
  // 它用宗门贡献，是宗门的事，放在坊市页里没人找得到。
  const bookRows = manualSourceRows(manuals.filter(m => m.city), 'book-buy');

  const aside =
    `<h3>行情</h3>`
  + `<p class="intro">当前行情 ×${S.world.market.toFixed(2)}`
  + `${S.world.market > 1 ? '（偏贵）' : S.world.market < 1 ? '（便宜）' : '（平常）'}`
  + ` · 坊市收购资金 ${S.world.marketCash ?? '—'}</p>`
  + `<div class="detail-list" style="border-top:0;padding-top:0">`
  + `<div><label>灵石</label><strong>${fmt(p.stones)}</strong></div>`
  + (S.renownInfo && S.renownInfo.buyDiscount > 0
      ? `<div><label>声望议价</label><strong>买入 −${Math.round(S.renownInfo.buyDiscount * 100)}%
           · 卖出 +${Math.round(S.renownInfo.sellBonus * 100)}%</strong></div>`
      : '')
  + `<div><label>宗门贡献</label><strong>${S.contribution ?? 0}</strong></div>`
  + `<div><label>灵核 / 凝元丹</label><strong>${p.cores ?? 0} / ${p.pills ?? 0}</strong></div>`
  + `<div><label>丹炉耐久</label><strong>${a.durability} / 40</strong></div>`
  + `</div>`
  + `<div class="year-controls" style="margin-top:10px">`
  + `<button data-action="furnace/repair">修炉（20 灵石）</button>`
  + `</div>`
  + hintBox(['本地出产的药材便宜两成。',
             '炼丹会花费时间；遇事暂停后，可回岁月纪事继续推进。',
             '出售受坊市收购资金限制，每月同类最多 8 件。']);

  // 子页签：买卖 / 炼丹 / 丹炉法器。
  // 之前这一页是「坊市 + 炼丹 + 丹炉 + 书坊 + 藏经阁」一路拉到底。
  const sub = subOf('market', 'shop');
  const mTabs = subTabs('market', [
    ['shop', '坊市买卖'],
    ['brew', '开炉炼丹', `${brew.length} 可炼`],
    ['kit', '丹炉与法器'],
  ], sub);
  const mHead = heading('坊市行囊', '买卖、炼丹、服用、装备。',
                        S.inCity ? '在城中' : '不在城中');

  if (sub === 'brew') {
    return mHead + mTabs
      + layout(panel('开炉炼丹', `${(S.brewOptions || []).length} 张丹方`, `<div style="padding:14px 22px 18px">
          <div class="recipe-controls"><label>查丹方 <input data-brew-search type="search" placeholder="丹方名称、效果或药材" value="${escape(brewSearch)}"></label>
          <label>开炉次数 <select data-brew-batch>${[1, 3, 5, 10].map(n => `<option value="${n}" ${n === brewBatch ? 'selected' : ''}>${n} 炉</option>`).join('')}</select></label></div>
          <p class="panel-note">下方灵石、耗时为每炉投入；批量炼制会逐炉扣料，遇事暂停。</p>
          <div class="craft-grid">${brewCards}</div>`
          + (blockedList ? `<details class="blocked-recipes"><summary>暂不可炼 ${blocked.length} 张 · 查看缺料与条件</summary>${blockedList}</details>` : '')
          + `</div>`),
        asideBlock('丹药行囊', potionRows)
          + hintBox([S.job ? `${S.job.name}尚余 ${S.job.remaining} 天，请在岁月纪事推进。` : '没有进行中的安排。']));
  }
  if (sub === 'kit') {
    return mHead + mTabs
      + `<div class="chronicle-layout">`
      + panel('法器行囊', '装备、铭刻、分解与出售',
              `<div style="padding:14px 22px 18px">${gearRows}</div>`)
      + `<aside class="plan-panel">`
      + asideBlock('丹炉', furnaces)
      + `<button data-action="furnace/repair">修炉（20 灵石）</button>`
      + `</aside></div>`
      + `<div style="margin-top:18px">${panel('书坊与功法抄本', '购入后需修习才能生效', `<div style="padding:14px 22px 18px">${bookRows || '<div class="empty">暂无可购入的功法。</div>'}</div>`)}</div>`;
  }
  return mHead + mTabs
       + layout(panel('坊市', '', `<div style="padding:14px 22px 0">
           <div class="shop-tabs">${tabs}</div><p class="market-material-note">普通坊市只供应一、二阶材料；高阶材料可在天机阁购买秘境位置，通关取得 ×10。</p></div>${shopBody}${stock}`), aside);
}
// ------------------------------------------------------------
function viewBattle() {
  const b = S.battle;
  const bar = (v, m) => `${Math.max(0, Math.min(100, (v / m) * 100))}%`;
  const npc = (S.nearby || []).find(n => n.id === b.npcId);
  const enemy = { ...(npc || {}), name: b.enemy, portrait: b.enemyPortrait || npc?.portrait, stage: b.enemyStage };
  const enemyKind = b.enemyBeast ? 'beast' : 'npc';
  const enemyRealm = b.enemyRealmName || npc?.realmName
    || (b.enemyStage != null ? ['炼气', '筑基', '金丹', '元婴', '化神'][Math.floor(b.enemyStage / 3)] : '');

  // 自身状态徽章：卡牌游戏的核心信息，之前一个都没显示
  const badges = battleStatusRows(b);

  const enemyBadges = [];
  if (b.enemyShield > 0) enemyBadges.push(`盾 ${b.enemyShield}`);
  if (b.poison > 0) enemyBadges.push(`中毒 ${b.poison} 层${b.poisonTurns ? ` · 剩 ${b.poisonTurns} 回合` : ''}`);
  if (b.burn > 0) enemyBadges.push(`灼烧：每回合 ${b.burn} 点${b.burnTurns ? ` · 剩 ${b.burnTurns} 回合` : ''}`);
  if (b.soul > 0) enemyBadges.push(`震荡 ${b.soul} / 6 层 · 神识爆发消耗`);

  const actor = (name, hp, max, shield, isEnemy) =>
    `<section class="combat-actor${isEnemy ? ' combat-enemy' : ''}" data-combat-actor="${isEnemy ? 'enemy' : 'player'}">`
  + `<span class="combat-label">${isEnemy ? '对手' : '自身'} · ${escape(isEnemy ? enemyRealm : S.player.realmName)}</span>`
  + `<div class="combat-character">${renderPortrait(isEnemy ? enemy : S.player, isEnemy ? enemyKind : 'player', 'combat-portrait')}`
  + `<div class="combat-character-stats"><h3>${escape(name)}</h3>`
  + `<div class="combat-vital"><b>${Math.max(0, hp)}</b>`
  + `<span>/ ${max} 气血</span><em>盾 ${shield}</em></div>`
  + `<div class="combat-health"><i style="width:${bar(hp, max)}"></i></div></div></div>`
  + (Number.isFinite(isEnemy ? b.enemySpeed : b.playerSpeed) ? `<p class="combat-speed">遁速 ${isEnemy ? b.enemySpeed : b.playerSpeed}</p>` : '')
  + `<div class="combat-status">`
  + ((isEnemy ? enemyBadges : badges).length
      ? (isEnemy ? enemyBadges : badges)
          .map(t => typeof t === 'string' ? `<span class="status-chip">${escape(t)}</span>`
            : `<span class="status-chip ${t.tone}"><b>${escape(t.label)}：${escape(t.value)}</b><small>${escape(t.scope)}</small></span>`).join('')
      : `<span class="status-none">${isEnemy ? '无状态' : '无增益'}</span>`)
  + `</div></section>`;

  // 手牌：显示此刻的真实伤害（来自 C++ 的 battlePreview），
  // 不能出的牌直接置灰并写明原因，不用玩家一张张试。
  const hand = b.hand.map(c => {
    const playable = c.playable !== false;
    return `<button class="card card-combat ${playable ? '' : 'card-disabled'}"
                    data-action="play" data-id="${c.uid}"
                    ${playable ? '' : 'disabled'}>
      <div class="card-top"><span class="card-energy">${c.cost}</span>`
  + `<strong>${escape(c.name)}</strong>`
  + `<span class="card-kind">${escape(cardKind(c))}</span></div>`
  + `<span class="card-school">${escape(c.dao)}系</span>`
  + `<span class="card-effect">${escape(cardNumbers(c)) || '—'}</span>`
  + `<span class="card-rule">${escape(cardRule(c))}</span>`
  + (playable
      ? `<span class="card-foot">${c.requiresTarget || c.kind === 'exhaust' ? '选择要移除的神通' : '点击出牌'}</span>`
      : `<span class="card-foot card-why">${escape(c.reason || '当前不可用')}</span>`)
  + `</button>`;
  }).join('') || `<div class="empty-hand">手牌已用完。结束回合后重新抽牌。</div>`;

  // 战报：末两条 + 可展开的全部
  const recent = b.log.slice(-2)
    .map(t => `<p>${escape(t)}</p>`).join('') || `<p>斗法开始。</p>`;
  const fullLog = b.log.length > 2
    ? `<details class="battle-history"><summary>全部战报（${b.log.length} 条）</summary>`
      + b.log.map(t => `<p>${escape(t)}</p>`).join('') + `</details>`
    : '';

  const cooldowns = Object.entries(b.cooldowns || {});
  const cdLine = cooldowns.length
    ? `<div class="cooldown-line">冷却中：`
      + cooldowns.map(([id, n]) => {
          const card = b.hand.find(c => c.id === id);
          const definition = card || (S.arts.spellList || []).find(c => c.id === id)
            || (spellData?.spells || []).find(c => c.id === id);
          return `${escape(definition?.name || id)}（${n}）`;
        }).join(' · ') + `</div>`
    : '';

  return `<div class="card-combat">`
    + `<header class="combat-round">`
    + `<span>${b.lethal
        ? '<b class="lethal-tag">生死斗法</b>'
        : '切磋 / 考核'} · 第 <b>${b.turn + 1}</b> / 24 回合</span>`
    + `<span>月历暂停 · 出牌阶段${b.playerFirst != null ? ` · ${b.playerFirst ? '你方先手' : '敌方先手'}` : ''}</span></header>`
    + (b.lethal
        ? `<div class="lethal-warn">${S.checkpoint?.enabled ? '战败会陨落。意外死亡后可选择接受结局，或回到最近存档；回档会撤回保存之后的进度。' : '战败会陨落并结束这一世，请留意双方气血与敌方意图。'}</div>`
        : '')

    + `<div class="combat-field">`
    + actor(S.player.name, b.hp, b.max, b.shield, false)
    + `<div class="combat-intent"><span>敌方意图</span>`
    + `<strong>${b.intentDamage ?? b.intentRaw ?? 0}</strong><b>${escape(b.intentName)}</b>`
    + `<p>${escape(b.intentDescription || '敌方在结束回合后行动；继续出牌会影响实际结算。')}</p>`
    + `<small>预估气血伤害，含本次结束回合的已有持续伤害${b.intentShield ? ` · 预计获得 ${b.intentShield} 护盾` : ''}${b.intentHeal ? ` · 预计治疗 ${b.intentHeal}` : ''}</small></div>`
    + actor(b.enemy, b.enemyHp, b.enemyMax, b.enemyShield, true)
    + `</div>`

    + `<div class="combat-flow">`
    + `<div class="energy-orb"><b>${b.mana}</b><span>灵力</span></div>`
    + `<p>${b.nextMana != null ? `下回合实际回复 <b>${b.nextMana}</b> 点灵力` : `基础每回合回复 <b>${b.maxMana}</b> 点灵力`} · 上限 ${b.manaCap ?? 8}<br>`
    + `<small>基础灵力 ${b.maxMana} · 每回合抽 5 张${b.drawNext ? `，下回合额外抽 ${b.drawNext} 张` : ''}<br>`
    + `神通与法宝可连续施展，敌方在结束回合后行动`
    + ` · 本回合施术 ${b.played} / 24 次<br>`
    + `${b.carryMana ? `已封存 ${b.carryMana} 点，下回合解封一次` : '未封存的剩余灵力不会带到下回合'}</small></p>`
    + `<button class="primary end-turn" data-action="end-turn">结束回合 →</button>`
    + `</div>`
    + cdLine
    + renderBattleArtifacts(b)
    // 手牌：之前 hand 算好了却没插进返回值，战斗界面一张牌都不显示
    + `<div class="combat-hand">${hand}</div>`
    + `<footer class="combat-bottom">`
    + `<div class="combat-piles"><span>抽牌堆 ${b.draw}</span>`
    + `<span>弃牌堆 ${b.discard}</span></div>`
    + `<button class="subtle" data-action="escape">${b.lethal
        ? Number.isFinite(b.escapeChance) && b.escapeChance >= 1 ? '撤离本场' : '尝试逃离（失败由敌方行动）'
        : '退出本场'}${b.lethal && Number.isFinite(b.escapeChance) ? ` · ${pct(b.escapeChance)}` : ''}</button></footer>`

    + `<details class="exhausted-pile" ${(b.exhaustedSpells || []).length ? 'open' : ''}><summary>本场移除 ${typeof b.exhausted === 'number' ? b.exhausted : (b.exhausted || []).length} 张 · 下一场恢复</summary>`
    + `<p>${(b.exhaustedSpells || []).length ? b.exhaustedSpells.map(sp => `${escape(sp.name)} ×${sp.copies}`).join(' · ') : '还没有移除神通。弃牌会参与重洗，本场移除的牌不会再抽到。'}</p></details>`
    + `<div class="combat-recent">${recent}</div>` + fullLog
    + `</div>`;
}

function removalOptions(card) {
  return (S?.battle?.removalOptions || []).filter(sp => sp.id !== card.id && sp.copies > 0);
}

function closeRemoval() {
  removalSelection = null;
  const dlg = $('#battle-target-modal');
  if (dlg?.open) dlg.close();
}

function renderRemoval() {
  if (!removalSelection) return;
  const card = S?.battle?.hand?.find(c => c.uid === removalSelection.uid);
  const options = card ? removalOptions(card) : [];
  if (!card || card.playable === false || !options.length) { closeRemoval(); return; }
  if (!options.some(sp => sp.id === removalSelection.target)) removalSelection.target = null;
  const chosen = options.find(sp => sp.id === removalSelection.target);
  const dlg = $('#battle-target-modal');
  dlg.innerHTML = `<div class="modal-inner removal-dialog"><div class="modal-heading"><div>`
    + `<div class="eyebrow">本场移除 · ${escape(card.name)}</div><h2>选择要移除的神通</h2></div>`
    + `<button class="subtle" data-action="removal-cancel" aria-label="取消移除">取消</button></div>`
    + `<p>移除所选神通在手牌、抽牌堆与弃牌堆中的全部副本，下一场斗法恢复。`
    + `确认才会消耗此牌与 ${card.cost} 点灵力。</p>`
    + `<div class="removal-options">${options.map(sp => `<button class="removal-option${sp.id === removalSelection.target ? ' selected' : ''}" `
      + `data-action="removal-select" data-id="${escape(sp.id)}" aria-pressed="${sp.id === removalSelection.target}">`
      + `<strong>${escape(sp.name)}</strong><span>${escape(sp.dao)}系 · 移除 ${sp.copies} 张</span></button>`).join('')}</div>`
    + `<div class="removal-confirm"><span>${chosen ? `将移除「${escape(chosen.name)}」${chosen.copies} 张` : '先选择一种神通'}</span>`
    + `<button class="primary" data-action="removal-confirm" ${chosen ? '' : 'disabled'}>确认移除并出牌</button></div></div>`;
  if (!dlg.open) dlg.showModal();
}


// 抉择弹窗
// ------------------------------------------------------------
function renderPending() {
  const p = S.pending;
  const dlg = $('#modal');
  if (!p) { if (dlg.open) dlg.close(); return; }

  const opts = p.options.map(o =>
    `<button class="choice-option" ${o.disabled ? 'disabled' : ''} `
  + `data-action="choose" data-id="${o.id}">`
  + `<strong>${escape(o.name)}</strong><span>${escape(o.desc)}</span></button>`).join('');

  $('#modal-content').innerHTML =
    `<div class="modal-inner"><div class="modal-heading"><div>`
  + `<div class="eyebrow">${escape(p.subtitle || '')}</div>`
  + `<h2>${escape(p.title)}</h2></div></div>`
  + `<p>${escape(p.text)}</p><div class="choice-options">${opts}</div></div>`;

  if (!dlg.open) dlg.showModal();
}

// ------------------------------------------------------------
// 结局结算画面
// ------------------------------------------------------------
let legCache = null;
let sectData = null;
let rosterReady = false;
let achData = null;
let buildSeed = 20261004;
let draftRoll = 0;
const pickedTalents = new Set();

async function renderEndingScreen() {
  if (!legCache) legCache = await loadLegacy(true);
  document.querySelectorAll('.main-tabs button').forEach(b => b.classList.remove('active'));
  renderSidebar();
  $('#workspace').innerHTML = renderEnding(S, legCache);
}
// ------------------------------------------------------------
// 关系条（知交页与通讯录都用）
// ------------------------------------------------------------
function relBar(v) {
  // 交情是 −100..100，映射到 0..100% 的宽度
  const w = Math.max(0, Math.min(100, (v + 100) / 2));
  return `<div class="rel-bar ${v < 0 ? 'neg' : ''}"><i style="width:${w}%"></i></div>`;
}

// ------------------------------------------------------------
// 剧情阅读器
// 之前这里被整段弄丢了，支线卡点了会直接按「选项一」结算，
// 玩家根本读不到正文，也选不了另一个选项。
// ------------------------------------------------------------
// ------------------------------------------------------------
// 建号面板：天赋选择 + 属性分配
//
// 之前 createBuildPanel 只是被 import 了，从来没调用过 ——
// 也就是说 web 版根本没有天赋界面，开局和「再写一世」
// 都直接拿默认天赋建号，玩家一次都没见过这个面板。
// ------------------------------------------------------------
let buildPanel = null;

function openBuildPanel(opts = {}) {
  const host = document.getElementById('build-host');
  if (!host) return;
  // .build-overlay 是 position:fixed 的全屏层，直接挂进去即可
  buildPanel?.unmount();
  host.innerHTML = '';

  buildPanel = createBuildPanel({
    onStart: async ({ name, gender, traits, allocation, budget, seed }) => {
      const r = await call('new', { name, gender, traits, allocation, budget, seed });
      if (r && !r.error) closeBuildPanel();
      return !!(r && !r.error);
    },
    // 之前首次进入时 onCancel 传 null，面板就成了没有出口的一堵墙：
    // 不建号就永远出不去，连主界面的页签都点不到。
    // 现在永远给「返回」，没开局时退回一堵说明。
    onCancel: () => closeBuildPanel(),
  });
  buildPanel.mount(host);
}

function closeBuildPanel() {
  buildPanel?.unmount();
  const host = document.getElementById('build-host');
  if (host) host.innerHTML = '';
  buildPanel = null;
  if (!S) renderWelcome();
}

function renderWelcome() {
  $('#workspace').innerHTML = panel('开始宁州一梦', '存档保存在本机',
    `<div class="welcome-actions"><p>先建立道号、挑选天赋与属性，或读取之前的存档。</p>`
    + `<button class="primary" data-action="new">开辟这一世</button>`
    + `<button data-action="settings">读取存档</button></div>`);
}

async function openSettings() {
  const d = await fetch('/api/slots').then(r => r.json()).catch(() => null);
  if (!d?.slots) { toast('读取存档列表失败，请确认服务还在运行', 'bad'); return; }
  const dlg = $('#settings-modal');
  const cp = S?.checkpoint;
  const nextAuto = Number.isFinite(cp?.nextAutoDay) ? `第 ${Math.floor(cp.nextAutoDay / 360) + 1} 年 ${Math.floor(cp.nextAutoDay % 360 / 30) + 1} 月 ${cp.nextAutoDay % 30 + 1} 日` : '每满十年';
  dlg.innerHTML = `<div class="modal-inner"><div class="modal-heading"><h2>存档与行卷</h2>`
    + `<button class="close-modal" data-action="close-settings" aria-label="关闭">×</button></div>`
    + `<p class="panel-note">快速保存写入存档位 1；这里可选择其他位置。保存会覆盖该位置，读取会替换当前这一世。</p>`
    + (cp ? `<div class="checkpoint-status"><strong>十年自动存档</strong><p>开局保存基准点，此后每满十年写入自动位。意外死亡后可选择回档；寿尽按这一世的结局正常结算。</p><small>下次自动保存：${escape(nextAuto)} · 已恢复 ${cp.restores || 0} 次</small></div>` : '')
    + d.slots.map(s => `<div class="save-slot${s.automatic ? ' automatic' : ''}"><div><strong>${s.automatic ? '十年自动存档' : `存档位 ${s.slot}`}</strong>`
      + `<p>${s.exists ? `${escape(s.name)} · ${escape(s.realm)} · 第 ${s.year + 1} 年 · ${s.age} 岁` : '空存档位'}</p></div>`
      + `<div>${s.automatic ? '<span class="automatic-slot-label">自动写入 · 只读</span>' : `<button data-action="save" data-slot="${s.slot}" ${S ? '' : 'disabled'}>保存${s.exists ? '覆盖' : ''}</button>`}`
      + `<button data-action="load-slot" data-slot="${s.slot}" ${s.exists ? '' : 'disabled'}>读取</button></div></div>`).join('')
    + `<div class="settings-row"><button data-action="export-save" ${S ? '' : 'disabled'}>导出行卷</button>`
    + `<button data-action="import-save">导回行卷</button></div></div>`;
  if (!dlg.open) dlg.showModal();
}

function openStory(id) {
  const s = (S.stories || []).find(x => x.id === id);
  const host = document.getElementById('story-host');
  if (!host || !s) return;

  const locName = (S.locations || []).find(l => l.id === s.location)?.name || s.location;
  const choice = (name, effect, idx) => {
    const enabled = s[`opt${idx + 1}Enabled`] !== false;
    const reason = s[`opt${idx + 1}Reason`];
    return `<button data-story="${escape(s.id)}" data-choice="${idx}" ${enabled ? '' : 'disabled'}>
       <strong>${escape(name)}</strong>
       <span>${escape(effect)}</span>
       ${!enabled && reason ? `<small>${escape(reason)}</small>` : ''}
     </button>`;
  };

  host.innerHTML = `<div class="story-overlay">
    <div class="story-panel">
      <div class="s-eyebrow">${escape(locName)} · 需 ${s.days} 天</div>
      <h2>${escape(s.name)}</h2>
      <p class="s-text">${escape(s.opening || s.desc || '')}</p>
      ${s.done
        ? `<div class="story-done-badge">这一段已经落笔了。</div>
           <div class="s-foot"><button data-story-cancel="1">收起</button></div>`
        : `<div class="story-choices">
             ${choice(s.opt1Name || '选项一', s.opt1Effect || '', 0)}
             ${choice(s.opt2Name || '选项二', s.opt2Effect || '', 1)}
           </div>
           <div class="s-foot">
             <button data-story-cancel="1">先不决断</button>
           </div>`}
    </div></div>`;
}

function closeStory() {
  const host = document.getElementById('story-host');
  if (host) host.innerHTML = '';
}

// ------------------------------------------------------------
// 五宗与成就数据（宗门知交页、机缘成就页共用）
// ------------------------------------------------------------
async function refreshSects() {
  [sectData, achData] = await Promise.all([loadSects(true), loadAchievements(true)]);
}
// ------------------------------------------------------------
// 主渲染
// ------------------------------------------------------------
function render() {
  if (!S) return;
  tianji.setVisible(tab === 'tianji' && !S.battle && !S.dungeons?.run && !S.ended);
  const oldViewport = document.querySelector('[data-map-viewport]');
  const mapScroll = oldViewport ? { x: oldViewport.scrollLeft, y: oldViewport.scrollTop, zoom: oldViewport.dataset.zoom } : null;
  if (!S.battle || S.ended) closeRemoval();
  if (S.dungeons?.run) tab = 'dungeon';
  $('#workspace').dataset.page = S.dungeons?.run ? 'dungeon' : S.battle ? 'battle' : tab;
  // 这一世结束 → 直接显示结局结算画面
  if (S.ended) { renderEndingScreen(); return; }
  renderSidebar();
  document.title = `${S.player.name} · 宁州一梦`;

  document.querySelectorAll('.main-tabs button').forEach(b =>
    b.classList.toggle('active', b.dataset.tab === tab));

  if (S.dungeons?.run) {
    $('#workspace').innerHTML = renderDungeons(S, { battleHTML: S.battle ? viewBattle() : '' });
    const routeViewport = document.querySelector('[data-dungeon-route-viewport]');
    const focusNode = document.querySelector('.dungeon-node.current') || document.querySelector('.dungeon-node.available');
    if (routeViewport && focusNode) routeViewport.scrollTop = Math.max(0, focusNode.offsetTop - routeViewport.clientHeight * .65);
  } else if (S.battle) {
    $('#workspace').innerHTML = viewBattle();
  } else {
    const views = {
      chronicle: viewChronicle, map: viewMap, arts: viewArts,
      society: viewSociety, roster: viewRoster, quests: viewQuests,
      market: viewMarket, economy: viewEconomy, tianji: () => tianji.render(),
      dungeon: () => renderDungeons(S, { mode: dungeonView, intelKind: dungeonIntelKind, intelSearch: dungeonIntelSearch }),
    };
    $('#workspace').innerHTML = (views[tab] || viewChronicle)();
    const viewport = document.querySelector('[data-map-viewport]');
    if (viewport) {
      if (mapScroll && mapScroll.zoom === viewport.dataset.zoom && !mapPanFocus) {
        viewport.scrollLeft = mapScroll.x; viewport.scrollTop = mapScroll.y;
      } else {
        const canvas = viewport.querySelector('[data-map-atlas]');
        const location = S.locations.find(l => l.id === mapFocus);
        if (location && canvas) {
          viewport.scrollLeft = canvas.offsetWidth * location.x / 100 - viewport.clientWidth / 2;
          viewport.scrollTop = canvas.offsetHeight * location.y / 100 - viewport.clientHeight / 2;
        }
      }
      mapPanFocus = false;
    }

    // 选中的人要带上可做的动作（从 state 里拿）
    // 任务与工坊要先把行动清单拉回来再画
    if ((tab === 'society' || tab === 'quests') && !sectData) {
      refreshSects().then(() => { if (tab === 'society' || tab === 'quests') render(); });
    }
    // 宗门页的「山门差事」也要用 ecoOpts，进页时一并拉回来
    if (tab === 'society' && !economyReady) {
      refreshEconomy().then(() => { if (tab === 'society') render(); });
    }
    if (tab === 'economy' && !economyReady) {
      refreshEconomy().then(() => { if (tab === 'economy') render(); });
    }
    if ((tab === 'society' || (tab === 'map' && subOf('map', 'atlas') === 'friends')) && selectedNpc) {
      loadSocialActions(selectedNpc);
      if (tab === 'map' && !economyReady && !economyLoading && Object.values(S.arts.materials || {}).some(count => count > 0)) {
        refreshEconomy().then(() => { if (tab === 'map') render(); });
      }
    }
    // 通讯录的筛选/排序/传讯是委托事件，插进 DOM 之后才绑得上
    if (tab === 'roster') bindRosterTab();
  }
  if (checkpointNotice) $('#workspace').insertAdjacentHTML('afterbegin', `<div class="checkpoint-restored" role="alert"><div><strong>已恢复最近存档</strong><p>${escape(checkpointNotice)}</p></div><button class="subtle" data-action="checkpoint-dismiss">知道了</button></div>`);
  renderPending();
  renderRemoval();
}

// 往来动作不在 state 里（数量多），单独拉一次
async function loadSocialActions(npcId) {
  const host = document.querySelector('.social-actions');
  if (!host || host.dataset.loaded === npcId) return;
  host.dataset.loaded = npcId;
  const res = await fetch('/api/social-actions', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ npc: npcId }),
  }).then(r => r.json()).catch(() => null);
  if (!res || !res.actions) { host.innerHTML = '<div class="empty">读取往来失败，请重新选择这位仙友。</div>'; return; }
  host.innerHTML = res.actions.map(a =>
    `<button ${a.enabled ? '' : 'disabled'} `
  + `data-action="social" data-npc="${npcId}" data-id="${a.id}">`
  + `<strong>${escape(a.name)}</strong>`
  + `<span class="${a.enabled ? '' : 'blocked'}">`
  + `${a.enabled ? escape(a.desc) : escape(a.reason)}</span>`
  + (a.days ? `<span> · ${a.days} 天</span>` : '')
  + (a.cost ? `<span> · ${a.cost} 灵石</span>` : '')
  + `</button>`).join('')
  // 野外冲突：只在险地出现，并预告对方可能的反应
  + (isWild(S.player.location)
      ? `<div class="conflict-box">
           <div class="conflict-head">野外冲突</div>
           <button data-action="conflict" data-npc="${npcId}" data-intent="rob">
             <strong>拦路索财</strong><span>恶名 +8 · 声望 −4 · 交情 −40</span></button>
           <button data-action="conflict" data-npc="${npcId}" data-intent="kill">
             <strong>野外截杀</strong><span>生死斗 · 恶名 +20 · 声望 −10</span></button>
           <div class="conflict-note" data-conflict-note="${npcId}"></div>
         </div>`
      : '');
  if (isWild(S.player.location)) loadConflictPreview(npcId);
}

// 险地判定（与后端 conflict::isWildLocation 保持一致）
function isWild(loc) {
  return S?.locations?.find(location => location.id === loc)?.type === 'wild';
}

// 拉取对方的反应预告，让玩家下手前就知道后果
async function loadConflictPreview(npcId) {
  const host = document.querySelector(`[data-conflict-note="${npcId}"]`);
  if (!host) return;
  const r = await fetch('/api/conflict/preview', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ npc: npcId, intent: 'rob' }),
  }).then(x => x.json()).catch(() => null);
  if (!r || !r.ok) { host.textContent = r?.error || ''; return; }
  host.innerHTML = `${escape(r.name)} 身怀 ${r.stones} 灵石、${r.herbs} 份灵药`
    + ` · 若出手，对方多半会「${escape(r.response)}」`;
}

// ------------------------------------------------------------
// 事件
// ------------------------------------------------------------
document.addEventListener('click', async e => {
  if (e.target.closest('[data-tianji]')) { await tianji.handleClick(e); return; }
  // 页签
  const tabBtn = e.target.closest('.main-tabs button');
  if (tabBtn) { if (S) navigateTab(tabBtn.dataset.tab); return; }

  // 剧情阅读器里的选择
  const sc = e.target.closest('[data-story]');
  if (sc) {
    if (sc.disabled || requestBusy) return;
    const sid = sc.dataset.story;
    const choice = Number(sc.dataset.choice) || 0;
    closeStory();
    await call('story', { id: sid, choice });
    invalidateRoster();
    return;
  }
  if (e.target.closest('[data-story-cancel]')) { closeStory(); return; }

  // 通讯录里的动作
  const ra = e.target.closest('[data-raction]');
  if (ra) {
    const act = ra.dataset.raction;
    const npc = ra.dataset.npc;
    if (act === 'message') await call('message', { npc });
    else if (act === 'invite') { await call('invite', { npc }); invalidateRoster(); }
    else if (act === 'meet') { selectedNpc = npc; tab = 'map'; setSub('map', 'friends'); render(); }
    return;
  }

  // 舆图上的地点
  const pin = e.target.closest('[data-map]');
  if (pin) {
    mapFocus = pin.dataset.map; mapPanFocus = true; render();
    if (globalThis.innerWidth <= 760) document.querySelector('.atlas-destination')?.scrollIntoView({ block: 'start', behavior: globalThis.matchMedia?.('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth' });
    return;
  }

  // 选中的人（地图页 / 宗门页共用）
  const person = e.target.closest('[data-person]');
  if (person) {
    selectedNpc = person.dataset.person;
    // 顺带把对方的背包拉回来（讨要用），拉完再重绘
    friendBag = null;
    const npcId = selectedNpc;
    loadNpcBag(npcId, true).then(b => {
      if (selectedNpc !== npcId) return;
      friendBag = b;
      render();
    });
    render();
    if (globalThis.innerWidth <= 760) document.querySelector('.friend-panel')?.scrollIntoView({ block: 'start', behavior: globalThis.matchMedia?.('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth' });
    return;
  }

  // 道友往来：见礼 / 送礼 / 讨要
  const fa = e.target.closest('[data-faction]');
  if (fa) {
    const npc = fa.dataset.npc;
    const act = fa.dataset.faction;
    if (act === 'greet') {
      const r = await call('greet', { npc });
      if (r && r.error) toast(r.error, 'warn');
      else if (r) {
        toast(r.firstTime
          ? `见礼：好感 ${r.affinityDelta >= 0 ? '+' : ''}${r.affinityDelta}`
          : '已经见过礼了', r.firstTime ? 'good' : 'info');
      }
      invalidateFriendCache(npc);
      const bag = await loadNpcBag(npc, true);
      if (selectedNpc === npc) friendBag = bag;
      render();
      return;
    }
    if (act === 'gift') {
      const r = await call('gift', {
        npc, kind: fa.dataset.kind, id: fa.dataset.id,
      });
      if (r && r.error) toast(r.error, 'warn');
      else if (r) {
        // 把「为什么加/减好感」说清楚 —— 不说玩家会觉得莫名其妙
        toast(`${r.text ? r.text + ' ' : '送礼：'}好感 ${r.affinityDelta >= 0 ? '+' : ''}${r.affinityDelta}`
              + ` · 交情 +${r.creditDelta}`, r.affinityDelta >= 0 ? 'good' : 'warn');
      }
      invalidateFriendCache(npc);
      const bag = await loadNpcBag(npc, true);
      if (selectedNpc === npc) friendBag = bag;
      render();
      return;
    }
    if (act === 'take') {
      const r = await call('npc-take', {
        npc, kind: fa.dataset.kind, id: fa.dataset.id,
      });
      if (r && r.error) toast(r.error, 'warn');
      else if (r) toast(`${r.text}（交情 −${r.cost}）`, 'good');
      invalidateFriendCache(npc);
      const bag = await loadNpcBag(npc, true);
      if (selectedNpc === npc) friendBag = bag;
      render();
      return;
    }
  }

  // 坊市分类
  const dungeonTab = e.target.closest('[data-dungeon-view]');
  if (dungeonTab) { dungeonView = dungeonTab.dataset.dungeonView; render(); return; }
  const intelKind = e.target.closest('[data-dungeon-intel-kind]');
  if (intelKind) { dungeonIntelKind = intelKind.dataset.dungeonIntelKind; render(); return; }
  const shopTab = e.target.closest('[data-shop]');
  if (shopTab) { shopKind = shopTab.dataset.shop; render(); return; }

  // 数量加减
  const qtyBtn = e.target.closest('[data-qty]');
  if (qtyBtn) {
    const id = qtyBtn.dataset.qty;
    const d = Number(qtyBtn.dataset.delta);
    shopQty[id] = Math.max(1, Math.min(99, (shopQty[id] ?? 1) + d));
    render();
    return;
  }

  // 子页签切换
  const st = e.target.closest('[data-subtab]');
  if (st) {
    setSub(st.dataset.subtab, st.dataset.sub);
    render();
    return;
  }

  const btn = e.target.closest('[data-action]');
  if (!btn) return;
  if (btn.disabled) return;
  if (requestBusy) return;
  const a = btn.dataset.action;
  const id = btn.dataset.id;

  switch (a) {
    case 'artifact-play': {
      const ability = S?.battle?.artifacts?.find(ability => ability.id === id);
      if (!ability?.playable) break;
      await call('battle/turn', { id });
      break;
    }
    case 'goto-equipment':
      tab = 'market'; setSub('market', 'kit'); render();
      break;
    case 'checkpoint-dismiss':
      checkpointNotice = ''; render();
      break;
    case 'checkpoint-restore':
      await call('checkpoint/restore');
      break;
    case 'dungeon-intel-open':
      dungeonView = 'intel'; tab = 'dungeon'; render();
      break;
    case 'dungeon-locate':
      tab = 'map'; setSub('map', 'atlas'); mapFocus = id; mapPanFocus = true; render();
      break;
    case 'dungeon-enter':
      await call('dungeon/enter', { id });
      break;
    case 'dungeon-choose':
      await call('dungeon/choose', { id });
      break;
    case 'dungeon-claim':
      await call('dungeon/claim', { id });
      break;
    case 'dungeon-leave':
      await call('dungeon/leave', {});
      break;
    case 'dungeon-intel':
      await call('dungeon/intel', { kind: btn.dataset.kind, id });
      dungeonView = 'sites'; render();
      break;
    case 'map-zoom':
      mapZoom = id === 'fit' ? .75 : Math.max(.75, Math.min(2, mapZoom + Number(id)));
      mapPanFocus = true; render();
      break;
    case 'location-service': {
      const result = await call('location-service', { id });
      if (result?.targetTab && !result.error) {
        const target = { market: 'market', sect: 'society', city: 'economy', dungeon: 'dungeon', intel: 'dungeon' }[result.targetTab];
        if (target) {
          tab = target;
          if (target === 'society') setSub('society', 'sect');
          if (result.targetTab === 'intel') dungeonView = 'intel';
          render();
        }
      }
      break;
    }
    case 'npc-learn': {
      const npcId = btn.dataset.npc;
      const result = await call('npc-learn', { npc: npcId, kind: btn.dataset.kind, id });
      if (result && !result.error) {
        invalidateFriendCache(npcId);
        const bag = await loadNpcBag(npcId, true);
        if (selectedNpc === npcId) friendBag = bag;
        render();
      }
      break;
    }
    case 'npc-learn-resume':
      if (S.job?.type === 'npcLearn') await call('advance', { days: S.job.remaining });
      break;
    case 'location-art': {
      const location = S?.locations.find(location => location.id === id);
      if (!location) break;
      const markup = renderLocationArt(location);
      if (!markup) break;
      const dlg = $('#location-art-modal');
      dlg.innerHTML = markup;
      if (!dlg.open) dlg.showModal();
      break;
    }
    case 'close-location-art':
      $('#location-art-modal').close();
      break;
    case 'advance':
      await call('advance', { days: Number(btn.dataset.days) || 360 });
      break;
    case 'advance-step':
      await call('advance', { days: Number(btn.dataset.days) || 1 });
      break;
    case 'advance-month':
      await call('advance', { days: S.clock.left, autoStop: false });
      break;
    case 'plan': {
      const r = await call('plan', { plan: id });
      if (r && !r.error) toast(`已改为${S.plans.find(p => p.id === id)?.name || '新的计划'}`, 'good');
      break;
    }
    case 'breakthrough':
      await call('breakthrough', {});
      break;
    case 'stipend':
      await call('stipend', {});
      break;
    case 'save': {
      const slot = Number(btn.dataset.slot) || 1;
      const r = await call('save', { slot });
      if (r && !r.error) {
        toast(`已保存到存档位 ${slot}`, 'good');
        $('#save-status').textContent = `已保存 · 存档位 ${slot}`;
        if ($('#settings-modal').open) await openSettings();
      }
      break;
    }
    case 'load-slot': {
      const slot = Number(btn.dataset.slot);
      const r = await call('load', { slot });
      if (r && !r.error) {
        $('#settings-modal').close();
        closeBuildPanel();
        toast(`已读取存档位 ${slot}`, 'good');
        $('#save-status').textContent = `已读取 · 存档位 ${slot}`;
      }
      break;
    }
    case 'travel-mode':
      mapModes[mapFocus] = id;
      render();
      break;
    case 'travel': {
      const destination = S.locations.find(location => location.id === id);
      const mode = selectedTravelMode(destination, mapModes[id]);
      if (!mode?.enabled) { toast(mode?.reason || '此程暂无法启程', 'warn'); break; }
      const r = await call('travel', { to: id, mode: mode.id, begin: btn.dataset.begin === '1' });
      showTravelResult(r, id);
      break;
    }
    case 'travel-step':
    case 'travel-resume': {
      if (!isTravelJob(S.job)) break;
      const to = S.job.to;
      const days = a === 'travel-step' ? 1 : S.job.remaining;
      const r = await call('advance', { days: Math.max(1, Number(days) || 1) });
      showTravelResult(r, to);
      break;
    }
    case 'node':
      await call('arts/node', { node: id });
      break;
    case 'focus':
      await call('arts/focus', { dao: id });
      break;
    case 'supply': {
      const kind = btn.dataset.kind;
      const level = Number(btn.dataset.level);
      await call('supply', { kind, level });
      break;
    }
    case 'cave-move':
      await call('cave/move', {});
      break;
    case 'cave-upgrade':
      await call('cave/upgrade', {});
      break;
    case 'set-main':
      await call('arts/main', { id });
      invalidateNodes();
      break;
    case 'set-aux':
      await call('arts/aux', { id });
      invalidateNodes();
      break;
    case 'meditate':
      await call('arts/meditate', { dao: id });
      invalidateNodes();
      break;
    case 'learn':
      await call('arts/learn', { id });
      break;
    case 'spell':
      await call('arts/spell', { id });
      break;
    case 'goto-art-builds':
      setSub('arts', 'builds');
      render();
      break;
    case 'clear-build-filter':
      artBuildFilter = 'all'; spellDaoFilter = 'all';
      render();
      break;
    case 'story':
      // 先打开阅读器让玩家读正文、自己选；
      // 之前这里直接按「选项一」结算，正文根本读不到。
      openStory(id);
      break;
    case 'wild':
      await call('wild', { id });
      break;
    case 'expedition':
      await call('expedition', {});
      break;
    case 'social': {
      const npcId = btn.dataset.npc;
      const result = await call('social', { npc: npcId, action: id });
      if (result && !result.error) {
        invalidateFriendCache(npcId);
        const bag = await loadNpcBag(npcId, true);
        if (selectedNpc === npcId) friendBag = bag;
        render();
      }
      break;
    }
    case 'bond':
      await call('bond', { kind: id });
      break;
    case 'choose':
      await call('choose', { id });
      break;
    case 'play': {
      const card = S?.battle?.hand?.find(c => c.uid === id);
      if (!card || card.playable === false) break;
      if (card.requiresTarget || card.kind === 'exhaust') {
        if (!removalOptions(card).length) { toast('本场没有其它可移除的神通', 'warn'); break; }
        removalSelection = { uid: id, target: null };
        renderRemoval();
      } else await call('battle/turn', { id });
      break;
    }
    case 'removal-select':
      if (removalSelection) { removalSelection.target = id; renderRemoval(); }
      break;
    case 'removal-cancel':
      closeRemoval();
      break;
    case 'removal-confirm': {
      const card = S?.battle?.hand?.find(c => c.uid === removalSelection?.uid);
      const target = removalSelection?.target;
      if (!card || card.playable === false || !removalOptions(card).some(sp => sp.id === target)) { closeRemoval(); break; }
      const uid = card.uid;
      closeRemoval();
      await call('battle/turn', { id: uid, target });
      break;
    }
    case 'end-turn':
      await call('battle/turn', { id: 'end' });
      break;
    case 'escape':
      await call('battle/turn', { id: 'escape' });
      break;
    case 'buy': {
      const count = Number(btn.dataset.count) || 1;
      const desk = btn.closest('.shop-row');
      const label = desk ? desk.querySelector('.item-name').textContent : '';
      const r = await call('buy', {
        kind: btn.dataset.kind, id, count,
      });
      if (r && !r.error) {
        pushFeed(`买入 ${label} ×${count}。`, '', 'good');
        shopQty[id] = 1;
      }
      break;
    }
    case 'craft': {
      const batch = Number(document.querySelector('[data-brew-batch]')?.value) || 1;
      const r = await call('craft', { kind: 'brew', id, batch });
      if (r && !r.error && S.job) toast(`${S.job.name}尚余 ${S.job.remaining} 天；在岁月纪事继续推进。`, 'info');
      break;
    }
    case 'use-pill':
      await call('use-pill', { id });
      break;
    case 'eco-run': {
      const r = await call('eco', { type: btn.dataset.type, id: btn.dataset.id });
      if (r && r.text) toast(r.text, 'good');
      break;
    }
    case 'forge-run': {
      const g = k => document.querySelector(`[data-forge="${k}"]`)?.value || '';
      await call('forge', { main: g('main'), aux: g('aux'), core: g('core'), shape: g('shape') });
      break;
    }
    case 'goto-breakthrough': {
      tab = 'chronicle';
      render();
      const el = document.getElementById('bt-panel');
      if (el) {
        el.scrollIntoView({ behavior: 'smooth', block: 'center' });
        el.classList.remove('bt-flash'); void el.offsetWidth; el.classList.add('bt-flash');
      }
      break;
    }
    case 'goto-tab': {
      navigateTab(btn.dataset.tab);
      break;
    }
    case 'tide-fight': {
      const r = await call('tide/fight');
      if (r && r.error) { toast(r.error, 'warn'); break; }
      if (r) toast(r.text || '你迎战兽潮。', 'good');
      render();
      break;
    }
    case 'tide-hunker': {
      const r = await call('tide/hunker');
      if (r && !r.error) toast(r.text || '你固守不出。', 'info');
      render();
      break;
    }
    case 'join-sect': {
      const r = await call('join', { sect: btn.dataset.sect });
      if (r && !r.error) {
        setSub('society', 'sect');
        if (S.player.sect === btn.dataset.sect) toast('已拜入宗门', 'good');
        else if (S.battle) toast('入门考核开始：胜利后拜入本门', 'info');
      }
      invalidateSects();
      await refreshSects();
      render();
      break;
    }
    case 'sect-locate':
      tab = 'map'; mapFocus = id; mapPanFocus = true; setSub('map', 'atlas'); render();
      break;
    case 'sect-open':
    case 'sect-trial-open':
      if (!S.locations.some(location => location.id === id && location.type === 'sect')) break;
      if (S.player.location !== id) {
        tab = 'map'; mapFocus = id; mapPanFocus = true; setSub('map', 'atlas');
      } else {
        tab = 'society';
        setSub('society', S.player.sect === id ? btn.dataset.sub || 'sect' : 'sect');
      }
      render();
      break;
    case 'sect-mentor-detail': {
      const npcId = btn.dataset.npc;
      if (!(S.nearby || []).some(npc => npc.id === npcId && npc.sect === S.player.sect)) break;
      selectedNpc = npcId; friendBag = null; tab = 'society'; setSub('society', 'members'); render();
      const bag = await loadNpcBag(npcId, true);
      if (selectedNpc === npcId) { friendBag = bag; render(); }
      break;
    }
    case 'renown-source-open': {
      const source = (S.renownInfo?.sources || []).find(source => typeof source === 'object' && source.id === id);
      if (!source?.entry) break;
      const entry = source.entry;
      if (entry.location && S.player.location !== entry.location) {
        tab = 'map'; mapFocus = entry.location; mapPanFocus = true; setSub('map', 'atlas');
      } else {
        tab = entry.tab || 'quests';
        if (entry.sub) setSub(tab, entry.sub);
      }
      render();
      break;
    }
    case 'library-spell':
      await call('library/spell', { id });
      break;
    case 'legacy-settle': {
      const r = await call('legacy/settle');
      if (r && r.error) { toast(r.error, 'warn'); break; }
      if (r) {
        const pts = r.points ?? 0;
        toast(pts > 0 ? `结算完成，获得 ${pts} 点传承（累计 ${r.earned}）`
                      : '本世没有新的里程碑，未获得传承点', pts > 0 ? 'good' : 'warn');
      }
      invalidateLegacy();
      legCache = null;
      await renderEndingScreen();
      break;
    }
    case 'legacy-new':
      // 开建号面板让玩家选天赋、分属性。
      // 之前这里是 window.location.reload()，刷新后又回到结局画面，
      // 天赋界面永远见不到。
      openBuildPanel({ firstTime: false });
      break;
    case 'legacy-reroll': {
      draftRoll += 1;
      const d = await draftTalents(buildSeed, draftRoll);
      $('#build-host').innerHTML = renderDraftPanel(d, draftRoll, buildSeed,
        d?.budget ?? 10, pickedTalents);
      break;
    }
    case 'legacy-seed':
      buildSeed = Math.floor(Math.random() * 90000000) + 10000000;
      draftRoll = 0;
      pickedTalents.clear();
      {
        const d = await draftTalents(buildSeed, draftRoll);
        $('#build-host').innerHTML = renderDraftPanel(d, draftRoll, buildSeed,
          d?.budget ?? 10, pickedTalents);
      }
      break;
    case 'export-save': {
      const r = await fetch('/api/export', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: '{}',
      }).then(x => x.json()).catch(() => null);
      if (!r || r.error) { toast(r?.error || '导出失败', 'bad'); break; }
      // 整包导出：游戏存档 + 传承档案，与 JS 的 exportSave 同构
      const payload = JSON.stringify({
        format: r.format, version: r.version,
        game: r.game, legacy: r.legacy,
      }, null, 2);
      const blob = new Blob([payload], { type: 'application/json;charset=utf-8' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = `宁州一梦-${r.name}-第${r.year}年-${r.realm}.json`;
      a.click();
      URL.revokeObjectURL(url);
      toast(`已导出行卷（${r.name} · 第 ${r.year} 年 · ${r.realm}）`, 'good');
      break;
    }
    case 'import-save': {
      // 用隐藏的 file input 选文件，避免 prompt 里粘贴几十万字符
      const inp = document.createElement('input');
      inp.type = 'file';
      inp.accept = '.json,application/json';
      inp.onchange = async () => {
        const f = inp.files && inp.files[0];
        if (!f) return;
        const text = await f.text();
        let parsed;
        try { parsed = JSON.parse(text); }
        catch { toast('这个文件不是有效的行卷 JSON', 'bad'); return; }
        // 兼容三种形态：整包 / {game:...} / 裸存档
        const body = parsed && parsed.game
          ? { game: parsed.game, legacy: parsed.legacy || '' }
          : { game: text };
        const r = await call('import', body);
        if (r && !r.error) {
          closeBuildPanel(); closeStory(); $('#settings-modal').close();
          toast('行卷已导入', 'good');
        }
      };
      inp.click();
      break;
    }
    case 'conflict': {
      const r = await call('conflict', {
        npc: btn.dataset.npc,
        intent: btn.dataset.intent,
      });
      if (r && r.text) toast(r.text, 'warn');
      break;
    }
    case 'sell':
      await call('sell', { kind: btn.dataset.kind, id });
      break;
    case 'trade-stock': {
      const r = await call('trade-stock', {
        kind: btn.dataset.kind,
        buy: btn.dataset.buy === '1',
        count: 1,
      });
      if (r && r.text) toast(r.text, 'good');
      break;
    }
    case 'gear/unequip':
      await call('gear/unequip', { id });
      break;
    case 'gear/salvage':
      await call('gear/salvage', { id });
      break;
    case 'gear/inscribe':
      await call('gear/inscribe', { id });
      break;
    case 'furnace/repair':
      await call('furnace/repair', {});
      break;
    case 'furnace/buy':
      await call('furnace/buy', { id });
      break;
    case 'book-buy':
      await call('book-buy', { id });
      break;
    case 'library':
      await call('library', { id });
      break;
    case 'cancel-job': {
      const journey = isTravelJob(S.job);
      const cost = S.job?.cost || 0;
      const r = await call('cancel-job', {});
      if (journey && r && !r.error) toast(`已中止赶路，仍在${S.player.locationName}${cost ? `；已付 ${cost} 灵石不返还` : ''}。`, 'info');
      break;
    }
    case 'equip': {
      const r = await call('equip', { id });
      if (r && !r.error) toast('已装备法器', 'good');
      break;
    }
    case 'new': {
      // 走建号面板：选天赋、分属性、掷先天气运。
      // 之前这里用 prompt 只问了个名字，天赋全是默认值。
      openBuildPanel({ firstTime: false });
      break;
    }
    case 'settings': {
      await openSettings();
      break;
    }
    case 'close-settings': {
      $('#settings-modal').close();
      break;
    }
    case 'help':
      alert('宁州一梦 · C++ 版\n\n'
        + '【推进】选好月度计划，点过月或过年会一直走到有事发生，'
        + '不用反复点。空格键也可以。\n\n'
        + '【舆图】点地图上的地点查看景观与交通报价。步行省钱，乘船稳妥，筑基后可御器。'
        + '「启程赶路」可逐日行进，「直接赶往」会推进至到达或途中遇事。'
        + '很多支线只在特定地点触发。\n\n'
        + '【仙友】在舆图的当地仙友或宗门的本门弟子页点修士，'
        + '可以闲谈、赠礼、切磋、请教、结伴。'
        + '好感度到 60 可以结为道侣，到 40 可以拜师。\n\n'
        + '【坊市】只在大城有。可以买卖药材丹药法器，'
        + '价格随行情浮动，本地药材便宜两成。\n\n'
        + '【炼丹】在坊市的开炉炼丹页选择丹方与炉数，耗时与投入列在卡片上。'
        + '遇事暂停后去岁月纪事继续推进；所有丹方均可查看。\n\n'
        + '【功法与神通】抄本从武陵城书坊、宗门藏经阁取得，需修习后生效。'
        + '洞府道法的神通搭配页可更换额外神通，槽位随大境界由 4 增至 12。'
        + '普通攻击与防御固定常备，各五张，不占槽位；流派组合页列出搭配与触发条件。\n\n'
        + '【斗法】每回合抽 5 张，出牌不触发反击，'
        + '点「结束回合」才结算敌方行动。');
      break;
    default:
      break;
  }
});

// 数量输入框
document.addEventListener('change', async e => {
  const inp = e.target.closest('[data-qtyinput]');
  if (inp) {
    const id = inp.dataset.qtyinput;
    shopQty[id] = Math.max(1, Math.min(99, Number(inp.value) || 1));
    render();
  }
  if (e.target.matches('[data-brew-batch]')) brewBatch = Number(e.target.value) || 1;
  if (e.target.matches('[data-forge]')) {
    forgeSelection[e.target.dataset.forge] = e.target.value;
    await refreshForgePreview();
  }
  if (e.target.matches('[data-spell-dao]')) { spellDaoFilter = e.target.value; render(); }
  if (e.target.matches('[data-art-build-filter]')) { artBuildFilter = e.target.value; spellDaoFilter = 'all'; render(); }
  if (e.target.matches('[data-map-filter]')) { mapFilter = e.target.value; render(); }
});

document.addEventListener('input', e => {
  if (e.target.matches('[data-dungeon-intel-search]')) {
    dungeonIntelSearch = e.target.value;
    const cursor = e.target.selectionStart;
    render();
    const input = document.querySelector('[data-dungeon-intel-search]');
    input?.focus(); input?.setSelectionRange(cursor, cursor);
    return;
  }
  if (e.target.matches('[data-map-search]')) {
    mapSearch = e.target.value;
    const cursor = e.target.selectionStart;
    render();
    const input = document.querySelector('[data-map-search]');
    input?.focus(); input?.setSelectionRange(cursor, cursor);
    return;
  }
  if (!e.target.matches('[data-brew-search]')) return;
  brewSearch = e.target.value;
  const cursor = e.target.selectionStart;
  render();
  const input = document.querySelector('[data-brew-search]');
  input?.focus(); input?.setSelectionRange(cursor, cursor);
});

// 快捷键
document.addEventListener('keydown', e => {
  if (document.querySelector('dialog[open]') || tab === 'tianji' || tianji.status?.encounter?.state === 'active' || buildPanel || $('#story-host').hasChildNodes() || requestBusy || !S || S.ended) return;
  if (e.target.closest('input, textarea, select, [contenteditable="true"]')) return;
  if (e.target.closest('[data-map-viewport]')) return;

  // 与 index.html 上的页签顺序一致（含新增的通讯录与任务工坊）
  const TABS = Array.from(document.querySelectorAll('.main-tabs button'), b => b.dataset.tab);
  if (e.code === 'Space' && !S.battle && !S.dungeons?.run && !e.repeat && e.target.tagName !== 'BUTTON') {
    e.preventDefault();
    call('advance', { days: 360 });
  } else if (e.key >= '1' && e.key <= '9') {
    tab = TABS[Number(e.key) - 1];
    render();
  } else if (e.code === 'Escape' && selectedNpc) {
    selectedNpc = null;
    render();
  }
});

$('#modal').addEventListener('cancel', e => { if (S?.pending) e.preventDefault(); });
$('#battle-target-modal').addEventListener('cancel', closeRemoval);
$('#battle-target-modal').addEventListener('close', () => { removalSelection = null; });

// ------------------------------------------------------------
// 启动
// ------------------------------------------------------------
(async function boot() {
  await ensureLogin();
  const res = await fetch('/api/state').then(r => r.json()).catch(() => null);
  // 只有后端明确说有存档时才用它；冷启动会返回 hasGame:false
  if (res && res.hasGame && res.state) { S = res.state; if (S.tianji?.hasEncounter) navigateTab('tianji'); else render(); return; }
  // 没有存档：先让玩家建号，不要直接拿默认天赋开局
  openBuildPanel({ firstTime: true });
})();
