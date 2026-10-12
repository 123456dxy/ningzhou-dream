// Behavioral contracts for the new frontend. No game server is contacted.
// node tools/v2-contract-check.mjs
// node tools/v2-contract-check.mjs --dom  (installed Playwright + Chromium)
// DOM mode renders isolated HTML documents; it never opens the running game.
import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { createApi, ApiError } from '../web/v2/api.mjs';
import { createStore } from '../web/v2/store.mjs';
import { createRouter, parseRoute } from '../web/v2/router.mjs';
import { esc, action, uiButton, card, numericPreview } from '../web/v2/ui.mjs';
import { renderBattle, renderDungeon } from '../web/v2/views/battle.mjs';
import { renderCreate } from '../web/v2/views/create.mjs';
import { renderHome } from '../web/v2/views/home.mjs';
import { renderMap } from '../web/v2/views/map.mjs';
import { renderArts } from '../web/v2/views/arts.mjs';
import { renderInventory } from '../web/v2/views/inventory.mjs';
import { renderSocial } from '../web/v2/views/social.mjs';
import { renderTianji } from '../web/v2/views/tianji.mjs';

const cases = [], failures = [];
let passed = 0;
const test = (name, fn) => cases.push({ name, fn });
const response = (result = { ok: true }, status = 200) => ({ ok: status >= 200 && status < 300, status, json: async () => structuredClone(result) });
const deferred = () => { let resolve, reject; const promise = new Promise((a,b) => { resolve = a; reject = b; }); return { promise, resolve, reject }; };
const storage = (initial = {}) => {
  const records = new Map(Object.entries(initial));
  return { getItem:key => records.get(key) ?? null, setItem:(key,value) => records.set(key,String(value)), records };
};
function harness(fetcher = async () => response(), saved = storage()) {
  const calls = [], transitions = [];
  const api = createApi({ storage:saved, timeout:1000, onBusy:value => transitions.push(value), fetcher:async (url,opts) => {
    calls.push({ url, opts, body:opts.body == null ? undefined : JSON.parse(opts.body) });
    return fetcher(url,opts);
  } });
  return { api, calls, transitions, storage:saved };
}

