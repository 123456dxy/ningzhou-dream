// Expanded player journeys, evaluated against the real v2 modules.
// Run: node tools/v2-journey-check.mjs
// Node built-ins only: no browser, game server, compiler, or save mutation.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { esc, fmt, action, uiButton, legacyLink } from '../web/v2/ui.mjs';
import { createStore } from '../web/v2/store.mjs';
import { healthLine, renderCharacterSummary, renderCharacter } from '../web/v2/views/character.mjs';
import { renderHome } from '../web/v2/views/home.mjs';
import { filterLocations, renderMap, renderMapPicker } from '../web/v2/views/map.mjs';
import { renderAdventures, renderTide, renderStories, renderStory } from '../web/v2/views/adventures.mjs';
import { endingSettled, renderEnding } from '../web/v2/views/ending.mjs';
import { mutationBlocker, serviceDestination } from '../web/v2/eligibility.mjs';
import { makeOutcome, renderOutcome, outcomeWorkspace, outcomeForWorkspace } from '../web/v2/outcomes.mjs';
import { detailsKeys, captureDisclosures, restoreDisclosures } from '../web/v2/disclosures.mjs';
import {renderSocial} from '../web/v2/views/social.mjs';
import { renderBattle, renderBattleDetail } from '../web/v2/views/battle.mjs';
import { renderTianji, renderTianjiBattleDetail } from '../web/v2/views/tianji.mjs';

