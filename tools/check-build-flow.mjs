// Deterministic async regressions against the real classic and mobile creation handlers.
// Run: node tools/check-build-flow.mjs (no server, browser or save mutation).
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { createStore } from '../web/v2/store.mjs';
import { renderCreate } from '../web/v2/views/create.mjs';
import { renderCharacter } from '../web/v2/views/character.mjs';
import * as buildPreview from '../web/build-preview.mjs';

const source = readFileSync(new URL('../web/v2/app.mjs', import.meta.url), 'utf8');
const functions = source.slice(source.indexOf('function nextBuildSeed('), source.indexOf('async function openSettings('));
const handlers = source.slice(source.indexOf('async function onUI('), source.indexOf("\ndocument.addEventListener('click'"));
const deferred = () => { let resolve, reject; const promise = new Promise((a, b) => { resolve = a; reject = b; }); return { promise, resolve, reject }; };
const options = seed => ({ innate: { seed, apt: 36, insight: 48 }, budget: 10, stats: [], talents: [],
  portraits: [{ id: 'player-female', gender: '女' }, { id: 'player-male', gender: '男' }] });
const checked = body => ({ valid: true, remain: 10, preview: { seed: body.seed,
  player: { name: body.name, portrait: body.portraitId }, arts: {}, gears: [] } });
function harness() {
  const store = createStore(), calls = [], mutations = [], errors = [];
  store.reset({ nickname: '测试' });
  const api = { busy: false, read: async (path, body) => {
    calls.push({ path, body });
    return path === 'build-options' ? options(body.seed) : checked(body);
  } };
  const context = { ...buildPreview, store, api, clearTimeout, closeSheet() {},
    account: () => store.get().account?.nickname || '',
    root: { querySelector: () => null }, router: { go() {} }, window: { scrollTo() {} },
    document: { querySelector: () => null }, handleError: async e => errors.push(e),
    mutate: async (path, body) => { mutations.push({ path, body }); return { ok: true }; },
  };
  vm.runInNewContext(`let buildTimer, buildSerial = 0;${functions}\n${handlers}\nthis.flow = { openBuild, checkBuild, editBuild, onUI, nextBuildSeed };`, context);
  return { ...context.flow, store, api, calls, mutations, errors };
}
let passed = 0;
async function test(name, run) { await run(); passed++; console.log('PASS ' + name); }