test('same-origin GET sends no JSON body and does not acquire mutation lock', async () => {
  const h=harness(); await h.api.read('state',{},'GET');
  assert.equal(h.calls[0].url,'/api/state'); assert.equal(h.calls[0].opts.method,'GET');
  assert.equal(h.calls[0].opts.credentials,'same-origin'); assert.equal(h.calls[0].opts.cache,'no-store');
  assert.equal(h.calls[0].body,undefined); assert.equal(h.api.busy,false); assert.deepEqual(h.transitions,[]);
});
test('queries with inputs preserve POST JSON field types', async () => {
  const h=harness(); await h.api.read('build-check',{traits:['one'],allocation:{body:2}});
  assert.deepEqual(h.calls[0].body,{traits:['one'],allocation:{body:2}});
  assert.equal(h.calls[0].opts.headers['Content-Type'],'application/json');
});
test('invalid build is returned for inline validation rather than thrown', async () => {
  const result={ok:true,valid:false,error:'天赋互斥',remain:-1};
  const h=harness(async()=>response(result)); assert.deepEqual(await h.api.read('build-check',{}),result);
});
test('JSON ok:false is a failure even when HTTP is 200', async () => {
  const h=harness(async()=>response({ok:false,error:'不可以'}));
  await assert.rejects(h.api.mutate('advance',{days:1}),e=>e instanceof ApiError && e.message==='不可以' && !e.uncertain);
  assert.equal(h.api.busy,false); assert.deepEqual(h.transitions,[true,false]);
});
test('malformed success JSON is uncertain and always releases busy', async () => {
  const h=harness(async()=>({ok:true,status:200,json:async()=>{throw new SyntaxError('bad json');}}));
  await assert.rejects(h.api.mutate('advance',{days:1}),e=>e instanceof ApiError && e.uncertain);
  assert.equal(h.api.busy,false); assert.equal(h.calls.length,1);
});
test('only one mutation can be in flight across ordinary and Tianji actions', async () => {
  const gate=deferred(),h=harness(async()=>{await gate.promise;return response();});
  const first=h.api.mutate('advance',{days:1}); assert.equal(h.api.busy,true);
  await assert.rejects(h.api.mutate('tianji/join',{}, {account:'a'}),e=>e.status===409);
  assert.equal(h.calls.length,1); gate.resolve(); await first;
  assert.equal(h.api.busy,false); assert.deepEqual(h.transitions,[true,false]);
});
test('read may finish during a pending mutation without releasing its lock', async () => {
  const gate=deferred(),h=harness(async url=>{if(url.endsWith('/advance'))await gate.promise;return response();});
  const first=h.api.mutate('advance',{days:1}); await h.api.read('state',{},'GET');
  assert.equal(h.api.busy,true); gate.resolve();await first;assert.equal(h.api.busy,false);
});
test('ordinary mutation is never automatically replayed on network failure', async () => {
  const h=harness(async()=>{throw new TypeError('offline');});
  await assert.rejects(h.api.mutate('new',{name:'test'}),e=>e.uncertain && e.status===0);
  assert.equal(h.calls.length,1);assert.equal(h.api.pendingFor('a'),null);assert.equal(h.api.busy,false);
});
test('ordinary authentication failure returns to controller without automatic login/replay', async () => {
  const h=harness(async()=>response({ok:false,error:'请登录'},401));
  await assert.rejects(h.api.mutate('new',{name:'test'}),e=>e.status===401 && !e.uncertain);
  assert.equal(h.calls.length,1);assert.equal(h.calls[0].url,'/api/new');
});
test('429 is a known failure; no automatic retry or retained Tianji command', async () => {
  const h=harness(async()=>response({ok:false,error:'稍后重试'},429));
  await assert.rejects(h.api.mutate('tianji/join',{}, {account:'a'}),e=>e.status===429);
  assert.equal(h.calls.length,1);assert.equal(h.api.pendingFor('a'),null);
});
test('timeout aborts transport and unlocks while preserving uncertain outcome', async () => {
  const api=createApi({storage:storage(),timeout:5,fetcher:async(_url,opts)=>new Promise((_resolve,reject)=>opts.signal.addEventListener('abort',()=>reject(Object.assign(new Error('timeout'),{name:'AbortError'})),{once:true}))});
  await assert.rejects(api.mutate('advance',{days:1}),e=>e.uncertain && e.status===0);assert.equal(api.busy,false);
});
for (const path of ['../state','https://evil.invalid/api/state','/state','state#fragment']) test(`unsafe path is rejected without a request: ${path}`,async()=>{
  const h=harness();await assert.rejects(h.api.read(path),ApiError);assert.equal(h.calls.length,0);
});
test('Tianji join is generated with a requestId and pending data excludes credentials',async()=>{
  const h=harness();await h.api.mutate('tianji/join',{}, {account:'a'});
  assert.match(h.calls[0].body.requestId,/^[A-Za-z0-9_.-]{1,64}$/);assert.deepEqual(Object.keys(h.calls[0].body),['requestId']);
  assert.equal(h.api.pendingFor('a'),null);assert.ok(!JSON.stringify([...h.storage.records]).includes('password'));
});
test('uncertain Tianji action retains exact command and locks only its account',async()=>{
  let fail=true;const h=harness(async()=>{if(fail)throw new Error('offline');return response();});
  const body={encounterId:'duel1',expectedRevision:2,id:'u1',target:'spell2'};
  await assert.rejects(h.api.mutate('tianji/action',body,{account:'a'}),e=>e.uncertain);
  const pending=structuredClone(h.api.pendingFor('a'));assert.equal(pending.account,'a');assert.equal(pending.path,'tianji/action');
  assert.deepEqual({...pending.body,requestId:undefined},{...body,requestId:undefined});
  await assert.rejects(h.api.mutate('advance',{days:1},{account:'a'}),e=>e.status===409);
  assert.equal(h.calls.length,1);fail=false;await h.api.mutate('advance',{days:1},{account:'b'});
  assert.deepEqual(h.api.pendingFor('a'),pending);assert.equal(h.api.pendingFor('b'),null);
});
test('Tianji retry reuses original path, body, revision and requestId exactly once',async()=>{
  let fail=true;const h=harness(async()=>{if(fail)throw new Error('offline');return response({ok:true,phase:'fighting'});});
  await assert.rejects(h.api.mutate('tianji/action',{encounterId:'duel1',expectedRevision:2,id:'u1'},{account:'a'}));
  const first=structuredClone(h.calls[0]);fail=false;
  await h.api.mutate('tianji/action',{id:'SHOULD_NOT_REPLACE'},{retry:true,account:'a'});
  assert.equal(h.calls[1].url,first.url);assert.deepEqual(h.calls[1].body,first.body);assert.equal(h.calls.length,2);assert.equal(h.api.pendingFor('a'),null);
});
test('pending Tianji command survives API recreation and cannot be retried by another account',async()=>{
  const saved=storage();const first=harness(async()=>{throw new Error('offline');},saved);
  await assert.rejects(first.api.mutate('tianji/challenge',{targetId:'shadow2'},{account:'a'}));
  const next=harness(undefined,saved);await assert.rejects(next.api.mutate('tianji/action',{}, {retry:true,account:'b'}),e=>e.status===409);
  assert.equal(next.calls.length,0);await next.api.mutate('tianji/action',{}, {retry:true,account:'a'});
  assert.deepEqual(next.calls[0].body,first.calls[0].body);assert.equal(next.calls[0].url,'/api/tianji/challenge');
});
test('Tianji 401 retains pending command for same-account recovery',async()=>{
  const h=harness(async()=>response({ok:false,error:'请登录'},401));
  await assert.rejects(h.api.mutate('tianji/action',{encounterId:'duel1',expectedRevision:0,id:'end'},{account:'a'}),e=>e.status===401);
  assert.ok(h.api.pendingFor('a'));assert.equal(h.api.busy,false);
});
test('Tianji 409 clears failed command so controller can accept synchronized revision',async()=>{
  const h=harness(async()=>response({ok:false,error:'状态已更新',code:'stale_revision'},409));
  await assert.rejects(h.api.mutate('tianji/action',{encounterId:'duel1',expectedRevision:0,id:'end'},{account:'a'}),e=>e.status===409 && e.result.code==='stale_revision');
  assert.equal(h.api.pendingFor('a'),null);assert.equal(h.api.busy,false);
});
test('Tianji 503 retains command for safe same-request retry',async()=>{
  const h=harness(async()=>response({ok:false,error:'写入失败',code:'storage'},503));
  await assert.rejects(h.api.mutate('tianji/join',{}, {account:'a'}),e=>e.uncertain && e.status===503);assert.ok(h.api.pendingFor('a'));
});
test('pending clear is account-specific',async()=>{
  const h=harness(async()=>{throw new Error('offline');});
  for(const account of ['a','b']) await assert.rejects(h.api.mutate('tianji/join',{}, {account}));
  h.api.clearPending('a');assert.equal(h.api.pendingFor('a'),null);assert.ok(h.api.pendingFor('b'));
});
test('unavailable sessionStorage does not break successful actions',async()=>{
  const h=harness(undefined,{getItem(){throw new Error('denied');},setItem(){throw new Error('full');}});
  await h.api.mutate('tianji/join',{}, {account:'a'});assert.equal(h.calls.length,1);assert.equal(h.api.pendingFor('a'),null);
});
test('malformed persisted JSON is ignored',async()=>{
  const h=harness(undefined,storage({'nz-v2-tianji-pending-v2':'{broken'}));await h.api.mutate('advance',{days:1});assert.equal(h.calls.length,1);
});
test('store accepts authoritative state by replacement, clearing completed battle/job/pending',()=>{
  const s=createStore();s.accept({state:{player:{name:'a'},battle:{turn:1},job:{remaining:1},pending:{title:'event'}}});
  s.data({spells:{old:true}});const rev=s.get().revision;s.accept({state:{player:{name:'a'}}});
  assert.equal(s.get().revision,rev+1);assert.equal(s.get().state.battle,undefined);assert.equal(s.get().state.job,undefined);assert.equal(s.get().state.pending,undefined);assert.deepEqual(s.get().data,{});
});
test('hasGame:false clears old game and cached resources',()=>{
  const s=createStore();s.accept({state:{player:{name:'a'}}});s.data({roster:[1]});s.accept({ok:true,hasGame:false});
  assert.equal(s.get().state,null);assert.deepEqual(s.get().data,{});
});
test('resource-only response does not replace current game or advance its revision',()=>{
  const s=createStore();s.accept({state:{player:{name:'a'}}});const before=s.get();s.accept({ok:true,options:[]});
  assert.equal(s.get().state,before.state);assert.equal(s.get().revision,before.revision);
});
test('a later response without a report clears the previous advance report',()=>{
  const s=createStore();const report={daysPassed:30,stopReason:'done',highlights:['old event']};
  s.accept({state:{player:{name:'a'}},report});assert.deepEqual(s.get().report,report);
  s.accept({state:{player:{name:'a'}},text:'已保存'});assert.equal(s.get().report,null);
});
test('no-game response clears stale report from a previous life',()=>{
  const s=createStore();s.accept({state:{player:{name:'a'}},report:{daysPassed:360}});s.accept({ok:true,hasGame:false});
  assert.equal(s.get().report,null);assert.equal(s.get().state,null);
});
test('store reset changes account, clears state/report/resources and increments generation',()=>{
  const s=createStore();s.accept({state:{player:{name:'a'}},report:{daysPassed:1}});s.data({roster:[1]});const rev=s.get().revision;s.reset('b');
  assert.equal(s.get().account,'b');assert.equal(s.get().state,null);assert.equal(s.get().report,null);assert.deepEqual(s.get().data,{});assert.equal(s.get().revision,rev+1);
});
test('account reset discards old new-life drafts and selected NPC UI',()=>{
  const s=createStore();s.ui({build:{name:'old account'},friend:'old-npc',mapFocus:'old-map',error:'old error'});s.reset({nickname:'b'});
  assert.equal(s.get().ui.build,undefined);assert.equal(s.get().ui.friend,undefined);assert.equal(s.get().ui.mapFocus,undefined);assert.equal(s.get().ui.error,undefined);assert.equal(s.get().ui.route,'home');
});
test('store subscription has deterministic notifications and disposal',()=>{
  const s=createStore(),seen=[];const off=s.subscribe(v=>seen.push(v));s.patch({account:'a'},false);assert.equal(seen.length,0);
  s.ui({route:'map'});assert.equal(seen.length,1);assert.equal(seen[0].ui.route,'map');assert.equal(seen[0].account,'a');off();s.data({a:1});assert.equal(seen.length,1);
});
test('router parses valid destinations and rejects unknown routes',()=>{
  assert.deepEqual(parseRoute('#/social/tianji'),{route:'social',section:'tianji'});assert.deepEqual(parseRoute('#map'),{route:'map',section:''});
  assert.deepEqual(parseRoute('#/unknown'),{route:'home',section:''});assert.deepEqual(parseRoute(''),{route:'home',section:''});
});
test('router navigation, browser events and disposal use one owned listener per event',()=>{
  const handlers=new Map(),seen=[],pushes=[];const win={location:{hash:'#/home'},addEventListener:(t,f)=>handlers.set(t,f),removeEventListener:(t,f)=>{if(handlers.get(t)===f)handlers.delete(t);},history:{pushState(_s,_t,url){pushes.push(url);win.location.hash=url;}}};
  const r=createRouter(value=>seen.push(value),win);r.start();r.go('social','tianji');assert.deepEqual(seen.at(-1),{route:'social',section:'tianji'});
  assert.deepEqual(pushes,['#/social/tianji']);r.go('social','tianji');assert.equal(pushes.length,1);
  win.location.hash='#/map';handlers.get('popstate')();assert.equal(seen.at(-1).route,'map');r.dispose();assert.equal(handlers.size,0);
});
test('all HTML helper text and action payload boundaries escape metacharacters',()=>{
  const hostile='<img src=x onerror="globalThis.BAD=1"> & \'quote\'';
  assert.equal(esc('&<>"\''),'&amp;&lt;&gt;&quot;&#39;');assert.equal(esc(null),'');
  const html=action(hostile,'gift',{npc:hostile,id:'" autofocus onfocus="BAD=1'},{title:hostile})+uiButton(hostile,'friend',hostile)+card(hostile,'safe')+numericPreview({text:hostile,reason:hostile});
  assert.ok(!html.includes('<img'));assert.ok(html.includes('&lt;img'));assert.ok(html.includes('&quot;'));assert.ok(!html.includes(' onfocus="BAD=1'));
});

