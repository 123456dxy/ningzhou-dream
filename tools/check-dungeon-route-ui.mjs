// 秘境纯展示定向检查；模拟权威字段，不替代真实后端扫荡与事件结算。
import assert from 'node:assert/strict';
import { renderDungeonBossPreview, renderDungeonDirectory, renderDungeonEntrances, renderDungeonRoute, renderDungeonRun } from '../web/dungeons.mjs';

let checks = 0;
// 用途：记录展示断言；流程：本工具的各个模拟状态；调用方式：传真假值与说明；返回值：无，失败抛错。
function check(value, message) { assert.ok(value, message); checks++; }
const site = { id: 'site-1', name: '试炼山谷', type: 'wild', realmName: '炼气初期', location: 'wuling', locationName: '武陵城',
  available: true, entryEnabled: true, expiresYear: 8, sweep: { enabled: true, reason: '', days: 1 },
  bossRewards: [{ kind: 'manual', name: '山河诀', description: '永久抄本 <效果>' }] };
const state = { player: { location: 'wuling', health: 100 }, dungeons: { sites: [site], specials: [] } };
const rewardHint = '筑基奖励档 · 材料2阶 · 灵石收益3倍 <后端>';
const tierDirectory = renderDungeonDirectory({ ...state, dungeons: { sites: [{ ...site, rewardHint }] } });
const tierRun = renderDungeonRun({ ...state, dungeons: { run: { siteId: site.id, phase: 'route', nodes: [], rewardHint } } });
check(tierDirectory.includes('data-dungeon-reward-hint') && tierRun.includes('data-dungeon-reward-hint')
  && tierDirectory.includes('灵石收益3倍 &lt;后端&gt;') && tierRun.includes('灵石收益3倍 &lt;后端&gt;'), '目录和探索页均原样展示后端境界奖励档并转义');
const entryRealm = renderDungeonRun({ ...state, dungeons: { run: { siteId: site.id, type: 'wild', phase: 'route', nodes: [], entryRealmName: '炼气初期', realmName: '筑基初期' } } });
const legacyRealm = renderDungeonRun({ ...state, dungeons: { run: { siteId: site.id, type: 'wild', phase: 'route', nodes: [], realmName: '筑基初期' } } });
check(entryRealm.includes('dungeon-eyebrow">炼气初期 · 野外秘境') && !entryRealm.includes('筑基初期')
  && legacyRealm.includes('dungeon-eyebrow">筑基初期 · 野外秘境'), '标题优先真实入口境界；旧视图无字段时回退既有境界');
const directory = renderDungeonDirectory(state);
check(directory.includes('data-action="dungeon-enter"') && directory.includes('data-action="dungeon-sweep"'), '正常探索与扫荡同时保留');
check(/data-action="dungeon-sweep"[^>]*data-id="site-1"/.test(directory) && directory.includes('扫荡 · 1 天'), '扫荡使用稳定站点ID和后端耗时');
check(!/data-action="dungeon-sweep"[^>]*disabled/.test(directory), '后端明确允许扫荡时才启用');
check(directory.includes('不获得沿途收益') && directory.includes('离开秘境时结算'), '明确扫荡主奖励与离境耗时');
check(/<details class="dungeon-boss-preview"[^>]*>/.test(directory) && !/<details class="dungeon-boss-preview"[^>]*\bopen\b/.test(directory), '目录通关奖励默认折叠');
check(directory.includes('永久抄本 &lt;效果&gt;'), '奖励预览兼容description并转义');
const blocked = renderDungeonDirectory({ ...state, player: { ...state.player, stage: 99 }, dungeons: { sites: [{ ...site,
  sweep: { enabled: false, reason: '需高出一个大境界 <受限>', days: 3 } }] } });
check(/data-action="dungeon-sweep"[^>]*disabled/.test(blocked) && blocked.includes('需高出一个大境界 &lt;受限&gt;'), '资格仅遵从后端，不按前端境界解锁');
check(blocked.includes('扫荡 · 3 天'), '耗时展示不写死一天');
const oldSite = { ...site }; delete oldSite.sweep;
check(!renderDungeonDirectory({ ...state, dungeons: { sites: [oldSite] } }).includes('data-action="dungeon-sweep"'), '旧视图没有扫荡报价时不伪造入口');
check(renderDungeonEntrances(state, 'wuling').includes('data-action="dungeon-sweep"'), '舆图当地入口也显示相同扫荡操作');
check(renderDungeonBossPreview(site, { details: true }).includes('永久抄本 &lt;效果&gt;'), '折叠详情使用同一奖励描述');

