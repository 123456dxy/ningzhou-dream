// Frontend regression checks: stale state, inaccessible actions and duplicate mutations.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import * as tianji from '../web/tianji.mjs';
import { renderDaos, renderSpellPicker, renderArtBuilds, renderManualIllustration } from '../web/arts.mjs';
import { renderTidePanel, shouldRenderTide, renderTideMarket, renderTideBattleNotice } from '../web/tide-panel.mjs';
import { renderFriendPanel, renderBondActions, renderNpcLearning, renderNpcLoadout } from '../web/friends.mjs';
import { bindRoster } from '../web/roster.mjs';
import { renderAtlas, selectedTravelMode, travelFeedback, isTravelJob } from '../web/map.mjs';
import { LOCATION_ART, locationArt, renderLocationArt } from '../web/location-art.mjs';
import * as portraits from '../web/portraits.mjs';
import * as shell from '../web/shell.mjs';
import * as cards from '../web/cards.mjs';
import * as dungeons from '../web/dungeons.mjs';
import * as caveUI from '../web/cave-ui.mjs';
import { renderEnding } from '../web/legacy.mjs';
import { renderSectPicker, renderSectSpellLibrary, renderSectMentors, renderSectTasks } from '../web/sects.mjs';
import * as artifacts from '../web/artifacts.mjs';
import { renderNumericPreview } from '../web/numeric-preview.mjs';
import { renderRenownSources } from '../web/renown.mjs';
import { renderEconomy, forgeRequirements, brewRequirements, renderPillEffect, marketGrade } from '../web/economy.mjs';
import { renderInventoryPanel } from '../web/inventory-panel.mjs';
import { renderTaskPanel } from '../web/task-panel.mjs';
import { renderCraftSupply, renderSalvageQuote } from '../web/workshop.mjs';

const rootDir = join(dirname(fileURLToPath(import.meta.url)), '..');
let checks = 0;
function verify(ok, label) { assert.ok(ok, label); checks++; }
const state = {
  player: { name: '测试', gender: '女', age: 16, lifespan: 100, stage: 0, realmName: '炼气初期',
    location: 'wuling', locationName: '武陵城', stones: 1000, health: 100, sword: 0 },
  clock: { year: 1, month: 1, day: 1, left: 30 }, world: { market: 1, coreStock: 2, pillStock: 2 },
  arts: { points: 3, nodes: ['0-1'], spells: ['one'], exp: { 金: 100 }, focus: '金', mods: { cult: 1 },
    manualList: [], manuals: {}, herbs: {}, potions: {}, durability: 40 },
  nearby: [{ id: 'friend', name: '道友', realmName: '炼气初期' }],
  market: [{ kind: 'gear', id: 'blade-1', name: '剑', grade: 1, price: 50 }],
  brewOptions: Array.from({ length: 15 }, (_, n) => ({ id: `r${n}`, name: `丹方${n}`, grade: 1, enabled: true, chance: .5, stoneCost: 8, days: 5 })),
  manualSources: Array.from({ length: 14 }, (_, n) => ({ id: `m${n}`, name: `功法${n}`, city: 'wuling', stage: 0, price: 10 })),
  furnaces: [], gears: [], inCity: true,
  locations: [
    { id: 'wuling', name: '武陵城', type: 'city', x: 42, y: 45, danger: 0, desc: '当前城池' },
    { id: 'tianxing', name: '天星城', type: 'city', x: 58, y: 62, danger: 0, desc: '目的地城池', travel: { from: 'wuling', to: 'tianxing', distance: 23, options: [
      { id: 'walk', name: '步行赶路', days: 8, cost: 0, enabled: true, desc: '沿陆路行进' },
      { id: 'boat', name: '驿站灵舟', days: 3, cost: 9, enabled: true, desc: '乘船行进' },
      { id: 'flight', name: '御器飞行', days: 2, cost: 5, enabled: false, reason: '御器飞行需达到筑基初期。', desc: '御器行进' },
    ] } },
    { id: 'sea', name: '无尽海', type: 'wild', x: 93, y: 50, danger: 60, desc: '海上险地', travel: { from: 'wuling', to: 'sea', distance: 51, options: [
      { id: 'walk', name: '步行赶路', days: 17, cost: 0, enabled: false, reason: '无尽海没有步行通路', desc: '陆路' },
      { id: 'boat', name: '驿站灵舟', days: 6, cost: 19, enabled: true, desc: '海路灵舟' },
    ] } },
    { id: 'jinhong', name: '金虹剑派', type: 'sect', x: 12, y: 20, danger: 0, desc: '宗门山门' },
  ],
};

const cityMap = renderAtlas(state);
verify(cityMap.includes('data-map-detail="wuling"') && cityMap.includes('当前城池')
  && cityMap.includes('assets/map/locations/wuling.webp') && !cityMap.includes('id="map-travel-start"'), '舆图默认显示当前城池详情与武陵城独立插画，无须先点一次地点');
const destination = state.locations[1];
const routeMap = renderAtlas(state, { focus: destination.id, mode: 'walk' });
verify(selectedTravelMode(destination).id === 'boat' && selectedTravelMode(destination, 'walk').id === 'walk', '可用灵舟为默认方式，选定的步行方式保留');
verify(routeMap.includes('8 天 · 免费') && routeMap.includes('3 天 · 9 灵石') && routeMap.includes('2 天 · 5 灵石')
  && routeMap.includes('御器飞行需达到筑基初期。'), '交通卡逐项显示引擎报价与境界限制');
verify(/data-travel-mode="flight"[^>]*disabled/.test(routeMap)
  && routeMap.includes('id="map-travel-start"') && routeMap.includes('data-begin="1"')
  && routeMap.includes('id="map-travel-now"') && routeMap.includes('data-begin="0"'), '不可用御器按钮禁用，启程和直接赶往各有入口');
const seaMap = renderAtlas(state, { focus: 'sea', mode: 'walk' });
verify(/data-travel-mode="walk"[^>]*disabled/.test(seaMap) && seaMap.includes('无尽海没有步行通路')
  && /id="map-travel-start"[^>]*disabled/.test(seaMap), '海路步行显示拒绝原因并阻止启程');
verify(renderAtlas(state, { focus: 'jinhong' }).includes('assets/map/locations/jinhong.webp')
  && seaMap.includes('assets/map/locations/sea.webp'), '金虹山门与无尽海使用各自地点插画');

// 读取引擎真实地点表作为测试输入，避免只检查一两个代表地点而漏掉其余地点。
const modelSource = readFileSync(join(rootDir, 'src', 'core', 'model.h'), 'utf8');
const localLocations = [...modelSource.matchAll(/\{\s*"([^\"]+)",\s*"([^\"]+)",\s*"(city|sect|wild)",\s*(\d+),\s*(\d+),\s*"([^\"]*)",\s*(\d+)\s*\}/g)]
  .map(([, id, name, type, x, y, desc, danger]) => ({ id, name, type, x: +x, y: +y, desc, danger: +danger }));
const reference = JSON.parse(readFileSync(join(rootDir, 'web', 'assets', 'map', 'location-reference.json'), 'utf8'));
verify(localLocations.length === reference.locations.length && new Set(localLocations.map(location => location.id)).size === reference.locations.length,
  '地图覆盖截图拼接与资料表中全部不同地点');
