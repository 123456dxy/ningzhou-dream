import assert from 'node:assert/strict';
import { mfRenderZhuji, mfBindZhuji, mfResetZhuji } from '../web/zhuji.mjs';

const Base = { available: true, revision: 0, status: 'unstarted', canStart: true, cost: 100, days: 90, pillAvailable: 6, coreAvailableCount: 1 };
const Preparation = mfRenderZhuji(Base);
assert.match(Preparation, /五行凝练/);
assert.match(Preparation, /周天冲穴/);
assert.match(Preparation, /90 天/);
assert.doesNotMatch(Preparation, /data-zhuji-talent|稳灵阵/);

const Active = { ...Base, revision: 1, route: 'elements', status: 'active', score: 101, round: 2, actionPoints: 3,
  carryLimit: 8, resources: { gold: 3, wood: 3, water: 3, fire: 1, earth: 0 }, projectedReward: { baseHpGain: 1, battleHpEquivalent: 2.4 },
  choices: [{ action: { kind: 'seal' }, quote: { enabled: true } }], history: ['<script>bad</script>'] };
const ElementHtml = mfRenderZhuji(Active);
assert.match(ElementHtml, /data-zhuji-keep="gold"/);
assert.match(ElementHtml, /跨轮最多保留8气/);
assert.match(ElementHtml, /基础凝液/);
assert.match(ElementHtml, /<details class="zhuji-rules"><summary>凝练规则<\/summary>/);
assert.match(ElementHtml, /&lt;script&gt;/);
assert.doesNotMatch(ElementHtml, /<script>bad/);

const Network = { ...Base, revision: 2, route: 'meridians', status: 'active', score: 0, qi: 115, gates: [],
  nodes: [{ id: 'start', label: '丹田', x: 50, y: 70, isCurrent: true, revealed: true, opened: true },
    { id: 'zhiyang', label: '至阳', x: 52, y: 31, isAdjacent: true, revealed: true }],
  edges: [{ from: 'start', to: 'zhiyang', locked: false }], choices: [], history: [] };
const NetworkHtml = mfRenderZhuji(Network);
assert.match(NetworkHtml, /data-zhuji-node="zhiyang"/);
assert.match(NetworkHtml, /M50 70L52 31/);
assert.match(NetworkHtml, /<details class="zhuji-rules"><summary>冲穴规则<\/summary>/);
assert.doesNotMatch(NetworkHtml, /返程.*[0-9]|returnCost/);

const Result = mfRenderZhuji({ ...Base, revision: 3, available: false, status: 'success', route: 'elements', score: 120,
  reward: { baseHpGain: 20, battleHpEquivalent: 48 }, lifeboundKind: 'innate', lifeboundSpell: '', lifeboundChoices: [{ id: 'flame', name: '焰火诀' }] });
assert.match(Result, /data-action="goto-lifebound"/);
assert.match(Result, /首张副本/);
assert.match(Result, /神通界面选择或更换/);
assert.doesNotMatch(Result, /data-zhuji-command="bind"|永久绑定|不能更改/);
assert.doesNotMatch(Result, /data-zhuji-command="start"/);

const ActionView = { ...Active, revision: 10, resources: { gold: 0, wood: 3, water: 0, fire: 0, earth: 0 },
  choices: [{ action: { kind: 'basic', spend: ['wood', 'wood'] }, quote: { enabled: true } }] };
mfRenderZhuji(ActionView);
let ResolveQuote;
const Requests = [];
const Host = { isConnected: true, innerHTML: '', contains: () => true };
mfBindZhuji(Host, { request: Body => { Requests.push(Body); return new Promise(Resolve => { ResolveQuote = Resolve; }); } });
const Button = { disabled: false, dataset: { zhujiElement: 'wood' }, hasAttribute: () => false };
const Event = { target: { closest: () => Button } };
const First = Host.onclick(Event);
await Host.onclick(Event);
assert.equal(Requests.length, 1, '连点只发一次报价');
mfRenderZhuji({ ...ActionView, revision: 11, score: 6 });
ResolveQuote({ quote: { enabled: true, effectText: '旧报价' }, revision: 10 });
await First;
assert.equal(Requests.length, 1, '过期报价不提交动作');
assert.match(Host.innerHTML, /6<small>/, '过期响应保持新快照分数');
let Attempts = 0;
mfBindZhuji(Host, { request: async () => {
  ++Attempts;
  if (Attempts === 1) throw new Error('隔离连接失败');
  return { quote: { enabled: false, reason: '可恢复的规则拒绝' }, revision: 11 };
} });
await Host.onclick(Event);
assert.match(Host.innerHTML, /隔离连接失败/, '错误后显示可理解反馈');
await Host.onclick(Event);
assert.equal(Attempts, 2, '错误后busy释放，下一次选择仍可报价');
assert.match(Host.innerHTML, /可恢复的规则拒绝/, '错误后恢复正常报价');
const MakeHost = () => ({ isConnected: true, innerHTML: '', contains: () => true,
  querySelector: Selector => Selector.includes('pills') ? { value: '1' } : { checked: false } });