await test('initial and rerolled seeds are nonzero and lockable', async () => {
  const h = harness(); await h.openBuild();
  const initial = h.store.get().ui.build.seed;
  assert.ok(Number.isInteger(initial) && initial > 0);
  await h.onUI('build-reroll');
  assert.notEqual(h.store.get().ui.build.seed, initial);
  await h.onUI('build-lock');
  assert.ok(h.store.get().ui.buildPlan);
  assert.ok(h.calls.filter(c => c.path === 'build-options').every(c => c.body.seed > 0));
});
await test('cancelled opening cannot overwrite a later opening or its options', async () => {
  const h = harness(), waiting = [];
  h.api.read = (path, body) => { if (path !== 'build-options') return Promise.resolve(checked(body));
    const pending = deferred(); waiting.push({ ...pending, seed: body.seed }); return pending.promise; };
  const first = h.openBuild();
  assert.match(renderCreate(h.store.get().ui, h.store.get().data), /data-ui="cancel-build"/);
  await h.onUI('cancel-build');
  const second = h.openBuild();
  waiting[1].resolve(options(waiting[1].seed)); await second;
  const accepted = h.store.get();
  waiting[0].resolve(options(waiting[0].seed)); await first;
  assert.equal(h.store.get().ui.build.seed, accepted.ui.build.seed);
  assert.equal(h.store.get().data.buildOptions, accepted.data.buildOptions);
});
await test('failed options can be retried, and reroll blocks edits until matching options arrive', async () => {
  const h = harness();
  h.api.read = async () => { throw new Error('temporary failure'); };
  await h.openBuild();
  assert.equal(h.store.get().ui.buildChecking, false);
  assert.match(renderCreate(h.store.get().ui, h.store.get().data), /data-ui="build-options-retry"/);
  const gate = deferred();
  h.api.read = (path, body) => path === 'build-options' ? gate.promise : Promise.resolve(checked(body));
  const retry = h.onUI('build-options-retry');
  assert.equal(h.editBuild({ gender: '男' }), false);
  gate.resolve(options(h.store.get().ui.build.seed)); await retry;
  assert.equal(h.store.get().ui.buildOptionsLoading, false);
  assert.equal(h.store.get().data.buildCheck.valid, true);
});
await test('out-of-order validation cannot restore an old draft', async () => {
  const h = harness(); await h.openBuild();
  const waiting = [];
  h.api.read = (path, body) => { const gate = deferred(); waiting.push({ ...gate, body }); return gate.promise; };
  const first = h.checkBuild();
  h.editBuild({ name: '新道号' });
  const second = h.checkBuild();
  waiting[1].resolve(checked(waiting[1].body)); await second;
  waiting[0].resolve(checked(waiting[0].body)); assert.equal(await first, null);
  assert.equal(h.store.get().data.buildCheck.preview.player.name, '新道号');
});
await test('unlock during final validation cancels character creation', async () => {
  const h = harness(); await h.openBuild(); await h.onUI('build-lock');
  const gate = deferred(), build = h.store.get().ui.build;
  h.api.read = () => gate.promise;
  const start = h.onUI('start-life');
  await h.onUI('build-unlock');
  gate.resolve(checked(build)); await start;
  assert.equal(h.mutations.length, 0);
  assert.equal(h.store.get().ui.buildPlan, null);
  assert.equal(h.store.get().ui.buildChecking, false);
});
await test('creation sends the complete immutable locked request after final validation', async () => {
  const h = harness(); await h.openBuild();
  h.editBuild({ name: '选定角色', gender: '男', portraitId: 'player-male', traits: ['alchemy'], allocation: { body: 2 } });
  await h.checkBuild(); await h.onUI('build-lock');
  const plan = h.store.get().ui.buildPlan;
  assert.ok(Object.isFrozen(plan.request) && Object.isFrozen(plan.request.traits) && Object.isFrozen(plan.request.allocation));
  await h.onUI('start-life');
  assert.equal(h.mutations.length, 1);
  assert.equal(h.mutations[0].path, 'new');
  assert.equal(h.mutations[0].body, plan.request);
});
await test('busy creation cannot be unlocked, cancelled or submitted again', async () => {
  const h = harness(); await h.openBuild(); await h.onUI('build-lock');
  const plan = h.store.get().ui.buildPlan;
  h.api.busy = true;
  await h.onUI('build-unlock'); await h.onUI('cancel-build'); await h.onUI('start-life');
  assert.equal(h.store.get().ui.buildPlan, plan);
  assert.equal(h.mutations.length, 0);
});