verify(reference.locations.every(r => localLocations.some(l => l.id === r.id && l.x === r.x && l.y === r.y)), '引擎地点坐标与核对过的截图相对布局一致');
verify(Object.keys(LOCATION_ART).length === localLocations.length && localLocations.every(location => LOCATION_ART[location.id])
  && locationArt('unknown-location') === null, '地点插画映射恰好覆盖真实地点表，未知地点不会误用其他地点图片');
const artSources = [];
const artDigests = [];
for (const location of localLocations) {
  const expectedSrc = locationArt(location)?.src;
  verify(expectedSrc && locationArt(location.id)?.src === expectedSrc
    && locationArt(location)?.alt.includes(location.name), `${location.name} 的地点对象和 id 均解析为正确图片与可读说明`);
  const html = renderAtlas({ ...state, locations: localLocations }, { focus: location.id });
  const imageTag = html.match(/<img\b[^>]*class="[^\"]*\bdestination-art\b[^\"]*"[^>]*>/)?.[0] || '';
  const src = imageTag.match(/\bsrc="([^\"]+)"/)?.[1];
  verify(src === expectedSrc
    && html.includes(`data-map-detail="${location.id}"`) && !/(?:city|sect|wild)-scene\.(?:png|webp)/.test(imageTag),
    `${location.name} 详情映射到指定地点意象，旧地点仍使用各自独立图片`);
  const png = readFileSync(join(rootDir, 'web', expectedSrc));
  verify(png.subarray(0, 4).toString() === 'RIFF' && png.subarray(8, 12).toString() === 'WEBP'
    && png.length > 1024, `${location.name} 的本地独立插画是真实的 WebP`);
  verify(src === `assets/map/locations/${location.id}.webp`, `${location.name} 使用自己的独立插画`);
  artSources.push(src);
  artDigests.push(createHash('sha256').update(png).digest('hex'));
}
verify(new Set(artSources).size === localLocations.length && new Set(artDigests).size === localLocations.length,
  '全部地点图片路径及文件内容各不相同');
const servicesMap = renderAtlas({ ...state, locations: [{ ...state.locations[0], services: [
  { id:'lecture', name:'学堂静心', desc:'读书静心', days:3, cost:2, enabled:true },
  { id:'closed', name:'高阶拍卖', days:0, cost:0, enabled:false, reason:'需金丹境界' },
] }] });
verify(servicesMap.includes('data-action="location-service" data-id="lecture"') && servicesMap.includes('3 天 · 2 灵石')
  && servicesMap.includes('需金丹境界'), '地点去处显示实际耗时费用及未开放原因');
const remoteServices = renderAtlas({ ...state, locations: [{ ...state.locations[1], services: [{ id:'lecture', name:'讲法', enabled:true }] }] });
verify(/data-action="location-service"[^>]*disabled/.test(remoteServices) && remoteServices.includes('需先抵达此地'), '远处地点功能入口需抵达后方可执行');
const travelState = { ...state, job: { type: 'travel', from: 'wuling', to: 'tianxing', mode: 'boat', total: 3, elapsed: 1, remaining: 2, cost: 9 } };
const journey = renderAtlas(travelState);
verify(isTravelJob(travelState.job) && journey.includes('data-map-detail="tianxing"')
  && journey.includes('data-from="wuling" data-to="tianxing"')
  && /class="map-pin[^\"]*here[^\"]*"[^>]*data-map="wuling"/.test(journey)
  && /class="map-pin[^\"]*destination[^\"]*"[^>]*data-map="tianxing"/.test(journey), '途中详情定位目的地，当前位置仍指向出发城池');
verify(journey.includes('data-travel-progress max="3" value="1"') && journey.includes('尚余 2 天')
  && journey.includes('启程已付 9 灵石') && /id="map-travel-start"[^>]*disabled/.test(journey), '途中显示实际进度与已付费用，并阻止重开行程');
const paused = renderAtlas({ ...travelState, pending: { title: '途中抉择' } });
verify(/id="map-travel-resume"[^>]*disabled/.test(paused) && /id="map-travel-step"[^>]*disabled/.test(paused), '途中有抉择时暂停继续和逐日按钮');
verify(!travelFeedback(travelState, 'tianxing', { report: { stopReason: 'done', daysPassed: 1 } }).arrived
  && travelFeedback({ ...travelState, pending: {} }, 'tianxing').kind === 'warn'
  && travelFeedback({ ...state, player: { ...state.player, location: 'tianxing' }, job: null }, 'tianxing').arrived,
  '赶路提示依据实际地点和未完成任务判断抵达，识别途中暂停');

const nodes = renderDaos(state, [{ id: '0-1', dao: '金', level: 1, cost: 1, name: '入门', unlocked: false }]);
verify(nodes.includes('已领悟'), '道法节点以最新 state 的拥有列表为准');
const spells = renderSpellPicker({ max: 4, spells: [{ id: 'one', name: '灵击', cost: 1, stage: 0, carried: false, kind: 'attack' }] }, state);
verify(spells.includes('已携带 · 点击卸下'), '神通携带状态不依赖旧缓存');
verify(spells.includes('直接攻击'), '神通选择说明其实际用途');
verify(spells.includes('所需境界：炼气初期'), '已达境界的神通仍显示携带门槛');
const dynamicState = { ...state, player: { ...state.player, stage: 3 }, arts: { ...state.arts,
  spellSlots: 6, spells: ['strike', 'defend', 'one'], mandatorySpells: [{ id: 'strike' }, { id: 'defend' }] } };
const dynamicSpells = renderSpellPicker({ max: 4, spells: [
  { id: 'strike', name: '普通攻击', cost: 20, actualCost: 1, stage: 0, mandatory: true },
  { id: 'defend', name: '防御', cost: 20, actualCost: 1, stage: 0, mandatory: true },
  { id: 'one', name: '灵击', cost: 20, actualCost: 1, stage: 0, kind: 'attack' },
  { id: 'high', name: '金丹法', cost: 40, actualCost: 2, stage: 6, stageName: '金丹初期' },
] }, dynamicState);
verify(dynamicSpells.includes('普通攻击与防御固定常备，各五张') && dynamicSpells.includes('每种额外神通入牌组两张'), '普通招式与额外神通分别显示五张和两张的牌组规则');
verify(dynamicSpells.includes('额外神通 1 / 6 槽') && !dynamicSpells.includes('data-id="strike"'), '槽位使用当前境界，固定牌不计额外槽且不可卸下');
verify(dynamicSpells.includes('灵力 1') && !dynamicSpells.includes('灵力 20'), '配装显示实际灵力费用');
verify(dynamicSpells.includes('所需境界：金丹初期') && dynamicSpells.includes('尚未达到金丹初期'), '高境界神通同时显示门槛与不可用原因');
const buildState = { ...dynamicState, artBuilds: [{ id: 'metal', name: '锐金弃牌', desc: '弃牌积累锐势，下一次直攻消耗锐势。', manuals: ['gold'], spells: ['one'], nodes: ['0-1'] }], arts: { ...dynamicState.arts, manualList: [{ id: 'gold', name: '金诀', isAux: true }] } };
const buildHTML = renderArtBuilds(buildState, { spells: [{ id: 'one', name: '灵击' }] }, [{ id: '0-1', name: '锐势' }]);
verify(buildHTML.includes('3 / 3 已具备') && buildHTML.includes('金诀') && buildHTML.includes('下一次直攻消耗锐势'), '流派组合解析组件与当前配装并显示真实触发条件');
const preview = { valid: true, text: '伤害 24；灼烧 +2 层，每回合 6 点，持续 3 回合。', conditions: '每层剑气追加伤害；当前剑气为 0。', context: '按当前境界与已装备功法计算，目标无护盾。' };
const learningState = { ...state, arts: { ...state.arts, knownSpells: ['known', 'blocked'] } };
const learningPicker = renderSpellPicker({ spells: [
  { id:'unknown', name:'未学神通', stage:0, kind:'fire', known:false, enabled:false, preview },
  { id:'known', name:'已学神通', stage:0, known:true, enabled:true, preview },
  { id:'blocked', name:'受限神通', stage:0, known:true, enabled:false, reason:'当前不可调整' },
] }, learningState);
verify(/class="card unlearned"[^>]*data-spell-id="unknown" disabled/.test(learningPicker)
  && !learningPicker.includes('data-action="spell" data-id="unknown"') && learningPicker.includes('未学会'), '未学神通明确置灰标记且不提供携带动作');