// These controller functions are not public exports. Execute their actual source
// with injected collaborators, rather than copying their implementation or merely
// checking that a guard's spelling exists. No boot code or real DOM is evaluated.
function controllerHarness(reader) {
  const source=readFileSync(new URL('../web/v2/app.mjs',import.meta.url),'utf8');
  const extract=(name,next)=>{
    const start=source.indexOf(`async function ${name}(`),end=source.indexOf(`\nasync function ${next}(`,start);
    assert.ok(start>=0&&end>start,`controller test boundary for ${name} must track refactors`);
    return source.slice(start,end);
  };
  const store=createStore();store.reset({nickname:'a'});const errors=[],notices=[],calls=[];
  const context=vm.createContext({store,loading:new Map(),loadSerial:0,account:()=>store.get().account?.nickname||'',
    api:{read:async(...args)=>{calls.push(args);return reader(...args);},pendingFor:()=>null,clearPending:()=>{}},
    handleError:async e=>errors.push(e),toast:message=>notices.push(message),ensureViewData:async()=>{},
    latestRead:new Map(),
  });
  vm.runInContext(extract('refreshState','mutate')+'\n'+extract('loadData','ensureViewData')+'\nthis.controllers={refreshState,loadData};',context);
  return {store,errors,notices,calls,context,...context.controllers};
}
test('resource queries coalesce identical requests within one state generation',async()=>{
  const gate=deferred(),h=controllerHarness(()=>gate.promise);
  const a=h.loadData('spells','arts/spells'),b=h.loadData('spells','arts/spells');assert.equal(h.calls.length,1);
  gate.resolve({ok:true,spells:[]});await Promise.all([a,b]);assert.deepEqual(h.store.get().data.spells,{ok:true,spells:[]});
});
test('late successful resource from previous account is discarded',async()=>{
  const gate=deferred(),h=controllerHarness(()=>gate.promise),work=h.loadData('roster','roster');
  h.store.reset({nickname:'b'});gate.resolve({ok:true,roster:[{name:'old account'}]});await work;
  assert.equal(h.store.get().data.roster,undefined);assert.equal(h.errors.length,0);
});
test('late failed resource from previous account cannot poison current data',async()=>{
  const gate=deferred(),h=controllerHarness(()=>gate.promise),work=h.loadData('roster','roster');
  h.store.reset({nickname:'b'});gate.reject(new ApiError('old failure',503));await work;
  assert.equal(h.store.get().data.roster,undefined);assert.equal(h.notices.length,0);
});
test('late 401 from previous account cannot log out the current account',async()=>{
  const gate=deferred(),h=controllerHarness(()=>gate.promise),work=h.loadData('roster','roster');
  h.store.reset({nickname:'b'});gate.reject(new ApiError('old auth failure',401));await work;
  assert.equal(h.errors.length,0);assert.equal(h.store.get().account.nickname,'b');
});
test('newer forge preview wins when an older selection resolves afterward',async()=>{
  const gates={iron:deferred(),jade:deferred()},h=controllerHarness((_p,body)=>gates[body.main].promise);
  h.store.ui({forge:{main:'iron'}});const old=h.loadData('forgePreview','forge-preview',{main:'iron'},'POST',true);
  h.store.ui({forge:{main:'jade'}});const next=h.loadData('forgePreview','forge-preview',{main:'jade'},'POST',true);
  gates.jade.resolve({ok:true,info:{name:'latest jade'}});await next;
  gates.iron.resolve({ok:true,info:{name:'obsolete iron'}});await old;
  assert.equal(h.store.get().data.forgePreview.info.name,'latest jade');
});
test('newer state read wins over an older successful read',async()=>{
  const first=deferred(),second=deferred();let n=0;const h=controllerHarness(()=>++n===1?first.promise:second.promise);
  const old=h.refreshState(),next=h.refreshState();second.resolve({ok:true,state:{player:{name:'latest'}}});await next;
  first.resolve({ok:true,state:{player:{name:'obsolete'}}});await old;assert.equal(h.store.get().state.player.name,'latest');
});
test('older state read error is ignored once a newer request has completed',async()=>{
  const first=deferred(),second=deferred();let n=0;const h=controllerHarness(()=>++n===1?first.promise:second.promise);
  const old=h.refreshState(),next=h.refreshState();second.resolve({ok:true,state:{player:{name:'latest'}}});await next;
  first.reject(new ApiError('obsolete auth failure',401));await old;assert.equal(h.errors.length,0);assert.equal(h.store.get().state.player.name,'latest');
});

