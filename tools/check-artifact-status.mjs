import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import vm from 'node:vm';
import { renderBattleDetail } from '../web/v2/views/battle.mjs';

const moduleUrl = new URL('../web/artifact-status.mjs', import.meta.url);
const helper = existsSync(moduleUrl) ? await import(moduleUrl) : {};
const cases = [];
const test = (name, run) => cases.push({ name, run });
const { isArtifactStatus, artifactStatusSnapshot, artifactStatusIsCurrent,
  artifactStatusRequest, renderArtifactStatus } = helper;
const ability = () => ({ id:'artifact-cleanse', name:'清浊', playable:true, cost:2,
  targetMode:'status-one', statusToken:'status-37', preview:{text:'选择一种自身负面状态，按品阶削除层数。'},
  targets:[{id:'poison',name:'中毒',layers:18,removeLayers:10,remainingLayers:8,unit:'层',description:'自身中毒。'},
    {id:'silence',name:'悬灵',layers:2,removeLayers:1,remainingLayers:1,unit:'回合',description:'限制灵力。'}] });
const battle = () => ({turn:3,artifacts:[ability()]});

test('single status contract is available', () => assert.equal(typeof isArtifactStatus, 'function'));
test('status-one detection does not capture hand-many or ordinary abilities', () => {
  assert.equal(isArtifactStatus(ability()),true);
  for (const item of [null,{}, {targetMode:'hand-many'}]) assert.equal(isArtifactStatus(item),false);
});
test('snapshot captures the current ability token and turn', () => {
  const b=battle(), snapshot=artifactStatusSnapshot(b,b.artifacts[0]);
  assert.equal(snapshot.expectedTurn,3); assert.equal(snapshot.expectedStatusToken,'status-37');
  assert.equal(artifactStatusIsCurrent(b,snapshot),true);
});
test('confirmation sends one status ID and concurrency guards, without a hand array', () => {
  const b=battle(), a=b.artifacts[0], snapshot=artifactStatusSnapshot(b,a);
  assert.deepEqual(artifactStatusRequest(b,a,'poison',snapshot),
    {id:'artifact-cleanse',target:'poison',expectedTurn:3,expectedStatusToken:'status-37'});
});
test('cancelling or omitting a status never creates an action', () => {
  const b=battle(), a=b.artifacts[0], snapshot=artifactStatusSnapshot(b,a);
  assert.equal(artifactStatusRequest(b,a,null,snapshot),null);
  assert.equal(artifactStatusRequest(b,a,'',snapshot),null);
});
test('unknown status IDs cannot be submitted', () => {
  const b=battle(), a=b.artifacts[0];
  assert.equal(artifactStatusRequest(b,a,'other',artifactStatusSnapshot(b,a)),null);
});
test('busy confirmation cannot create a duplicate request', () => {
  const b=battle(), a=b.artifacts[0];
  assert.equal(artifactStatusRequest(b,a,'poison',artifactStatusSnapshot(b,a),{busy:true}),null);
});
test('cooldown or insufficient resources invalidate an open selection', () => {
  const b=battle(), a=b.artifacts[0], snapshot=artifactStatusSnapshot(b,a); a.playable=false;
  assert.equal(artifactStatusIsCurrent(b,snapshot),false);
  assert.equal(artifactStatusRequest(b,a,'poison',snapshot),null);
});
test('turn change invalidates an open selection', () => {
  const b=battle(), snapshot=artifactStatusSnapshot(b,b.artifacts[0]); b.turn++;
  assert.equal(artifactStatusIsCurrent(b,snapshot),false);
});
test('status token change invalidates an open selection', () => {
  const b=battle(), snapshot=artifactStatusSnapshot(b,b.artifacts[0]); b.artifacts[0].statusToken='status-38';
  assert.equal(artifactStatusIsCurrent(b,snapshot),false);
});
test('removed ability or removed targets invalidate an open selection', () => {
  const b=battle(), snapshot=artifactStatusSnapshot(b,b.artifacts[0]); b.artifacts=[];
  assert.equal(artifactStatusIsCurrent(b,snapshot),false);
  const c=battle(), other=artifactStatusSnapshot(c,c.artifacts[0]); c.artifacts[0].targets.pop();
  assert.equal(artifactStatusIsCurrent(c,other),false);
});
test('layer changes invalidate selection even if an old server reuses its token', () => {
  const b=battle(), snapshot=artifactStatusSnapshot(b,b.artifacts[0]); b.artifacts[0].targets[0].layers=20;
  assert.equal(artifactStatusIsCurrent(b,snapshot),false);
});
test('missing status token fails closed', () => {
  const b=battle(); delete b.artifacts[0].statusToken;
  assert.equal(artifactStatusRequest(b,b.artifacts[0],'poison',artifactStatusSnapshot(b,b.artifacts[0])),null);
});
test('backend rejection permits a fresh selection of the same unchanged ability', () => {
  const b=battle(), a=b.artifacts[0];
  assert.ok(artifactStatusRequest(b,a,'poison',artifactStatusSnapshot(b,a)));
  assert.ok(artifactStatusRequest(b,a,'silence',artifactStatusSnapshot(b,a)));
});
test('desktop renders authoritative current, removal and remaining numbers', () => {
  const b=battle(), html=renderArtifactStatus(b,b.artifacts[0],'poison');
  assert.match(html,/当前 18 层/); assert.match(html,/可削 10 层/); assert.match(html,/剩余 8 层/);
  assert.match(html,/当前 2 回合/); assert.match(html,/可削 1 回合/); assert.match(html,/剩余 1 回合/);
  assert.match(html,/data-action="artifact-status-select" data-id="poison"/);
  assert.match(html,/aria-pressed="true"/); assert.match(html,/确认削除「中毒」/);
  assert.match(html,/确认才消耗 2 灵力/);
});
test('rendering never derives removal amounts from grade or player attributes', () => {
  const b=battle(); Object.assign(b.artifacts[0].targets[0],{removeLayers:7,remainingLayers:11});
  const html=renderArtifactStatus(b,b.artifacts[0],'poison');
  assert.match(html,/可削 7 层/); assert.match(html,/剩余 11 层/);
});
test('mobile uses delegated UI actions and disables confirmation until selected', () => {
  const b=battle(), html=renderArtifactStatus(b,b.artifacts[0],null,{mode:'mobile'});
  assert.match(html,/data-ui="artifact-status-select" data-value="poison"/);
  assert.match(html,/data-ui="artifact-status-confirm"[^>]* disabled/);
  assert.match(html,/data-ui="artifact-status-cancel"/);
});
test('busy renderer disables all target and confirm controls', () => {
  const b=battle(), html=renderArtifactStatus(b,b.artifacts[0],'poison',{busy:true});
  assert.equal((html.match(/<button/g)||[]).length,(html.match(/ disabled/g)||[]).length);
});
test('target names, descriptions, units and IDs are escaped', () => {
  const b=battle(); Object.assign(b.artifacts[0].targets[0],{id:'<id>',name:'<毒>',unit:'<层>',description:'<script>bad</script>'});
  const html=renderArtifactStatus(b,b.artifacts[0],'<id>');
  assert.ok(!html.includes('<script>')); assert.match(html,/&lt;毒&gt;/); assert.match(html,/&lt;层&gt;/);
  assert.match(html,/data-id="&lt;id&gt;"/);
});
test('no current targets displays a clear empty state and cannot confirm', () => {
  const b=battle(); b.artifacts[0].targets=[];
  const html=renderArtifactStatus(b,b.artifacts[0],null);
  assert.match(html,/当前没有可削除的状态/);
  assert.match(html,/data-action="artifact-status-confirm"[^>]* disabled/);
});
test('mobile artifact detail opens a status selection instead of sending an untargeted action', () => {
  const detail=renderBattleDetail({battle:battle()},'artifacts');
  assert.match(detail.content,/data-ui="artifact-status-open" data-value="artifact-cleanse"/);
  assert.ok(!detail.content.includes('data-command="battle/turn"'));
});
const mobileSource=readFileSync(new URL('../web/v2/app.mjs',import.meta.url),'utf8');
const mobileActions=mobileSource.slice(mobileSource.indexOf('async function onUI('),mobileSource.indexOf("\ndocument.addEventListener('click'"));
function mobileStatusAction(result) {
  const b=battle(), a=b.artifacts[0], current={ui:{reconciling:false},state:{battle:b}}, requests=[];
  let reads=0;
  const context={...helper,store:{get:()=>current},api:{busy:false},loginRequired:false,
    artifactStatusSelection:{id:a.id,selected:'poison',snapshot:artifactStatusSnapshot(b,a)},
    closeSheet(){context.artifactStatusSelection=null;},toast(){},
    async mutate(path,body){requests.push({path,body});return result;},
    async refreshState(){reads++;a.targets[0].layers=17;a.targets[0].remainingLayers=7;a.statusToken='status-38';}};
  vm.runInNewContext(mobileActions,context);
  return {context,current,requests,get reads(){return reads;}};
}
test('mobile rejected status action reads current state before permitting a fresh selection, without replaying the mutation', async () => {
  const h=mobileStatusAction(null);
  await h.context.onUI('artifact-status-confirm','',null);
  assert.equal(h.reads,1,'a rejected stale-token action must refresh the current status snapshot');
  assert.equal(h.requests.length,1,'refresh must never replay the action');
  assert.equal(h.current.state.battle.artifacts[0].targets[0].layers,17);
  const a=h.current.state.battle.artifacts[0];
  assert.ok(artifactStatusRequest(h.current.state.battle,a,'poison',artifactStatusSnapshot(h.current.state.battle,a)));
});
test('successful mobile status action uses its response state without an extra refresh', async () => {
  const h=mobileStatusAction({ok:true});
  await h.context.onUI('artifact-status-confirm','',null);
  assert.equal(h.requests.length,1); assert.equal(h.reads,0);
});

let passed=0;
for (const item of cases) {
  try { await item.run(); console.log(`PASS ${item.name}`); passed++; }
  catch (error) { console.error(`FAIL ${item.name}: ${error.message}`); }
}
console.log(`${passed}/${cases.length} artifact status checks passed`);
if (passed!==cases.length) process.exitCode=1;