verify(learningPicker.includes('已学会 · 未携带') && learningPicker.includes('data-action="spell" data-id="known"')
  && !learningPicker.includes('data-action="spell" data-id="blocked"'), '已学未携带与后端禁止调整分别展示，动作遵守核心可用状态');
verify(learningPicker.includes('伤害 24') && learningPicker.includes('每回合 6 点') && learningPicker.includes('目标无护盾'), '非战斗神通显示实际数值与试算依据');
const catalogBuild = renderArtBuilds({ ...state, manualCatalog:[{id:'hidden',name:'重元转轮诀'}], manualSources:[{id:'copied',name:'问心诀',owned:true}], artBuilds:[
  {id:'copy',name:'功法进度',manuals:['hidden','copied'],spells:['unknown','known']}
] }, { spells:[{id:'unknown',name:'灵击',known:false},{id:'known',name:'明心咒',known:true}] }, []);
verify(catalogBuild.includes('重元转轮诀') && !catalogBuild.includes('>hidden<') && catalogBuild.includes('抄本已得 · 待修习')
  && catalogBuild.includes('未学会') && catalogBuild.includes('已学会 · 未携带'), '组合由完整中文目录解析功法，并区分抄本、修习、学习与携带状态');
verify(renderNumericPreview({valid:true,text:'<script>1</script>'}).includes('&lt;script&gt;')
  && renderNumericPreview({valid:false,text:'错误数值'}) === '', '数值预览转义文本且不把无效推演当作正常数字');
const numberedForge = artifacts.renderForgePreview({info:{valid:true,shapeName:'剑',grade:1,chance:.6,days:8,cost:33,
  passive:{text:'攻击倍率 +12 个百分点；气血上限 +0。'},ability:{id:'test',name:'天阳剑',dao:'剑',kind:'attack',cost:2,cooldown:1,preview},
  inscribedAbility:{id:'test',name:'铭刻天阳剑',dao:'剑',kind:'attack',preview:{valid:true,text:'伤害 27'}},inscribedPassive:{text:'攻击倍率 +13.4 个百分点。'}}});
verify(numberedForge.includes('伤害 24') && numberedForge.includes('攻击倍率 +12') && numberedForge.includes('铭刻后的数值') && numberedForge.includes('伤害 27'), '炼器同时显示装备属性、主动技能数字和铭刻对照');
const statusRows = cards.battleStatusRows({ buff: .3, reduction: -.2, effectiveReduction: .35, guard: .1,
  manaBonus: 2, manaPenalty: 1, carryMana: 3, carryCap: 4, drawNext: 2, lock: 2,
  weakened: 2, weakenedPenalty: .3, discardPower: .16, swordSequence: 3 });
verify(statusRows.some(x => x.label === '攻击增伤' && x.value === '+30%' && /本场/.test(x.scope))
  && statusRows.some(x => x.label === '总减伤' && x.value === '35%'), '战斗增伤与实际总减伤显示数值和持续范围');
verify(statusRows.some(x => x.label === '封存灵力' && x.value === '3 / 4 点')
  && statusRows.some(x => x.label === '锁灵' && x.value.includes('剩 2 回合'))
  && statusRows.some(x => x.label === '预谋' && x.value.includes('2 张')), '蓄灵、预谋、锁灵显示数量与剩余范围');
verify(statusRows.some(x => x.label === '弃牌锐势' && x.value.includes('+16%'))
  && statusRows.some(x => x.label === '扰灵' && x.value.includes('−30%')), '下次直攻增伤与减伤显示实际百分比');
verify(renderFriendPanel(state, 'friend', null).includes('class="social-actions"'), '道友往来保留社会动作入口');
verify(renderFriendPanel({...state,job:{type:'npcLearn',name:'请教灵击',remaining:2,total:3}},'friend',null)
  .includes('data-action="npc-learn-resume"'), '授业被暂停后道友面板保留继续研习入口');
const materialGift = renderFriendPanel({...state,arts:{...state.arts,materials:{iron:2}}},'friend',null,{materials:[{id:'iron',name:'铁矿'}]});
verify(materialGift.includes('data-kind="material"') && materialGift.includes('铁矿'), 'NPC赠送材料入口使用既有材料定义显示中文名');
const learningHTML = renderNpcLearning({ id:'friend', learning:[
  { kind:'spell',id:'one',name:'灵击',dao:'金',stage:3,days:8,creditCost:10,stoneCost:5,affinityNeed:30,enabled:true },
  { kind:'manual',id:'manual',name:'心诀',dao:'气',stage:6,learned:false,enabled:false,reason:'交情不足' },
  { kind:'spell',id:'known',name:'已知神通',stage:0,learned:true,enabled:false },
] });
verify(learningHTML.includes('需筑基初期') && learningHTML.includes('8 天 · 所需好感 30 · 消耗交情 10 · 5 灵石')
  && learningHTML.includes('data-kind="spell"') && learningHTML.includes('data-kind="manual"') && learningHTML.includes('交情不足')
  && learningHTML.includes('已领悟'), 'NPC学习分别列出神通/功法、境界、实际消耗、已知状态与拒绝原因');
const loadoutHTML = renderNpcLoadout({ id:'friend', dao:'金', loadout:{
  manuals:[{id:'gold',name:'金虹心诀',level:3,main:true}],spells:[{id:'one',name:'锐金诀'}],nodes:[{id:'gold1',name:'金道入门'}],
} });
verify(loadoutHTML.includes('主修 金虹心诀 3 层') && loadoutHTML.includes('锐金诀') && loadoutHTML.includes('金道入门')
  && loadoutHTML.includes('data-npc-loadout="friend"'), 'NPC详情展示后端实际功法层数、神通与悟道');
verify(portraits.portraitSource({gender:'女'}, 'player').endsWith('player-female.webp')
  && portraits.portraitSource({portrait:'assets/portraits/sect-xinghe.webp'}).endsWith('sect-xinghe.webp')
  && portraits.portraitSource({stage:9}, 'beast').endsWith('beast-3.webp'), '立绘优先读引擎路径，玩家性别和妖兽大境界回退正确');
const battleBefore = {enemy:'道友',npcId:'friend',turn:0,hp:100,shield:5,enemyHp:100,enemyShield:8};
verify(portraits.battleImpacts(null, battleBefore).length === 0
  && portraits.battleImpacts(battleBefore, {...battleBefore}).length === 0
  && portraits.battleImpacts(battleBefore, {...battleBefore,shield:0}).length === 0, '加载、相同状态与主动消耗护盾不制造虚假受击');
