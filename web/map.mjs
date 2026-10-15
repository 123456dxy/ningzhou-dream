// 宁州舆图：地点、交通报价与行程都取自引擎，地图仅绘制路线。
import { locationArt } from './location-art.mjs';
import { visibleDungeonSites, renderDungeonEntrances } from './dungeons.mjs';

// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = value => String(value ?? '').replace(/[&<>"']/g,
  ch => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[ch]));
const TYPES = { city: '城池', sect: '宗门', wild: '险地' };
const MODES = { walk: '步行', boat: '乘船', flight: '御器' };

// 用途：识别真实赶路安排。
// 流程：检查job.type是否为travel。
// 调用方式：地图、快捷键与取消安排传入job。
// 返回值：布尔值。
export function isTravelJob(job) { return job?.type === 'travel'; }

// 用途：选择可用交通方式。
// 流程：优先请求的方式，再选可用灵舟或其他可用方式。
// 调用方式：地图传入location与requestedId。
// 返回值：交通方式对象或null。
export function selectedTravelMode(location, requestedId = '') {
  const options = location?.travel?.options || [];
  return options.find(option => option.id === requestedId)
    || options.find(option => option.id === 'boat' && option.enabled)
    || options.find(option => option.enabled) || options[0] || null;
}

// 用途：解释赶路动作结果。
// 流程：依据响应和state.job判断是否抵达或遇事暂停，补充目的地名称。
// 调用方式：赶路反馈传入state、to与result。
// 返回值：反馈对象。
export function travelFeedback(state, to, result = {}) {
  if (result.error) return { text: result.error, kind: 'bad', arrived: false };
  if (state.ended) return { text: '此世已终，行程止于途中。', kind: 'warn', arrived: false };
  const destination = (state.locations || []).find(location => location.id === to);
  if (result.arrived !== false && state.player.location === to && !state.job)
    return { text: `已抵达${destination?.name || state.player.locationName}`, kind: 'good', arrived: true };
  if (state.battle || result.report?.stopReason === 'battle')
    return { text: '途中遇上斗法，行程暂缓；处理完后再继续赶路。', kind: 'warn', arrived: false };
  if (state.pending || result.report?.stopReason === 'event')
    return { text: '途中遇事，行程暂缓；先处理眼前的抉择。', kind: 'warn', arrived: false };
  if (isTravelJob(state.job)) {
    const passed = result.report?.daysPassed;
    return { text: `${passed ? `已行进 ${passed} 天，` : '已启程，'}距${destination?.name || '目的地'}尚余 ${state.job.remaining} 天。`, kind: 'info', arrived: false };
  }
  return { text: result.text || '行程尚未完成，请查看岁月纪事。', kind: 'info', arrived: false };
}

// 用途：绘制当前赶路路线。
// 流程：从起终点坐标生成连线并显示实际进度位置。
// 调用方式：地图传入from、to与progress。
// 返回值：SVG路线片段字符串。
function routeDrawing(from, to, progress = 0) {
  if (!from || !to || from.id === to.id) return '';
  const x1 = Number(from.x) * 10, y1 = Number(from.y) * 6.25;
  const x2 = Number(to.x) * 10, y2 = Number(to.y) * 6.25;
  const cx = (x1 + x2) / 2;
  const cy = Math.max(25, Math.min(600, (y1 + y2) / 2 - Math.min(70, Math.abs(x2 - x1) * .15 + 25)));
  const path = `M ${x1} ${y1} Q ${cx} ${cy} ${x2} ${y2}`;
  const t = Math.max(0, Math.min(1, progress)), inv = 1 - t;
  const px = inv * inv * x1 + 2 * inv * t * cx + t * t * x2;
  const py = inv * inv * y1 + 2 * inv * t * cy + t * t * y2;
  return `<svg class="atlas-route" data-map-route viewBox="0 0 1000 625" aria-hidden="true">`
    + `<path class="route-underlay" d="${path}"/><path class="route-line" d="${path}"/>`
    + `${progress > 0 ? `<circle class="route-traveller" cx="${px}" cy="${py}" r="7"/>` : ''}</svg>`;
}