// Minimal DOM and clock adapters preserve actual button disabled states, delegated
// events and AbortSignal lifetimes while executing the full classic module.
class PanelHost {
  constructor() { this.nodes = new Map(); this.listeners = new Map(); this.binds = 0; this.html = ''; }
  set innerHTML(value) { this.html = value; this.nodes.clear(); this.index(value); }
  get innerHTML() { return this.html; }
  index(html) {
    for (const [, tag, attrs] of html.matchAll(/<([a-z][a-z0-9]*)\b([^>]*)>/gi)) {
      const id = attrs.match(/\bid="([^"]+)"/)?.[1];
      if (!id && !/class="[^"]*\bbuild-panel\b/.test(attrs)) continue;
      const host = this;
      const node = { id: id || '', tagName: tag.toUpperCase(), disabled: /(?:^|\s)disabled(?:\s|$|=)/.test(attrs),
        dataset: {}, scrollTop: 0, html: '', value: attrs.match(/\bvalue="([^"]*)"/)?.[1] || '',
        set innerHTML(value) { this.html = value; host.index(value); }, get innerHTML() { return this.html; },
        focus() {}, scrollIntoView() {},
        closest(selector) { return selector === 'button:disabled' ? this.tagName === 'BUTTON' && this.disabled ? this : null
          : selector === '#' + this.id ? this : null; },
      };
      this.nodes.set(id ? '#' + id : '.build-panel', node);
    }
  }
  querySelector(selector) { return this.nodes.get(selector) || null; }
  contains(node) { return [...this.nodes.values()].includes(node); }
  addEventListener(type, listener, { signal } = {}) {
    this.binds++;
    const entry = { listener, active: true };
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type).push(entry);
    signal?.addEventListener('abort', () => { entry.active = false; });
  }
  async click(id) {
    const target = this.querySelector('#' + id);
    assert.ok(target, 'real rendered button exists: ' + id);
    await Promise.all((this.listeners.get('click') || []).filter(entry => entry.active).map(entry => entry.listener({ target })));
  }
}
function classicHarness() {
  const host = new PanelHost(), calls = [], starts = [], timers = new Map();
  let timerId = 0;
  const network = { run: async (path, body) => ({ ok: true, json: async () => path.endsWith('build-options') ? options(body.seed) : checked(body) }) };
  const context = { ...buildPreview, AbortController, document: { activeElement: null },
    setTimeout: fn => { const id = ++timerId; timers.set(id, fn); return id; }, clearTimeout: id => timers.delete(id),
    fetch: (path, request) => { const body = JSON.parse(request.body); calls.push({ path, body }); return network.run(path, body); },
  };
  const classic = readFileSync(new URL('../web/build-panel.mjs', import.meta.url), 'utf8')
    .replace(/import\s*\{[\s\S]*?\}\s*from\s*['"]\.\/build-preview\.mjs['"];?/, '')
    .replace('export function createBuildPanel(', 'function createBuildPanel(');
  vm.runInNewContext(classic + '\nthis.createBuildPanel = createBuildPanel;', context);
  const panel = context.createBuildPanel({ onStart: async request => starts.push(request), onCancel: () => panel.unmount() });
  const flush = async () => { const pending = [...timers.values()]; timers.clear(); for (const run of pending) await run(); };
  return { host, calls, starts, network, panel, flush, timers };
}
await test('classic initial seed is nonzero, validation restores reroll, and locked creation uses the preview seed', async () => {
  const h = classicHarness(); await h.panel.mount(h.host); await h.flush();
  const seed = h.calls[0].body.seed;
  assert.ok(Number.isInteger(seed) && seed > 0);
  assert.equal(h.host.querySelector('#bp-reroll').disabled, false);
  await h.host.click('bp-lock'); await h.host.click('bp-start');
  assert.equal(h.starts.length, 1);
  assert.equal(h.starts[0].seed, seed);
  assert.equal(h.starts[0].portraitId, 'player-female');
  h.panel.unmount();
});
await test('classic pending reroll prevents a second request and restores the reroll button on completion', async () => {
  const h = classicHarness(); await h.panel.mount(h.host); await h.flush();
  const gate = deferred(), prior = h.network.run;
  let requestedSeed;
  h.network.run = (path, body) => { if (path.endsWith('build-options')) { requestedSeed = body.seed; return gate.promise; } return prior(path, body); };
  const first = h.host.click('bp-reroll');
  assert.equal(h.host.querySelector('#bp-reroll').disabled, true);
  await h.host.click('bp-reroll');
  assert.equal(h.calls.filter(c => c.path.endsWith('build-options')).length, 2);
  gate.resolve({ ok: true, json: async () => options(requestedSeed) });
  await first; await h.flush();
  assert.equal(h.host.querySelector('#bp-reroll').disabled, false);
  assert.match(h.host.querySelector('#bp-innate').innerHTML, new RegExp('>' + requestedSeed + '<'));
  await h.host.click('bp-lock'); await h.host.click('bp-start');
  assert.equal(h.starts[0].seed, requestedSeed);
  h.panel.unmount();
});
await test('classic HTTP failure and malformed options remain retryable instead of becoming option data', async () => {
  for (const mode of ['http', 'malformed', 'network']) {
    const h = classicHarness(), success = h.network.run;
    h.network.run = async (path, body) => {
      if (mode === 'network') throw new Error('offline');
      return mode === 'http' ? { ok: false, json: async () => options(body.seed) }
        : { ok: true, json: async () => ({ error: 'invalid response' }) };
    };
    await h.panel.mount(h.host); await h.flush();
    assert.match(h.host.innerHTML, /无法读取建号数据/);
    assert.ok(h.host.querySelector('#bp-retry'));
    assert.equal(h.calls.filter(c => c.path.endsWith('build-check')).length, 0);
    h.network.run = success;
    await h.host.click('bp-retry'); await h.flush();
    assert.equal(h.host.querySelector('#bp-lock').disabled, false);
    assert.equal(h.host.querySelector('#bp-reroll').disabled, false);
    h.panel.unmount();
  }
});
await test('classic late retry response cannot replace the latest accepted options', async () => {
  const h = classicHarness();
  h.network.run = async () => ({ ok: false, json: async () => ({ error: 'offline' }) });
  await h.panel.mount(h.host);
  const waiting = [];
  h.network.run = (path, body) => {
    if (path.endsWith('build-check')) return Promise.resolve({ ok: true, json: async () => checked(body) });
    const gate = deferred(); waiting.push({ ...gate, seed: body.seed }); return gate.promise;
  };
  const first = h.host.click('bp-retry'), second = h.host.click('bp-retry');
  const fresh = options(waiting[1].seed); fresh.innate.apt = 101;
  waiting[1].resolve({ ok: true, json: async () => fresh }); await second;
  const stale = options(waiting[0].seed); stale.innate.apt = 17;
  waiting[0].resolve({ ok: true, json: async () => stale }); await first; await h.flush();
  assert.match(h.host.querySelector('#bp-innate').innerHTML, /<strong>101<\/strong>/);
  assert.doesNotMatch(h.host.querySelector('#bp-innate').innerHTML, /<strong>17<\/strong>/);
  h.panel.unmount();
});
await test('classic unmount during initial load prevents binding or rendering after the response', async () => {
  const h = classicHarness(), gate = deferred();
  let seed;
  h.network.run = (path, body) => { seed = body.seed; return gate.promise; };
  const mount = h.panel.mount(h.host);
  h.panel.unmount();
  gate.resolve({ ok: true, json: async () => options(seed) });
  await mount; await h.flush();
  assert.equal(h.host.innerHTML, '');
  assert.equal(h.host.binds, 0);
  assert.equal(h.calls.length, 1);
});
await test('classic unmount during reroll leaves the host empty and does not bind or validate again', async () => {
  const h = classicHarness(); await h.panel.mount(h.host); await h.flush();
  const gate = deferred(), binds = h.host.binds, checks = h.calls.filter(c => c.path.endsWith('build-check')).length;
  let seed;
  h.network.run = (path, body) => { seed = body.seed; return gate.promise; };
  const reroll = h.host.click('bp-reroll');
  h.panel.unmount();
  gate.resolve({ ok: true, json: async () => options(seed) });
  await reroll; await h.flush();
  assert.equal(h.host.innerHTML, '');
  assert.equal(h.host.binds, binds);
  assert.equal(h.calls.filter(c => c.path.endsWith('build-check')).length, checks);
});
await test('mobile character detail shows the authoritative selected portrait after creation', async () => {
  const player = { name: '已选角色', gender: '女', sect: '', portrait: '/assets/portraits/sect-lihuo-female.webp', realmName: '炼气初期' };
  const html = renderCharacter({ player });
  assert.match(html, /<header class="character-profile"><img src="\/assets\/portraits\/sect-lihuo-female\.webp"/);
  assert.match(html, /width="72" height="96" alt="已选角色立绘"/);
  assert.ok(html.indexOf('character-profile') < html.indexOf('data-ui="character-tab"'), 'selected portrait is in the profile header');
  const unsafe = renderCharacter({ player: { ...player, portrait: 'https://example.invalid/not-a-game-portrait.webp' } });
  assert.ok(!unsafe.includes('<img'), 'unrecognized sources do not become portrait requests');
});
console.log(`Character creation flow checks: ${passed} passed.`);
