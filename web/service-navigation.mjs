// 服务导航只保存浏览器访问意图。路线与资格始终读取后端，旅行仍走现有 travel/advance。
import { esc } from './shell.mjs';
const STORAGE_KEY = 'nz-desktop-service-navigation';
const TARGETS = { market: [''], workshop: ['brew', 'forge'], dungeon: ['intel'] };

// 用途：校验服务目标页面；流程：导航恢复与按钮提交；用法：传页签和子页；返回：是否合法。
function validTarget(tab, sub = '') { return Object.hasOwn(TARGETS, tab) && TARGETS[tab].includes(sub); }

// 用途：从后端路线生成可恢复导航意图；流程：启程前校验；用法：传状态、路线、目标页面；返回：合法意图或 null。
export function createServiceIntent(state, route, { tab = '', sub = '' } = {}) {
  if (!state?.player || route?.enabled !== true || route.alreadyHere === true || !validTarget(tab, sub)
      || !(state.locations || []).some(location => location.id === route.destination)
      || typeof route.mode !== 'string' || !route.mode) return null;
  return { destination: route.destination, tab, sub, playerName: state.player.name };
}

// 用途：保存或清除本地服务导航；流程：启程、取消或抵达；用法：传合法意图，可注入测试存储；返回：无。
export function saveServiceIntent(intent, storage = globalThis.sessionStorage) {
  try { if (intent) storage?.setItem(STORAGE_KEY, JSON.stringify(intent)); else storage?.removeItem(STORAGE_KEY); }
  catch { /* 浏览器拒绝存储时仍可在当前会话继续赶路。 */ }
}

// 用途：恢复白名单中的导航意图；流程：页面刷新；用法：传当前状态，可注入测试存储；返回：合法意图或 null。
export function loadServiceIntent(state, storage = globalThis.sessionStorage) {
  try {
    const intent = JSON.parse(storage?.getItem(STORAGE_KEY) || 'null');
    if (intent && intent.playerName === state?.player?.name && validTarget(intent.tab, intent.sub)
        && (state.locations || []).some(location => location.id === intent.destination))
      return { destination: intent.destination, tab: intent.tab, sub: intent.sub, playerName: intent.playerName };
  } catch { /* 畸形偏好不参与导航。 */ }
  return null;
}

// 用途：判定旅途是否真正抵达；流程：动作响应与刷新；用法：传状态与本地意图；返回：arrived/travelling/paused/invalid。
export function serviceIntentStatus(state, intent) {
  if (!intent || intent.playerName !== state?.player?.name || state.ended
      || !(state.locations || []).some(location => location.id === intent.destination)) return 'invalid';
  if (state.job?.type === 'travel') return state.job.to === intent.destination ? 'travelling' : 'invalid';
  if (state.player.location === intent.destination && !state.job && !state.pending && !state.battle && !state.dungeons?.run) return 'arrived';
  return 'paused';
}

// 用途：展示一键前往的后端报价；流程：异地购买服务；用法：传路线、白名单页面和 routeKey；返回：HTML，无路线时空串。
export function renderServiceRoute(route, { tab = '', sub = '', routeKey = '' } = {}) {
  if (!route || route.alreadyHere === true || !validTarget(tab, sub)) return '';
  if (!route.destination) return route.reason ? `<p class="service-route-note">${esc(route.reason)}</p>` : '';
  const hasQuote = Number.isFinite(route.days) && Number.isFinite(route.cost);
  return `<div class="service-route" data-service-route-panel><button data-action="service-travel" data-service-route="${esc(routeKey)}" data-target-tab="${esc(tab)}" data-target-sub="${esc(sub)}" ${route.enabled === true && hasQuote ? '' : 'disabled'}>`
    + `一键前往${esc(route.destinationName || '目的地')}${hasQuote ? ` · ${esc(route.days)} 天 / ${esc(route.cost)} 灵石` : ''}</button>`
    + `${route.reason ? `<small>${esc(route.reason)}</small>` : ''}</div>`;
}

// 用途：展示未完成的服务行程；流程：途中暂停及刷新；用法：传状态与意图；返回：恢复提示 HTML，不自动推进。
export function renderServiceJourney(state, intent) {
  const status = serviceIntentStatus(state, intent);
  if (status === 'invalid' || status === 'arrived') return '';
  const destination = (state.locations || []).find(location => location.id === intent.destination);
  const blocked = !!(state.pending || state.battle || state.ended || state.dungeons?.run);
  return `<div class="service-journey" data-service-journey><div><strong>服务行程 · 前往${esc(destination?.name || intent.destination)}</strong>`
    + `<p>${status === 'travelling' ? `尚余 ${esc(state.job.remaining)} 天。${blocked ? '请先处理眼前事情，再继续赶路。' : '抵达后会打开所选服务页面。'}` : '行程尚未完成。可在舆图查看目的地，完成沿途事情后继续。'}</p></div>`
    + `<div>${status === 'travelling' ? `<button class="primary" data-action="travel-resume" ${blocked ? 'disabled' : ''}>继续赶路</button>` : ''}`
    + `<button data-action="dungeon-locate" data-id="${esc(intent.destination)}">查看目的地</button>`
    + `<button class="subtle" data-action="service-navigation-refresh">刷新行程</button>`
    + `<button class="subtle" data-action="service-navigation-dismiss">收起服务导航</button></div></div>`;
}

// 用途：展示远程领取俸禄；流程：岁月纪事与宗门页；用法：传后端 bonds 报价；返回：资格明确的按钮 HTML。
export function renderRemoteStipend(bonds) {
  const quote = bonds?.stipendAction;
  if (!quote) return `<button data-action="stipend">领俸禄（${esc(bonds?.stipendBalance || 0)}）</button>`;
  return `<button data-action="stipend" ${quote.enabled === true ? '' : 'disabled'} title="${esc(quote.reason || '各地均可领取宗门俸禄')}">`
    + `领取宗门俸禄${quote.paid > 0 ? ` · ${esc(quote.paid)} 灵石` : ''}</button>`;
}
