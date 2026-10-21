import assert from 'node:assert/strict';
import { bindPillBatchControls } from '../web/pill-batch.mjs';

// Only the DOM surface used by the controller is emulated; real timers and
// deferred network results exercise request ordering rather than HTML snapshots.
function section(id) {
  const node = { dataset: { pillBatch: id }, isConnected: true, status: { textContent: '' } };
  const inputs = ['range', 'number'].map(type => ({ value: '1', disabled: true, type,
    matches: selector => selector === '[data-pill-count]', closest: () => node }));
  const buttons = ['use', 'max', 'retry'].map(kind => ({ disabled: true, hidden: kind === 'retry', textContent: '',
    hasAttribute: attribute => attribute === `data-pill-batch-${kind}`, closest: selector => selector === '[data-pill-batch]' ? node : buttons.find(button => button.hasAttribute(`data-pill-batch-${kind}`)) }));
  node.inputs = inputs; node.buttons = buttons;
  node.closest = () => null;
  node.querySelector = selector => selector === '[data-pill-quote]' ? node.status : selector === 'input[type="number"]' ? inputs[1] : buttons.find(button => button.hasAttribute(selector.slice(1, -1)));
  node.querySelectorAll = selector => selector === '[data-pill-count]' ? inputs : selector === 'button,input' ? [...buttons, ...inputs] : selector === 'button' ? buttons : buttons.slice(0, 2);
  return node;
}
const delay = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));
const a = section('A'), b = section('B'), listeners = new Map(), requests = [], actions = [];
const root = { addEventListener: (type, listener) => listeners.set(type, listener), querySelectorAll: () => [a, b], contains: () => true };
let scope = 'life-one', deferred, finishAction;
const response = count => ({ enabled: true, maxUsableCount: 10, effect: `current:${count}` });
bindPillBatchControls(root, {
  scope: () => scope, isBusy: () => false,
  requestQuote: body => { requests.push(body); return body.id === 'A' && body.count === 4 ? new Promise(resolve => { deferred = resolve; }) : Promise.resolve(response(body.count)); },
  onUse: body => { actions.push(body); return new Promise(resolve => { finishAction = resolve; }); },
});
await delay(0);
function input(node, value) { node.inputs[1].value = String(value); listeners.get('input')({ target: node.inputs[1] }); }
input(a, 2); input(b, 3);
await delay(190);
assert(requests.some(body => body.id === 'A' && body.count === 2), 'A and B keep independent debounce timers');
assert(requests.some(body => body.id === 'B' && body.count === 3), 'B submits its selected quantity');
assert.equal(a.buttons[0].disabled, false, 'A recovers from pending status');
assert.equal(b.buttons[0].disabled, false, 'B recovers from pending status');
input(a, 4); await delay(190); input(a, 5); deferred(response(4)); await delay(0);
assert(!a.status.textContent.includes('current:4'), 'a response invalidated at input time cannot render during the next debounce');
await delay(190); assert(a.status.textContent.includes('current:5'));
const click = () => ({ target: a.buttons[0], preventDefault() {}, stopPropagation() {} });
const first = listeners.get('click')(click());
await listeners.get('click')(click());
assert.equal(actions.length, 1, 'repeated clicks commit only one batch mutation');
assert.deepEqual(actions[0], { id: 'A', count: 5 }); finishAction(); await first;
const requestCount = requests.length;
input(a, 6); a.isConnected = false;
input(b, 7); scope = 'life-two';
await delay(190);
assert.equal(requests.length, requestCount, 'detached and old-scope debounce tasks do not issue new queries');

// A mutation can refresh/rebuild the workspace while its outer onUse promise
// still awaits error reconciliation. Exercise the actual DOM replacement;
// waiting for the mutation first would hide the controller's separate lock.
const recoveryListeners = new Map(), recoveryActions = [];
let mounted = [section('recovery')], resolveUse, quoteFails = false;
const recoveryRoot = { addEventListener: (type, listener) => recoveryListeners.set(type, listener), querySelectorAll: () => mounted, contains: node => mounted.some(item => item.buttons.includes(node)) };
const recoveryOptions = {
  scope: () => 'same-life', isBusy: () => false,
  requestQuote: () => quoteFails ? Promise.reject(new Error('模拟报价故障')) : Promise.resolve(response(1)),
  onUse: body => { recoveryActions.push(body); return new Promise(resolve => { resolveUse = resolve; }); },
};
const flushQuotes = async () => { await Promise.resolve(); await Promise.resolve(); };
const clickCurrent = () => ({ target: mounted[0].buttons[0], preventDefault() {}, stopPropagation() {} });
bindPillBatchControls(recoveryRoot, recoveryOptions); await flushQuotes();
const pendingUse = recoveryListeners.get('click')(clickCurrent());
assert.equal(recoveryActions.length, 1);
mounted[0].isConnected = false; mounted = [section('recovery')];
bindPillBatchControls(recoveryRoot, recoveryOptions); await flushQuotes();
assert(mounted[0].inputs.every(input => input.disabled), 'a successful quote cannot unlock replacement inputs while onUse is pending');
assert(mounted[0].buttons.every(button => button.disabled), 'all replacement actions remain visibly locked while onUse is pending');
quoteFails = true;
bindPillBatchControls(recoveryRoot, recoveryOptions); await flushQuotes();
assert.equal(mounted[0].buttons[2].hidden, false, 'quote failure exposes retry');
assert.equal(mounted[0].buttons[2].disabled, true, 'retry respects the pending mutation lock');
quoteFails = false;
resolveUse(); await pendingUse; await flushQuotes();
assert(mounted[0].inputs.every(input => !input.disabled), 'settling onUse refreshes the currently mounted inputs');
assert.equal(mounted[0].buttons[0].disabled, false, 'the replacement use button automatically recovers after reconciliation');
const recoveredUse = recoveryListeners.get('click')(clickCurrent());
assert.equal(recoveryActions.length, 2, 'the visibly recovered button really submits a second onUse');
resolveUse(); await recoveredUse;
console.log('Pill batch controller: independent quantities, stale responses, duplicate clicks, detached/scope guards and replacement-DOM reconciliation passed.');