const nodes = [
  { id: 'start', type: 'start', name: '入口', row: 0, lane: 1, visited: true, next: ['fight', 'risk'] },
  { id: 'fight', type: 'elite', name: '断崖', row: 1, lane: 0, available: true, encounterName: '岩甲妖猿', encounterHint: '凝甲后重击 <预告>', next: ['camp'] },
  { id: 'risk', type: 'event', name: '古碑', row: 1, lane: 2, available: true, eventName: '血契祭坛', eventHint: '付出气血换取遗物', next: ['boss'] },
  { id: 'camp', type: 'camp', name: '松风营地', row: 2, lane: 0, next: ['boss'] },
  { id: 'boss', type: 'boss', name: '守关平台', encounterName: '玄火守卫', row: 3, lane: 1, next: [] },
];
const run = { siteId: site.id, siteName: site.name, type: 'wild', realmName: '炼气初期', phase: 'route', current: 'start',
  nodes, routeSummary: '共4层 · 强敌1处 <路线>', deck: [], relics: [] };
const route = renderDungeonRoute(run);
check(route.includes('data-dungeon-route-summary') && route.includes('共4层 · 强敌1处 &lt;路线&gt;'), '路线摘要只展示后端字符串并转义');
check((route.match(/data-dungeon-option="/g) || []).length === 2, '仅为当前可达节点生成路线对照卡');
check(!route.includes('岩甲妖猿') && !route.includes('凝甲后重击') && !route.includes('断崖')
  && !route.includes('玄火守卫') && !route.includes('守关平台'), '未触发战斗不在正文、title、aria或备用节点名披露怪物详情');
check(route.includes('血契祭坛') && route.includes('付出气血换取遗物'), '事件主题和风险预告继续保留，不随怪物遮蔽扩大范围');
check(route.includes('高风险 · 高奖励') && route.includes('后续可达') && route.includes('松风营地')
  && route.includes('守关者'), '精英风险与真实连接去向保留，后续守关者只显示通用类型');
check(route.includes('dungeon-room-legend') && route.includes('守关者') && route.includes('行商'), '保留节点类型图例');
const paused = renderDungeonRoute({ ...run, phase: 'battle' });
check(!paused.includes('data-dungeon-option="'), '战斗与领奖中不提供可提交的路线对照卡');
const oldRoute = renderDungeonRoute({ ...run, routeSummary: undefined, nodes: nodes.map(node => ({ ...node,
  encounterName: undefined, encounterHint: undefined, eventName: undefined, eventHint: undefined })) });
check(!oldRoute.includes('undefined') && !oldRoute.includes('断崖') && !oldRoute.includes('守关平台')
  && oldRoute.includes('古碑'), '旧响应name/text回退也不能泄漏未触发怪物，非战斗节点仍正常显示');
const currentFight = renderDungeonRoute({ ...run, current: 'fight', phase: 'battle' });
check(currentFight.includes('岩甲妖猿') && currentFight.includes('凝甲后重击 &lt;预告&gt;')
  && !currentFight.includes('玄火守卫'), '当前已触发遭遇揭晓详情，未来守关者继续隐藏');
const visitedFight = renderDungeonRoute({ ...run, nodes: nodes.map(node => node.id === 'fight' ? { ...node, visited: true } : node) });
check(visitedFight.includes('岩甲妖猿') && visitedFight.includes('凝甲后重击 &lt;预告&gt;')
  && !visitedFight.includes('玄火守卫'), '已访问战斗保留回顾信息，未访问怪物仍隐藏');
const eventRun = { ...run, phase: 'reward', current: 'risk', rewards: [{ id: 'offer', kind: 'eventChoice', name: '献血取宝',
  description: '获得后端给定遗物', cost: 12, healthCost: 8, enabled: false, reason: '气血不足 <拒绝>' }] };
const event = renderDungeonRun({ ...state, dungeons: { run: eventRun } });
check(event.includes('获得后端给定遗物') && event.includes('12 灵石') && event.includes('气血 -8'), '事件显示后端效果、灵石与气血代价');
check(/data-action="dungeon-claim"[^>]*data-id="offer"[^>]*disabled/.test(event) && event.includes('气血不足 &lt;拒绝&gt;'), '不可执行事件选项禁用并显示原因');
check(event.includes('继续前行') && !event.includes('放弃本次奖励')
  && !event.includes('沿途神通与遗物只在本次秘境生效，构筑会带入后续斗法。'), '奖励阶段的奇遇仍使用事件说明和继续文案');
const complete = renderDungeonRun({ ...state, dungeons: { run: { ...run, phase: 'complete', cleared: true } } });
check(complete.includes('秘境已通关') && !complete.includes('守关者已被击败'), '完成文案同时适用于正常探索和扫荡');
console.log(`秘境路线纯展示检查：${checks}/${checks} 通过（模拟权威字段）`);