const hostile='<script>globalThis.XSS=1</script>';
const state={player:{name:hostile,location:'wuling',locationName:'武陵城',realmName:'炼气初期',stage:0,xp:5,xpNeed:100,health:100,age:16,lifespan:100,stones:100,escapeSpeed:10},clock:{year:0,label:'第 1 年'},arts:{points:1,nodes:[],mods:{},manuals:{},manualList:[],herbs:{},materials:{},potions:{},spells:[]},plans:[{id:'rest',name:'休养',desc:'休息',preview:'恢复气血'}],plan:'rest',logs:[{text:hostile,year:0}],locations:[{id:'wuling',name:'武陵城',type:'city',services:[]},{id:'sea',name:'无尽海',type:'wild',travel:{options:[{id:'walk',name:'步行',days:10,cost:0,enabled:false,reason:'无步行通路'},{id:'boat',name:'灵舟',days:3,cost:9,enabled:true}]}}],gears:[],furnaces:[],nearby:[],market:[],manualSources:[],dungeons:{sites:[],specials:[],intel:[],run:null},bonds:{stipendBalance:0}};
const battle={enemy:hostile,hp:50,max:100,enemyHp:-10,enemyMax:100,mana:1,maxMana:3,shield:0,enemyShield:0,turn:0,lethal:true,intentName:'攻击',intentDescription:'攻击一次',intentDamage:8,draw:5,log:[hostile,'原始战报敌方气血 -10'],hand:[{uid:'card-uid-1',id:'spell-definition-1',name:hostile,kind:'attack',cost:1,damage:0,playable:true,text:'实际伤害 0'},{uid:'target-uid',id:'remove',name:'澄心',kind:'exhaust',requiresTarget:true,cost:2,playable:false,reason:'灵力不足'}],artifacts:[{id:'artifact:gear1',name:'剑气',cost:1,damage:2,playable:true}],removalOptions:[{id:'spell-definition-1',name:'神通',copies:2}]};
const entry={id:'shadow1',name:'甲',nickname:hostile,realm:'炼气',eventId:1,registeredYear:50,score:0,wins:0,draws:0,losses:0,build:{stats:{hp:100},traits:[],manuals:[],spells:[],nodes:[],gear:[]}};
const active={ok:true,phase:'fighting',canJoin:false,entry,opponents:[],window:{year:50,open:true,eventId:1,startYear:50,endYear:59},encounter:{id:'duel1',revision:4,state:'active',target:{id:'shadow2',nickname:'乙'},battle,log:battle.log}};
test('unplayable removal card render cannot expose an enabled target action',()=>{
  const html=renderBattle({...state,battle});
  const button=html.match(/<button\b[^>]*data-ui="battle-target"[^>]*>/g)?.find(item=>item.includes('data-value="target-uid"'));
  assert.ok(!button || /\sdisabled(?:\s|>|=)/.test(button),'playable:false target card must not expose an enabled target button');
});
test('ordinary battle render clamps negative HP label while preserving raw battle log',()=>{
  const html=renderBattle({...state,battle});
  const actor=html.slice(html.indexOf('class="actor enemy"'),html.indexOf('class="combat-intent"'));
  assert.ok(!actor.includes('-10'),'negative overkill HP should be displayed as 0');
  assert.ok(html.includes('原始战报敌方气血 -10'));assert.equal(battle.enemyHp,-10);
});
const renders=[
  ['home',()=>renderHome(state,{}, {},null)],['map',()=>renderMap(state,{mapFocus:'sea'})],['ordinary battle',()=>renderBattle({...state,battle})],
  ['new life',()=>renderCreate({build:{name:hostile,gender:'女',traits:[],allocation:{}}},{buildOptions:{budget:10,talents:[],stats:[],innate:{}},buildCheck:{valid:true,remain:10}})],
  ['Tianji',()=>renderTianji(state,{}, {tianji:active})],
  ...['manuals','spells','daos','cave','builds'].map(tab=>['arts/'+tab,()=>renderArts(state,{artsTab:tab},{})]),
  ...['potions','herbs','items','gear','market','brew','forge','jobs'].map(tab=>['inventory/'+tab,()=>renderInventory(state,{inventoryTab:tab},{})]),
  ...['friends','sects','tianji'].map(tab=>['social/'+tab,()=>renderSocial(state,{social:tab},{})]),
];
for(const [name,render] of renders)test(`${name} view accepts minimal server collections and never emits unescaped script`,()=>{
  const html=render();assert.equal(typeof html,'string');assert.ok(html.length>0);assert.ok(!html.includes('<script>'));
});
test('completed dungeon with unclaimed boss reward exposes reward rather than an empty route',()=>{
  const html=renderDungeon({...state,dungeons:{run:{siteName:'秘境',phase:'reward',cleared:true,bossRewardClaimed:false,rewards:[{id:'reward1',name:'传承',enabled:true}],nodes:[],deck:[],relics:[],lootItems:[]}}});
  assert.ok(html.includes('传承'));assert.ok(html.includes('dungeon/claim'));assert.ok(!html.includes('略过并继续'));
});

