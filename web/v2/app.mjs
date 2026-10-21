import { captureUiPosition, restoreUiPosition } from '../ui-position.mjs';
import { bindPillBatchControls } from '../pill-batch.mjs';
import {createStore} from './store.mjs';
import { mountTianjiSteward } from '../tianji-steward.mjs';
import {mfCreateMusicControls,mfRenderMusicButton,mfRenderMusicSettingsEntry} from '../music-controls.mjs';
import {createApi} from './api.mjs';
import {createRouter,destinations} from './router.mjs';
import {esc,fmt,card,action,uiButton,empty,legacyLink,meter} from './ui.mjs';
import {renderCharacter} from './views/character.mjs';
import {mfRenderZhuji,mfBindZhuji,mfResetZhuji} from '../zhuji.mjs';
import {renderEnding,endingSettled} from './views/ending.mjs';
import {renderStory} from './views/adventures.mjs';
import {mutationBlocker,serviceDestination} from './eligibility.mjs';
import {makeOutcome,renderOutcome,outcomeWorkspace,outcomeForWorkspace} from './outcomes.mjs';
import {captureDisclosures,restoreDisclosures} from './disclosures.mjs';
import {renderHome} from './views/home.mjs';
import {renderMap,renderMapPicker} from './views/map.mjs';
import {renderBattle,renderDungeon,renderBattleDetail} from './views/battle.mjs';
import {renderCreate} from './views/create.mjs';
import {buildRequest,buildKey,defaultBuildPortrait,lockBuildPlan,matchesBuildPlan} from '../build-preview.mjs';
import {renderInventory} from './views/inventory.mjs';
import {renderArts} from './views/arts.mjs';
import {renderSocial,renderFriendPicker} from './views/social.mjs';
import {renderTianjiBattleDetail} from './views/tianji.mjs';
import {hasNewForge,forgeRequest,normalizeForgeSelection,editForgeSelection} from '../forge.mjs';
import {isArtifactRedraw,artifactRedrawSnapshot,artifactRedrawIsCurrent,toggleArtifactRedraw,artifactRedrawRequest,renderArtifactRedraw} from '../artifact-redraw.mjs';
import {isArtifactStatus,artifactStatusSnapshot,artifactStatusIsCurrent,artifactStatusRequest,renderArtifactStatus} from '../artifact-status.mjs';

