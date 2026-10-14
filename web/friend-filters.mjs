// 当地仙友的纯客户端筛选。NPC 身份与构筑均读取服务端；不推进时间或重算配装。
import { esc } from './shell.mjs';
const REALMS = ['炼气', '筑基', '金丹', '元婴', '化神'];
const RELATIONS = [['all', '全部好感'], ['hostile', '负好感'], ['acquaintance', '0～39'], ['friendly', '40～59'], ['close', '60及以上']];
const SORTS = [['default', '默认顺序'], ['stage-asc', '境界从低到高'], ['stage-desc', '境界从高到低'], ['relation-desc', '好感从高到低'], ['relation-asc', '好感从低到高']];
const STORAGE_KEY = 'nz-desktop-nearby-filters';

// 用途：取得实际流派或旧协议大道后备；流程：列表筛选与标签；用法：传 NPC；返回：稳定筛选 ID 和名称。
export function friendBuild(npc) {
  if (typeof npc.buildId === 'string' && npc.buildId) return { id: npc.buildId, name: npc.buildName || npc.dao || '流派' };
  return { id: `dao:${npc.dao || 'unknown'}`, name: npc.dao ? `${npc.dao}道` : '流派待查' };
}

// 用途：校验浏览器筛选草稿；流程：恢复、修改与纯测试；用法：传任意对象；返回：完整合法筛选对象。
export function normalizeFriendFilters(value = {}) {
  return { build: typeof value?.build === 'string' && value.build.length <= 100 ? value.build : 'all',
    realm: value?.realm === 'all' || /^[0-4]$/.test(String(value?.realm)) ? String(value.realm) : 'all',
    relation: RELATIONS.some(([id]) => id === value?.relation) ? value.relation : 'all',
    search: typeof value?.search === 'string' ? value.search.slice(0, 100) : '',
    sort: SORTS.some(([id]) => id === value?.sort) ? value.sort : 'default' };
}

// 用途：组合筛选并稳定排序；流程：当地仙友名单展示；用法：传原 NPC 数组与偏好；返回：新数组，保留原对象与 ID。
export function filterNearbyFriends(nearby, preferences = {}) {
  const filters = normalizeFriendFilters(preferences), keyword = filters.search.trim().toLocaleLowerCase('zh-CN');
  const list = nearby.filter(npc => {
    const build = friendBuild(npc), relation = Number(npc.relation) || 0;
    return (filters.build === 'all' || filters.build === build.id)
      && (filters.realm === 'all' || Math.floor(Number(npc.stage) / 3) === Number(filters.realm))
      && (filters.relation === 'all' || (filters.relation === 'hostile' && relation < 0)
        || (filters.relation === 'acquaintance' && relation >= 0 && relation < 40)
        || (filters.relation === 'friendly' && relation >= 40 && relation < 60)
        || (filters.relation === 'close' && relation >= 60))
      && (!keyword || `${npc.name || ''} ${npc.sectName || ''} ${npc.realmName || ''} ${build.name}`.toLocaleLowerCase('zh-CN').includes(keyword));
  });
  const sign = filters.sort.endsWith('asc') ? 1 : -1;
  return list.sort((left, right) => filters.sort.startsWith('stage-')
    ? (Number(left.stage) - Number(right.stage)) * sign || (Number(right.relation) || 0) - (Number(left.relation) || 0)
    : ((Number(left.relation) || 0) - (Number(right.relation) || 0)) * sign);
}

// 用途：恢复本地列表偏好；流程：桌面启动；用法：可注入存储；返回：合法偏好，存储失败使用默认值。
export function loadFriendFilters(storage = globalThis.localStorage) {
  try { return normalizeFriendFilters(JSON.parse(storage?.getItem(STORAGE_KEY) || '{}')); }
  catch { return normalizeFriendFilters(); }
}

// 用途：保存筛选偏好；流程：玩家修改筛选；用法：传偏好，可注入存储；返回：无，不修改游戏存档。
export function saveFriendFilters(preferences, storage = globalThis.localStorage) {
  try { storage?.setItem(STORAGE_KEY, JSON.stringify(normalizeFriendFilters(preferences))); }
  catch { /* 本地存储失败不阻止当前筛选。 */ }
}

// 用途：展示流派、境界、好感、搜索与排序；流程：当地仙友名单；用法：传原名单、偏好、匹配数；返回：HTML。
export function renderFriendFilters(nearby, preferences, count) {
  const filters = normalizeFriendFilters(preferences), builds = new Map();
  nearby.forEach(npc => { const build = friendBuild(npc); builds.set(build.id, build.name); });
  if (filters.build !== 'all' && !builds.has(filters.build)) builds.set(filters.build, '已选流派（本地暂无）');
  const select = (key, label, options) => `<label>${label}<select data-friend-filter="${key}" aria-label="${label}">${options.map(([id, name]) => `<option value="${esc(id)}" ${filters[key] === id ? 'selected' : ''}>${esc(name)}</option>`).join('')}</select></label>`;
  return `<section class="friend-filters" data-friend-filters>`
    + select('build', '流派', [['all', '全部流派'], ...builds])
    + select('realm', '大境界', [['all', '全部境界'], ...REALMS.map((name, index) => [String(index), name])])
    + select('relation', '好感', RELATIONS) + select('sort', '排序', SORTS)
    + `<label class="friend-search">搜索<input type="search" data-friend-search value="${esc(filters.search)}" placeholder="姓名、宗门或流派" aria-label="搜索当地仙友"></label>`
    + `<div class="friend-filter-summary"><span>匹配 ${count} / ${nearby.length} 位仙友</span><button class="subtle" data-friend-filter-reset>重置筛选</button></div></section>`;
}