const tests = [], failures = [];
const test = (name, run) => tests.push({ name, run });
// Decode once, as HTML parsing does. Never re-decode user-supplied entity text.
const decode = value => value.replace(/&(amp|lt|gt|quot|#39);/g, (_, entity) => ({amp:'&',lt:'<',gt:'>',quot:'"','#39':"'"}[entity]));
const text = html => decode(html.replace(/<[^>]*>/g, ' ').replace(/\s+/g, ' ')).trim();
const attributes = source => Object.fromEntries([...source.matchAll(/([\w:-]+)(?:="([^"]*)")?/g)].map(([,key,value]) => [key, value === undefined ? true : decode(value)]));
const buttons = html => [...html.matchAll(/<button\b([^>]*)>([\s\S]*?)<\/button>/g)].map(([,attrs,body]) => ({...attributes(attrs),label:text(body)}));
const commands = (html, path) => buttons(html).filter(b => b['data-command'] && (!path || b['data-command'] === path)).map(b => ({path:b['data-command'],body:JSON.parse(b['data-body']),disabled:b.disabled === true,title:b.title,label:b.label}));
const controls = (html, id) => buttons(html).filter(b => b['data-ui'] === id);
const progress = html => [...html.matchAll(/<progress\b([^>]*)>/g)].map(([,attrs]) => attributes(attrs));
const selects = html => [...html.matchAll(/<select\b([^>]*)>([\s\S]*?)<\/select>/g)].map(([,attrs,content]) => ({...attributes(attrs),options:[...content.matchAll(/<option\b([^>]*)>([\s\S]*?)<\/option>/g)].map(([,attrs,label])=>({...attributes(attrs),label:text(label)}))}));
const noExecutableText = (html, payload) => { assert.ok(!html.includes(payload)); assert.ok(html.includes(esc(payload))); assert.doesNotMatch(html, /<script\b|<svg\b|<img\b[^>]*onerror\s*=/i); };
const hostile = '<svg onload="BAD()">&\'talent\'</svg>';
const base = () => ({
  player:{name:'验收道友',apt:37,insight:63,health:72,healthMax:100,healthUnit:'%',body:11,luck:29,sword:7,alchemy:13,age:31,lifespan:120,lifeBonus:10,escapeSpeed:18,renown:9,rank:'散修',sect:'',sectName:'散修',location:'city',locationName:'试验城',realmName:'筑基初期',xp:341,xpNeed:900,stones:723,
    talents:[{id:'talent-id',name:'所选天赋',desc:'服务端天赋说明',tag:'成长'}],
    combatReference:{reference:true,hp:648,hpMax:900,mana:6,manaCap:14,manaRegen:4,shield:12,swordQi:3,reduction:0.13,speed:22,context:'服务器开局参考，不含敌方行动。'},
    cultivation:{manual:101,apt:1.37,insight:1.63,realm:1.5,legacy:1.2,stone:1.1,qi:1.4,cave:1.25,talentMul:1.18,total:777.25,base:222.5,note:'只在洞府生效。'},
  },
  clock:{days:412,year:1,month:2,day:23,label:'第 2 年 2 月 23 日'},plan:'cultivate',
  plans:[{id:'cultivate',name:'修炼',desc:'依计划修炼',preview:'30 天：服务端预计修为 +39'}],
  breakthrough:{next:'筑基中期',chance:0.735,cost:199,ready:true,enabled:true,reason:'',days:90,failureLoss:0.175,pillsConsumed:1,coresConsumed:2,breakBuffConsumed:true},
  arts:{points:4,potions:{},herbs:{},materials:{},manualList:[],spells:[],spellList:[]},gears:[],logs:[{id:1,text:'原有纪事'}],bonds:{stipendBalance:0},
  poison:{current:12,cultPenalty:0.8,affecting:true,threshold:10,daysToClear:21},
});
const home = s => renderHome(s,{},{});

test('character summary uses authoritative apt, insight and health percentage', () => {
  const s=base();Object.assign(s.player,{aptitude:998,wisdom:997,hp:996,maxHp:995});
  const html=renderCharacterSummary(s),copy=text(html);
  assert.match(copy,/资质 37/);assert.match(copy,/悟性 63/);assert.match(copy,/健康度 72%/);
  assert.doesNotMatch(copy,/998|997|996|995|648|900/);assert.equal(controls(html,'character').length,1);
});
test('health meter is explicitly percentage, independent of combat HP maximum', () => {
  const html=healthLine(base().player),bar=progress(html)[0];
  assert.match(text(html),/健康度.*72%/);assert.equal(bar.value,'72');assert.equal(bar.max,'100');
});
test('growth view displays server attributes and escaped talent name/desc', () => {
  const s=base();s.player.talents=[{id:'do-not-display-id',name:hostile,desc:hostile,tag:hostile}];
  const html=renderCharacter(s);noExecutableText(html,hostile);assert.doesNotMatch(text(html),/do-not-display-id|\[object Object\]/);
  for(const expected of ['资质 37','悟性 63','体魄 11','气运 29','剑道 7','丹道 13'])assert.ok(text(html).includes(expected),expected);
});
test('ordinary combat reference HP comes from combatReference, never health/100', () => {
  const s=base();s.player.health=24;
  const html=renderCharacter(s,{characterTab:'combat'}),bar=progress(html)[0];
  assert.equal(bar.value,'648');assert.equal(bar.max,'900');assert.equal(bar['aria-label'],'参考气血');
  assert.match(text(html),/6 \/ 14/);assert.match(text(html),/自然回灵 4/);assert.match(text(html),/服务器开局参考/);
});
test('live combat HP supersedes the opening reference without a fixed 100 cap', () => {
  const s=base();s.battle={hp:513,max:812};const bar=progress(renderCharacter(s,{characterTab:'combat'}))[0];
  assert.equal(bar.value,'513');assert.equal(bar.max,'812');assert.equal(bar['aria-label'],'本场实际气血');
});
test('missing combat reference is disclosed rather than fabricated', () => {
  const s=base();delete s.player.combatReference;const html=renderCharacter(s,{characterTab:'combat'});
  assert.match(text(html),/尚未提供.*开局参考/);assert.ok(progress(html).every(b=>b['aria-label']==='健康度'));
});
test('cultivation total is a server theoretical value with cave multiplier and caveat', () => {
  const html=renderCharacter(base(),{characterTab:'cultivation'}),copy=text(html);
  assert.match(copy,/理论修炼速度 777\.25 \/ 年/);assert.match(copy,/配给前基础速度 222\.5 \/ 年/);
  assert.match(copy,/洞府气场 ×1\.25/);assert.match(copy,/理论速度由服务端给出/);assert.match(copy,/实际收益.*付得起配给/);
  assert.match(copy,/30 天：服务端预计修为 \+39/);assert.match(copy,/只在洞府生效/);
  assert.equal(controls(html,'navigate').find(b=>b['data-value']==='arts/cave')?.label,'管理功法与洞府');
});
test('cultivation breakthrough preparation summarizes the real string token without leaking it', () => {
  const s=base();s.arts.breakBuff='breakBuff:fixture';
  const copy=text(renderCharacter(s,{characterTab:'cultivation'}));
  assert.match(copy,/已有突破丹药准备/);assert.doesNotMatch(copy,/breakBuff:fixture|\[object Object\]/);
  s.arts.breakBuff='';assert.doesNotMatch(text(renderCharacter(s,{characterTab:'cultivation'})),/已有突破丹药准备/);
});
test('home foregrounds engine HP and XP, with health percent reserved for character details', () => {
  const html=home(base()),bars=progress(html);
  assert.deepEqual(bars.slice(0,2).map(b=>[b.value,b.max]),[['648','900'],['341','900']]);
  assert.equal(bars[0]['aria-label'],'气血 · 斗法开局参考');assert.equal(bars[1]['aria-label'],'修为 / 经验');
  assert.doesNotMatch(text(html),/健康度|72%/);assert.match(text(renderCharacter(base())),/健康度.*72%/);
  assert.match(text(html),/资质 37/);assert.match(text(html),/悟性 63/);
});
test('home uses live battle HP when supplied and does not fabricate a missing reference',()=>{
  const s=base();s.battle={hp:412,max:853};assert.deepEqual(progress(home(s)).slice(0,1).map(b=>[b.value,b.max]),[['412','853']]);
  delete s.battle;delete s.player.combatReference;const html=home(s);assert.match(text(html),/气血开局参考未返回/);assert.deepEqual(progress(html).map(b=>b['aria-label']),['修为 / 经验']);assert.doesNotMatch(text(html),/健康度|72%/);
});
test('native daily plan, advance controls and local outcome share the first panel in that order',()=>{
  const s=base();s.plans.push({id:'rest',name:'休养',preview:'休养预览'});const outcome={label:'推进岁月',text:'实际走过七天',changes:['历时 +7 天','修为 +19'],path:'advance'};
  const html=renderHome(s,{}, {},null,outcome),panel=html.slice(0,html.indexOf('</section>'));
  const select=selects(panel)[0];assert.equal(select.id,'daily-plan');assert.equal(select['data-ui'],'daily-plan');assert.equal(select['data-mutation'],'plan');
  assert.deepEqual(select.options.map(o=>[o.value,o.selected===true]),[['cultivate',true],['rest',false]]);
  assert.deepEqual(commands(panel,'advance').map(x=>x.body.days),[1,30,360,3600]);assert.equal(controls(panel,'plans').length,0);
  assert.ok(panel.indexOf('id="daily-plan"')<panel.indexOf('data-command="advance"'));assert.ok(panel.lastIndexOf('data-command="advance"')<panel.indexOf('data-ui="outcome-detail"'));assert.match(text(panel),/修为 \+19/);assert.match(text(panel),/历时 \+7 天/);
  assert.equal(controls(html,'outcome-detail').length,1);
});
test('ten-year action submits 3600 days and leaves authoritative interruption behavior enabled',()=>{
  const command=commands(home(base()),'advance').find(x=>x.body.days===3600);assert.ok(command);assert.equal(command.label,'过 10 年');assert.deepEqual(command.body,{days:3600});assert.match(text(home(base())),/遇到抉择或斗法自动暂停/);
  const css=readFileSync(new URL('../web/v2/styles.css',import.meta.url),'utf8');assert.match(css,/\.home-advance\{[^}]*repeat\(4,minmax\(0,1fr\)\)/);assert.match(css,/\.home-advance \.btn\{[^}]*min-height:46px/);
});
test('home secondary jobs, breakthrough, history and business are initially collapsed',()=>{
  const s=base();s.job={name:'修习',type:'learn',remaining:17,total:30};const html=home(s),secondary=html.slice(html.indexOf('class="home-secondary"'));
  const details=[...secondary.matchAll(/<details\b([^>]*)>/g)].map(([,attrs])=>attributes(attrs));
  assert.deepEqual(details.map(d=>d['data-disclosure-key']),['home-plan-detail','job-adjust','home-breakthrough','home-logs','home-business']);assert.ok(details.every(d=>!d.open));
  assert.equal(controls(html.slice(0,html.indexOf('</section>')),'cancel-job').length,0);
});
test('native daily plan is disabled for busy/reconciling and state blockers',()=>{
  for(const ui of [{busy:true},{reconciling:true}])assert.equal(selects(renderHome(base(),ui))[0].disabled,true);
  for(const patch of [{pending:{}},{battle:{hp:5,max:20}},{ended:true},{dungeons:{run:{}}},{tianji:{hasEncounter:true}}])assert.equal(selects(home({...base(),...patch}))[0].disabled,true);
  assert.equal(selects(home({...base(),job:{name:'修习',remaining:17,total:30}}))[0].disabled,undefined);
});
for(const remaining of [1,7,17,89,361])test(`ongoing home job offers exact ${remaining} remaining days without 30/360 plan shortcuts`, () => {
  const s=base();s.job={name:'正在修习',type:'learn',remaining,total:400,elapsed:400-remaining};const html=home(s),advance=commands(html,'advance');
  assert.deepEqual(advance.map(x=>x.body.days),[remaining,1]);assert.match(text(html),/完成本安排后即停/);
  assert.equal(controls(html,'plans').length,0);assert.equal(controls(html,'cancel-job').length,1);
});
test('idle home alone offers normal 1/30/360 day plan advancement', () => {
  assert.deepEqual(commands(home(base()),'advance').map(x=>x.body.days),[1,30,360,3600]);
});
test('already-settled social time cannot be cancelled from home', () => {
  const s=base();s.job={type:'socialElapsed',name:'往来耗时',remaining:3,total:7};const html=home(s);
  assert.equal(controls(html,'cancel-job').length,0);assert.match(text(html),/往来已结算/);
});
test('breakthrough readiness alone does not override server-disabled eligibility', () => {
  const s=base();Object.assign(s.breakthrough,{ready:true,enabled:false,reason:'伤势过重 <休养>'});
  const html=home(s),button=commands(html,'breakthrough')[0];assert.equal(button.disabled,true);assert.equal(button.title,'伤势过重 <休养>');assert.ok(html.includes('伤势过重 &lt;休养&gt;'));
});
test('breakthrough displays exact days, failure loss percentage and start consumption', () => {
  const html=home(base()),copy=text(html);assert.equal(commands(html,'breakthrough')[0].disabled,false);
  for(const value of ['73.5%','199 灵石','90 天','失败损失 17.5% 修为','凝元丹 × 1','灵核 × 2','已有突破丹药准备'])assert.ok(copy.includes(value),value);
});
test('missing breakthrough enabled contract fails closed and explains why', () => {
  const s=base();delete s.breakthrough.enabled;const html=home(s);assert.equal(commands(html,'breakthrough')[0].disabled,true);assert.match(text(html),/未提供完整突破资格/);
});

for(const [target,body,expected] of [
  ['market',{},['inventory','market']],['sect',{},['social','sects']],['society',{},['social','sects']],
  ['dungeon',{id:'intel'},['map','intel']],['dungeon',{id:'other'},['map','dungeons']],
  ['city',{},['inventory','jobs']],['economy',{},['inventory','jobs']],['unknown',{},null],
])test(`service destination resolves ${target}/${body.id||''}`,()=>assert.deepEqual(serviceDestination(target,body),expected));

const mapState = () => ({...base(),locations:[
  {id:'city',name:'试验城',desc:'平安交易',type:'city',danger:0,services:[{id:'market',name:'坊市',desc:'交易',days:0,cost:0,enabled:true},{id:'insight',name:'研读',days:3,cost:8,enabled:false,reason:'本月已研读'}]},
  {id:'sect',name:'星河宗',desc:'Mountain STREAM',type:'sect',danger:0},
  {id:'wild',name:'险峻山野',desc:'mountain stream',type:'wild',danger:28,travel:{options:[{id:'walk',name:'步行',days:13,cost:0,enabled:true},{id:'boat',name:'灵舟',days:4,cost:39,enabled:false,reason:'灵石不足'}]}},
  {id:'wild-safe',name:'平安山野',desc:'山林',type:'wild',danger:0},
]});
test('map search matches names and descriptions case-insensitively', () => {
  assert.deepEqual(filterLocations(mapState(),{mapSearch:'StReAm'}).map(x=>x.id),['sect','wild']);
  assert.deepEqual(filterLocations(mapState(),{mapSearch:'试验'}).map(x=>x.id),['city']);
});
test('map type, risk and text filters combine rather than replace each other', () => {
  assert.deepEqual(filterLocations(mapState(),{mapSearch:'山野',mapFilter:'wild',mapRisk:'danger'}).map(x=>x.id),['wild']);
  assert.deepEqual(filterLocations(mapState(),{mapFilter:'wild',mapRisk:'safe'}).map(x=>x.id),['wild-safe']);
  assert.equal(filterLocations(mapState(),{mapFilter:'all',mapRisk:'all'}).length,4);
});
test('mobile picker includes live search, all type choices, risk and matching results', () => {
  const html=renderMapPicker(mapState(),{mapSearch:'山野',mapFilter:'wild',mapRisk:'danger'});
  assert.match(html,/type="search" data-input="map-search"/);assert.match(html,/select data-ui="map-risk"/);
  assert.deepEqual(controls(html,'map-filter-tab').map(b=>b['data-value']),['all','city','sect','wild']);
  assert.deepEqual(controls(html,'map-focus').map(b=>b['data-value']),['wild']);assert.match(text(html),/找到 1 \/ 4 处/);
  assert.match(text(html),/不代表旅行绝无风险/);assert.match(text(html),/危险 28/);
});
test('mobile map has an honest empty state and escapes the search value', () => {
  const html=renderMapPicker(mapState(),{mapSearch:hostile});noExecutableText(html,hostile);
  assert.equal(controls(html,'map-focus').length,0);assert.match(text(html),/没有符合筛选的地点/);
});
test('map main view exposes one selected current-location service with eligibility', () => {
  const html=renderMap(mapState(),{});assert.equal(controls(html,'map-picker').length,1);
  assert.deepEqual(commands(html,'location-service').map(x=>[x.body.id,x.disabled]),[['market',false]]);
  assert.deepEqual(selects(html).find(s=>s['data-ui']==='location-service-select').options.map(o=>o.value),['market','insight']);
  const unavailable=renderMap(mapState(),{locationService:'insight'});assert.deepEqual(commands(unavailable,'location-service').map(x=>[x.body.id,x.disabled]),[['insight',true]]);assert.match(text(unavailable),/本月已研读/);
});
test('map travel submits only selected destination/mode and disables an unavailable selected route', () => {
  const html=renderMap(mapState(),{mapFocus:'wild'}),travel=commands(html,'travel');
  assert.deepEqual(travel.map(x=>x.body),[{to:'wild',mode:'walk',begin:true}]);assert.deepEqual(travel.map(x=>x.disabled),[false]);assert.match(text(html),/13 天 · 0 灵石/);
  const boat=commands(renderMap(mapState(),{mapFocus:'wild',travelMode:'boat'}),'travel');assert.deepEqual(boat.map(x=>x.body),[{to:'wild',mode:'boat',begin:true}]);assert.equal(boat[0].disabled,true);
});
test('an existing map journey replaces departure in place with exact continuation', () => {
  const s=mapState();s.job={type:'travel',name:'灵舟',remaining:7,total:17,elapsed:10,cost:39,to:'wild',mode:'boat'};
  const html=renderMap(s,{mapFocus:'city'});assert.equal(commands(html,'travel').length,0);assert.match(text(html),/剩 7 天/);
  assert.deepEqual(commands(html,'advance').map(x=>x.body.days),[7]);assert.equal(selects(html).find(x=>x['data-ui']==='map-focus').disabled,true);assert.equal(selects(html).find(x=>x['data-ui']==='travel-mode').disabled,true);
});

const storyState = () => ({...base(),stories:[{id:'story"&id',name:'旧剑归途',desc:'短简介',opening:'完整开篇',location:'试验城',days:7,available:true,done:false,opt1Name:'归还旧剑',opt1Effect:'声望 +3',opt1Enabled:false,opt1Reason:'需先结交剑修',opt2Name:'留下旧剑',opt2Effect:'获得旧剑',opt2Enabled:true}]});
test('story sheet uses real opening and exact story ID with zero-based choice payloads', () => {
  const s=storyState(),html=renderStory(s,s.stories[0].id),choices=commands(html,'story');
  assert.match(text(html),/完整开篇/);assert.doesNotMatch(text(html),/短简介/);
  assert.deepEqual(choices.map(x=>x.body),[{id:'story"&id',choice:0},{id:'story"&id',choice:1}]);
  assert.deepEqual(choices.map(x=>x.disabled),[true,false]);assert.match(text(html),/需先结交剑修/);
});
test('an unavailable story disables both choices even with individually enabled flags', () => {
  const s=storyState();Object.assign(s.stories[0],{available:false,opt1Enabled:true});assert.ok(commands(renderStory(s,s.stories[0].id),'story').every(x=>x.disabled));
});
test('story list offers only available unfinished stories', () => {
  const s=storyState();s.stories.push({...s.stories[0],id:'done',done:true},{...s.stories[0],id:'locked',available:false});
  assert.deepEqual(controls(renderStories(s),'story').map(x=>x['data-value']),[s.stories[0].id]);
});
test('missing story does not create choices or invent content', () => {
  const html=renderStory(storyState(),'missing');assert.equal(commands(html).length,0);assert.match(text(html),/已不在当前列表/);
});
test('story names, opening, effects and reasons escape server text', () => {
  const s=storyState();Object.assign(s.stories[0],{opening:hostile,opt1Name:hostile,opt1Effect:hostile,opt1Reason:hostile});noExecutableText(renderStory(s,s.stories[0].id),hostile);
});
const activeTide = () => ({...base(),tide:{active:true,status:'妖潮正在逼近',monthsLeft:9,dangerBonus:16,wavesFought:3,wavesWon:2,npcDeaths:4,fallen:['故人']}});
test('active tide uses true fight/hunker endpoints without a fabricated day advance', () => {
  const html=renderTide(activeTide()),actions=commands(html);
  assert.deepEqual(actions.map(x=>x.path),['tide/fight','tide/hunker']);assert.ok(actions.every(x=>!x.disabled));assert.ok(actions.every(x=>Object.keys(x.body).length===0));
  assert.match(text(html),/固守只记录你的选择，不会自动推进时间/);assert.match(text(html),/9 个月/);
});
test('inactive tide exposes no fight or hunker action', () => {
  assert.equal(commands(renderTide({...base(),tide:{active:false,status:'天下暂安'}})).length,0);
});
for(const [name,patch] of [['job',{job:{name:'闭关',remaining:11}}],['battle',{battle:{}}],['pending',{pending:{}}],['dungeon',{dungeons:{run:{}}}],['Tianji',{tianji:{hasEncounter:true}}]])test(`tide choices are disabled during ${name}`,()=>{
  assert.ok(commands(renderTide({...activeTide(),...patch})).every(x=>x.disabled));
});
test('wild fee is a reward, never an invented fee to pay', () => {
  const s={...base(),wildTasks:[{id:'beast',name:'猎妖',days:8,fee:275,enabled:true}]};const html=renderAdventures(s),copy=text(html);
  assert.match(copy,/胜出赏金 275 灵石/);assert.match(copy,/费用：当前服务未提供报价/);assert.doesNotMatch(copy,/费用 275|消耗 275/);
  assert.equal(commands(html,'wild').length,0);assert.equal(controls(html,'wild-confirm').length,0);assert.match(copy,/入场费用未报价，暂不可在新版接取/);assert.match(html,/href="index.html"/);
});
test('wild explicit cost and unavailable reason remain distinct from reward', () => {
  const s={...base(),wildTasks:[{id:'beast',name:'猎妖',days:8,cost:17,fee:275,enabled:false,reason:'地点不符'}]};const html=renderAdventures(s);
  assert.match(text(html),/费用 17 灵石 · 胜出赏金 275 灵石/);assert.match(text(html),/地点不符/);
  assert.equal(commands(html,'wild').length,0);assert.equal(controls(html,'wild-confirm').length,0);assert.ok(buttons(html).some(x=>x.disabled&&x.label==='当前不可接取'));
});
test('available wild task with an explicit quote submits the exact task ID',()=>{
  const s={...base(),wildTasks:[{id:'beast',name:'猎妖',days:8,cost:17,fee:275,enabled:true}]};
  const html=renderAdventures(s);assert.deepEqual(commands(html,'wild').map(x=>x.body),[{id:'beast'}]);assert.equal(controls(html,'wild-confirm').length,0);
});
test('expedition requires actual server quote instead of invented default numbers', () => {
  const unknown=renderAdventures(base());assert.equal(commands(unknown,'expedition').length,0);assert.match(text(unknown),/未提供探访资格与费用/);
  const quoted=renderAdventures({...base(),adventure:{expedition:{days:13,cost:47,enabled:false,reason:'先拜入宗门'}}});assert.match(text(quoted),/13 天 · 47 灵石/);assert.equal(commands(quoted,'expedition')[0].disabled,true);
});

test('no-game mutations are blocked, but creation and restore/import routes remain available', () => {
  assert.match(mutationBlocker(null,'advance'),/建立人物/);for(const path of ['new','load','import'])assert.equal(mutationBlocker(null,path),'');
});
test('Tianji session blocks even replacement/save routes but preserves its own endpoints', () => {
  const s=base();for(const path of ['new','load','save','advance','legacy/settle'])assert.match(mutationBlocker(s,path,{tianjiActive:true}),/天机/);
  assert.equal(mutationBlocker(s,'tianji/action',{tianjiActive:true}),'');assert.match(mutationBlocker({...s,tianji:{hasEncounter:true}},'advance'),/天机/);
});
test('persistence and recovery exceptions survive death, pending choice and dungeon', () => {
  const s={...base(),ended:true,pending:{},dungeons:{run:{}}};for(const path of ['save','legacy/settle','checkpoint/restore','new','load','import'])assert.equal(mutationBlocker(s,path),'');
});
test('dungeon guard wins and permits only dungeon actions or battle turn', () => {
  const s={...base(),dungeons:{run:{}}};assert.match(mutationBlocker(s,'advance'),/秘境/);assert.equal(mutationBlocker(s,'dungeon/choose'),'');assert.equal(mutationBlocker(s,'battle/turn'),'');
});
test('battle and pending choice guards preserve only valid progress routes', () => {
  assert.match(mutationBlocker({...base(),battle:{}},'choose'),/斗法/);assert.equal(mutationBlocker({...base(),battle:{}},'battle/turn'),'');
  assert.match(mutationBlocker({...base(),pending:{}},'advance'),/抉择/);assert.equal(mutationBlocker({...base(),pending:{}},'choose'),'');
});
test('job guard discloses exact remaining days and preserves documented exceptions', () => {
  const s={...base(),job:{name:'研习',remaining:17}};assert.match(mutationBlocker(s,'buy'),/研习.*17 天/);
  for(const path of ['advance','cancel-job','plan','choose','battle/turn'])assert.equal(mutationBlocker(s,path),'');
});

const ended = () => ({...base(),ended:true,ending:{cause:'寿终',age:120,realm:'金丹',score:345,friends:5,settled:false,canRestore:true},checkpoint:{days:737,source:'manual',slot:2}});
test('ending does not confuse lifetime earned with this-life settlement', () => {
  const s=ended(),data={legacy:{earned:83,budget:19,milestones:[{label:'新成就',points:7,reached:true,claimed:false},{label:'旧成就',points:9,reached:true,claimed:true}]}};
  assert.equal(endingSettled(s,data),false);const html=renderEnding(s,data),copy=text(html);
  assert.match(copy,/跨世累计：83 传承点/);assert.match(copy,/本次实际新增点数以结算回执为准/);assert.match(copy,/新成就/);assert.doesNotMatch(copy,/旧成就|本次新增 83/);
  assert.equal(commands(html,'legacy/settle').length,1);assert.equal(controls(html,'new-life').length,0);assert.equal(controls(html,'open-build')[0].label,'确认放弃未结算收获');
});
test('ending receipt uses awarded points separately from lifetime earned and build budget', () => {
  const html=renderEnding(ended(),{legacy:{earned:999,budget:999}},{points:7,earned:90,budget:21,reasons:['本世成就 +7']}),copy=text(html);
  assert.match(copy,/本次新增 7 传承点/);assert.match(copy,/累计传承 90 点 · 下一世建号预算 21 点/);assert.doesNotMatch(copy,/999/);
  assert.equal(commands(html,'legacy/settle').length,0);assert.equal(commands(html,'checkpoint/restore').length,0);assert.equal(controls(html,'new-life').length,1);
});
test('zero-point receipt still counts as settled and does not re-offer settlement', () => {
  const receipt={points:0,earned:83,budget:19};assert.equal(endingSettled(ended(),{},receipt),true);assert.equal(commands(renderEnding(ended(),{},receipt),'legacy/settle').length,0);
});
test('persisted settled flag survives refresh without the in-memory receipt', () => {
  for(const [s,data] of [[{...ended(),ending:{...ended().ending,settled:true}},{}],[ended(),{legacy:{ending:{settled:true},earned:83,budget:19}}]]){
    assert.equal(endingSettled(s,data),true);const html=renderEnding(s,data);assert.equal(commands(html,'legacy/settle').length,0);assert.equal(commands(html,'checkpoint/restore').length,0);assert.equal(controls(html,'new-life').length,1);
  }
});
test('unsettled death offers the actual manual checkpoint with calendar date', () => {
  const html=renderEnding(ended());assert.equal(commands(html,'checkpoint/restore').length,1);assert.match(text(html),/手动存档位 2/);assert.match(text(html),/第 3 年 1 月 18 日/);
});

// Execute the actual onUI controller, with only the collaborators required by
// these paths replaced. This catches menu bypasses that render-only tests miss.
function uiHarness(state,data={},settlement=null,options={}) {
  const source=readFileSync(new URL('../web/v2/app.mjs',import.meta.url),'utf8');
  const start=source.indexOf('async function onUI('),end=source.indexOf('\ndocument.addEventListener(',start);
  assert.ok(start>=0&&end>start,'onUI extraction boundary must track controller refactors');
  const store=createStore();store.patch({state,data,settlement});store.ui({reconciling:!!options.reconciling});const shown=[],calls=[],confirmations=[],requests=[],scrolls=[],focuses=[],notices=[],errors=[],frames=[];
  const win={scrollX:options.scrollX??11,scrollY:options.scrollY??257,scrollTo:position=>scrolls.push({...position})};
  const planSelect={focus:options=>focuses.push({...options})};
  const api={busy:!!options.busy,mutate:async(path,body,opts)=>{requests.push({path,body:JSON.parse(JSON.stringify(body)),opts:JSON.parse(JSON.stringify(opts))});if(options.fail)throw options.fail;return options.result||{state:{...store.get().state,plan:body.plan},text:'计划已调整'};}};
  const sheet={dataset:{kind:''}},context=vm.createContext({store,sheet,endingSettled,action,uiButton,esc,legacyLink,renderMapPicker,renderStory,
    api,mutationBlocker,makeOutcome,outcomeWorkspace,serviceDestination,account:()=> 'test',loadSerial:0,toast:(...args)=>notices.push(args),handleError:async e=>errors.push(e),requestAnimationFrame:fn=>frames.push(fn),
    showSheet:(...args)=>{shown.push(args);sheet.dataset.kind=args[2];},openBuild:async()=>calls.push('build'),closeSheet:()=>{sheet.dataset.kind='';calls.push('close');},
    confirmAction:(...args)=>confirmations.push(args),ensureViewData:async()=>calls.push('load'),router:{go:(...args)=>calls.push(['route',...args])},document:{querySelector:selector=>selector==='#daily-plan'?planSelect:null},window:win,
  });
  const mutateStart=source.indexOf('async function mutate('),mutateEnd=source.indexOf('\nasync function loadData(',mutateStart);assert.ok(mutateStart>=0&&mutateEnd>mutateStart);
  vm.runInContext(source.slice(mutateStart,mutateEnd)+'\n'+source.slice(start,end)+'\nthis.onUI=onUI;this.mutate=mutate;',context);
  return {store,shown,calls,confirmations,requests,scrolls,focuses,notices,errors,sheet,onUI:context.onUI,mutate:context.mutate,flushFrames:()=>{for(const fn of frames.splice(0))fn();}};
}
test('native daily-plan executes the actual controller and mutate path with exact plan-only payload',async()=>{
  const s=base();s.job={name:'研习',type:'learn',remaining:17,total:30};const h=uiHarness(s);await h.onUI('daily-plan','rest',{});
  assert.deepEqual(h.requests,[{path:'plan',body:{plan:'rest'},opts:{quiet:true,account:'test'}}]);assert.equal(h.store.get().state.plan,'rest');assert.deepEqual(h.store.get().state.job,s.job);assert.equal(h.store.get().state.clock.days,s.clock.days);
  h.flushFrames();assert.ok(h.scrolls.length>0);assert.ok(h.scrolls.every(p=>p.left===11&&p.top===257&&p.behavior==='instant'));assert.deepEqual(h.focuses,[{preventScroll:true}]);assert.equal(h.errors.length,0);assert.equal(h.notices.length,0);assert.equal(h.shown.length,0);assert.equal(h.confirmations.length,0);
});
test('advance restores daily-loop scroll immediately and after render without extra advancement',async()=>{
  const before=base(),after=structuredClone(before);after.clock.days+=7;const h=uiHarness(before,{},null,{result:{state:after,report:{daysPassed:7}}});await h.mutate('advance',{days:7});h.flushFrames();
  assert.deepEqual(h.requests.map(r=>[r.path,r.body]),[['advance',{days:7}]]);assert.equal(h.store.get().state.clock.days,before.clock.days+7);assert.ok(h.scrolls.length>=2);assert.ok(h.scrolls.every(p=>p.top===257));
});
test('daily-loop scroll restoration does not override an interrupted battle or event',async()=>{
  for(const patch of [{battle:{}},{pending:{}}]){const h=uiHarness(base(),{},null,{result:{state:{...base(),...patch}}});await h.mutate('advance',{days:30});h.flushFrames();assert.deepEqual(h.scrolls,[]);}
});
test('native daily-plan is a no-op when choosing the already selected plan',async()=>{
  const h=uiHarness(base());await h.onUI('daily-plan','cultivate',{});assert.deepEqual(h.requests,[]);assert.deepEqual(h.calls,[]);assert.deepEqual(h.scrolls,[]);assert.deepEqual(h.focuses,[]);
});
for(const mode of ['busy','reconciling'])test(`actual daily-plan mutation refuses ${mode} even if a change event is delivered`,async()=>{
  const h=uiHarness(base(),{},null,{[mode]:true});await h.onUI('daily-plan','rest',{});assert.deepEqual(h.requests,[]);assert.equal(h.store.get().state.plan,'cultivate');assert.deepEqual(h.scrolls,[]);assert.deepEqual(h.focuses,[]);
});
test('failed plan change preserves state and does not pretend to restore successful focus',async()=>{
  const h=uiHarness(base(),{},null,{fail:Object.assign(new Error('灵石不足'),{status:400})});await h.onUI('daily-plan','rest',{});
  assert.equal(h.requests.length,1);assert.equal(h.store.get().state.plan,'cultivate');assert.deepEqual(h.scrolls,[]);assert.deepEqual(h.focuses,[]);assert.equal(h.errors[0].message,'灵石不足');
});
test('actual busy callback disables and restores native mutation selects alongside commands',()=>{
  const source=readFileSync(new URL('../web/v2/app.mjs',import.meta.url),'utf8'),start=source.indexOf('const api=createApi('),end=source.indexOf('\nconst router=',start);assert.ok(start>=0&&end>start);
  const nodes=[{dataset:{},disabled:false},{dataset:{},disabled:true}],status={textContent:''};let onBusy;
  const context=vm.createContext({createApi:options=>{onBusy=options.onBusy;return {};},store:createStore(),document:{body:{setAttribute(){}},querySelector:()=>status,querySelectorAll:selector=>{assert.ok(selector.includes('select[data-mutation]'));return nodes;}}});vm.runInContext(source.slice(start,end),context);
  onBusy(true);assert.deepEqual(nodes.map(n=>n.disabled),[true,true]);assert.equal(status.textContent,'正在处理');onBusy(false);assert.deepEqual(nodes.map(n=>n.disabled),[false,true]);assert.ok(nodes.every(n=>n.dataset.wasDisabled===undefined));
});
test('unquoted wild and invite actions stay blocked with an explicit classic entry',async()=>{
  for(const id of ['wild-confirm','confirm-invite']){const h=uiHarness({...base(),wildTasks:[{id:'beast',name:'猎妖',days:8,fee:275,enabled:true}]});await h.onUI(id,'beast',{});assert.equal(h.confirmations.length,0);assert.equal(h.requests.length,0);assert.equal(h.shown[0][2],'missing-quote');assert.match(h.shown[0][1],/href="index.html"/);}
});
test('social selection stays on the same now-unavailable action instead of jumping to a gift',()=>{
  const s={...base(),nearby:[{id:'npc',name:'叶望生',realmName:'炼气',relation:37,credit:20,greeted:true}]};const actions=[{id:'chat',name:'闲谈',days:1,cost:0,enabled:true,desc:'闲谈'}, {id:'gift',name:'赠送灵药',days:1,cost:15,enabled:true}];
  const before=renderSocial(s,{social:'friends',friend:'npc'},{friendActions:{actions}});assert.equal(commands(before,'social')[0].body.action,'chat');actions[0]={...actions[0],enabled:false,reason:'本月已闲谈'};
  const after=renderSocial(s,{social:'friends',friend:'npc'},{friendActions:{actions}});assert.equal(commands(after,'social')[0].body.action,'chat');assert.equal(commands(after,'social')[0].disabled,true);assert.match(text(after),/本月已闲谈/);
});
test('social mutation persists selected action and retains display-only query snapshot while invalidating live eligibility',async()=>{
  const s=base(),h=uiHarness(s,{friendActions:{actions:[{id:'chat',enabled:true}]},friendActionsNpc:'npc'},null,{result:{state:{...s,logs:[{text:'闲谈完成'}]}}});h.store.ui({route:'social',social:'friends',friend:'npc'});await h.mutate('social',{npc:'npc',action:'chat'});
  assert.equal(h.store.get().ui.friendAction,'chat');assert.equal(h.store.get().data.friendActions,undefined);assert.equal(h.store.get().data.display.friendActions.actions[0].id,'chat');assert.equal(h.store.get().outcome.npcId,'npc');
});
test('menu new-life cannot bypass unsettled ending confirmation',async()=>{
  const h=uiHarness(ended(),{legacy:{earned:83}});await h.onUI('new-life','',{});
  assert.deepEqual(h.calls,[]);assert.equal(h.shown.length,1);assert.equal(h.shown[0][2],'new-confirm');assert.equal(commands(h.shown[0][1],'legacy/settle').length,1);
  assert.equal(controls(h.shown[0][1],'open-build')[0].label,'确认放弃收获，继续建号');
});
test('menu new-life also protects a living save; settled and no-game paths can open creation',async()=>{
  const living=uiHarness(base());await living.onUI('new-life','',{});assert.deepEqual(living.calls,[]);assert.match(text(living.shown[0][1]),/替换当前进度/);
  for(const h of [uiHarness(null),uiHarness(ended(),{},{points:0})]){await h.onUI('new-life','',{});assert.deepEqual(h.calls,['build']);}
});
test('map picker type/risk changes stay inside the sheet and preserve the search',async()=>{
  const h=uiHarness(mapState());h.store.ui({mapSearch:'山野'});await h.onUI('map-picker','',{});
  await h.onUI('map-filter-tab','wild',{});await h.onUI('map-risk','danger',{});
  assert.equal(h.store.get().ui.mapSearch,'山野');assert.equal(h.store.get().ui.mapFilter,'wild');assert.equal(h.store.get().ui.mapRisk,'danger');
  assert.deepEqual(controls(h.shown.at(-1)[1],'map-focus').map(b=>b['data-value']),['wild']);assert.equal(h.sheet.dataset.kind,'map-picker');
});

test('outcome reports actual signed authoritative deltas, not preview guesses',()=>{
  const before=base(),after=structuredClone(before);after.clock.days+=7;Object.assign(after.player,{stones:700,xp:352.5,health:68,renown:10,apt:39,insight:66});after.arts.points=6;
  const o=makeOutcome(before,{state:after,text:'实际结算'},'advance');
  assert.deepEqual(o.changes,['历时 +7 天','灵石 -23','修为 +11.5','健康度 -4 个百分点','声望 +1','资质 +2','悟性 +3','悟道点 +2']);assert.equal(o.text,'实际结算');
});
test('outcome includes consumed and acquired inventory, learned manual, equipment and spell changes',()=>{
  const before=base(),after=structuredClone(before);before.arts.potions={pill:2};after.arts.potions={pill:1};before.arts.herbs={灵草:3};after.arts.herbs={灵草:0};after.arts.materials={ore:2};
  after.pillCatalog=[{id:'pill',name:'养元丹'}];after.market=[{id:'ore',name:'玄铁'}];after.arts.manualList=[{id:'book',name:'长生诀',level:2}];after.gears=[{id:'sword',name:'青锋剑',equipped:true}];after.arts.spells=['first'];
  const o=makeOutcome(before,{state:after,relationDelta:-3},'gift');
  assert.deepEqual(o.changes,['养元丹 -1','灵草 -3','玄铁 +2','长生诀 第 2 层','当前装备：青锋剑','携带神通已更新，共 1 种','好感 -3']);
});
test('outcome excludes unchanged or unavailable values and does not invent a pre-load delta',()=>{
  assert.deepEqual(makeOutcome(base(),{state:base()},'save').changes,[]);assert.deepEqual(makeOutcome(null,{state:base()},'load').changes,[]);assert.equal(makeOutcome(base(),{ok:true},'save'),null);
  const after=base();delete after.player.stones;assert.ok(!makeOutcome(base(),{state:after},'save').changes.some(x=>x.startsWith('灵石')));
});
test('outcome selects new log only when it changed and preserves actual report',()=>{
  const before=base(),after=base();after.logs=[{id:2,text:'新的结果'}];const report={daysPassed:7,highlights:['途中见闻']};
  const o=makeOutcome(before,{state:after,report},'advance');assert.equal(o.text,'新的结果');assert.equal(o.report,report);
  assert.equal(makeOutcome(before,{state:before},'save').text,'已按服务器最新状态更新。');
});
test('completed brewing receipt follows authoritative elapsed time and job, not stale arranged text',()=>{
  const before=base(),after=base();after.clock.days=before.clock.days+4;after.logs=[{id:8,text:'本炉未能成丹。'}];
  const result=makeOutcome(before,{state:after,text:'已安排一炉，需四天。'},'craft');
  assert.equal(result.label,'炼丹');assert.match(result.text,/实际经过 4 天/);assert.match(result.text,/安排已结束/);assert.match(result.text,/本炉未能成丹/);assert.doesNotMatch(result.text,/已安排|炼制成功/);
  after.job={remaining:3,name:'炼丹'};const paused=makeOutcome(before,{state:after,text:'已安排'},'craft');assert.match(paused.text,/尚余 3 天/);assert.doesNotMatch(paused.text,/已结束/);
});
test('receipts stay in their submitted route and workspace, including workshop aliases',()=>{
  const origin={route:'inventory',inventoryTab:'brew'},receipt={workspace:outcomeWorkspace(origin),path:'craft',text:'炼丹结果'};
  assert.equal(outcomeForWorkspace(receipt,{route:'inventory',inventoryTab:'workshop',workshopTab:'brew'}),receipt);
  for(const ui of [{route:'social',social:'friends'},{route:'map'},{route:'home'},{route:'inventory',inventoryTab:'gear'},{route:'inventory',inventoryTab:'workshop',workshopTab:'forge'}])assert.equal(outcomeForWorkspace(receipt,ui),null);
  assert.equal(outcomeForWorkspace({text:'旧无归属回执'},{route:'social'}),null);
});
test('actual mutation records submission workspace even when a service may navigate',async()=>{
  const h=uiHarness(base());h.store.ui({route:'inventory',inventoryTab:'workshop',workshopTab:'brew'});await h.mutate('craft',{kind:'brew',id:'pill',batch:1});
  assert.equal(h.store.get().outcome.workspace,'inventory/workshop/brew');
});
test('outcome is accessible and collapsed with an action plus actual-delta summary',()=>{
  const o={label:hostile,text:hostile,changes:['甲','乙','丙','丁',hostile],report:{highlights:[hostile]}};const html=renderOutcome(o);noExecutableText(html,hostile);
  assert.match(html,/role="status"/);assert.match(html,/data-disclosure-key="outcome"/);assert.match(html,/^<details\b[^>]*>/);assert.doesNotMatch(html.match(/^<details\b[^>]*>/)[0],/\sopen(?:\s|>)/);
  assert.match(text(html.match(/<summary>([\s\S]*?)<\/summary>/)[1]),/甲 · 乙/);assert.equal(controls(html,'dismiss-outcome').length,1);
  assert.equal(renderOutcome(null),'');
});

// Minimal objects intentionally model only the DOM interface used by the
// disclosure module; no browser is loaded and no DOM implementation is copied.
function fakeDetails(specs) {
  const root={querySelectorAll:selector=>{assert.equal(selector,'details');return nodes;}};
  const nodes=specs.map(({key,summary='详情',heading='面板',open=false})=>{
    const parent={parentElement:root,querySelector:()=>heading?{textContent:heading}:null};
    return {dataset:key?{disclosureKey:key}:{},open,parentElement:parent,querySelector:selector=>{assert.equal(selector,':scope > summary');return {textContent:summary};}};
  });return {root,nodes};
}
test('disclosure explicit keys survive DOM replacement and changed numeric values',()=>{
  const a=fakeDetails([{key:'home-logs',summary:'纪事 12 则',open:true},{key:'job-adjust',open:false}]),memory=new Map();captureDisclosures(a.root,'account|home',memory);
  const b=fakeDetails([{key:'home-logs',summary:'纪事 19 则'},{key:'job-adjust',open:true}]);restoreDisclosures(b.root,'account|home',memory);assert.deepEqual(b.nodes.map(n=>n.open),[true,false]);
});
test('fallback disclosure keys ignore changing counts and distinguish repeated panels',()=>{
  const a=fakeDetails([{summary:'战报 · 12 条',open:true},{summary:'战报 · 2 条',open:false}]),memory=new Map();captureDisclosures(a.root,'combat',memory);
  const b=fakeDetails([{summary:'战报 · 19 条'},{summary:'战报 · 5 条',open:true}]);restoreDisclosures(b.root,'combat',memory);assert.deepEqual(b.nodes.map(n=>n.open),[true,false]);assert.equal(new Set(detailsKeys(b.root,'combat').map(x=>x[1])).size,2);
});
test('disclosure memory is scoped by account/route and leaves unknown defaults alone',()=>{
  const a=fakeDetails([{key:'details',open:true}]),memory=new Map();captureDisclosures(a.root,'alice|home',memory);
  for(const scope of ['bob|home','alice|map']){const b=fakeDetails([{key:'details'}]);restoreDisclosures(b.root,scope,memory);assert.equal(b.nodes[0].open,false);}
  const unknown=fakeDetails([{key:'new',open:true}]);restoreDisclosures(unknown.root,'alice|home',memory);assert.equal(unknown.nodes[0].open,true);
});
test('fallback disclosures distinguish nearby section headings',()=>{
  const f=fakeDetails([{heading:'人物',summary:'详情'},{heading:'道法',summary:'详情'}]);const keys=detailsKeys(f.root,'home').map(x=>x[1]);assert.notEqual(keys[0],keys[1]);
});

const battleState=()=>({...base(),battle:{enemy:'试招道友',hp:98,max:120,enemyHp:180,enemyMax:250,mana:5,manaCap:14,maxMana:4,nextMana:3,shield:12,enemyShield:8,turn:2,lethal:true,intentName:'剑气斩',intentDescription:'当前实际意图',intentDamage:17,draw:8,discard:3,exhausted:2,played:1,log:[],swordQi:3,charge:2,water:4,soul:2,manaPenalty:1,lock:2,playerPoison:4,playerPoisonTurns:2,weakened:1,poison:3,poisonTurns:2,burn:5,burnTurns:3,
  hand:[{uid:'u1',name:'御剑诀',kind:'sword',cost:1,damage:31,playable:true,description:'试验规则'},{uid:'u2',name:'水灵盾',kind:'shield',cost:2,shield:28,playable:false,reason:'灵力不足',description:'试验规则'}],
  artifacts:[{id:'artifact:test',name:'飞剑神通',kind:'sword',cost:3,cooldown:2,cooldownRemaining:1,damage:25,energy:-3,qi:1,text:'当前试算伤害25。',description:'法宝说明',playable:false,reason:'法宝冷却中，还剩1个回合。'}],
}});
test('battle shows real per-actor HP maxima without using health percent or fixed 100',()=>{
  const bars=progress(renderBattle(battleState()));assert.deepEqual(bars.map(b=>[b.value,b.max]),[['98','120'],['180','250']]);
});
test('battle uses manaCap for capacity and nextMana for recovery, not maxMana',()=>{
  const copy=text(renderBattle(battleState()));assert.match(copy,/5 \/ 14 灵力/);assert.match(copy,/下回合回复 3/);assert.doesNotMatch(copy,/5 \/ 4 灵力/);
});
test('battle zero nextMana is respected instead of falling back to base recovery',()=>{
  const s=battleState();s.battle.nextMana=0;assert.match(text(renderBattle(s)),/下回合回复 0/);
});
test('battle exposes ten live status badges before hand without opening disclosures',()=>{
  const html=renderBattle(battleState()),match=html.match(/<div class="status-inline" aria-label="当前关键战况">([\s\S]*?)<\/div>/);assert.ok(match);
  assert.equal((match[1].match(/class="badge/g)||[]).length,10);const copy=text(match[1]);
  for(const expected of ['剑气 3','重势 2','水势 4','震荡 2','回灵 −1','锁灵 2 回','受毒 4/回','扰灵 1 层','敌毒 3 层','敌灼烧 5/回'])assert.ok(copy.includes(expected),expected);
  const before=html.slice(0,match.index);assert.equal((before.match(/<details\b/g)||[]).length,(before.match(/<\/details>/g)||[]).length);assert.ok(match.index<html.indexOf('spell-grid hand'));
});
test('battle does not show zero/absent live statuses or invent a mana capacity',()=>{
  const s=battleState();for(const key of ['swordQi','charge','water','soul','manaPenalty','lock','playerPoison','weakened','poison','burn'])s.battle[key]=0;delete s.battle.manaCap;
  const html=renderBattle(s);assert.doesNotMatch(html,/aria-label="当前关键战况"/);assert.match(text(html),/5 灵力/);assert.doesNotMatch(text(html),/5 \/ 4 灵力/);
});
test('battle artifact uses actual cost, signed energy and current cooldown eligibility',()=>{
  const html=renderBattleDetail(battleState(),'artifacts').content,copy=text(html);assert.match(copy,/飞剑神通 灵力 3/);assert.match(copy,/伤 25 · 势 1 · 灵力净变 -3/);assert.match(copy,/冷却 2 回合 · 剩余 1 回合/);assert.match(copy,/法宝冷却中/);
  assert.equal(commands(html,'battle/turn').find(x=>x.body.id==='artifact:test').disabled,true);
});
test('battle ready artifact stays enabled and hand uses UID, quoted result and server reason',()=>{
  const s=battleState();Object.assign(s.battle.artifacts[0],{playable:true,cooldownRemaining:0});const html=renderBattle(s)+renderBattleDetail(s,'artifacts').content,actions=commands(html,'battle/turn');
  assert.equal(actions.find(x=>x.body.id==='artifact:test').disabled,false);assert.equal(actions.find(x=>x.body.id==='u1').disabled,false);assert.equal(actions.find(x=>x.body.id==='u2').disabled,true);assert.match(text(html),/伤 31/);assert.match(text(html),/盾 28/);assert.match(text(html),/灵力不足/);
});
test('battle names, rules, artifact reasons and logs escape server content',()=>{
  const s=battleState();s.player.name=hostile;s.battle.enemy=hostile;s.battle.intentName=hostile;s.battle.intentDescription=hostile;s.battle.log=[hostile];Object.assign(s.battle.artifacts[0],{name:hostile,description:hostile,reason:hostile});
  noExecutableText(renderBattle(s),hostile);for(const key of ['artifacts','intent','log'])noExecutableText(renderBattleDetail(s,key).content,hostile);
});
test('ordinary combat main screen keeps every card, latest result and end turn without inline disclosures',()=>{
  const s=battleState();s.battle.log=['早期记录','最新一击：敌方气血 -31'];const html=renderBattle(s);
  assert.doesNotMatch(html,/<details\b/);assert.match(html,/class="combat-screen"/);assert.match(html,/class="spell-grid hand" tabindex="0"/);assert.match(text(html),/最新一击：敌方气血 -31/);assert.doesNotMatch(text(html),/早期记录/);
  assert.deepEqual(commands(html,'battle/turn').map(c=>c.body.id),['u1','u2','end']);assert.deepEqual(controls(html,'combat-detail').map(c=>c['data-value']),['more','intent','log','card:u1','card:u2','artifacts']);
});
test('ordinary combat detail returns exact UID action and escaped rules; stale cards return null',()=>{
  const s=battleState(),detail=renderBattleDetail(s,'card:u1');assert.equal(detail.title,'御剑诀');assert.match(text(detail.content),/试验规则/);assert.deepEqual(commands(detail.content,'battle/turn').map(c=>c.body),[{id:'u1'}]);
  assert.equal(renderBattleDetail(s,'card:missing'),null);assert.equal(renderBattleDetail({},'card:u1'),null);assert.equal(renderBattleDetail(s,'unknown'),null);
});
test('ordinary removal card opens target selection rather than submitting an incomplete command',()=>{
  const s=battleState();s.battle.hand.push({id:'exhaust',uid:'remove:1',kind:'exhaust',name:'忘机咒',cost:1,requiresTarget:true,playable:true});
  for(const html of [renderBattle(s),renderBattleDetail(s,'card:remove:1').content]){assert.ok(controls(html,'battle-target').some(b=>b['data-value']==='remove:1'));assert.ok(!commands(html,'battle/turn').some(c=>c.body.id==='remove:1'));}
});
test('ordinary battle exit stays in deliberate more sheet and full logs remain available',()=>{
  const s=battleState();s.battle.log=['第一击','最后一击'];assert.ok(!commands(renderBattle(s),'battle/turn').some(c=>c.body.id==='escape'));
  assert.deepEqual(commands(renderBattleDetail(s,'more').content,'battle/turn').map(c=>c.body),[{id:'escape'}]);assert.match(text(renderBattleDetail(s,'log').content),/第一击.*最后一击/);
});

const tianjiStatus=()=>({phase:'fighting',entry:{name:'冻结留影'},encounter:{id:'duel-7',revision:12,state:'active',log:['留影上一击','留影最新一击'],battle:battleState().battle}});
test('Tianji active screen uses same compact action loop with encounter-bound IDs',()=>{
  const status=tianjiStatus(),html=renderTianji(base(),{}, {tianji:status});assert.doesNotMatch(html,/<details\b/);assert.match(html,/class="combat-screen"/);assert.match(text(html),/留影最新一击/);assert.doesNotMatch(text(html),/留影上一击/);
  assert.deepEqual(commands(html,'tianji/action').map(c=>c.body),['u1','u2','end'].map(id=>({encounterId:'duel-7',expectedRevision:12,id})));assert.ok(!commands(html).some(c=>c.path==='battle/turn'));
});
test('Tianji card/artifact/forfeit sheets retain encounter and expected revision',()=>{
  const status=tianjiStatus();for(const [key,id] of [['card:u1','u1'],['artifacts','artifact:test'],['forfeit','forfeit']]){
    const detail=renderTianjiBattleDetail(status,{},key);assert.ok(detail);assert.deepEqual(commands(detail.content,'tianji/action')[0].body,{encounterId:'duel-7',expectedRevision:12,id});
  }
  assert.ok(!commands(renderTianji(base(),{}, {tianji:status}),'tianji/action').some(c=>c.body.id==='forfeit'));
});
test('Tianji removal sheet filters targets and submits UID plus target without stale assumptions',()=>{
  const status=tianjiStatus();status.encounter.battle.hand.push({id:'exhaust',uid:'remove:1',kind:'exhaust',name:'忘机咒',cost:2,requiresTarget:true,playable:true});status.encounter.battle.removalOptions=[{id:'exhaust',name:'自身',copies:1},{id:'sword',name:'御剑诀',copies:3},{id:'empty',name:'空',copies:0}];
  const detail=renderTianjiBattleDetail(status,{},'target:remove:1');assert.deepEqual(commands(detail.content,'tianji/action').map(c=>c.body),[{encounterId:'duel-7',expectedRevision:12,id:'remove:1',target:'sword'}]);assert.equal(renderTianjiBattleDetail(status,{},'target:u1'),null);assert.equal(renderTianjiBattleDetail(status,{},'target:missing'),null);
});
for(const ui of [{busy:true},{tianjiPending:true}])test(`Tianji ${Object.keys(ui)[0]} locks card, artifact and forfeit sheet mutations`,()=>{
  const status=tianjiStatus();status.encounter.battle.artifacts[0].playable=true;
  for(const key of ['card:u1','artifacts','forfeit'])assert.ok(commands(renderTianjiBattleDetail(status,ui,key).content,'tianji/action').every(c=>c.disabled));
  assert.ok(commands(renderTianji(base(),ui,{tianji:status}),'tianji/action').every(c=>c.disabled));
});
test('settled Tianji disables surviving detail actions and missing encounter cannot fabricate a sheet',()=>{
  const status=tianjiStatus();status.encounter.state='settled';assert.ok(commands(renderTianjiBattleDetail(status,{},'card:u1').content,'tianji/action').every(c=>c.disabled));assert.equal(renderTianjiBattleDetail({}, {},'more'),null);
});

let passed=0;
for(const {name,run} of tests){try{await run();passed++;console.log(`PASS ${name}`);}catch(error){failures.push({name,message:error.message});console.error(`FAIL ${name}\n${error.stack}`);}}
console.log(JSON.stringify({suite:'v2-journey-check',passed,failed:failures.length,browserUsed:false,serverUsed:false,failures},null,2));
if(failures.length)process.exitCode=1;