for(const {name,fn} of cases) {
  try { await fn();passed++; }
  catch(error) { failures.push({name,message:error.message});console.error(`FAIL ${name}\n  ${error.stack}`); }
}

if(process.argv.includes('--dom')) {
  const domTest=async(name,fn)=>{try{await fn();passed++;}catch(error){failures.push({name,message:error.message});console.error(`FAIL ${name}\n  ${error.stack}`);}};
  let browser;
  try {
    const require=createRequire(import.meta.url);let playwright;
    try{playwright=require('playwright');}catch{playwright=createRequire(process.execPath)('playwright');}
    browser=await playwright.chromium.launch({executablePath:process.env.CHROMIUM_PATH||'/usr/bin/chromium',headless:true,args:['--no-sandbox']});
    const page=await browser.newPage();await page.route('**/*',route=>route.abort());
    const set=html=>page.setContent('<!doctype html><meta charset="utf-8"><main>'+html+'</main>');
    const commands=()=>page.locator('[data-command]').evaluateAll(nodes=>nodes.map(n=>({path:n.dataset.command,body:JSON.parse(n.dataset.body),disabled:n.disabled,label:n.textContent})));
    await domTest('real DOM escaping preserves action payload text without executing HTML',async()=>{
      await set(action(hostile,'gift',{npc:hostile}));assert.equal(await page.locator('script,img').count(),0);assert.equal(await page.evaluate(()=>globalThis.XSS),undefined);
      assert.deepEqual((await commands())[0].body,{npc:hostile});assert.equal(await page.locator('button').textContent(),hostile);
    });
    await domTest('ordinary battle submits unique card UID and distinct artifact identifier',async()=>{
      await set(renderBattle({...state,battle}));const all=await commands();assert.ok(all.some(x=>x.path==='battle/turn'&&x.body.id==='card-uid-1'));assert.ok(all.some(x=>x.body.id==='artifact:gear1'));
      assert.ok(!all.some(x=>x.body.id==='spell-definition-1'));assert.ok(all.some(x=>x.body.id==='end'));assert.ok(all.some(x=>x.body.id==='escape'));
    });
    await domTest('unplayable target card cannot open an actionable target selector',async()=>{
      await set(renderBattle({...state,battle}));const target=page.locator('[data-ui="battle-target"][data-value="target-uid"]');assert.ok(await target.count()===0 || await target.isDisabled());
    });
    await domTest('ordinary overkill HP clamps display to zero without changing raw log',async()=>{
      await set(renderBattle({...state,battle}));assert.ok(!(await page.locator('.actor.enemy').textContent()).includes('-10'));assert.equal(battle.enemyHp,-10);assert.ok((await page.locator('main').textContent()).includes('原始战报敌方气血 -10'));
    });
    await domTest('travel commands preserve destination/mode/begin and disabled sea walking',async()=>{
      await set(renderMap(state,{mapFocus:'sea'}));const all=(await commands()).filter(x=>x.path==='travel');assert.equal(all.length,2);
      assert.deepEqual(all.find(x=>x.body.mode==='boat').body,{to:'sea',mode:'boat',begin:true});assert.equal(all.find(x=>x.body.mode==='walk').disabled,true);assert.equal(all.find(x=>x.body.mode==='boat').disabled,false);
    });
    await domTest('invalid new-life build keeps actual submit disabled',async()=>{
      const ui={build:{name:'甲',traits:[],allocation:{}}},data={buildOptions:{budget:10,talents:[],stats:[]},buildCheck:{valid:false,error:'互斥'}};
      await set(renderCreate(ui,data));assert.equal(await page.locator('[data-ui="start-life"]').isDisabled(),true);assert.ok((await page.locator('main').textContent()).includes('互斥'));
    });
    await domTest('valid new-life build enables submit and cancel has no game command',async()=>{
      await set(renderCreate({build:{name:'甲',traits:[],allocation:{}}},{buildOptions:{budget:10,talents:[],stats:[]},buildCheck:{valid:true,remain:10}}));
      assert.equal(await page.locator('[data-ui="start-life"]').isDisabled(),false);assert.equal(await page.locator('[data-ui="cancel-build"]').getAttribute('data-command'),null);
    });
    await domTest('Tianji commands carry encounter revision and never ordinary battle path',async()=>{
      await set(renderTianji(state,{}, {tianji:active}));const all=await commands();assert.ok(!all.some(x=>x.path==='battle/turn'));
      const turns=all.filter(x=>x.path==='tianji/action');assert.ok(turns.length>=2);assert.ok(turns.every(x=>x.body.encounterId==='duel1'&&x.body.expectedRevision===4));
    });
    await domTest('Tianji unresolved request disables all action commands and offers retry',async()=>{
      await set(renderTianji(state,{tianjiPending:true},{tianji:active}));const all=(await commands()).filter(x=>x.path.startsWith('tianji/'));assert.ok(all.every(x=>x.disabled));assert.equal(await page.locator('[data-ui="retry-tianji"]').count(),1);
    });
    await domTest('settled Tianji does not allow replaying old cards',async()=>{
      const settled={...active,phase:'settled',encounter:{...active.encounter,state:'settled',result:'win',scoreDelta:3}};
      await set(renderTianji(state,{}, {tianji:settled}));assert.ok((await commands()).filter(x=>x.path==='tianji/action').every(x=>x.disabled));
    });
    await domTest('empty Tianji board shows honest empty state without invented entrants',async()=>{
      await set(renderTianji(state,{tianjiTab:'board'},{tianjiBoard:{ok:true,list:[],total:0}}));assert.equal(await page.locator('ol li').count(),0);assert.ok((await page.locator('main').textContent()).includes('尚无真人报名'));
    });
    await domTest('dungeon route offers only available nodes',async()=>{
      await set(renderDungeon({...state,dungeons:{run:{siteName:'秘境',phase:'route',nodes:[{id:'a',name:'可走',available:true,row:1},{id:'b',name:'未连通',available:false,row:2}],deck:[],relics:[],lootItems:[]}}}));
      const choices=(await commands()).filter(x=>x.path==='dungeon/choose');assert.deepEqual(choices.map(x=>x.body.id),['a']);
    });
    await domTest('boss rewards cannot be skipped and early leave is disabled before claim',async()=>{
      await set(renderDungeon({...state,dungeons:{run:{siteName:'秘境',phase:'reward',cleared:true,bossRewardClaimed:false,rewards:[{id:'r1',name:'传承',enabled:true}],nodes:[],deck:[],relics:[],lootItems:[]}}}));
      const all=await commands();assert.ok(!all.some(x=>x.path==='dungeon/claim'&&x.body.id==='skip'));assert.ok(all.filter(x=>x.path==='dungeon/leave').every(x=>x.disabled));
    });
    for(const [name,render] of renders)await domTest(`${name} creates no executable injected element in real DOM`,async()=>{
      await set(render());assert.equal(await page.locator('script,[onerror],[onload],[onclick]').count(),0);assert.equal(await page.evaluate(()=>globalThis.XSS),undefined);
    });
  } catch(error) { failures.push({name:'DOM test infrastructure',message:error.message});console.error(error.stack); }
  finally { await browser?.close(); }
} else console.log('DOM checks not run; add --dom for isolated real-DOM render contracts. No server or live save is used.');

console.log(JSON.stringify({suite:'v2-contract-check',passed,failed:failures.length,domRequested:process.argv.includes('--dom'),failures},null,2));
if(failures.length)process.exitCode=1;