const store=createStore(),root=document.querySelector('#app'),sheet=document.querySelector('#sheet');
const musicControls=mfCreateMusicControls();
let noticeTimer,buildTimer,buildSerial=0,loadSerial=0,authBusy=false,loginMessage='',loginRequired=false,returnFocus=null;
let artifactRedrawSelection=null;
let artifactStatusSelection=null;
let stewardDispose=null;
const loading=new Map(),latestRead=new Map(),disclosures=new Map();let disclosureScope='',combatScrollKey='',renderViewKey='',alchemySupplyRead=0;
const api=createApi({onBusy:busy=>{ document.body.setAttribute('aria-busy',String(busy));document.querySelectorAll('[data-command],select[data-mutation],.player-workspace select,.social-workspace select,.location-workspace select').forEach(button=>{if(busy){button.dataset.wasDisabled=button.disabled?'1':'0';button.disabled=true;}else if(button.dataset.wasDisabled!=null){button.disabled=button.dataset.wasDisabled==='1';delete button.dataset.wasDisabled;}});const status=document.querySelector('#request-status');if(status)status.textContent=busy?'正在处理':store.get().ui.checkpointWarning?'存档待确认':'已同步';}});
const router=createRouter(({route,section})=>{closeSheet();const patch={route};if(section&&route==='social')patch.social=section;if(section&&route==='inventory')patch.inventoryTab=section;if(section&&route==='arts')patch.artsTab=section;if(section&&route==='map')patch.mapTab=section;store.ui(patch);ensureViewData();});
const account=()=>store.get().account?.nickname||'';
function toast(text,error=false){const n=document.querySelector('#notice');n.textContent=text;n.className='toast'+(error?' error':'');n.hidden=false;clearTimeout(noticeTimer);noticeTimer=setTimeout(()=>n.hidden=true,error?7000:3500);}
function titleFor(route){return destinations.find(x=>x[0]===route)?.[1]||'修行';}
function render(){
  stewardDispose?.();stewardDispose=null;
  const {state:s,account:a,ui,data,report,outcome,settlement}=store.get();
  musicControls.setWeather(s?.world?.weather);
  const nextViewKey=`${a?.nickname||''}|${ui.route}|${ui.inventoryTab}|${ui.artsTab}|${ui.social}|${ui.mapTab}|${ui.workshopTab}|${s?.zhuji?.lifeId||s?.player?.name||''}|${!!s?.pending}|${!!s?.dungeons?.run}|${!!s?.battle}|${!!ui.build}`;
  root.querySelectorAll('.home-secondary,.player-picker,.bounded-log').forEach(element=>{if(!element.dataset.uiScroll)element.dataset.uiScroll=element.classList.contains('home-secondary')?'home-secondary':element.classList.contains('player-picker')?'player-picker':'bounded-log';});
  const uiPosition=nextViewKey===renderViewKey?captureUiPosition(root,nextViewKey):null;
  const viewPosition=nextViewKey===renderViewKey?{left:window.scrollX,top:window.scrollY,behavior:'instant'}:null;renderViewKey=nextViewKey;
  const mainScroll=root.querySelector('.main-content')?.scrollTop||0;
  const currentTianji=data.tianji||data.display?.tianji;
  const nextCombatKey=s?.battle?`normal:${s.battle.kind}:${s.battle.enemy}`:currentTianji?.encounter?.state==='active'?`tianji:${currentTianji.encounter.id}`:'';
  const handScroll=nextCombatKey===combatScrollKey?(root.querySelector('.combat-screen .hand')?.scrollLeft||0):0;combatScrollKey=nextCombatKey;
  captureDisclosures(root,disclosureScope,disclosures);
  disclosureScope=`${a?.nickname||''}|${ui.route}|${ui.inventoryTab}|${ui.artsTab}|${ui.social}|${ui.mapTab}`;
  if(loginRequired||!a){root.innerHTML=renderLogin();musicControls.refreshControls();return;}
  const focused=document.activeElement,focusId=focused?.dataset?.input,selectStart=focused?.selectionStart;
  const focusSelector=focused?.id?`#${CSS.escape(focused.id)}`:focusId?`[data-input="${CSS.escape(focusId)}"]`:focused?.tagName==='SELECT'&&focused.dataset.ui?`select[data-ui="${CSS.escape(focused.dataset.ui)}"]${focused.dataset.field?`[data-field="${CSS.escape(focused.dataset.field)}"]`:''}`:null;
  const viewData={...(data.display||{}),...data};
  const readsPending=!!data.display&&(ui.route==='arts'&&(data.nodes==null||data.spells==null)||ui.route==='inventory'&&(data.economy==null||['bag','workshop','forge','items'].includes(ui.inventoryTab)&&data.forge==null)||ui.route==='social'&&(ui.social==='tianji'?data.tianji==null:ui.social==='friends'&&ui.friend&&data.friendActions==null));
  const tianjiPending=api.pendingFor(account());
  const localOutcome=outcomeForWorkspace(outcome,ui);
  const viewUI={...ui,busy:api.busy||readsPending,tianjiPending:!!tianjiPending,playerActionResult:localOutcome};
  let body;
  if(ui.build)body=renderCreate(viewUI,data);
  else if(!s)body=card('此生尚未落笔',`<h1>山河待你入梦</h1><p>选择天赋、分配属性，写下自己的修仙人生。</p><div class="stack">${uiButton('开辟这一世','new-life','','primary')}${uiButton('读取已有存档','settings')}${legacyLink('经典界面')}</div>`);
  else if(s.ended)body=renderEnding(s,data,settlement);
  else if(s.dungeons?.run)body=renderDungeon(s);
  else if(s.battle)body=renderBattle(s);
  else if(s.zhuji?.status==='active')body=`<div data-zhuji-host>${mfRenderZhuji(s.zhuji,{busy:api.busy})}</div>`;
  else if(ui.route==='map')body=renderMap(s,viewUI);
  else if(ui.route==='inventory')body=renderInventory(s,viewUI,viewData);
  else if(ui.route==='arts')body=renderArts(s,viewUI,viewData);
  else if(ui.route==='social')body=renderSocial(s,viewUI,viewData);
  else body=renderHome(s,viewUI,data,localOutcome?report:null,localOutcome);
  const activeTianji=data.tianji?.encounter?.state==='active'||s?.tianji?.hasEncounter;
  const combatVisible=!!s?.battle||!!(ui.route==='social'&&ui.social==='tianji'&&ui.tianjiTab!=='board'&&viewData.tianji?.encounter?.state==='active');
  const homeVisible=!!s&&!s.ended&&!s.battle&&!s.dungeons?.run&&!ui.build&&ui.route==='home'&&s.zhuji?.status!=='active';
  const attention=(activeTianji&&!(ui.route==='social'&&ui.social==='tianji'))?`<div class="notice blocked-banner"><div class="row"><div><strong>留影斗法尚未结束</strong><p>日常修行暂时暂停，随时可接续出牌。</p></div>${uiButton('继续天机斗法','navigate','social/tianji','primary')}</div></div>`:'';
  const pendingNotice=tianjiPending&&!combatVisible?`<div class="notice blocked-banner"><strong>天机操作结果待确认</strong><p>已保留同一请求编号，重试不会重复结算。</p><div class="row">${uiButton('同步会场','refresh-tianji')}${uiButton('重试同一操作','retry-tianji')}</div></div>`:'';
  root.innerHTML=`<div class="app-shell ${combatVisible?'combat-mode':homeVisible?'home-mode':''}"><header class="topbar"><div class="topbar-inner"><div class="brand"><span class="seal" aria-hidden="true">宁</span><span class="brand-title">宁州一梦</span></div><button class="top-status as-button" data-ui="character" aria-label="查看人物成长与状态"><strong>${esc(s?.player?.name||a.nickname)}${s?.player?.realmName?' · '+esc(s.player.realmName):''}</strong><span>人物 · ${s?`${fmt(s.player.stones)} 灵石 · ${esc(s.clock?.month||1)} 月 ${esc(s.clock?.day||1)} 日`:'掌中山河'}</span></button>${mfRenderMusicButton()}<button class="icon-button" data-ui="menu" aria-label="打开更多菜单">☰</button></div></header><main class="main-content" id="main" tabindex="-1" aria-label="${esc(titleFor(ui.route))}">${attention}${pendingNotice}${ui.checkpointWarning?`<div class="notice error" role="alert"><strong>存档写入警告</strong><p>${esc(ui.checkpointWarning)}</p><p>当前进度仍在内存中，请确认保存或导出成功后再退出。</p>${uiButton('打开存档设置','settings')}</div>`:''}${ui.error?`<div class="notice error-banner error" role="alert"><p>${esc(ui.error)}</p>${uiButton('刷新当前进度','refresh')}</div>`:''}${!ui.build&&!s?.battle&&!s?.ended&&!activeTianji&&!homeVisible&&!['inventory','arts','map','social'].includes(ui.route)?renderOutcome(outcome):''}${body}<footer class="page-footer"><span id="request-status">${api.busy?'正在处理':ui.checkpointWarning?'存档待确认':'已同步'}</span> · C++ 规则引擎<br>掌中山河 · v2 试行</footer></main><nav class="bottom-nav" aria-label="主导航">${destinations.map(([id,label,icon])=>`<button data-ui="navigate" data-value="${id}" ${id===ui.route?'aria-current="page"':''}><span class="nav-icon" aria-hidden="true">${icon}</span><span>${label}</span></button>`).join('')}</nav></div>`;
  root.querySelectorAll('.home-secondary,.player-picker,.bounded-log').forEach(element=>{if(!element.dataset.uiScroll)element.dataset.uiScroll=element.classList.contains('home-secondary')?'home-secondary':element.classList.contains('player-picker')?'player-picker':'bounded-log';});
  restoreDisclosures(root,disclosureScope,disclosures);
  stewardDispose=mountTianjiSteward(root,{
    request:async(Path,Body)=>{
      const path=Path.replace(/^\/api\//,'');
      if(path.endsWith('/quote'))return api.read(path,Body,'POST');
      const result=await mutate(path,Body);
      if(!result)throw new Error('交易未确认，请查看提示后重试。');
      return result;
    },
    onKindChange:Kind=>store.ui({stewardKind:Kind}),
  });
  musicControls.refreshControls();
  const ZhujiHost=root.querySelector('[data-zhuji-host]');
  if(ZhujiHost)mfBindZhuji(ZhujiHost,{
    scope:()=>`${account()}:${store.get().state?.zhuji?.lifeId || ''}`,
    request:Body=>Body?api.mutate('zhuji',Body,{account:account()}):api.read('zhuji',{},'GET'),
    onState:State=>store.accept({state:State}),
    isBusy:()=>api.busy,
    setBusy:Busy=>document.body.setAttribute('aria-busy',String(Busy)),
  });
  const hand=root.querySelector('.combat-screen .hand');if(hand)hand.scrollLeft=handScroll;
  if(viewPosition){const main=root.querySelector('.main-content');if(main)main.scrollTop=mainScroll;window.scrollTo(viewPosition);}
  restoreUiPosition(root,uiPosition,nextViewKey);
  bindPillBatchControls(root,{scope:()=>renderViewKey,requestQuote:Body=>api.read('pills/use-quote',Body),onUse:Body=>mutate('pills/use',Body),isBusy:()=>api.busy||store.get().ui.reconciling});
  if(focusSelector){const el=root.querySelector(focusSelector);el?.focus({preventScroll:true});if(typeof selectStart==='number'&&el?.setSelectionRange)try{el.setSelectionRange(selectStart,selectStart);}catch{}}
  if(s?.pending&&!ui.build)showPending(s.pending);else if(sheet.dataset.kind==='pending')closeSheet();
  if(artifactRedrawSelection&&!artifactRedrawIsCurrent(s?.battle,artifactRedrawSelection.snapshot))closeSheet();
  if(artifactStatusSelection&&!artifactStatusIsCurrent(s?.battle,artifactStatusSelection.snapshot))closeSheet();
  root.querySelectorAll('[data-command]').forEach(button=>{const reason=mutationBlocker(s,button.dataset.command,{tianjiActive:activeTianji});if(reason){button.disabled=true;button.title=reason;}});
  if(readsPending)root.querySelectorAll('[data-command],.player-workspace select,.social-workspace select,.location-workspace select').forEach(b=>{b.disabled=true;b.title='正在刷新当前条件，请稍候';});
  if(ui.reconciling)document.querySelectorAll('[data-command]').forEach(b=>{b.disabled=true;b.title='正在核对上次操作结果，请刷新成功后继续';});
  if(activeTianji)document.querySelectorAll('[data-command]').forEach(b=>{if(!b.dataset.command.startsWith('tianji/')){b.disabled=true;b.title='请先完成天机留影斗法';}});
  if(api.busy)document.querySelectorAll('[data-command],select[data-mutation],.player-workspace select,.social-workspace select,.location-workspace select').forEach(b=>{b.dataset.wasDisabled=b.disabled?'1':'0';b.disabled=true;});
}
function renderJobBanner(job){const days=Math.max(1,Math.min(3600,Number(job.remaining)||1));return `<section class="notice task-banner"><div><strong>${esc(job.name)}</strong><p>尚余 ${fmt(job.remaining)} 天；完成后不会额外推进日常计划。</p></div><div class="row">${action(`继续 ${days} 天至${days===job.remaining?'完成':'下一段'}`,'advance',{days},{className:'primary'})}${job.type==='socialElapsed'?'<span class="meta">往来已结算，仍需履行剩余天数。</span>':uiButton('中止安排','cancel-job')}</div></section>`;}
function renderLogin(){return `<main class="auth-wrap"><span class="seal">宁</span><p class="eyebrow">山河一梦 · 自此启程</p><h1>重逢宁州</h1><div class="music-login-entry">${mfRenderMusicButton()}</div><p>以道号接续你的修仙人生。</p><form id="login-form" class="card auth-card"><div class="field"><label for="nickname">道号</label><input id="nickname" name="nickname" autocomplete="username" maxlength="16" placeholder="汉字、字母、数字或下划线" required></div><div class="field"><label for="password">密码</label><input id="password" name="password" type="password" autocomplete="current-password" minlength="4" maxlength="64" placeholder="至少 4 位" required></div><p class="meta">新道号首次登录即注册。请记住密码，同一道号可在其他设备继续。</p><p id="login-error" class="error" role="alert" ${loginMessage?'':'hidden'}>${esc(loginMessage)}</p><button type="submit" class="btn primary wide">登入 / 注册</button></form><p class="page-footer">存档由当前游戏服务器保管<br>${legacyLink('前往经典界面')}</p></main>`;}
function showSheet(title,content,kind='info',locked=false){if(!sheet.open)returnFocus=document.activeElement;if(sheet.open)sheet.close();sheet.dataset.kind=kind;sheet.dataset.locked=String(locked);sheet.innerHTML=`<div class="sheet-inner"><header class="sheet-header"><h2 id="sheet-title">${esc(title)}</h2>${locked?'':`<button data-ui="close-sheet" aria-label="关闭弹层">关闭</button>`}</header>${content}</div>`;sheet.showModal();const current=store.get();sheet.querySelectorAll('[data-command]').forEach(button=>{const reason=mutationBlocker(current.state,button.dataset.command,{tianjiActive:current.data.tianji?.encounter?.state==='active'});if(api.busy||current.ui.reconciling||reason){button.disabled=true;button.title=reason||'请等待当前操作核对完成';}});}
function closeSheet(){artifactRedrawSelection=null;artifactStatusSelection=null;const wasOpen=sheet.open;if(wasOpen)sheet.close();sheet.dataset.kind='';sheet.dataset.locked='false';if(wasOpen&&returnFocus?.isConnected)returnFocus.focus?.({preventScroll:true});returnFocus=null;}
function showPending(p){if(sheet.open&&sheet.dataset.kind==='pending'&&sheet.dataset.pending===JSON.stringify(p))return;showSheet(p.title,`<p class="eyebrow">${esc(p.subtitle)}</p><p>${esc(p.text)}</p><div class="stack">${(p.options||[]).map(o=>`<button class="choice-option" data-command="choose" data-body="${esc(JSON.stringify({id:o.id}))}" ${o.disabled?'disabled':''}><strong>${esc(o.name)}</strong><span class="meta">${esc(o.desc)}</span></button>`).join('')}</div>`,'pending',true);sheet.dataset.pending=JSON.stringify(p);}
sheet.addEventListener('cancel',event=>{event.preventDefault();if(sheet.dataset.locked!=='true')closeSheet();});
sheet.addEventListener('click',event=>{if(event.target===sheet&&sheet.dataset.locked!=='true')closeSheet();});
store.subscribe(render);

async function handleError(error){toast(error.message||'请求未完成。',true);if(error.status===401){mfResetZhuji();loginMessage='登录已失效，请重新登录。登录后会恢复服务端进度，请重新确认刚才的操作。';loginRequired=true;loadSerial++;buildSerial++;closeSheet();store.patch({state:null,data:{},ui:{...store.get().ui,build:null}});return;}
  store.ui({error:error.message});
}
async function refreshState(){const ticket=++loadSerial,who=account();try{const result=await api.read('state');if(ticket!==loadSerial||who!==account())return;const display={...store.get().data};delete display.display;store.accept(result);if(result.state&&Object.keys(display).length)store.data({display});store.ui({error:'',reconciling:false,...(result.checkpointWarning?{checkpointWarning:result.checkpointWarning}:{})});await ensureViewData();}catch(e){if(ticket===loadSerial&&who===account())await handleError(e);}}
async function mutate(path,body={},opts={}){if(api.busy)return;if(store.get().ui.reconciling){toast('请先刷新确认上次操作结果。',true);return;}const blocker=mutationBlocker(store.get().state,path,{tianjiActive:store.get().data.tianji?.encounter?.state==='active'});if(blocker){toast(blocker,true);return;}const before=store.get().state;const workspace=outcomeWorkspace(store.get().ui);const choice={};if(['use-pill','pills/use'].includes(path)||path==='sell'&&body.kind==='potion')choice.inventoryItem='potions:'+body.id;if(path==='buy')choice.marketItem=body.kind+':'+body.id;if(['craft','brew'].includes(path))choice.recipeId=body.id;if(['arts/learn','arts/main','arts/aux'].includes(path)||path==='npc-learn'&&body.kind==='manual')choice.manualId=body.id;if(['arts/spell','library/spell'].includes(path)||path==='npc-learn'&&body.kind==='spell')choice.spellId=body.id;if(path==='arts/node')choice.nodeId=body.node;if(path==='social')choice.friendAction=body.action;if(path==='location-service')choice.locationService=body.id;if(path==='travel'){choice.travelMode=body.mode;choice.mapFocus=body.to;}if(Object.keys(choice).length)store.patch({ui:{...store.get().ui,...choice}},false);const homePosition=store.get().ui.route==='home'&&['plan','advance'].includes(path)?{left:window.scrollX,top:window.scrollY,behavior:'instant'}:null;const gear=['equip','gear/unequip','gear/inscribe','gear/salvage'].includes(path)||path==='sell'&&body.kind==='gear'?before?.gears?.find(x=>x.id===body.id):null;if(gear)store.patch({ui:{...store.get().ui,gearSlot:gear.slot,selectedGear:gear.id}},false);loadSerial++;try{
  const result=await api.mutate(path,body,{...opts,account:account()});
  closeSheet();
  if(['new','load','import','checkpoint/restore'].includes(path))mfResetZhuji();
  if(path.startsWith('tianji/')||opts.retry){store.data({tianji:result});await refreshState();await loadData('tianji','tianji/status',{},'GET',true);}
  else{if(['new','load','import','checkpoint/restore'].includes(path))store.patch({ui:{...store.get().ui,build:null,error:'',friend:null,mapFocus:null},report:null,outcome:null,settlement:null},false);const display={...store.get().data};delete display.display;store.accept(result);if(!['new','load','import','checkpoint/restore'].includes(path)&&Object.keys(display).length)store.data({display});
    store.patch({outcome:{...makeOutcome(['new','load','import','checkpoint/restore'].includes(path)?null:before,result,path),workspace,itemId:body.id||body.node||null,npcId:body.npc||null},...(path==='legacy/settle'?{settlement:{points:result.points,earned:result.earned,budget:result.budget,reasons:result.reasons||[]}}:{})});store.ui({error:'',...(result.checkpointWarning?{checkpointWarning:result.checkpointWarning}:path==='save'?{checkpointWarning:''}:{})});const destination=serviceDestination(result.targetTab,body);if(destination)router.go(...destination);else await ensureViewData();}
  if(homePosition&&store.get().ui.route==='home'&&!result.state?.battle&&!result.state?.pending){window.scrollTo(homePosition);requestAnimationFrame(()=>{if(store.get().ui.route==='home'&&!store.get().state?.battle&&!store.get().state?.pending)window.scrollTo(homePosition);});}
  if(!opts.quiet||result.checkpointWarning)toast(result.checkpointWarning||(path==='craft'?store.get().outcome?.text:'')||result.text||(result.report?`已推进 ${result.report.daysPassed} 天`:path==='save'?`已保存到存档位 ${body.slot}`:'进度已更新'));
  return result;
}catch(e){if(e.uncertain&&!path.startsWith('tianji/')&&!opts.retry)store.ui({reconciling:true});await handleError(e);if(e.status!==401){if(path.startsWith('tianji/')||opts.retry)await loadData('tianji','tianji/status',{},'GET',true);else if(e.uncertain||e.status===409)await refreshState();}return null;}}
async function loadData(key,path,body={},method='POST',force=false){const {revision}=store.get(),who=account(),id=`${revision}:${who}:${key}:${loadSerial}:${JSON.stringify(body)}`;if(!force&&store.get().data[key]!=null)return store.get().data[key];if(loading.has(id))return loading.get(id);const serial=loadSerial,ticket=Symbol(key);latestRead.set(key,ticket);
  const isCurrent=()=>store.get().revision===revision&&account()===who&&serial===loadSerial&&latestRead.get(key)===ticket&&(!body.npc||store.get().ui.friend===body.npc);
  const work=(async()=>{try{const result=await api.read(path,body,method);if(!isCurrent())return null;
    const patch={[key]:result};if(body.npc)patch[key+'Npc']=body.npc;
    if(key==='tianji'){const pending=api.pendingFor(who);const accepted=pending&&(pending.path==='tianji/join'?!!result.entry:pending.path==='tianji/challenge'?result.encounter?.target?.id===pending.body.targetId:result.encounter?.id===pending.body.encounterId&&result.encounter.revision>pending.body.expectedRevision);if(accepted)api.clearPending(who);}
    store.data(patch);return result;
  }catch(e){if(!isCurrent())return null;if(e.status===401)await handleError(e);else{store.data({[key]:{error:e.message}});store.ui({error:e.message});toast(e.message,true);}return null;}finally{loading.delete(id);}})();loading.set(id,work);return work;
}
async function ensureViewData(){const {state:s,ui}=store.get();if(!s||ui.build||loginRequired)return;if(s.ended){await loadData('legacy','legacy');return;}
  if(ui.route==='arts')await Promise.all([loadData('nodes','arts/nodes'),loadData('spells','arts/spells')]);
  if(ui.route==='inventory'){await loadData('economy','eco-options');if(['bag','items','forge','workshop'].includes(ui.inventoryTab)){const f=await loadData('forge','forge-options');if(f?.materials?.length&&f?.shapes?.length){if(hasNewForge(f))store.ui({forge:normalizeForgeSelection(store.get().ui.forge,f)});else if(!store.get().ui.forge)store.ui({forge:{main:f.materials[0].id,aux:f.materials[1]?.id||f.materials[0].id,core:f.materials[2]?.id||f.materials[0].id,shape:f.shapes[0].id}});}if(ui.inventoryTab==='forge'||ui.inventoryTab==='workshop'&&ui.workshopTab==='forge')await refreshForge();}}
  if(ui.route==='social'){if(ui.social==='tianji'){await loadData('tianji','tianji/status',{},'GET');if(ui.tianjiTab==='board')await loadData('tianjiBoard','tianji/board',{},'GET');}else if(ui.social==='sects')await Promise.all([loadData('sects','sects'),loadData('economy','eco-options')]);else{await loadData('roster','roster',{sort:'relation',strangers:false});let npc=store.get().ui.friend;if(!s.nearby?.some(x=>x.id===npc)){npc=[...(s.nearby||[])].sort((a,b)=>Number(b.relation||0)-Number(a.relation||0))[0]?.id;if(npc)store.ui({friend:npc});}if(npc)await Promise.all([loadData('friendActions','social-actions',{npc}),loadData('friendBag','npc-bag',{npc})]);}}
}
async function refreshForge(){const current=store.get(),forge=current.ui.forge;if(!forge)return;const result=await loadData('forgePreview','forge-preview',hasNewForge(current.data.forge)?forgeRequest(forge):forge,'POST',true);if(result?.info?.availableEffects?.length&&!result.info.availableEffects.some(x=>x.id===store.get().ui.forge?.effect)){store.patch({ui:{...store.get().ui,forge:{...store.get().ui.forge,effect:result.info.availableEffects[0].id}},data:{...store.get().data,forgePreview:null}});await refreshForge();}}
function nextBuildSeed(previous = 0) {
  const seed = Math.floor(Math.random() * 2000000000) + 1;
  return seed === previous ? seed % 2000000000 + 1 : seed;
}

async function loadBuildOptions(seed) {
  const serial = ++buildSerial, who = account();
  if (!store.get().ui.build) return;
  const isCurrent = () => serial === buildSerial && who === account() && !!store.get().ui.build;
  store.ui({ buildChecking: true, buildOptionsLoading: true, buildOptionsError: '' });
  try {
    const options = await api.read('build-options', { seed });
    if (!isCurrent()) return;
    if (!options?.innate || options.innate.seed !== seed || !Array.isArray(options.portraits))
      throw new Error('建角选项未完整返回，请重试。');
    const build = store.get().ui.build;
    const portraitId = options.portraits.some(p => p.id === build.portraitId && p.gender === build.gender)
      ? build.portraitId : defaultBuildPortrait(options, build.gender);
    store.patch({ data: { ...store.get().data, buildOptions: options, buildCheck: null },
      ui: { ...store.get().ui, build: { ...build, seed, portraitId }, buildOptionsLoading: false } });
    await checkBuild();
  } catch (error) {
    if (!isCurrent()) return;
    store.ui({ buildChecking: false, buildOptionsLoading: false, buildOptionsError: error.message });
    await handleError(error);
  }
}

async function openBuild() {
  closeSheet();
  clearTimeout(buildTimer);
  const build = { name: store.get().state?.player?.name || account() || '沈知微', gender: '女',
    portraitId: '', traits: [], allocation: {}, seed: nextBuildSeed() };
  store.patch({ ui: { ...store.get().ui, build, buildPlan: null, buildChecking: true,
      buildOptionsLoading: true, buildOptionsError: '' },
    data: { ...store.get().data, buildCheck: null, buildOptions: null } });
  await loadBuildOptions(build.seed);
}

async function checkBuild() {
  clearTimeout(buildTimer);
  const serial = ++buildSerial;
  const build = store.get().ui.build;
  if (!build) return null;
  const key = buildKey(build);
  store.ui({ buildChecking: true });
  try {
    const result = await api.read('build-check', buildRequest(build));
    if (serial !== buildSerial || !store.get().ui.build || key !== buildKey(store.get().ui.build)) return null;
    store.patch({ data: { ...store.get().data, buildCheck: result },
      ui: { ...store.get().ui, buildChecking: false, ...(result.valid ? {} : { buildPlan: null }) } });
    return result;
  } catch (error) {
    if (serial === buildSerial && store.get().ui.build) {
      store.patch({ data: { ...store.get().data, buildCheck: { valid: false, error: error.message } },
        ui: { ...store.get().ui, buildChecking: false, buildPlan: null } });
      await handleError(error);
    }
    return null;
  }
}

function editBuild(patch, notify = true) {
  const current = store.get();
  if (!current.ui.build || current.ui.buildPlan || current.ui.buildOptionsLoading || api.busy) return false;
  buildSerial++;
  store.patch({ data: { ...current.data, buildCheck: null },
    ui: { ...current.ui, build: { ...current.ui.build, ...patch }, buildPlan: null, buildChecking: true } }, notify);
  return true;
}
async function openSettings(){showSheet('存档与设置',empty('正在读取存档位……'),'settings');const result=await api.read('slots').catch(handleError);if(!result||sheet.dataset.kind!=='settings')return;const s=store.get().state;showSheet('存档与设置',`<p class="meta account-name">当前道号：${esc(account())}</p>${mfRenderMusicSettingsEntry()}<p class="notice">保存会覆盖所选位置；读取会替换当前人生。自动位仅供读取。</p><div class="list">${(result.slots||[]).map(x=>`<article class="list-item"><div><h3>${x.automatic?'十年自动存档':'存档位 '+esc(x.slot)}</h3><p class="meta">${x.exists?`${esc(x.name)} · ${esc(x.realm)} · ${fmt(x.age)} 岁`:'空存档位'}</p></div><div class="row">${!x.automatic?(x.exists?uiButton('覆盖保存','confirm-save',x.slot):action('保存','save',{slot:x.slot},{disabled:!s})):''}${x.exists?uiButton('读取','confirm-load',x.slot):''}</div></article>`).join('')}</div><div class="divider"></div><div class="stack">${uiButton('导出行卷备份','export')}${uiButton('导入行卷','import')}${uiButton('修改密码','password')}${uiButton('切换道号','logout')}${legacyLink('经典界面 · 全部功能')}</div><p class="meta">经典界面与新版共用服务器存档，无须迁移。离开此页不会自动认输。</p>`,'settings');}
function confirmAction(title,text,path,body){showSheet(title,`<p>${esc(text)}</p><div class="row">${uiButton('取消','close-sheet')}${action('确认',''+path,body,{className:'primary'})}</div>`,'confirm');}
function openMenu(){showSheet('行囊之外',`<div class="stack">${uiButton('身世与状态','character')}${uiButton('存档与设置','settings')}${mfRenderMusicSettingsEntry()}${uiButton('刷新服务器进度','refresh')}${uiButton('再开一世','new-life')}${uiButton('当前功能范围','coverage')}${legacyLink('经典界面 · 全部功能')}</div>`,'menu');}
function openCharacter(){const {state:s,ui}=store.get();if(!s)return;showSheet('人物 · '+s.player.name,renderCharacter(s,ui),'character');}
function openBattleTarget(uid){const b=store.get().state?.battle,c=b?.hand?.find(c=>c.uid===uid);if(!c||c.playable===false)return;showSheet('移除哪种神通？',`<p>确认后消耗本牌与 ${fmt(c.cost)} 灵力，将目标全部副本移出本场。</p><div class="stack">${(b.removalOptions||[]).filter(x=>x.id!==c.id&&x.copies>0).map(x=>action(`确认移除 ${x.name} × ${x.copies}`,'battle/turn',{id:c.uid,target:x.id})).join('')||empty('当前没有可移除的目标。')}</div>`,'battle-target');}
function showArtifactRedraw(){const battle=store.get().state?.battle,ability=battle?.artifacts?.find(item=>item.id===artifactRedrawSelection?.id);if(!ability?.playable||!artifactRedrawIsCurrent(battle,artifactRedrawSelection?.snapshot)){artifactRedrawSelection=null;closeSheet();return;}showSheet(ability.name,renderArtifactRedraw(battle,ability,artifactRedrawSelection.selected,{mode:'mobile',busy:api.busy}),'artifact-redraw');}
function showArtifactStatus(){const battle=store.get().state?.battle,ability=battle?.artifacts?.find(item=>item.id===artifactStatusSelection?.id);if(!artifactStatusIsCurrent(battle,artifactStatusSelection?.snapshot)){closeSheet();return;}showSheet(ability.name,renderArtifactStatus(battle,ability,artifactStatusSelection.selected,{mode:'mobile',busy:api.busy}),'artifact-status');}
function openPlans(){const s=store.get().state;if(!s)return;showSheet('这段日子的打算',`<div class="stack">${(s.plans||[]).map(p=>`<article><h3>${esc(p.name)}</h3><p>${esc(p.desc)}</p>${p.preview?`<p class="meta">${esc(p.preview)}</p>`:''}${action(p.id===s.plan?'当前计划':'采用此计划','plan',{plan:p.id},{disabled:p.id===s.plan,className:'wide'})}</article>`).join('')}</div>`,'plans');}

async function onUI(id,value,target){const {ui,state:s}=store.get();
  if(id.startsWith('forge-')){
    if(api.busy||ui.reconciling)return;
    const catalog=store.get().data.forge;
    if(id==='forge-run-v2'){const quote=store.get().data.forgePreview?.info;if(quote?.enabled===true&&quote.quoteToken)await mutate('forge',forgeRequest(ui.forge,quote));return;}
    if(id==='forge-field'||['forge-add','forge-fill','forge-remove','forge-clear'].includes(id)){
      const forge=editForgeSelection(ui.forge,catalog,id==='forge-field'?target.dataset.field:id.slice(6),value,ui.forgeView?.slot);
      const empty=forge.materials.indexOf('');
      const forgeView={...(ui.forgeView||{filters:{owned:true}}),...(['forge-add','forge-fill'].includes(id)?{slot:empty<0?null:empty}:{})};
      store.patch({ui:{...ui,forge,forgeView},data:{...store.get().data,forgePreview:null}});await refreshForge();return;
    }
    const forgeView={slot:null,filters:{owned:true},...ui.forgeView};
    if(id==='forge-slot')forgeView.slot=Number(value);
    if(id==='forge-picker-close')forgeView.slot=null;
    if(id==='forge-owned'){forgeView.filters={...forgeView.filters,owned:!forgeView.filters.owned};forgeView.page=0;}
    if(id==='forge-filter'){forgeView.filters={...forgeView.filters,[target.dataset.field]:value};forgeView.page=0;}
    if(id==='forge-page')forgeView.page=Number(value);
    if(['forge-slot','forge-picker-close','forge-owned','forge-filter','forge-page'].includes(id)){store.ui({forgeView});return;}
  }
  if(id==='artifact-redraw-open'){const ability=s?.battle?.artifacts?.find(item=>item.id===value);if(!api.busy&&ability?.playable&&isArtifactRedraw(ability)){artifactRedrawSelection={id:value,selected:[],snapshot:artifactRedrawSnapshot(s.battle)};showArtifactRedraw();}return;}
  if(id==='artifact-redraw-toggle'){if(api.busy||!artifactRedrawSelection)return;const ability=s?.battle?.artifacts?.find(item=>item.id===artifactRedrawSelection.id);artifactRedrawSelection.selected=toggleArtifactRedraw(s.battle,ability,artifactRedrawSelection.selected,value);showArtifactRedraw();return;}
  if(id==='artifact-redraw-cancel'){artifactRedrawSelection=null;closeSheet();return;}
  if(id==='artifact-redraw-confirm'){if(api.busy)return;const ability=s?.battle?.artifacts?.find(item=>item.id===artifactRedrawSelection?.id),body=artifactRedrawRequest(s?.battle,ability,artifactRedrawSelection?.selected,artifactRedrawSelection?.snapshot);if(!body){artifactRedrawSelection=null;closeSheet();toast('手牌已变化，请重新选择。',true);return;}artifactRedrawSelection=null;await mutate('battle/turn',body);return;}
  if(id==='artifact-status-open'){const ability=s?.battle?.artifacts?.find(item=>item.id===value);if(!api.busy&&!ui.reconciling&&ability?.playable&&isArtifactStatus(ability)){artifactRedrawSelection=null;artifactStatusSelection={id:value,selected:null,snapshot:artifactStatusSnapshot(s.battle,ability)};showArtifactStatus();}return;}
  if(id==='artifact-status-select'){if(api.busy||!artifactStatusSelection)return;const ability=s?.battle?.artifacts?.find(item=>item.id===artifactStatusSelection.id);if(!artifactStatusIsCurrent(s?.battle,artifactStatusSelection.snapshot)){closeSheet();return;}if(ability.targets?.some(item=>item.id===value))artifactStatusSelection.selected=value;showArtifactStatus();return;}
  if(id==='artifact-status-cancel'){closeSheet();return;}
  if(id==='artifact-status-confirm'){
    if(api.busy)return;
    const ability=s?.battle?.artifacts?.find(item=>item.id===artifactStatusSelection?.id),body=artifactStatusRequest(s?.battle,ability,artifactStatusSelection?.selected,artifactStatusSelection?.snapshot,{busy:api.busy});
    if(!body){closeSheet();toast('状态已变化，请重新选择。',true);return;}
    closeSheet();
    const result=await mutate('battle/turn',body);
    if(!result&&!loginRequired)await refreshState();
    return;
  }
  if(id==='alchemy-supply-quote'){
    if(api.busy)return;
    const payload=JSON.parse(value),key=JSON.stringify(payload),ticket=++alchemySupplyRead,stateBefore=s,scope=renderViewKey;
    try{const response=await api.read('craft/materials/quote',payload);if(ticket===alchemySupplyRead&&store.get().state===stateBefore&&scope===renderViewKey)store.data({alchemySupply:{key,quote:response.quote||response}});}
    catch(error){if(ticket===alchemySupplyRead&&store.get().state===stateBefore&&scope===renderViewKey)store.data({alchemySupply:{key,quote:{error:error.message}}});}
    return;
  }
  if(id==='navigate'){const [route,section]=value.split('/');router.go(route,section);document.querySelector('#main')?.focus({preventScroll:true});window.scrollTo({top:0,behavior:'instant'});return;}
  if(id==='outcome-detail'){const current=store.get();showSheet('本次操作结果',current.outcome?`<p>${esc(current.outcome.text)}</p>${current.outcome.changes.map(x=>`<p>${esc(x)}</p>`).join('')}${(current.outcome.report?.highlights||[]).map(x=>`<p>${esc(x)}</p>`).join('')}`:(current.report?.highlights||[]).map(x=>`<p>${esc(x)}</p>`).join(''),'outcome');return;}
  if(id==='combat-detail'){const detail=renderBattleDetail(s,value);if(detail)showSheet(detail.title,detail.content,'combat-detail');return;}
  if(id==='tianji-detail'){const detail=renderTianjiBattleDetail(store.get().data.tianji,{...ui,busy:api.busy,tianjiPending:!!api.pendingFor(account())},value);if(detail)showSheet(detail.title,detail.content,'tianji-detail');return;}
  if(id==='tianji-tab')closeSheet();
  if(id==='daily-plan'){if(value===s?.plan)return;const position={left:window.scrollX,top:window.scrollY,behavior:'instant'};const result=await mutate('plan',{plan:value},{quiet:true});if(result){window.scrollTo(position);document.querySelector('#daily-plan')?.focus({preventScroll:true});}return;}
  const selections={'inventory-item':'inventoryItem','gear-select':'selectedGear','manual-select':'manualId','spell-select':'spellId','node-select':'nodeId','market-item':'marketItem','spell-source':'spellSource','friend-task':'friendTask','friend-action':'friendAction','friend-lesson':'friendLesson','friend-gift':'friendGift','friend-trade':'friendTrade','friend-bond':'friendBond','brew-select':'recipeId','cave-supply':'caveSupply'};
  if(selections[id]){if(api.busy||ui.reconciling)return;const gear=id==='gear-select'?s?.gears?.find(x=>x.id===value):null;store.patch({ui:{...ui,[selections[id]]:value,...(gear?{gearSlot:gear.slot}:{})},outcome:null});return;}
  if(id==='dismiss-outcome'){store.patch({outcome:null});return;}
  if(id==='character-tab'){store.patch({ui:{...ui,characterTab:value}},false);openCharacter();return;}
  if(id==='confirm-save'){confirmAction('覆盖存档位 '+value,'确认用当前人物进度覆盖该存档？其他存档位与自动位不受影响。','save',{slot:Number(value)});return;}
  if(id==='confirm-invite'||id==='wild-confirm'){showSheet('费用尚未提供',`<p>当前接口没有返回这项行动的费用，暂不能在新版确认扣费。</p>${legacyLink('在经典界面查看')}`,'missing-quote');return;}
  if(id==='story'){const story=s?.stories?.find(x=>x.id===value);if(story)showSheet(story.name,renderStory(s,value),'story');return;}
  if(id==='dungeon-leave'){if(s?.dungeons?.run)confirmAction('提前离开秘境','离开后本次入口消失，只带回已经取得的战利品；尚未取得的奖励不会补发。','dungeon/leave',{});return;}
  if(id==='cancel-job'){if(s?.job)confirmAction('中止当前安排','将中止'+s.job.name+'；已支付费用可能不会返还。','cancel-job',{});return;}
  if(id==='gear-confirm-salvage'||id==='gear-confirm-sell'){const gear=s?.gears?.find(x=>x.id===value);if(!gear)return;const quote=gear.actions?.[id==='gear-confirm-salvage'?'salvage':'sell'];confirmAction(id==='gear-confirm-salvage'?'分解法器':'出售法器',`将永久失去「${gear.name}」。${Number.isFinite(quote?.returnStones)?`回收 ${quote.returnStones} 灵石。`:'回收金额待核心结算，当前接口未提供报价。'}`,id==='gear-confirm-salvage'?'gear/salvage':'sell',{...(id==='gear-confirm-sell'?{kind:'gear'}:{}),id:gear.id});return;}
  if(id==='close-sheet'){closeSheet();return;}if(id==='menu'){openMenu();return;}if(id==='settings'){await openSettings();return;}if(id==='character'){openCharacter();return;}if(id==='plans'){openPlans();return;}
  if(id==='new-life'){if(s?.ended&&!endingSettled(s,store.get().data,store.get().settlement)){showSheet('先结算这一世',`<p>本世收获尚未确认结算，直接新建会替换当前人生。</p><div class="stack">${action('先结算本世收获','legacy/settle',{}, {className:'primary'})}${uiButton('确认放弃收获，继续建号','open-build')}</div>`,'new-confirm');}else if(s&&!s.ended){showSheet('另写一世',`<p>新人生会替换当前进度。请选择存档位置保存，或导出备份，再开始下一世。</p><div class="stack">${uiButton('选择存档位置 / 导出','settings')}${uiButton('已备份，继续建号','open-build','','primary')}</div>`,'new-confirm');}else await openBuild();return;}
  if(id==='open-build'){if(!api.busy)await openBuild();return;}if(id==='cancel-build'){if(api.busy)return;clearTimeout(buildTimer);buildSerial++;store.ui({build:null,buildPlan:null,buildChecking:false,buildOptionsLoading:false});return;}
  if (id === 'build-unlock') { if(api.busy)return; buildSerial++; store.ui({ buildPlan: null, buildChecking: false }); return; }
  if (id === 'build-lock') {
    if (!ui.build || ui.buildChecking || ui.buildOptionsLoading || api.busy) return;
    const result = await checkBuild();
    if (!result || !store.get().ui.build) return;
    const plan = lockBuildPlan(store.get().ui.build, result);
    store.ui({ buildPlan: plan });
    if (plan) root.querySelector('.opening-preview')?.scrollIntoView({ block: 'nearest' });
    return;
  }
  if (id.startsWith('build-') && ui.buildPlan) return;
  if(id==='build-trait'){if(!ui.build)return;const traits=ui.build.traits.includes(value)?ui.build.traits.filter(x=>x!==value):[...ui.build.traits,value];if(editBuild({traits}))await checkBuild();return;}
  if(id==='build-alloc'){if(!ui.build)return;const allocation={...ui.build.allocation,[value]:Math.max(0,(ui.build.allocation[value]||0)+Number(target.dataset.delta))};if(editBuild({allocation}))await checkBuild();return;}
  if(id==='build-portrait'){if(editBuild({portraitId:value}))await checkBuild();return;}
  if (id === 'build-reroll' || id === 'build-options-retry') {
    if (!ui.build || ui.buildChecking || ui.buildOptionsLoading || api.busy) return;
    await loadBuildOptions(id === 'build-reroll' ? nextBuildSeed(ui.build.seed) : ui.build.seed);
    return;
  }
  if (id === 'start-life') {
    const plan = ui.buildPlan;
    if (api.busy || ui.buildChecking || !ui.build || !matchesBuildPlan(plan, ui.build)) return;
    const result = await checkBuild();
    if (!result?.valid || store.get().ui.buildPlan !== plan || !store.get().ui.build
        || !matchesBuildPlan(plan, store.get().ui.build)) return;
    if (await mutate('new', plan.request)) {
      router.go('home'); window.scrollTo({ top: 0, behavior: 'instant' });
      document.querySelector('#main')?.focus({ preventScroll: true });
    }
    return;
  }
  if(id==='friend-picker'){showSheet('查找道友',renderFriendPicker(s,ui),'friend-picker');return;}
  if(id==='friend-filter-tab'&&sheet.dataset.kind==='friend-picker'){store.patch({ui:{...ui,friendFilter:value}},false);showSheet('查找道友',renderFriendPicker(s,store.get().ui),'friend-picker');return;}
  if(id==='map-picker'){showSheet('选择目的地',renderMapPicker(s,ui),'map-picker');return;}
  if((id==='map-filter-tab'||id==='map-risk')&&sheet.dataset.kind==='map-picker'){store.patch({ui:{...ui,[id==='map-risk'?'mapRisk':'mapFilter']:value}},false);showSheet('选择目的地',renderMapPicker(s,store.get().ui),'map-picker');return;}
  if(id==='map-focus'){const position={left:window.scrollX,top:window.scrollY,behavior:'instant'};closeSheet();store.ui({mapFocus:value,travelMode:null});window.scrollTo(position);return;}if(id==='locate'){store.ui({mapFocus:value,mapTab:'places'});router.go('map');return;}
  const routeTabs={'inventory-tab':'inventory','arts-tab':'arts','social-tab':'social','map-tab':'map'};
  if(routeTabs[id]){router.go(routeTabs[id],value);return;}
  const keys={'inventory-tab':'inventoryTab','arts-tab':'artsTab','map-tab':'mapTab','map-filter-tab':'mapFilter','map-risk':'mapRisk','travel-mode':'travelMode','location-service-select':'locationService','social-tab':'social','tianji-tab':'tianjiTab','market-kind-tab':'marketKind','market-availability-tab':'marketAvailability','friend-filter-tab':'friendFilter','spell-filter-tab':'spellFilter','arts-dao':'artsDao','brew-batch':'brewBatch','inventory-kind-tab':'inventoryKind','gear-slot-tab':'gearSlot','manual-filter-tab':'manualFilter','spell-dao':'spellDao'};
  if(keys[id]){store.ui({[keys[id]]:value});await ensureViewData();return;}
  if(id==='workshop-tab'){store.ui({workshopTab:value});router.go('inventory','workshop');return;}
  if(id==='forge-select'){store.patch({ui:{...ui,forge:{...ui.forge,[target.dataset.field]:value}},data:{...store.get().data,forgePreview:null}});await refreshForge();return;}
  if(id==='friend'){const position={left:window.scrollX,top:window.scrollY,behavior:'instant'};closeSheet();store.patch({ui:{...ui,friend:value,social:'friends'},outcome:null,data:{...store.get().data,friendActions:null,friendBag:null}});await ensureViewData();window.scrollTo(position);return;}
  if(id==='refresh'){closeSheet();await refreshState();return;}
  if(id==='refresh-social'){store.patch({data:{}});await ensureViewData();return;}
  if(id==='refresh-tianji'){await loadData('tianji','tianji/status',{},'GET',true);return;}
  if(id==='refresh-tianji-board'){await loadData('tianjiBoard','tianji/board',{},'GET',true);return;}
  if(id==='retry-tianji'){const pending=api.pendingFor(account());if(pending)await mutate(pending.path,pending.body,{retry:true});return;}
  if(id==='battle-target'){openBattleTarget(value);return;}
  if(id==='confirm-load'){confirmAction('读取存档',`将用存档位 ${value} 替换当前进度，未保存的变化将丢失。`,'load',{slot:Number(value)});return;}
  if(id==='export'){try{const result=await api.read('export');const blob=new Blob([JSON.stringify(result,null,2)],{type:'application/json'}),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=`宁州一梦-${result.name||'行卷'}.json`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);toast('行卷已生成，请保管下载文件。');}catch(e){await handleError(e);}return;}
  if(id==='import'){showSheet('导入行卷',`<p class="notice">导入会替换当前人生；整包行卷还会导入传承。请先保留备份。</p><label class="field">选择 JSON 行卷<input id="import-file" type="file" accept=".json,application/json"></label><div id="import-preview"></div>`,'import');return;}
  if(id==='password'){showSheet('修改密码',`<form id="password-form"><label class="field">原密码<input name="old" type="password" autocomplete="current-password" required></label><label class="field">新密码<input name="new" type="password" autocomplete="new-password" minlength="4" maxlength="64" required></label><label class="field">再次输入新密码<input name="repeat" type="password" autocomplete="new-password" required></label><p class="meta">修改后其他设备需要重新登录。</p><button class="btn primary wide" type="submit">确认修改</button></form>`,'password');return;}
  if(id==='logout'){try{await api.read('logout');loginRequired=true;mfResetZhuji();store.reset(null);closeSheet();}catch(e){await handleError(e);}return;}
  if(id==='coverage'){showSheet('v2 功能范围',`<p>新版独立入口与经典界面共用 C++ 引擎和存档。</p><p>已接入：建号、日常推进、旅行、物品、炼丹炼器、道法、斗法、秘境、道友、宗门、天机大比、存档。</p><p class="notice">尚未完整迁入：后山探访的资格与费用预览、冲突掠夺、声望与成就总览、完整人物名册筛选及传承档案。请从经典界面进入这些功能。</p>${legacyLink()}`,'coverage');return;}
}