const Click = (Dataset = {}, Attributes = []) => ({ target: { closest: () => ({ disabled: false, dataset: Dataset,
  hasAttribute: Name => Attributes.includes(Name) }) } });
const AccountA = MakeHost();
mfRenderZhuji({ ...Base, lifeId: 'life-a' });
mfBindZhuji(AccountA, { scope: 'account-a:life-a', request: async Body => {
  if (!Body) return { zhuji: { ...Base, lifeId: 'life-a' } };
  throw new Error('账号A入关待确认');
} });
await AccountA.onclick(Click({ zhujiCommand: 'start' }));
await AccountA.onclick(Click({}, ['data-zhuji-confirm']));
assert.match(AccountA.innerHTML, /data-zhuji-retry/, '同账号失败保留同一动作重试');
const AccountB = MakeHost();
AccountB.innerHTML = mfRenderZhuji({ ...Base, lifeId: 'life-a' });
mfBindZhuji(AccountB, { scope: 'account-b:life-a', request: async Body => {
  if (!Body) return { zhuji: { ...Base, lifeId: 'life-a' } };
  throw new Error('账号B入关待确认');
} });
assert.doesNotMatch(AccountB.innerHTML, /data-zhuji-retry|账号A入关待确认/, '即使导入相同lifeId，切换账号也清除旧待重试动作和错误');
await AccountB.onclick(Click({ zhujiCommand: 'start' }));
await AccountB.onclick(Click({}, ['data-zhuji-confirm']));
assert.match(AccountB.innerHTML, /data-zhuji-retry/, '同账号结果未确认仍可重试');
mfResetZhuji();
AccountB.innerHTML = mfRenderZhuji({ ...Base, lifeId: 'life-a' });
mfBindZhuji(AccountB, { scope: 'account-b:life-a' });
assert.doesNotMatch(AccountB.innerHTML, /data-zhuji-retry|账号B入关待确认|role="dialog"/, '显式重置清理相同人生读档后的旧pending/error/confirm');

mfResetZhuji();
let ResolveOldCommit, ResolveNewQuote, OldStateCalls = 0;
const OldHost = MakeHost();
mfRenderZhuji({ ...Base, lifeId: 'rollback-life' });
mfBindZhuji(OldHost, { scope: 'account:rollback-life',
  request: () => new Promise(Resolve => { ResolveOldCommit = Resolve; }), onState: () => ++OldStateCalls });
await OldHost.onclick(Click({ zhujiCommand: 'start' }));
const OldCommit = OldHost.onclick(Click({}, ['data-zhuji-confirm']));
mfResetZhuji(); // Loading a prior save can retain both account and lifeId.
const NewHost = MakeHost();
const CurrentView = { ...ActionView, lifeId: 'rollback-life' };
mfRenderZhuji(CurrentView);
mfBindZhuji(NewHost, { scope: 'account:rollback-life',
  request: () => new Promise(Resolve => { ResolveNewQuote = Resolve; }) });
const NewQuote = NewHost.onclick(Event);
ResolveOldCommit({ zhuji: { ...Base, lifeId: 'rollback-life', revision: 1, status: 'preparing' },
  state: { zhuji: { ...Base, lifeId: 'rollback-life', revision: 1, status: 'preparing' } } });
await OldCommit;
assert.equal(OldStateCalls, 0, '同人生读档后旧响应不发布onState');
assert.match(NewHost.innerHTML, /aria-busy="true"/, '旧请求finally不能释放新请求busy');
assert.doesNotMatch(NewHost.innerHTML, /正在完成90天/, '旧准备快照不能覆盖新局');
ResolveNewQuote({ quote: { enabled: false, reason: '新局报价有效' }, revision: CurrentView.revision });
await NewQuote;
assert.match(NewHost.innerHTML, /新局报价有效/);
assert.match(NewHost.innerHTML, /aria-busy="false"/);
mfResetZhuji();
let TransportBusy = true;
const LoadHost = MakeHost();
mfRenderZhuji({ ...Base, lifeId: 'loaded-life' });
mfBindZhuji(LoadHost, { scope: 'account:loaded-life', isBusy: () => TransportBusy });
TransportBusy = false;
await Promise.resolve();
assert.match(LoadHost.innerHTML, /aria-busy="false"/, '主app读档在finally释放transport锁后，筑基按钮恢复可用');
console.log('筑基前端定向契约检查通过（双路线、交互恢复、账号人生隔离、同世回档和旧异步响应）。');