// 用途：显示进行中的旅程。
// 流程：核实travel安排，解析起终点及剩余天数，显示推进或中止入口。
// 调用方式：地图传入state。
// 返回值：旅程HTML；不在赶路时空字符串。
function journeyPanel(state) {
  const job = state.job;
  if (!isTravelJob(job)) return '';
  const places = state.locations || [];
  // 用途：解析旅途地点中文名。
  // 流程：在places中查找地点，缺少目录时回退标识。
  // 调用方式：journeyPanel传入地点id。
  // 返回值：地点名称字符串。
  const name = id => places.find(location => location.id === id)?.name || id;
  const total = Math.max(1, Number(job.total ?? job.days) || 1);
  const remaining = Math.max(0, Number(job.remaining) || 0);
  const elapsed = Math.max(0, Math.min(total, Number(job.elapsed ?? total - remaining) || 0));
  const blocked = state.pending || state.battle;
  return `<section class="travel-job" data-travel-job data-from="${esc(job.from)}" data-to="${esc(job.to)}">`
    + `<div class="travel-job-heading"><span class="journey-seal" aria-hidden="true">行</span><div>`
    + `<span class="travel-eyebrow">${esc(MODES[job.mode] || job.mode || '赶路')} · 途中</span>`
    + `<h3>${esc(name(job.from))} <span>→</span> ${esc(name(job.to))}</h3></div>`
    + `<b>尚余 ${remaining} 天</b></div>`
    + `<progress data-travel-progress max="${total}" value="${elapsed}" aria-label="赶路进度">${elapsed} / ${total}</progress>`
    + `<div class="travel-job-meta"><span>已行 ${elapsed} / ${total} 天</span>`
    + `<span>${blocked ? '途中遇事，处理完后继续' : '未抵达目的地'}</span></div>`
    + `<div class="travel-job-actions"><button id="map-travel-resume" class="primary" data-action="travel-resume" ${blocked ? 'disabled' : ''}>继续赶路 · ${remaining} 天</button>`
    + `<button id="map-travel-step" data-action="travel-step" ${blocked ? 'disabled' : ''}>行进一天</button>`
    + `<button id="map-travel-cancel" class="subtle" data-action="cancel-job" ${blocked ? 'disabled' : ''}>中止赶路</button></div>`
    + `<p class="travel-paid">${job.cost > 0 ? `启程已付 ${job.cost} 灵石，中止不会返还。` : '本段赶路无需灵石。'}途中遇事会暂停。</p></section>`;
}

const JOINABLE_SECTS = new Set(['jinhong', 'xinghe', 'zhushan', 'lihuo', 'huachen', 'xuandao']);

// 用途：显示地点山门入口。
// 流程：读取宗门和当前人物身份，区分入宗考核或返回宗门。
// 调用方式：地点详情传入state、location与blocked。
// 返回值：山门入口HTML字符串。
export function renderSectEntrance(state, location, blocked = '') {
  if (!JOINABLE_SECTS.has(location?.id)) return '';
  const player = state.player || {};
  const here = player.location === location.id;
  const own = player.sect === location.id;
  return `<section class="map-sect-entrance" data-sect-entrance="${esc(location.id)}"><h4>${own ? '本宗山门' : '山门入口'}</h4>`
    + `<p>${own ? '入内可办理宗门差事、拜师受业与藏经阁传承。' : !player.sect ? '抵达后可报名实战考核，获胜才能正式拜入宗门。' : '已拜入另一宗门，可以查看这处山门的情况。'}</p>`
    + (here ? `<button class="primary" data-action="${own ? 'sect-open' : 'sect-trial-open'}" data-id="${esc(location.id)}" data-sub="${own ? 'sect' : 'sect'}" ${blocked ? 'disabled' : ''}>${own ? '进入宗门' : !player.sect ? '山门报名与考核' : '查看山门'}</button>`
        + (own ? `<div class="sect-entry-actions"><button data-action="sect-open" data-id="${esc(location.id)}" data-sub="mentors" ${blocked ? 'disabled' : ''}>拜师与受业</button><button data-action="sect-open" data-id="${esc(location.id)}" data-sub="jobs" ${blocked ? 'disabled' : ''}>宗门差事</button><button data-action="sect-open" data-id="${esc(location.id)}" data-sub="library" ${blocked ? 'disabled' : ''}>藏经阁</button></div>` : '')
        + `${blocked ? `<small>${esc(blocked)}</small>` : ''}`
      : '<small>需先完成下方赶路，抵达山门后才可进入。</small>') + '</section>';
}

