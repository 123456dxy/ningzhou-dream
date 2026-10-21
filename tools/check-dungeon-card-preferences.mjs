// 临时神通奖励偏好纯展示/存储契约，不替代后端过滤和真实入场验证。
import assert from 'node:assert/strict';
import { renderDungeonDirectory, renderDungeonEntrances, renderDungeonRun } from '../web/dungeons.mjs';
import { loadDungeonCardRewardPreference, saveDungeonCardRewardPreference, renderDungeonCardRewardPreference,
  dungeonCardRewardSummary } from '../web/dungeon-card-preferences.mjs';

let checks = 0;
// 用途：记录本工具断言；流程：偏好/冻结显示的独立夹具；返回：无，失败抛错。
function check(condition, message) { assert.ok(condition, message); checks++; }
const options = { modes: [{ id: 'all', name: '全部类型' }, { id: 'none', name: '不获取临时神通' }, { id: 'selected', name: '指定类型' }],
  daos: [{ id: 'metal', name: '金道' }, { id: 'water', name: '水道' }] };
const fixture = { player: { location: 'wuling', health: 100 }, dungeons: { cardRewardOptions: options, sites: [{ id: 'one',
  type: 'wild', name: '临时秘境', location: 'wuling', realmName: '炼气初期' }] } };
const memory = new Map(); let writes = 0;
globalThis.localStorage = { getItem: key => memory.get(key) ?? null, setItem: (key, value) => { writes++; memory.set(key, value); } };
check(loadDungeonCardRewardPreference().mode === 'all', '首次默认全部类型');
const all = renderDungeonCardRewardPreference(options, { mode: 'all', daos: [] });
check(all.includes('data-dungeon-card-mode') && /value="all"[^>]*selected/.test(all), '模式由后端候选渲染且默认all');
check((all.match(/data-dungeon-card-dao/g) || []).length === 2 && (all.match(/type="radio"[^>]*disabled/g) || []).length === 2,
  '只列后端实际有奖励的道，all禁用单系选项，不凭空加入体道');
const none = renderDungeonCardRewardPreference(options, { mode: 'none', daos: [] });
check(none.includes('不获取临时神通') && /value="none"[^>]*selected/.test(none)
  && (none.match(/type="radio"[^>]*disabled/g) || []).length === 2, 'none明确不出临时神通并禁用类型选择');
const single = { mode: 'selected', daos: ['water'] };
const selected = renderDungeonCardRewardPreference(options, single);
check((selected.match(/type="radio"[^>]*checked/g) || []).length === 1 && !selected.includes('disabled')
  && (selected.match(/name="dungeon-card-dao"/g) || []).length === 2 && selected.includes('单选'), 'selected使用同名单选项，只能指定一个派系');
check(dungeonCardRewardSummary(single, options).includes('水道'), '单系摘要使用后端中文名，不从中文反推机器ID');
check(renderDungeonCardRewardPreference(options, { mode: 'selected', daos: [] }).includes('请选择一个'), '空选明确提示，不静默变成all');
check(!renderDungeonCardRewardPreference(undefined).includes('data-dungeon-card-mode'), '旧接口无候选目录时不伪造模式和类型控件');
saveDungeonCardRewardPreference(single);
const writeCount = writes, before = JSON.stringify(fixture);
check(renderDungeonDirectory(fixture).includes('data-dungeon-card-mode') && renderDungeonDirectory(fixture).includes('指定类型：水道'), '秘境目录显示单系偏好控件与当前摘要');
check(renderDungeonEntrances(fixture, 'wuling').includes('指定类型：水道'), '地图入口显示当前单系入场偏好摘要');
const frozen = renderDungeonRun({ ...fixture, dungeons: { ...fixture.dungeons, run: { siteId: 'one', siteName: '临时秘境',
  phase: 'route', nodes: [], cardRewardPreference: { mode: 'none', daos: [] } } } });
check(frozen.includes('不获取临时神通') && frozen.includes('途中不能更改，永久通关奖励不受影响')
  && !frozen.includes('data-dungeon-card-mode'), '探索只展示后端冻结none，不读取本地单系覆盖或提供途中修改');
const noCardReward = renderDungeonRun({ ...fixture, dungeons: { ...fixture.dungeons, run: { siteId: 'one', siteName: '临时秘境',
  phase: 'reward', current: 'fight', nodes: [{ id: 'fight', type: 'battle', visited: true }], rewards: [],
  cardRewardPreference: { mode: 'none', daos: [] } } } });
check(noCardReward.includes('已按偏好不提供临时神通，继续前行') && noCardReward.includes('>继续前行</button>')
  && !noCardReward.includes('放弃本次奖励'), '后端因偏好返回空奖励时说明原因并允许继续，不伪造额外收益');
check(writes === writeCount && JSON.stringify(fixture) === before, '目录/地图/探索渲染不写偏好、不改游戏状态');
const copy = loadDungeonCardRewardPreference(); copy.daos.push('fire');
check(loadDungeonCardRewardPreference().daos.join(',') === 'water', '加载返回独立副本，不泄漏内部偏好数组');
const storageDescriptor = Object.getOwnPropertyDescriptor(globalThis, 'localStorage');
Object.defineProperty(globalThis, 'localStorage', { configurable: true, get() { throw new Error('storage blocked'); } });
saveDungeonCardRewardPreference({ mode: 'selected', daos: ['water', 'water'] });
check(loadDungeonCardRewardPreference().mode === 'selected' && loadDungeonCardRewardPreference().daos.join(',') === 'water',
  '浏览器存储被禁止时以内存降级保留选择并去重');
Object.defineProperty(globalThis, 'localStorage', storageDescriptor);
const hostileOptions = { modes: [{ id: 'selected', name: '<img src=x onerror=1>' }], daos: [{ id: 'metal', name: '<svg onload=1>' }] };
const hostile = renderDungeonCardRewardPreference(hostileOptions, { mode: 'selected', daos: ['metal'] });
check(hostile.includes('&lt;img src=x onerror=1&gt;') && hostile.includes('&lt;svg onload=1&gt;') && !hostile.includes('<svg'), '后端候选名称按文本转义，不插入HTML');
saveDungeonCardRewardPreference({ mode: 'selected', daos: ['metal', '" onfocus="bad', 'metal'] });
check(loadDungeonCardRewardPreference().daos.join(',') === 'metal', '本地草稿只保存稳定ID形状，不接受注入字符串');
memory.set('nz-desktop-dungeon-card-rewards', JSON.stringify({ mode: 'selected', daos: ['metal', 'water'] }));
const legacyWriteCount = writes;
check(loadDungeonCardRewardPreference().mode === 'all' && loadDungeonCardRewardPreference().daos.length === 0,
  '旧浏览器多系偏好明确重置全部，不静默挑第一个派系');
const reset = renderDungeonCardRewardPreference(options);
check(/value="all"[^>]*selected/.test(reset) && reset.includes('原多系奖励偏好已重置为全部') && writes === legacyWriteCount,
  '重置提示可见，默认控件为全部且只读渲染不改浏览器存储');
check(dungeonCardRewardSummary({ mode: 'selected', daos: ['metal', 'water'] }, options).includes('金道、水道'),
  '旧在途多系冻结摘要真实显示原类型，不错误标成全部');
saveDungeonCardRewardPreference({ mode: 'selected', daos: ['metal', 'water'] });
check(loadDungeonCardRewardPreference().mode === 'all', '保存边界也拒绝多系草稿，不扩大为多个新入场类型');
console.log(`秘境临时神通偏好展示契约：${checks}/${checks} 通过。`);