verify(portraits.battleImpacts(battleBefore, {...battleBefore,enemyHp:91})[0].damage === 9
  && portraits.battleImpacts(battleBefore, {...battleBefore,turn:1,hp:80})[0].side === 'player', '双方真实气血下降分别产生受击数值');
const bondState = { bondActions: ['lesson', 'teach', 'together', 'part', 'dismiss'].map(id => ({ id, name: id, days: 5, cost: 10, enabled: id !== 'dismiss', reason: id === 'dismiss' ? '尚无师尊' : '' })) };
const bondPanel = renderBondActions(bondState);
verify((bondPanel.match(/data-action="bond"/g) || []).length === 5 && bondPanel.includes('尚无师尊'), '五种师门与道侣动作可见，不能做时显示原因');

const listeners = {};
const root = { addEventListener(type, fn) { (listeners[type] ||= []).push(fn); } };
let actions = 0;
for (let n = 0; n < 8; n++) bindRoster(root, { onAction() { actions++; }, onRerender() {} });
const rosterButton = { dataset: { raction: 'message', npc: 'friend' } };
for (const fn of listeners.click) fn({ target: { closest(sel) { return sel === '[data-raction]' ? rosterButton : null; } }, stopPropagation() {} });
verify(actions === 1 && listeners.input.length === 1, '通讯录重绘八次仍只触发一次传讯');

const elements = new Map();
function element(sel) {
  if (!elements.has(sel)) elements.set(sel, { textContent: '本地存档', innerHTML: '', dataset: {}, open: false,
    className: '', hidden: true, hasChildNodes: () => false, addEventListener() {}, close() { this.open = false; }, showModal() { this.open = true; } });
  return elements.get(sel);
}
const handlers = {};
const document = { querySelector: element, getElementById(id) { return element('#' + id); },
  querySelectorAll() { return []; }, addEventListener(type, fn) { handlers[type] = fn; },
  body: { setAttribute() {}, removeAttribute() {} } };
const requests = [];
const context = { ...tianji, location: { hash: '' }, window: { addEventListener() {} }, ...shell, ...cards, ...portraits, ...dungeons, ...caveUI, ...artifacts, renderNumericPreview, renderAtlas, selectedTravelMode, travelFeedback, isTravelJob, LOCATION_ART, locationArt, renderLocationArt, document, console, setTimeout, clearTimeout,
  renderSectMentors, renderSectTasks, brewRequirements, renderPillEffect, marketGrade, renderInventoryPanel, renderTaskPanel, renderCraftSupply, renderSalvageQuote,
  renderManualIllustration, renderTidePanel, shouldRenderTide, renderTideMarket, renderTideBattleNotice,
  invalidateEco() {}, invalidateRoster() {}, invalidateSects() {}, invalidateSpells() {},
  invalidateFriendCache() {}, loadNpcBag:async()=>null,
  fetch: async (path, options) => { requests.push({ path, body: JSON.parse(options.body) }); return { ok: true, json: async () => ({ ok: true }) }; } };
for (const name of ['ningzhou-atlas']) {
  const img = readFileSync(join(rootDir, 'web', 'assets', 'map', name + '.webp'));
  verify(img.subarray(0, 4).toString() === 'RIFF' && img.subarray(8, 12).toString() === 'WEBP'
    && img.length > 1024, `地图插画 ${name} 是真实的 WebP`);
}
let source = readFileSync(join(rootDir, 'web', 'app.mjs'), 'utf8').replace(/^import[\s\S]*?;\r?\n/gm, '');
source = source.slice(0, source.indexOf('(async function boot()'));
source += `\nS = ${JSON.stringify(state)}; render = () => {}; globalThis.api = { viewMarket, viewBattle, viewMap, renderSidebar, call, manualSourceRows, setSub, setTab:next=>tab=next, setShopKind:next=>shopKind=next, getTab:()=>tab, getState: () => S, focusMap: (id, mode) => { mapFocus = id; mapModes[id] = mode; } };`;
vm.runInNewContext(source, context);
context.api.renderSidebar();
verify(element('#player-card').innerHTML.includes('<strong>16</strong><span>岁</span>'), '年龄显示单位而不粘连月份');
verify(element('#player-card').innerHTML.includes('player-female.webp') && element('#player-card').innerHTML.includes('遁速')
  && !element('#player-card').innerHTML.includes('<label>剑意</label>'), '角色侧栏使用性别立绘与遁速，移除旧永久剑意属性');
context.api.setTab('workshop');
context.api.setSub('workshop', 'brew');
const brew = context.api.viewMarket();
verify(brew.includes('丹方14'), '完整可炼丹方可访问，不截断为前九条');
verify(brew.includes('data-brew-batch'), '批量炼丹入口可访问');
context.api.setTab('market');
verify(!context.api.viewMarket().includes('功法13') && !context.api.viewMarket().includes('data-action="trade-stock"'), '坊市普通分类不附加书坊或突破物品长列表');
context.api.setShopKind('book');
verify(context.api.viewMarket().includes('功法13'), '完整书坊可访问，不截断为前六条');
context.api.setShopKind('stock');
verify(context.api.viewMarket().includes('data-action="trade-stock"') && !context.api.viewMarket().includes('功法13'), '突破物品集中在独立坊市分类');
context.api.setShopKind('herb');
const clippedPill = {pillCatalog:[{id:'5001',effect:'当前回复气血0点',baseEffect:'回复气血15点',catalogEffect:'回复气血20点',basePoison:3,poisonAfter:3,poisonDelta:3}]};
verify(renderPillEffect(clippedPill,'5001',{catalog:true}).includes('回复气血20点') && !renderPillEffect(clippedPill,'5001',{catalog:true}).includes('回复气血0点'), '丹药目录优先完整catalogEffect，不用健康度截断的当前回复');
verify(renderPillEffect(clippedPill,'5001').includes('当前回复气血0点'), '实际服用详情保留当前健康度对应的真实效果');
verify(['sword','seal','armor','bell','pearl'].every(shape=>marketGrade({gears:[]},{kind:'gear',id:shape+'-1'})===1), '旧接口未拥有的五种商城法器按稳定模型ID补齐一阶');
verify(marketGrade({gears:[]},{kind:'gear',id:'forge-4143-1'})===0&&marketGrade({gears:[]},{kind:'gear',id:'unknown-1'})===0, '商城品阶回退不误判自炼ID或未知器型');
verify(marketGrade({pillCatalog:[{id:'5001',grade:1}]},{kind:'potion',id:'5001'})===1&&marketGrade({gears:[]},{kind:'gear',id:'sword-1',grade:2})===2, '旧丹药接口使用权威目录，新grade字段优先于模型回退');
const ownedBooks = context.api.manualSourceRows([{ id: 'owned', name: '抄本', owned: true, canLearn: true, learnDays: 20, learnCost: 12 }], 'book-buy');
verify(ownedBooks.includes('data-action="learn"') && ownedBooks.includes('开始修习'), '购得抄本后显示修习入口');
context.api.getState().battle = { hp: 100, max: 100, shield: 0, enemy: '敌方', enemyHp: 100, enemyMax: 100, enemyShield: 0,
  playerPoison: 7, playerPoisonTurns: 2, poison: 3, burn: 4, hand: [], log: [], turn: 0, cooldowns: {} };