// 用途：显示舆图选中地点详情。
// 流程：读取地点服务、交通报价、秘境入口与插画，生成行动区。
// 调用方式：舆图传入state、focus与requestedMode。
// 返回值：地点详情HTML字符串。
function destinationPanel(state, focus, requestedMode) {
  if (!focus) return '<aside class="atlas-destination"><div class="empty">暂时没有可查看的地点。</div></aside>';
  const here = focus.id === state.player.location;
  const travelling = isTravelJob(state.job);
  const option = selectedTravelMode(focus, requestedMode);
  const art = locationArt(focus);
  const blocked = state.battle ? '先完成眼前的斗法' : state.pending ? '先处理眼前的抉择'
    : state.job ? travelling ? '正在赶路，可在上方继续或中止当前行程' : '先完成或中止当前营生' : '';
  const available = !here && !blocked && !!option?.enabled;
  const options = focus.travel?.options || [];
  const services = (focus.services || []).filter(service => !(service.id === 'sect' && JOINABLE_SECTS.has(focus.id))).map(service => {
    const enabled = here && !blocked && service.enabled;
    const reason = !here ? '需先抵达此地' : blocked || service.reason;
    return `<div class="location-service"><div><strong>${esc(service.name)}</strong>`
      + `<p>${esc(service.desc)}</p><small>${service.days || 0} 天${service.cost ? ` · ${service.cost} 灵石` : ''}${!enabled && reason ? ` · ${esc(reason)}` : ''}</small></div>`
      + `<button data-action="location-service" data-id="${esc(service.id)}" ${enabled ? '' : 'disabled'}>${enabled ? '前往' : '未开放'}</button></div>`;
  }).join('');
  return `<aside class="atlas-destination" data-map-detail="${esc(focus.id)}">`
    + (art ? `<button class="destination-art-button" data-action="location-art" data-id="${esc(focus.id)}" aria-label="查看${esc(focus.name)}景观大图">`
      + `<img class="destination-art" src="${esc(art.src)}" alt="${esc(art.alt)}" width="1600" height="900">`
      + `<span class="destination-art-expand" aria-hidden="true">查看大图 ↗</span></button>` : '')
    + `<div class="destination-body"><button type="button" class="nz-map-return">↑ 返回舆图 · 切换地点</button><div class="destination-title"><div><span class="travel-eyebrow">${esc(TYPES[focus.type] || '山川')}${here ? ' · 当前所在' : ' · 目的地'}</span>`
    + `<h3>${esc(focus.name)}</h3></div><span class="destination-danger${focus.danger >= 50 ? ' high' : ''}">${focus.danger ? `危险 ${focus.danger}` : '安宁'}</span></div>`
    + (art ? `<p class="destination-theme" data-location-theme><span>${art.illustrative ? '景观意象' : '景观'}</span>${esc(art.subtitle)}</p>` : '')
    + `<p class="destination-description">${esc(focus.desc)}</p>`
    + renderSectEntrance(state, focus, blocked)
    + (here ? `<div class="destination-here">你正在${esc(focus.name)}。点舆图上的另一处地点，安排下一程。</div>`
      : `<div class="route-quote"><span>${esc(state.player.locationName)} → ${esc(focus.name)}</span>`
        + `<small>地图距离 ${esc(focus.travel?.distance ?? '—')}</small></div>`
        + `<div class="travel-modes" role="radiogroup" aria-label="赶路方式">${options.map(mode => `<button class="travel-mode${mode.id === option?.id ? ' selected' : ''}" role="radio" aria-checked="${mode.id === option?.id}" `
          + `data-action="travel-mode" data-travel-mode="${esc(mode.id)}" data-id="${esc(mode.id)}" ${mode.enabled ? '' : 'disabled'}>`
          + `<span class="travel-mode-top"><strong>${esc(mode.name)}</strong><b>${mode.days} 天 · ${mode.cost ? `${mode.cost} 灵石` : '免费'}</b></span>`
          + `<span class="travel-mode-desc">${esc(mode.desc)}</span>`
          + `${!mode.enabled && mode.reason ? `<span class="travel-mode-reason">${esc(mode.reason)}</span>` : ''}</button>`).join('')}</div>`
        + `${!options.length ? '<p class="travel-mode-reason">正在读取此地的交通报价。</p>' : ''}`
        + `<div class="travel-start-actions"><button id="map-travel-start" class="primary" data-action="travel" data-id="${esc(focus.id)}" data-begin="1" ${available ? '' : 'disabled'}>启程赶路</button>`
        + `<button id="map-travel-now" data-action="travel" data-id="${esc(focus.id)}" data-begin="0" ${available ? '' : 'disabled'}>直接赶往</button></div>`
        + `<p class="travel-start-note">${esc(blocked || (!option?.enabled ? option?.reason || '暂无可用交通方式' : '启程后可逐日行进；直接赶往会推进至到达，遇事自动暂停。'))}</p>`)
    + renderDungeonEntrances(state, focus.id)
    + (services ? `<section class="location-services" data-location-services><h4>此地去处</h4>${services}</section>` : '')
    + `</div></aside>`;
}