document.addEventListener('click',event=>{const button=event.target.closest('[data-command],[data-ui],[data-build-portrait]');if(!button||button.disabled||button.tagName==='SELECT')return;if(button.dataset.buildPortrait){onUI('build-portrait',button.dataset.buildPortrait,button).catch(handleError);}else if(button.dataset.command){let body;try{body=JSON.parse(button.dataset.body||'{}');}catch{toast('操作参数无法读取。',true);return;}mutate(button.dataset.command,body);}else onUI(button.dataset.ui,button.dataset.value||'',button).catch(handleError);});
document.addEventListener('change',async event=>{const el=event.target;if(el.matches('select[data-ui]')){await onUI(el.dataset.ui,el.value,el);return;}if(el.id==='import-file'){const file=el.files?.[0];if(!file)return;const preview=document.querySelector('#import-preview');try{if(file.size>8*1024*1024)throw new Error('行卷超过 8 MB，无法导入。');const raw=await file.text(),parsed=JSON.parse(raw);const body=typeof parsed.game==='string'?{game:parsed.game,...(typeof parsed.legacy==='string'?{legacy:parsed.legacy}:{})}:{game:raw};preview.innerHTML=`<p>${esc(file.name)} · 已读取，确认后替换。</p>${action('确认导入并替换','import',body,{className:'primary wide'})}`;}catch(e){preview.textContent=e.message||'不是有效的 JSON 行卷。';}}});
document.addEventListener('input',event=>{const el=event.target,key=el.dataset.input;if(!key)return;const {ui}=store.get();if (key === 'build-name' || key === 'build-gender') {
  if (!ui.build || ui.buildPlan) return;
  const patch = key === 'build-name' ? { name: el.value } : {
    gender: el.value, portraitId: defaultBuildPortrait(store.get().data.buildOptions, el.value),
  };
  if (!editBuild(patch, false)) return;
  root.querySelectorAll('[data-ui="start-life"],[data-ui="build-lock"]').forEach(button => { button.disabled = true; });
  clearTimeout(buildTimer);
  buildTimer = setTimeout(checkBuild, 120);
}else if(['map-search','market-search','friend-search','inventory-search','brew-search','manual-search','spell-search'].includes(key)){store.patch({ui:{...ui,[{'map-search':'mapSearch','market-search':'marketSearch','friend-search':'friendSearch','inventory-search':'inventorySearch','brew-search':'brewSearch','manual-search':'manualSearch','spell-search':'spellSearch'}[key]]:el.value}},false);clearTimeout(buildTimer);buildTimer=setTimeout(()=>{if(sheet.dataset.kind==='map-picker'){const pos=el.selectionStart;sheet.querySelector('.sheet-inner').innerHTML=`<header class="sheet-header"><h2 id="sheet-title">选择目的地</h2><button data-ui="close-sheet">关闭</button></header>${renderMapPicker(store.get().state,store.get().ui)}`;const next=sheet.querySelector('[data-input="map-search"]');next?.focus();if(next&&pos!=null)next.setSelectionRange(pos,pos);}else if(sheet.dataset.kind==='friend-picker'){const pos=el.selectionStart;sheet.querySelector('.sheet-inner').innerHTML=`<header class="sheet-header"><h2 id="sheet-title">查找道友</h2><button data-ui="close-sheet">关闭</button></header>${renderFriendPicker(store.get().state,store.get().ui)}`;const next=sheet.querySelector('[data-input="friend-search"]');next?.focus();if(next&&pos!=null)next.setSelectionRange(pos,pos);}else render();},120);}});
document.addEventListener('submit',async event=>{if(event.target.id==='login-form'){event.preventDefault();if(authBusy)return;const form=event.target,values=new FormData(form),nickname=String(values.get('nickname')).trim(),password=String(values.get('password'));const err=document.querySelector('#login-error');if(!/^[A-Za-z0-9_\u3400-\u4DBF\u4E00-\u9FFF\uF900-\uFAFF]{1,16}$/u.test(nickname)){err.hidden=false;err.textContent='道号需为 1～16 个汉字、字母、数字或下划线。';return;}authBusy=true;form.querySelector('button').disabled=true;try{const result=await api.read('login',{nickname,password});form.reset();loginRequired=false;loginMessage='';mfResetZhuji();store.reset(result);await refreshState();router.start();}catch(e){err.hidden=false;err.textContent=e.message;}finally{authBusy=false;form.querySelector('button')?.removeAttribute('disabled');}}
  if(event.target.id==='password-form'){event.preventDefault();const form=event.target,d=new FormData(form);if(d.get('new')!==d.get('repeat')){toast('两次新密码不一致。',true);return;}const button=form.querySelector('button');button.disabled=true;try{await api.read('password',{old:d.get('old'),new:d.get('new')});form.reset();closeSheet();toast('密码已更新。');}catch(e){await handleError(e);}finally{button.disabled=false;}}});
window.addEventListener('online',()=>toast('网络已恢复，可以刷新进度。'));
window.addEventListener('offline',()=>toast('网络已断开，请等恢复后刷新确认进度。',true));
async function boot(){try{
  const runtimeMode=await api.read('mode',{},'GET');
  if(typeof runtimeMode?.local!=='boolean')throw new Error('无法确认游戏运行模式，请刷新重试。');
  // --local deliberately closes whoami. Only an explicit server capability may
  // bypass authentication; network and mode failures keep their error message.
  const who=runtimeMode.local
    ? {ok:true,local:true,nickname:typeof runtimeMode.nickname==='string'&&runtimeMode.nickname.trim()?runtimeMode.nickname:'本地道友'}
    : await api.read('whoami',{},'GET');
  store.patch({account:who});await refreshState();router.start();
}catch(e){if(e.status!==401)loginMessage=e.message;loginRequired=true;render();}}
boot();