const combat = context.api.viewBattle();
verify(combat.includes('受毒：每回合 7 点 · 剩 2 回合') && combat.indexOf('中毒 3 层') > combat.indexOf('对手'), '斗法玩家受毒和敌方毒层分别显示在双方状态');
context.api.getState().battle = null;

const actionButton = { disabled: false, dataset: { action: 'advance-step', days: '1' } };
await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(requests.length === 1 && requests[0].path === '/api/advance' && requests[0].body.days === 1, '过一天按钮发送一天推进请求');
actionButton.dataset = { action: 'bond', id: 'lesson' };
await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(requests.at(-1).path === '/api/bond' && requests.at(-1).body.kind === 'lesson', '师尊授业发送规范 bond 动作');
actionButton.dataset = { action:'npc-learn',npc:'friend',kind:'spell',id:'one' };
const learningRequests = requests.length;
await handlers.click({ target:{ closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(requests.length === learningRequests + 1 && requests.at(-1).path === '/api/npc-learn'
  && requests.at(-1).body.npc === 'friend' && requests.at(-1).body.kind === 'spell', 'NPC请教只发送一次准确知识请求');
actionButton.dataset = { action:'location-service',id:'market' };
const previousFetch = context.fetch;
context.fetch = async (path,options) => { requests.push({path,body:JSON.parse(options.body)}); return {ok:true,json:async()=>({ok:true,targetTab:'market'})}; };
await handlers.click({ target:{ closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(context.api.getTab() === 'market' && requests.at(-1).path === '/api/location-service', '地点坊市去处跳转真实坊市界面');
context.fetch = previousFetch;

context.api.setSub('map', 'atlas');
verify(context.api.viewMap().includes('data-map-detail="wuling"'), '实际 map 页首次打开即可显示当前城池');
context.api.focusMap('tianxing', 'walk');
actionButton.dataset = { action: 'travel', id: 'tianxing', begin: '1' };
await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(requests.at(-1).path === '/api/travel' && requests.at(-1).body.to === 'tianxing'
  && requests.at(-1).body.mode === 'walk' && requests.at(-1).body.begin === true, '启程按钮发送选中交通方式并要求仅开工');
actionButton.dataset.begin = '0';
await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(requests.at(-1).body.begin === false, '直接赶往按钮要求真实推进行程');
context.api.getState().job = travelState.job;
for (const [action, days] of [['travel-step', 1], ['travel-resume', 2]]) {
  actionButton.dataset = { action };
  await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
  verify(requests.at(-1).path === '/api/advance' && requests.at(-1).body.days === days, `${action} 仅推进所需天数且不再发送付费赶路请求`);
}
context.api.getState().job = null;
const beforeArtwork = requests.length;
actionButton.dataset = { action: 'location-art', id: 'wuling' };
await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(element('#location-art-modal').open && element('#location-art-modal').innerHTML.includes('assets/map/locations/wuling.webp')
  && requests.length === beforeArtwork, '地点大图入口展示本地点图片且不发送推进请求');
actionButton.dataset = { action: 'close-location-art' };
await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(!element('#location-art-modal').open && requests.length === beforeArtwork, '关闭地点大图保持游戏状态不变');

const targetBattle = { hp: 100, max: 100, shield: 0, enemy: '敌方', enemyHp: 100, enemyMax: 100, enemyShield: 0,
  mana: 4, maxMana: 3, nextMana: 6, manaCap: 9, hand: [{ uid: 'purge-1', id: 'purge', name: '涤念', kind: 'exhaust', cost: 1, requiresTarget: true }],
  log: [], turn: 0, cooldowns: {}, draw: 4, discard: 2, exhausted: 2, exhaustedSpells: [{ id: 'removed', name: '旧神通', copies: 2 }],
  removalOptions: [{ id: 'purge', name: '涤念', copies: 2 }, { id: 'strike', name: '普通攻击', dao: '体', copies: 5 }] };
context.api.getState().battle = targetBattle;
context.api.getState().arts.spellList = [{ id: 'swordFocus', name: '剑意激发' }];
targetBattle.cooldowns = { swordFocus: 3 };
const targetView = context.api.viewBattle();
verify(targetView.includes('剑意激发（3）') && !targetView.includes('swordFocus（'), '打出的神通不在手牌时，冷却仍显示中文名称');
verify(targetView.includes('<span class="card-energy">1</span>'), '手牌包含实际灵力数字，供卡面费用圆标显示');
const uiCSS = readFileSync(join(rootDir, 'web', 'enhance.css'), 'utf8');
verify(/\.combat-hand \.card-energy, \.sc-top \.card-energy\s*\{[^}]*position:\s*static/.test(uiCSS), '费用圆标定位在卡面流中，避免使用页面绝对坐标');
verify(targetView.includes('本场移除 2 张') && targetView.includes('旧神通 ×2') && targetView.includes('弃牌堆 2'), '本场移除清单与弃牌堆分开显示');
verify(targetView.includes('下回合灵力恢复到 <b>6</b> 点') && targetView.includes('上限 9'), '下一回合灵力设为权威总值，文案区分恢复到与增量回复');
const clickAction = async (action, id) => {
  actionButton.dataset = { action, id };
  await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
};
const beforeTarget = requests.length;
await clickAction('play', 'purge-1');
verify(element('#battle-target-modal').open && requests.length === beforeTarget
  && !element('#battle-target-modal').innerHTML.includes('data-id="purge"'), '移除牌先打开目标弹窗且不发请求，不能移除自身');
await clickAction('removal-select', 'strike');
verify(requests.length === beforeTarget && element('#battle-target-modal').innerHTML.includes('将移除「普通攻击」5 张'), '选择目标预览五张普通牌，不消耗卡牌');
await clickAction('removal-cancel');
verify(!element('#battle-target-modal').open && requests.length === beforeTarget, '取消目标选择不消耗回合、卡牌与灵力');
await clickAction('play', 'purge-1');
await clickAction('removal-select', 'strike');
await clickAction('removal-confirm');
await clickAction('removal-confirm');
verify(requests.length === beforeTarget + 1 && requests.at(-1).path === '/api/battle/turn'
  && requests.at(-1).body.id === 'purge-1' && requests.at(-1).body.target === 'strike', '确认移除只发送一次带定义 ID 目标的出牌请求');
context.api.getState().battle = null;

// 秘境地点情报、爬塔状态和洞府门槛均以真实 API 字段为准。
const endingState = {...state,ended:true,ending:{cause:'斗法陨落',canRestore:true},checkpoint:{enabled:true,days:390,source:'manual',slot:2}};
const accidentalEnding = renderEnding(endingState,{milestones:[]});
verify(accidentalEnding.includes('data-action="checkpoint-restore"') && accidentalEnding.includes('接受死亡并结算')
  && accidentalEnding.includes('手动存档位 2') && accidentalEnding.includes('第 2 年 2 月 1 日'), '意外陨落显示接受死亡或回档，以及真实保存日期与来源');
verify(!renderEnding({...endingState,ending:{cause:'寿元耗尽',canRestore:false}},{milestones:[]}).includes('checkpoint-restore'), '自然寿尽不提供回档入口');
verify(!renderEnding({...endingState,checkpoint:{enabled:false}},{milestones:[]}).includes('checkpoint-restore'), '没有可恢复存档时不提供回档入口');
const memberState = {...state,player:{...state.player,sect:'jinhong',location:'jinhong'},bondActions:[{id:'lesson',enabled:false,reason:'本月已受业'}]};
const mentorHTML = renderSectMentors({mentors:[{id:'elder',name:'同门长老',realmName:'筑基初期',relation:12,location:'jinhong',enabled:false,reason:'好感未达40'},
  {id:'master',name:'师尊',realmName:'金丹初期',relation:60,location:'tianxing',isMaster:true}]},memberState);
verify(mentorHTML.includes('好感未达40') && /data-id="master" disabled/.test(mentorHTML)
  && mentorHTML.includes('data-action="sect-mentor-detail" data-npc="elder"') && mentorHTML.includes('data-action="sect-locate" data-id="tianxing"'), '师门页完整显示未满足拜师条件、当面往来入口和远处师尊所在');
const sectJobsHTML = renderSectTasks(memberState,[{type:'task',id:'sect-patrol',name:'巡守山门',days:7,pay:25,contribution:12,dao:'金',daoExp:4,enabled:true},
  {type:'task',id:'city',name:'城市差事',enabled:true}],{sectTaskUsed:1,sectTaskLimit:3});
verify(sectJobsHTML.includes('本月已接 1 / 3') && sectJobsHTML.includes('贡献 +12') && sectJobsHTML.includes('金道经验 +4')
  && !sectJobsHTML.includes('城市差事') && sectJobsHTML.includes('data-action="eco-run" data-type="task" data-id="sect-patrol"'), '宗门任务板只展示宗门任务，明示真实报酬和共享额度');
const memberMap = renderAtlas(memberState,{focus:'jinhong'});
verify(memberMap.includes('进入宗门') && memberMap.includes('data-sub="mentors"') && memberMap.includes('data-sub="jobs"')
  && !renderAtlas(state,{focus:'jinhong'}).includes('data-action="sect-open"'), '地图抵达本宗后直达拜师、任务与藏经阁，远处宗门不能越过赶路');
const sectsHTML = renderSectPicker({sects:[{id:'jinhong',name:'金虹剑派',enabled:false,reason:'需抵达金虹剑派山门',trial:{name:'金剑试炼',desc:'非致命的金剑连击考核',cost:10,days:3,stageName:'炼气初期'}}]},state.player);
verify(sectsHTML.includes('金剑试炼') && sectsHTML.includes('10 灵石 · 3 天') && /data-action="join-sect"[^>]*disabled/.test(sectsHTML)
  && sectsHTML.includes('data-action="sect-locate" data-id="jinhong"'), '宗门卡片显示真实试炼费用与时间，异地不可报名且可在舆图定位山门');
const sectLibraryState = {...state,player:{...state.player,sect:'jinhong'},librarySpells:[
  {id:'gold',name:'金光连斩',dao:'金',sect:'jinhong',desc:'金系连击消耗锐意',stage:3,points:18,enabled:true},
  {id:'sword',name:'剑痕',dao:'剑',sect:'jinhong',desc:'叠加剑道印记',stage:0,points:8,enabled:false,known:true},
  {id:'flame',name:'烈焰',dao:'火',sect:'lihuo',stage:0,points:8,enabled:true},
]};
const sectLibraryHTML=renderSectSpellLibrary(sectLibraryState);
verify(sectLibraryHTML.includes('金光连斩') && sectLibraryHTML.includes('金系连击消耗锐意') && sectLibraryHTML.includes('筑基初期 · 18 贡献')
  && !sectLibraryHTML.includes('烈焰'), '藏经阁仅显示本宗神通与实际规则、所需境界、贡献价格');
verify(/data-id="sword" disabled/.test(sectLibraryHTML) && sectLibraryHTML.includes('洞府道法 → 神通搭配'), '已学神通不可重复购买，学习和装备流程明确');
const dungeonState = { ...state, player: {...state.player,cave:'wuling'}, cave: {
  tier:0,tierName:'寻常洞府',maxSupply:1,
  upgrade:{tier:1,name:'灵脉洞府',cost:120,minRenown:20,enabled:false,reason:'声望不足 20。',desc:'支持二档灵气配给。'},
  supplies:[{level:1,name:'初阶灵气',tierName:'寻常洞府',minRenown:0},
    {level:2,name:'灵脉引气',tierName:'灵脉洞府',minRenown:20,locked:true,reason:'需灵脉洞府'}],
}, dungeons:{earlyPeriod:3,latePeriod:50,nextSpawnYear:4,sites:[
  {id:'known',name:'碧泉洞天',type:'intel',location:'wuling',locationName:'武陵城',stage:0,realmName:'炼气初期',revealed:true,available:true,entryEnabled:true,expiresYear:4,targetName:'灵叶草',targetKind:'herb',targetGrade:3},
  {id:'hidden',name:'隐秘高阶灵矿',location:'tianxing',realmName:'元婴',revealed:false,available:true,entryEnabled:false},
  {id:'sea',name:'无尽海秘境',type:'sea',location:'sea',locationName:'无尽海',realmName:'筑基',revealed:true,available:false,entryEnabled:false,nextOpenYear:11,reason:'第 11 年开放'},
],intel:[
  {kind:'herb',id:'leaf',name:'灵叶草',desc:'三阶灵药',grade:3,stageName:'金丹初期',cost:150,enabled:true},
  {kind:'material',id:'ore',name:'寒晶',desc:'四阶矿晶',grade:4,stageName:'元婴初期',cost:240,enabled:false,reason:'境界不足'},
]}};
const siteDirectory = dungeons.renderDungeonDirectory(dungeonState);
verify(siteDirectory.includes('碧泉洞天') && siteDirectory.includes('灵叶草 ×10') && !siteDirectory.includes('隐秘高阶灵矿'), '秘境目录只显示已知位置，目标材料终点十份');
verify(siteDirectory.includes('第 11 年开放') && /data-id="sea" disabled/.test(siteDirectory), '周期秘境未开门时显示下次年份并禁用入口');
const tianjiState = {...dungeonState,player:{...dungeonState.player,location:'tianji'}};
const intelHTML = dungeons.renderDungeonIntel(tianjiState,{kind:'material',grade:'4'});
verify(intelHTML.includes('寒晶秘境') && intelHTML.includes('境界不足') && !intelHTML.includes('灵叶草秘境'), '天机阁情报可按炼器材料筛选且说明境界限制');
verify(dungeons.renderDungeonIntel(dungeonState).includes('需先抵达天机阁') && /data-action="dungeon-intel"[^>]*disabled/.test(dungeons.renderDungeonIntel(dungeonState)), '远程目录不可购买情报，显示前往天机阁入口');
verify(dungeons.renderDungeonIntel(tianjiState).includes('灵叶草秘境') && !dungeons.renderDungeonIntel(tianjiState).includes('寒晶秘境'), '天机阁情报默认只显示三阶');
verify(!dungeons.renderDungeonIntel(tianjiState,{kind:'herb',grade:'4',search:'寒晶'}).includes('寒晶秘境'), '情报品阶、类型与搜索同时过滤，不绕过类型条件');
const mapWithSite = renderAtlas(dungeonState);
verify(mapWithSite.includes('atlas-dungeon-pin') && mapWithSite.includes('data-location-dungeons') && !mapWithSite.includes('隐秘高阶灵矿'), '地图标出开放已知秘境，并在地点详情显示入口');
verify(caveUI.caveSupplyAccess(dungeonState,'qi',1).enabled && !caveUI.caveSupplyAccess(dungeonState,'qi',2).enabled
  && caveUI.caveSupplyAccess(dungeonState,'stone',3).enabled, '洞府限制灵气档位，普通灵石配给仍可配置');
verify(caveUI.renderCaveUpgrade(dungeonState).includes('声望达到 20') && /data-action="cave-upgrade" disabled/.test(caveUI.renderCaveUpgrade(dungeonState)), '洞府升级显示所需声望与费用，不足时禁用');
verify(caveUI.renderCaveSupplyLimits(dungeonState).includes('需灵脉洞府'), '灵气方案逐项解释洞府档位门槛');
const runState = {...dungeonState,dungeons:{...dungeonState.dungeons,run:{siteId:'known',siteName:'碧泉洞天',phase:'route',type:'intel',realmName:'炼气初期',health:64,current:'start',elapsedDays:2,cleared:0,targetName:'灵叶草',nodes:[
  {id:'start',type:'start',row:0,lane:1,name:'入境',visited:true,next:['fight','camp']},
  {id:'fight',type:'battle',row:1,lane:0,name:'林中斗法',available:true,next:['boss']},
  {id:'camp',type:'camp',row:1,lane:2,name:'清泉营地',available:true,next:['boss']},
  {id:'boss',type:'boss',row:2,lane:1,name:'守关者',available:false,next:[]},
],deck:[{uid:'strike-1',spell:'strike',name:'普通攻击',cost:1,dao:'体'}],relics:[{id:'heal',name:'碧泉玉',desc:'每场斗法后回复气血'}],lootItems:[{kind:'herb',id:'leaf',name:'灵叶草',count:2}],rewards:[]}}};
const routeHTML = dungeons.renderDungeons(runState);
verify(routeHTML.includes('dungeon-edge') && routeHTML.includes('left:16.666666666666668%') && routeHTML.includes('由下至上')
  && /data-id="boss"[^>]*disabled/.test(routeHTML), '爬塔分支连接显示真实路线，未相连守关者不可直达');
verify(routeHTML.includes('气血 <b>64</b>') && routeHTML.includes('碧泉玉') && routeHTML.includes('灵叶草 ×2'), '秘境显示当前气血、真实遗物与可读战利品');
verify(routeHTML.includes('提前退出后入口消失，无法再次进入') && routeHTML.includes('主动退出带回已得物资，战斗中逃离只带回一半'), '未通关离开入口附近明确提示入口消失与两种战利品规则');
runState.dungeons.run.phase = 'reward';
runState.dungeons.run.rewards = [{id:'choice-0',kind:'spell',name:'水箭',desc:'一张水道神通'},{id:'choice-1',kind:'relic',name:'护心玉',desc:'持续获得护盾'},{id:'choice-2',kind:'stones',name:'灵石',desc:'本次行囊加 12 灵石'}];
const rewardHTML = dungeons.renderDungeons(runState);
verify((rewardHTML.match(/class="dungeon-reward-card /g)||[]).length === 3 && rewardHTML.includes('data-action="dungeon-claim" data-id="skip"'), '三选一奖励与放弃入口均可操作');
verify(/data-id="fight"[^>]*disabled/.test(rewardHTML), '奖励未处理时无法选择下一房');
runState.dungeons.run.nodes.find(node=>node.id==='start').type='camp';
runState.dungeons.run.rewards=[{id:'one',kind:'remove',name:'遗忘·剑雨',desc:'移除这一张牌',spell:'rain-one'},
  {id:'two',kind:'remove',name:'遗忘·剑雨',desc:'移除这一张牌',spell:'rain-two'}];
verify((dungeons.renderDungeons(runState).match(/class="dungeon-reward-card remove/g)||[]).length===1
  && dungeons.renderDungeons(runState).includes('data-id="one"'), '营地同种神通副本合并展示，仍请求具体可删的一张');
runState.dungeons.run.phase = 'battle';
runState.battle = {id:'dummy'};
verify(dungeons.renderDungeons(runState,{battleHTML:'<div>既有卡牌战斗</div>'}).includes('既有卡牌战斗')
  && /data-action="dungeon-leave" disabled/.test(dungeons.renderDungeons(runState,{battleHTML:'battle'})), '秘境战斗复用原战斗界面，战斗中先处理逃离');
runState.dungeons.run.phase='complete'; runState.battle=null;
verify(dungeons.renderDungeons(runState).includes('秘境已通关') && dungeons.renderDungeons(runState).includes('结算战利品并离开'), '通关后给出明确结算战利品入口');

const bossRewards=[{id:'boss-0',kind:'learnSpell',name:'永久水箭',spell:'water',desc:'通关后学会'},
  {id:'boss-1',kind:'manual',name:'化雨诀抄本',manual:'rainform',desc:'带回后修习'},
  {id:'boss-2',kind:'stones',name:'灵石遗藏',amount:730,desc:'大量灵石'}];
runState.dungeons.run.bossRewards=bossRewards;
runState.dungeons.run.rewards=bossRewards;
runState.dungeons.run.cleared=true;
runState.dungeons.run.phase='reward';
const bossHTML=dungeons.renderDungeons(runState);
verify(bossHTML.includes('通关主奖励 · 三选一') && bossHTML.includes('永久神通') && bossHTML.includes('功法抄本')
  && /data-action="dungeon-leave" disabled/.test(bossHTML) && bossHTML.includes('领取灵石奖励'), 'Boss先领取永久三选一，离开入口禁用，跳过按灵石领取');
const naturalSite={...dungeonState.dungeons.sites[0],targetName:'',targetId:'',targetKind:'',bossRewards};
const naturalDirectory=dungeons.renderDungeonDirectory({...dungeonState,dungeons:{...dungeonState.dungeons,sites:[naturalSite]}});
verify(naturalDirectory.includes('永久水箭') && naturalDirectory.includes('化雨诀抄本') && naturalDirectory.includes('730 灵石')
  && !naturalDirectory.includes('终点保底'), '自然秘境展示固定永久技能、抄本与实际灵石主奖励，材料不充当主标题');
runState.dungeons.run.phase='complete'; runState.dungeons.run.bossRewardClaimed=true;
runState.dungeons.run.bossRewardTaken={id:'boss-0',kind:'stones',name:'重复知识折算',amount:292};
verify(dungeons.renderDungeons(runState).includes('重复知识折算 · 292 灵石'), '通关显示引擎实际所得，重复技能折算结果不误报为学会');

const ability={id:'artifact:bell-one',gearId:'bell-one',name:'聚灵钟·催动',dao:'气',kind:'mana',description:'恢复2灵力',cost:0,cooldown:2,stageName:'炼气初期'};
const artifactHTML=artifacts.renderBattleArtifacts({artifacts:[{...ability,playable:true,energy:2,damage:100,text:'伤害100；本次新增灼烧28'},
  {...ability,id:'artifact:bell-two',name:'冷却钟·催动',playable:false,cooldownRemaining:2,reason:'法宝冷却尚余2回合'}]});
verify(artifactHTML.includes('无需抽牌，不占额外神通槽') && artifactHTML.includes('data-action="artifact-play" data-id="artifact:bell-one"')
  && /data-id="artifact:bell-two" disabled/.test(artifactHTML) && artifactHTML.includes('法宝冷却尚余2回合'), '法宝独立栏显示灵力、实际效果和冷却原因，使用能力ID而非手牌UID');
verify(artifactHTML.includes('伤害 100') && artifactHTML.includes('本次新增灼烧28'), '法宝同时显示伤害摘要与完整预览，数值大于零也不吞掉新增状态说明');
verify(artifacts.renderEquippedArtifacts([{name:'聚灵钟',equipped:true,ability},{name:'未装备钟',equipped:false,ability}])
  .includes('装备法宝神通 <b>1</b>') && artifacts.renderArtifactAbility(ability).includes('需炼气初期'), '角色卡只列实际装备法宝能力并显示境界门槛');
verify(artifacts.renderForgePreview({info:{valid:true,enabled:false,reason:'主料不足4份',shapeName:'钟',grade:2,chance:.7,mainName:'寒铁',mainElement:'金',mainPolarity:'地阴',days:12,cost:30,ability}})
  .includes('主料不足4份') && artifacts.renderForgePreview({info:{valid:false,reason:'未选器型'}}).includes('未选器型'), '炼器预览显示真实配方条件，合法欠材料仍可查看能力');
const forgeDefs={materials:[{id:'iron',name:'锻剑铁',owned:10,grade:1,element:'剑',polarity:'天阳'}],shapes:[{id:'sword',name:'剑',slot:'weapon'}]};
verify(/data-action="forge-run" disabled/.test(renderEconomy(state,[],forgeDefs).forge)
  && !/data-action="forge-run" disabled/.test(renderEconomy(state,[],forgeDefs,{}, {info:{valid:true,enabled:true}}).forge), '初次与更换配方待推演时禁开炉，只有真实预览确认条件满足才启用');
const forgeSelection={main:'iron',aux:'jade',core:'wood',shape:'sword'};
const missingForgeDefs={...forgeDefs,materials:[{id:'iron',name:'锻剑铁',owned:1},{id:'jade',name:'岩玉',owned:0},{id:'wood',name:'桃木',owned:2}]};
const missingForge=renderEconomy({...state,player:{...state.player,stones:10}},[],missingForgeDefs,forgeSelection,{info:{valid:true,enabled:false,cost:33,reason:'炼器材料不足：iron需4份'}}).forge;
verify(missingForge.includes('所需') && missingForge.includes('已有') && missingForge.includes('缺少')
  && missingForge.includes('锻剑铁缺3份') && missingForge.includes('岩玉缺3份') && missingForge.includes('桃木缺1份')
  && missingForge.includes('灵石缺23枚'), '旧版后端预览也显示每项材料和灵石的全部缺口');
const repeatedForge=forgeRequirements(state,[{id:'iron',name:'锻剑铁',owned:4}],{main:'iron',aux:'iron',core:'iron'},{info:{valid:true,enabled:false,cost:33}});
verify(repeatedForge.rows.length===1 && repeatedForge.rows[0].required===10 && repeatedForge.rows[0].missing===6,
  '重复配料合并计数，不把同四份材料重复当作三个角色都够用');
const liveRequirements=[{id:'iron',name:'锻剑铁',roles:['main','aux','core'],required:10,owned:10,missing:0}];
const liveForge=renderEconomy(state,[],missingForgeDefs,{main:'iron',aux:'iron',core:'iron'},
  {info:{valid:true,enabled:true,cost:33,materials:liveRequirements,stonesOwned:100,blockers:[]}}).forge;
verify(liveForge.includes('主料、辅料、灵核') && liveForge.includes('材料与灵石已备齐') && !/data-action="forge-run" disabled/.test(liveForge),
  '配方实时预览库存优先于选项缓存，足量时显示可见开炉按钮及备齐提示');
verify(forgeRequirements(state,missingForgeDefs.materials,forgeSelection,
  {info:{valid:true,enabled:false,cost:33,reason:'境界不足以炼制此品阶法器。'}}).reason.includes('境界不足'),
  '材料不足不能掩盖境界等额外开炉门槛');
const renownSources=[{id:'wildTask',name:'野外悬赏',gainText:'每胜 +2',conditions:['气血至少50'],limit:'本月已接 3 / 3',enabled:false,reason:'本月已满三次',entry:{tab:'quests',sub:'wild',location:'fanghu',label:'查看野外悬赏'}}];
const renownHTML=renderRenownSources(renownSources);
verify(renownHTML.includes('每胜 +2') && renownHTML.includes('本月已接 3 / 3') && renownHTML.includes('本月已满三次')
  && /data-action="renown-source-open" data-id="wildTask"/.test(renownHTML) && !renownHTML.includes('disabled'), '声望来源显示实际数量、条件、限制与原因，不满足条件仍可查看入口');
context.api.getState().renownInfo={sources:renownSources};
actionButton.dataset={action:'renown-source-open',id:'wildTask'};
const renownRequests=requests.length;
await handlers.click({target:{closest(sel){return sel==='[data-action]'?actionButton:null;}}});
verify(requests.length===renownRequests && context.api.getTab()==='map', '声望入口只定位所需地点，不自动报名或消耗时间资源');
context.api.getState().battle={artifacts:[{...ability,playable:false}]};
actionButton.dataset={action:'artifact-play',id:ability.id};
await handlers.click({target:{closest(sel){return sel==='[data-action]'?actionButton:null;}}});
verify(requests.length===renownRequests, '不可用法宝的过期点击按当前状态拒绝，不发送无效施展请求');
context.api.getState().battle.artifacts[0].playable=true;

for (const [action, path, data] of [
  ['dungeon-enter','dungeon/enter',{id:'known'}],['dungeon-choose','dungeon/choose',{id:'fight'}],
  ['dungeon-claim','dungeon/claim',{id:'choice-1'}],['dungeon-leave','dungeon/leave',{}],
  ['dungeon-intel','dungeon/intel',{kind:'material',id:'ore'}],['cave-upgrade','cave/upgrade',{}],
  ['checkpoint-restore','checkpoint/restore',{}],
  ['library-spell','library/spell',{id:'gold'}],
  ['artifact-play','battle/turn',{id:'artifact:bell-one'}],
]) {
  actionButton.dataset = {action,...data};
  const count = requests.length;
  await handlers.click({target:{closest(sel){return sel==='[data-action]'?actionButton:null;}}});
  verify(requests.length===count+1 && requests.at(-1).path==='/api/'+path && JSON.stringify(requests.at(-1).body)===JSON.stringify(data), `${action} 只发送一次规范动作请求`);
}

context.fetch = async () => ({ ok: false, json: async () => ({ ok: false, error: '存档失败' }) });
actionButton.dataset = { action: 'save' };
await handlers.click({ target: { closest(sel) { return sel === '[data-action]' ? actionButton : null; } } });
verify(element('#toast').textContent === '存档失败', '保存失败时保留失败原因，不显示已保存');

let resolveFetch;
let fetchCount = 0;
context.fetch = () => { fetchCount++; return new Promise(resolve => { resolveFetch = resolve; }); };
const first = context.api.call('advance', { days: 1 });
await context.api.call('advance', { days: 1 });
resolveFetch({ ok: true, json: async () => ({ ok: true }) });
await first;
verify(fetchCount === 1, '请求未完成时连点不会重复推进');
clearTimeout(element('#toast')._t);
console.log(`✓ 前端回归检查 ${checks} 项通过`);
