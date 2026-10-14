// 本轮便利功能的纯展示/客户端意图检查；不连接玩家服务，不改变存档。
import assert from 'node:assert/strict';
import { renderFriendList } from '../web/friends.mjs';
import { renderDungeonIntel } from '../web/dungeons.mjs';
import { renderCraftSupply } from '../web/workshop.mjs';

const nearby = [
  { id: 'gold-a', name: '金修甲', stage: 3, realmName: '筑基初期', relation: 55, dao: '金', buildId: 'gold', buildName: '金虹弃牌', age: 30 },
  { id: 'wood-b', name: '木修乙', stage: 5, realmName: '筑基后期', relation: 90, dao: '木', buildId: 'wood', buildName: '毒生回春', age: 40 },
  { id: 'gold-c', name: '金修丙', stage: 6, realmName: '金丹初期', relation: -10, dao: '金', buildId: 'gold', buildName: '金虹弃牌', age: 90 },
];
const route = { enabled: true, alreadyHere: false, destination: 'wuling', destinationName: '武陵城', mode: 'walk', days: 4, cost: 0 };
const state = { player: { name: '隔离角色', location: 'wild' }, nearby, locations: [{ id: 'wild' }, { id: 'wuling' }, { id: 'tianji' }],
  serviceRoutes: { market: route, intel: { ...route, destination: 'tianji', destinationName: '天机阁', days: 8, cost: 10 } },
  dungeons: { intel: [] } };
let passed = 0;
function check(value, label) { assert.ok(value, label); passed++; console.log(`PASS ${label}`); }
const combined = renderFriendList(state, { filters: { build: 'gold', realm: '1', relation: 'friendly', search: '甲' } });
check(combined.includes('data-person="gold-a"') && !combined.includes('data-person="wood-b"') && !combined.includes('data-person="gold-c"'), '当地仙友可组合流派、境界、好感与姓名筛选');
const empty = renderFriendList(state, { filters: { search: '查不到的姓名' } });
check(empty.includes('没有符合条件的仙友') && !empty.includes('data-person='), '无匹配不退回全部仙友');
const ascending = renderFriendList(state, { filters: { sort: 'stage-asc' } });
check(ascending.indexOf('data-person="gold-a"') < ascending.indexOf('data-person="wood-b"') && ascending.indexOf('data-person="wood-b"') < ascending.indexOf('data-person="gold-c"'), '按真实境界排序且保留NPC稳定ID');
check(combined.includes('data-friend-filter="build"') && combined.includes('data-friend-filter="realm"') && combined.includes('data-friend-filter="relation"') && combined.includes('匹配 1 / 3'), '列表显示筛选项与匹配数');
check(renderDungeonIntel(state).includes('一键前往天机阁') && renderDungeonIntel(state).includes('8 天 / 10 灵石'), '情报异地显示后端路线天数与费用');
check(renderCraftSupply({ enabled: false, totalCost: 25, rows: [], travel: { ...route, targetTab: 'workshop', targetSub: 'brew' } }, { kind: 'brew', id: 'test', batch: 1 }).includes('一键前往武陵城'), '补料报价有可用城市路线入口');
const navigation = await import('../web/service-navigation.mjs');
const filters = await import('../web/friend-filters.mjs');
const intent = navigation.createServiceIntent(state, route, { tab: 'workshop', sub: 'brew' });
check(intent?.destination === 'wuling' && intent.tab === 'workshop' && intent.sub === 'brew', '导航意图只保存地点及白名单页签');
check(!navigation.createServiceIntent(state, { ...route, destination: '../../secrets' }, { tab: 'workshop', sub: 'brew' }), '不存在地点不能成为导航意图');
check(!navigation.createServiceIntent(state, route, { tab: 'javascript:alert(1)' }), '未知目标页签不能成为导航意图');
check(navigation.serviceIntentStatus({ ...state, job: { type: 'travel', to: 'wuling', remaining: 2 }, pending: {} }, intent) === 'travelling', '遇事暂停仍保留未完成旅途');
check(navigation.serviceIntentStatus({ ...state, player: { ...state.player, location: 'wuling' }, job: null }, intent) === 'arrived', '真实到达且无工作后才打开目标页面');
check(navigation.serviceIntentStatus({ ...state, player: { ...state.player, location: 'wuling' }, pending: {} }, intent) !== 'arrived', '到达时还有事件则先处理事件');
const storage = new Map(); const fakeStorage = { getItem: key => storage.get(key) || null, setItem: (key, value) => storage.set(key, value), removeItem: key => storage.delete(key) };
navigation.saveServiceIntent(intent, fakeStorage);
check(navigation.loadServiceIntent(state, fakeStorage)?.destination === 'wuling', '刷新后可恢复合法服务导航意图');
check(!navigation.loadServiceIntent({ ...state, player: { ...state.player, name: '另一角色' } }, fakeStorage), '角色切换不套用另一角色导航');
navigation.saveServiceIntent(null, fakeStorage);
check(!navigation.loadServiceIntent(state, fakeStorage), '取消导航可清除本地意图');
check(filters.filterNearbyFriends(nearby, { relation: 'hostile' }).map(npc => npc.id).join() === 'gold-c', '负好感边界可筛选');
check(filters.filterNearbyFriends(nearby, { sort: 'relation-desc' })[0].id === 'wood-b', '好感高低排序按真实数值');
const fallback = { id: 'unknown', stage: 1, relation: 0, dao: '水' };
check(filters.friendBuild(fallback).id === 'dao:水' && filters.friendBuild(fallback).name === '水道', '缺流派字段时按大道兼容展示');
const hostile = renderFriendList({ ...state, nearby: [{ ...nearby[0], buildName: '<img src=x onerror=alert(1)>' }] });
check(!hostile.includes('<img src=x'), '人物流派展示转义文本');
const disabled = renderDungeonIntel({ ...state, serviceRoutes: { intel: { ...route, enabled: false, reason: '先处理眼前事情' } } });
check(/data-action="service-travel"[^>]*disabled/.test(disabled) && disabled.includes('先处理眼前事情'), '不能启程时导航禁用并显示后端原因');
console.log(`便利功能纯展示 ${passed} 项通过。`);