// 用途：显示宁州舆图。
// 流程：读取地点和当前行程，应用搜索、类型、焦点与缩放，组合路线和详情。
// 调用方式：地图页传入state与地图选项。
// 返回值：舆图HTML字符串。
export function renderAtlas(state, { focus: focusId = null, search = '', filter = 'all', mode = '', zoom = 1 } = {}) {
  const locations = state.locations || [];
  const job = isTravelJob(state.job) ? state.job : null;
  const focus = locations.find(location => location.id === focusId)
    || locations.find(location => location.id === (job?.to || state.player.location)) || locations[0];
  const here = locations.find(location => location.id === state.player.location);
  const from = job ? locations.find(location => location.id === job.from) : here;
  const to = job ? locations.find(location => location.id === job.to) : focus;
  // 用途：判断地点是否符合舆图筛选。
  // 流程：同时比较当前地点类型与搜索关键词。
  // 调用方式：renderAtlas筛选locations时调用。
  // 返回值：布尔值。
  const matches = location => (filter === 'all' || filter === location.type)
    && (!search.trim() || `${location.name} ${location.desc || ''}`.includes(search.trim()));
  const pins = locations.map(location => {
    const current = location.id === state.player.location;
    const destination = !current && location.id === to?.id;
    const cls = ['map-pin', location.type, current ? 'here' : '', destination ? 'destination' : '',
      location.id === focus?.id ? 'focused' : '', !matches(location) ? 'dimmed' : '',
      location.x >= 86 ? 'edge-right' : location.x <= 10 ? 'edge-left' : '',
      location.y >= 75 ? 'edge-bottom' : ''].filter(Boolean).join(' ');
    return `<button class="${cls}" style="left:${Number(location.x)}%;top:${Number(location.y)}%" data-map="${esc(location.id)}" data-map-loc="${esc(location.id)}" `
      + `title="${esc(location.name)} · ${esc(TYPES[location.type])}${current ? ' · 当前所在' : destination ? ' · 目的地' : ''}" aria-pressed="${location.id === focus?.id}">`
      + `<span class="pin-dot"></span><span class="pin-name">${esc(location.name)}${current ? '<small>此处</small>' : destination ? '<small>目的地</small>' : ''}</span></button>`;
  }).join('');
  const filtered = locations.filter(matches);
  const dungeonPins = visibleDungeonSites(state).filter(site => site.available && !site.cleared).map((site, index) => {
    const location = locations.find(location => location.id === site.location);
    if (!location) return '';
    const offset = visibleDungeonSites(state, site.location).indexOf(site);
    return `<button class="atlas-dungeon-pin" data-action="dungeon-locate" data-id="${esc(location.id)}" style="left:${Number(location.x)}%;top:calc(${Number(location.y)}% + ${24 + offset * 22}px)" aria-label="${esc(site.name)}，${esc(location.name)}，${esc(site.realmName)}" title="${esc(site.name)} · ${esc(site.realmName)}"><span>♜</span>${esc(site.name)}</button>`;
  }).join('');
  // 手机以独立列表呈现秘境，避免同地点的 22px 标记间距挤压触控区域。
  const mobileSites = visibleDungeonSites(state).filter(site => site.available && !site.cleared);
  const mobileDungeons = mobileSites.length ? `<details class="nz-mobile-dungeons"><summary>地图秘境 · ${mobileSites.length} 处</summary><div>`
    + mobileSites.map(site => `<button data-map="${esc(site.location)}"><strong>${esc(site.name)}</strong><small>${esc(locations.find(location => location.id === site.location)?.name || site.location)} · ${esc(site.realmName)}</small></button>`).join('')
    + `</div></details>` : '';
  const scale = Math.max(.75, Math.min(2, Number(zoom) || 1));
  const directory = `<section class="atlas-directory"><div class="atlas-directory-body"><div class="atlas-search"><label><input data-map-search type="search" aria-label="搜索地点" value="${esc(search)}" placeholder="搜索城池、宗门、险地"></label>`
    + `<select data-map-filter aria-label="地点类型">${['all', 'city', 'sect', 'wild'].map(type => `<option value="${type}" ${filter === type ? 'selected' : ''}>${TYPES[type] || '全部地点'}</option>`).join('')}</select></div>`
    + `<details class="atlas-directory-list" ${search || filter !== 'all' ? 'open' : ''}><summary>选择目的地 <span>${search || filter !== 'all' ? `${filtered.length} / ` : ''}${locations.length} 处山川</span></summary>`
    + `<div class="atlas-place-list">${filtered.map(location => `<button class="atlas-place${location.id === focus?.id ? ' selected' : ''}" data-map="${esc(location.id)}" data-map-loc="${esc(location.id)}">`
      + `<strong>${esc(location.name)}</strong><small>${esc(TYPES[location.type])}${location.id === state.player.location ? ' · 此处' : ''}${location.danger ? ` · 危险 ${location.danger}` : ''}</small></button>`).join('') || '<p class="atlas-no-results">没有符合条件的地点。</p>'}</div></details></div></section>`;
  return journeyPanel(state) + `<div class="atlas-layout"><section class="atlas-main"><div class="atlas-frame">`
    + `<div class="atlas-map-heading"><h3>宁州山川</h3><div class="atlas-controls"><button data-action="map-zoom" data-id="-.25" aria-label="缩小舆图" ${scale <= .75 ? 'disabled' : ''}>−</button>`
    + `<button data-action="map-zoom" data-id="fit">全览</button><button data-action="map-zoom" data-id=".25" aria-label="放大舆图" ${scale >= 2 ? 'disabled' : ''}>＋</button></div></div>`
    + `<div class="atlas-viewport" data-map-viewport data-zoom="${scale}" tabindex="0" aria-label="宁州舆图，可滚动查看">`
    + `<div class="map-canvas atlas-canvas" data-map-atlas style="width:${scale * 100}%;min-width:${scale < 1 ? '100%' : `${Math.round(660 * scale)}px`}"><img class="atlas-art" src="assets/map/ningzhou-atlas.webp" alt="宁州山川、河流与海岸舆图" width="1600" height="1000">`
    + routeDrawing(from, to, job ? (Number(job.elapsed) || 0) / Math.max(1, Number(job.total) || 1) : 0)
    + pins + dungeonPins + `<span class="atlas-compass" aria-hidden="true">北<br>↑</span></div></div>`
    + `<div class="atlas-legend"><span><i class="city"></i>城池</span><span><i class="sect"></i>宗门</span><span><i class="wild"></i>险地</span>`
    + `<span><i class="here"></i>当前所在</span><span><i class="destination"></i>目的地</span><small>滚动查看 · 点选地点</small></div><p class="nz-map-hint">全览看方位；放大舆图或使用下方「选择目的地」目录，可准确选择地点。</p></div>`
    + directory + mobileDungeons + `</section>` + destinationPanel(state, focus, mode || (job?.to === focus?.id ? job.mode : '')) + `</div>`;
}
