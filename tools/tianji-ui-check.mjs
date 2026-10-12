// Pure UI/controller contract checks. All account-shaped records here are isolated test data,
// never inserted into a server, persisted as entrants, or used by production rendering.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createTianji, renderTianji, renderTianjiBoard, renderTianjiBuild, renderTianjiReminder, tianjiCardNumbers } from '../web/tianji.mjs';
let count = 0;
function check(value, name) { assert.ok(value, name); count++; }
const build = { traits: [{ id: 'smart', name: '聪颖' }], main: { id: 'gold', name: '金虹心诀', level: 3 }, aux: [], manuals: [{ name: '金虹心诀', level: 3 }], nodes: [{ name: '金道入门' }], spells: [{ id: 'one', name: '锐金诀', dao: '金' }], gear: [{ id: 'gear', name: '试验长剑', grade: 2, slot: '武器' }], stats: { hp: 100, mana: 3, power: 90, body: 10, mind: 12, speed: 15 } };
const me = { id: 'account-a:event-1', nickname: '隔离测试甲', name: '甲', realm: '炼气初期', group: '炼气', eventId: 1, registeredYear: 50, snapshotVersion: 1, score: 0, wins: 0, draws: 0, losses: 0, attempts: 0, build };
const opponent = { ...me, id: 'account-b:event-1', nickname: '隔离测试乙', name: '乙', canChallenge: true };
const battle = { hp: 100, max: 100, shield: 3, enemy: '乙', enemyHp: 100, enemyMax: 100, enemyShield: 0, turn: 0, playerFirst: true, mana: 3, maxMana: 3, manaCap: 8, draw: 10, discard: 0, exhausted: 0, played: 0, intentName: '攻守有度', intentDamage: 10, hand: [{ uid: 'u-1', id: 'one', name: '锐金诀', dao: '金', kind: 'attack', cost: 1, damage: 12, playable: true }, { uid: 'u-2', id: 'exhaust', name: '澄心', kind: 'exhaust', cost: 1, playable: true }], removalOptions: [{ id: 'one', name: '锐金诀', copies: 3 }], artifacts: [{ id: 'artifact:gear', name: '剑气', dao: '金', cost: 1, damage: 10, playable: true }], log: ['两位真人的冻结留影进入会场。'] };
const eligible = { ok: true, phase: 'eligible', canJoin: true, window: { year: 50, eventId: 1, startYear: 50, endYear: 59, nextStartYear: 100, open: true }, entry: null, opponents: [], encounter: null, previewBuild: build };
const ready = { ...eligible, phase: 'ready', canJoin: false, entry: me, opponents: [opponent] };
const active = { ...ready, phase: 'fighting', encounter: { id: 'fight1', revision: 0, state: 'active', target: opponent, battle, log: battle.log } };
const settled = { ...active, phase: 'settled', encounter: { ...active.encounter, revision: 2, state: 'settled', result: 'win', scoreDelta: 3, log: ['甲获胜。'] } };
check(renderTianji(eligible).includes('查看配装并报名'), 'eligible signup is reviewable');
check(renderTianji({ ...eligible, phase: 'closed', canJoin: false, reason: '尚未到赛期', window: { ...eligible.window, year: 49, open: false, nextStartYear: 50 } }).includes('第 50 年开始'), 'closed window comes from server displayed year');
check(/data-tianji="join-review"[^>]*disabled/.test(renderTianji({ ...eligible, canJoin: false })), 'ineligible signup disabled');
const waiting = renderTianji({ ...ready, opponents: [] });
check(waiting.includes('等待其他真人参赛') && !waiting.includes('data-tianji="challenge-review"'), 'no opponents is honest waiting with no synthesized entrant');
check(tianjiCardNumbers({ kind: 'attack', damage: 0 }).includes('伤 0'), 'zero actual damage remains explicit in compact cards');
check(tianjiCardNumbers({ kind: 'heal', heal: 0 }, { hp: 100, max: 100 }).includes('气血已满'), 'full-health healing has explicit zero-effect reason');
check(!tianjiCardNumbers({ kind: 'heal', heal: 0 }, { hp: 90, max: 100 }).includes('气血已满'), 'zero healing does not invent a full-health reason');
check(tianjiCardNumbers({ kind: 'mana', text: '回复 2 点灵力' }) === '回复 2 点灵力', 'non-damage preview uses exact server text instead of invented values');
const rendered = renderTianji(active);
check(rendered.includes('data-tianji="play"') && rendered.includes('data-tianji="end"') && rendered.includes('data-tianji="artifact"'), 'manual human cards, end-turn and artifacts are interactive');
check(!rendered.includes('data-action="play"') && !rendered.includes('data-action="artifact-play"'), 'Tianji battle cannot invoke ordinary battle handlers');
check(rendered.includes('我的冻结配装') && rendered.includes('查看对手配装'), 'both immutable loadouts visible');
check(!renderTianji(settled).includes('data-tianji="play"') && renderTianji(settled).includes('本场积分 +3'), 'settled fight displays outcome and disables cards');
check(renderTianjiBuild(build).includes('金虹心诀') && renderTianjiBuild(build).includes('试验长剑'), 'frozen build includes manuals, spells, talents and gear');
const overkill = { ...active, encounter: { ...active.encounter, battle: { ...battle, hp: -2, enemyHp: -73 }, log: ['原始战报：伤害 339，敌方气血 -73。'] } };
const overkillActor = renderTianji(overkill); const overkillHud = renderTianji(overkill, { compact: true });
check(!overkillActor.includes('<b>-73</b>') && !overkillActor.includes('<b>-2</b>') && overkillActor.includes('<b>0</b><span>/ 100'), 'actor HP display clamps defeated combatants to zero');
check(overkillHud.includes('<b>0 / 100</b>') && !overkillHud.includes('<b>-73 / 100</b>'), 'compact HUD HP display clamps at zero');
check(overkillActor.includes('伤害 339，敌方气血 -73') && overkill.encounter.battle.enemyHp === -73, 'display clamping preserves raw combat log and state values');
const malicious = renderTianji({ ...ready, opponents: [{ ...opponent, name: '<script>evil()</script>', nickname: '<img src=x onerror=evil()>' }] });
check(!malicious.includes('<script>') && !malicious.includes('<img src=x'), 'user-authored entrant names escaped');
check(!renderTianji({ ...active, encounter: { ...active.encounter, enemyHand: [{ name: 'SECRET' }], battle: { ...battle, enemyHand: [{ name: 'SECRET' }], rng: 'SECRET' } } }).includes('SECRET'), 'hidden hand or RNG data never rendered');
const board = { ok: true, updatedAt: '2026-10-12T04:00:00Z', total: 1, list: [{ ...me, rank: 1, score: 3 }] };
check(renderTianjiBoard(board).includes('第 1 届') && renderTianjiBoard(board).includes('炼气初期'), 'global board labels event and realm');
check(renderTianjiBoard({ ...board, total: 0, list: [] }).includes('尚无真人报名记录'), 'empty board does not manufacture rankings');
check(renderTianjiReminder({ window: { open: true } }).includes('data-tab="tianji"'), 'open-window reminder points at single hub');
check(renderTianjiReminder({ window: { open: false } }) === '', 'no irrelevant closed-window reminder');
check(renderTianjiReminder({ window: { open: false }, hasEncounter: true }).includes('继续'), 'persistent battle has resume reminder outside window');

function harness(initial = eligible) {
  let current = initial, fail = null, held = null, seq = 0, settledCount = 0, focusCount = 0, scrollCount = 0;
  const requests = [], tasks = new Map(), listeners = new Map();
  const doc = { querySelector: () => ({ focus() { focusCount++; }, scrollIntoView() { scrollCount++; } }), hidden: false, activeElement: { focus() {} }, body: { append() {} }, addEventListener: (key, value) => listeners.set(key, value), removeEventListener: key => listeners.delete(key),
    createElement() { return { open: false, innerHTML: '', setAttribute() {}, addEventListener() {}, showModal() { this.open = true; }, close() { this.open = false; }, remove() {}, querySelector() { return { value: 'one' }; } }; } };
  const scheduler = { setTimeout(fn, ms) { const key = ++seq; tasks.set(key, { fn, ms }); return key; }, clearTimeout(key) { tasks.delete(key); } };
  const controller = createTianji({ doc, scheduler, onSettled: () => settledCount++, requestId: () => `test-${++seq}`, fetcher: async (path, opts) => {
    const body = opts.body ? JSON.parse(opts.body) : null;
    requests.push({ path, body });
    if (held) await held;
    if (fail && opts.method === 'POST') { const f = fail; fail = null; if (f.throw) throw new Error('Disconnected'); return { ok: false, status: f.status, json: async () => ({ ok: false, error: '状态版本已变化', code: f.code || 'conflict' }) }; }
    return { ok: true, status: 200, json: async () => path.endsWith('/board') ? board : current };
  } });
  return { controller, requests, tasks, doc, listeners, set: value => current = value, failure: value => fail = value, hold: value => held = value, get settledCount() { return settledCount; }, get focusCount() { return focusCount; }, get scrollCount() { return scrollCount; }, click: (action, attrs = {}) => controller.handleClick({ target: { closest: () => ({ disabled: false, dataset: { tianji: action, ...attrs } }) } }) };
}
const h = harness();
h.controller.setVisible(true); await h.controller.refresh();
check(h.requests.length === 1 && h.requests[0].path === '/api/tianji/status', 'hub entry fetches status once');
check(h.tasks.size === 0, 'event screen does not poll leaderboard');
await h.click('join-review');
check(h.requests.length === 1, 'opening signup review does not register');
h.set(ready); await h.click('confirm-dialog');
check(h.requests.at(-1).path === '/api/tianji/join' && h.requests.at(-1).body.requestId, 'explicit signup confirmation uses separate idempotent endpoint');
await h.click('challenge-review', { id: opponent.id }); await h.click('close-dialog');
check(h.requests.at(-1).path === '/api/tianji/join', 'cancel challenge does not start fight');
await h.click('challenge-review', { id: opponent.id }); h.set(active); await h.click('confirm-dialog');
check(h.requests.at(-1).path === '/api/tianji/challenge' && h.requests.at(-1).body.targetId === opponent.id, 'start targets exact other human entry');
await h.click('play', { id: 'u-1' });
check(h.requests.at(-1).body.id === 'u-1' && h.requests.at(-1).body.encounterId === 'fight1' && h.requests.at(-1).body.expectedRevision === 0, 'card action carries current encounter and revision');
const beforeTarget = h.requests.length;
await h.click('play', { id: 'u-2' });
check(h.requests.length === beforeTarget, 'removal card waits for target confirmation');
await h.click('confirm-dialog');
check(h.requests.at(-1).body.target === 'one', 'confirmed removal uses target id');
await h.click('artifact', { id: 'artifact:gear' });
check(h.requests.at(-1).body.id === 'artifact:gear', 'artifact action preserves engine identifier');
await h.click('end');
check(h.requests.at(-1).body.id === 'end', 'end-turn submitted to tournament endpoint');
const beforeForfeit = h.requests.length;
await h.click('forfeit'); await h.click('close-dialog');
check(h.requests.length === beforeForfeit, 'cancel forfeit preserves battle');
await h.click('forfeit'); h.set(settled); await h.click('confirm-dialog');
check(h.requests.at(-1).body.id === 'forfeit' && h.settledCount === 1, 'forfeit confirmed once and refreshes base save after settlement');
await h.click('section', { section: 'board' });
check(h.requests.at(-1).path.endsWith('/board') && h.tasks.size === 1 && [...h.tasks.values()][0].ms === 30000, 'visible board schedules exactly one 30-second refresh');
h.doc.hidden = true; h.listeners.get('visibilitychange')();
check(h.tasks.size === 0, 'hidden document cancels polling');
h.doc.hidden = false; h.listeners.get('visibilitychange')(); await new Promise(resolve => setImmediate(resolve));
check(h.tasks.size === 1, 'document return resumes single polling timer');
h.controller.setVisible(false);
check(h.tasks.size === 0, 'navigating away stops board polling');
h.controller.destroy();

const conflict = harness(active); await conflict.controller.refresh();
conflict.failure({ status: 409 }); conflict.set({ ...active, encounter: { ...active.encounter, revision: 1 } }); await conflict.click('end');
check(conflict.requests.at(-1).path.endsWith('/status') && conflict.controller.status.encounter.revision === 1, 'stale revision refreshes authoritative state');
check(conflict.requests.filter(r => r.body).length === 1, 'conflict does not automatically replay action');
check(conflict.controller.render().includes('已同步服务器进度'), 'conflict visibly explains reconciliation');
const storage = harness(active); await storage.controller.refresh(); storage.failure({ status: 503, code: 'storage' }); await storage.click('end');
check(!storage.controller.render().includes('重试上次操作'), 'confirmed storage rollback does not leave ambiguous pending lock');
await storage.click('forfeit'); storage.set(settled); await storage.click('confirm-dialog');
check(storage.requests.at(-1).body?.id === 'forfeit', 'forfeit remains available after confirmed storage failure'); storage.controller.destroy();
const mobileBattle = renderTianji(active, { compact: true });
check(mobileBattle.indexOf('class="tj-battle"') < mobileBattle.indexOf('class="tj-window"'), 'mobile active battle precedes collapsed tournament metadata');
check(mobileBattle.includes('tj-battle-hud') && mobileBattle.includes('我方气血') && mobileBattle.includes('对方气血'), 'mobile sticky HUD exposes both battle HP values');
check(mobileBattle.includes('tj-card-rules') && !/<button[^>]*>[\s\S]*?<summary/.test(mobileBattle.split('</button>')[0]), 'card rule disclosure is separate from play button');
const retry = harness(active); await retry.controller.refresh(); retry.failure({ throw: true }); await retry.click('end');
check(retry.controller.render().includes('重试上次操作'), 'uncertain request offers exact retry');
const oldId = retry.requests.find(r => r.body).body.requestId;
await retry.click('retry');
check(retry.requests.at(-1).body.requestId === oldId && retry.requests.at(-1).body.expectedRevision === 0, 'uncertain retries reuse request id and revision');
const recovered = harness(active); await recovered.controller.refresh(); recovered.failure({ throw: true }); recovered.set({ ...active, encounter: { ...active.encounter, revision: 1 } }); await recovered.click('end');
check(!recovered.controller.render().includes('重试上次操作') && recovered.controller.status.encounter.revision === 1, 'committed-but-disconnected action resumes without duplicate submission');
const busy = harness(active); await busy.controller.refresh(); let release; busy.hold(new Promise(resolve => release = resolve)); const first = busy.click('end'); await busy.click('end'); release(); await first;
check(busy.requests.filter(r => r.body).length === 1, 'rapid double-click sends one mutation');
for (const test of [conflict, retry, recovered, busy]) test.controller.destroy();
const source = readFileSync(new URL('../web/tianji.mjs', import.meta.url), 'utf8');
check(!/fetch\(['"]\/api\/battle|call\(['"]battle/.test(source), 'Tianji transport never calls normal battle route');
const css = readFileSync(new URL('../web/tianji.css', import.meta.url), 'utf8');
check(css.includes('font-size: 16px') && css.includes('min-height: 44px') && css.includes('grid-template-columns: 1fr'), 'mobile rules provide readable type, touch targets and wrapping');
// New or resumed fights become visible once; ordinary moves and board reads never force a jump.
const focusTest = harness(ready); focusTest.controller.setVisible(true); await focusTest.controller.refresh();
await focusTest.click('challenge-review', { id: opponent.id }); focusTest.set(active); await focusTest.click('confirm-dialog'); await new Promise(resolve => setTimeout(resolve, 5));
check(focusTest.scrollCount === 1 && focusTest.focusCount === 1, 'successful challenge focuses and scrolls to newly promoted battle');
await focusTest.click('play', { id: 'u-1' }); await focusTest.click('refresh'); await new Promise(resolve => setTimeout(resolve, 5));
check(focusTest.scrollCount === 1, 'playing cards and ordinary status refresh do not force repeated scrolling');
focusTest.controller.setVisible(false); focusTest.controller.setVisible(true); await focusTest.controller.refresh(); await new Promise(resolve => setTimeout(resolve, 5));
check(focusTest.scrollCount === 2, 're-entering active fight scrolls to resume point'); focusTest.controller.destroy();
const leaveTest = harness(ready); leaveTest.controller.setVisible(true); await leaveTest.controller.refresh(); await leaveTest.click('challenge-review', { id: opponent.id });
let releaseChallenge; leaveTest.hold(new Promise(resolve => releaseChallenge = resolve)); leaveTest.set(active); const lateChallenge = leaveTest.click('confirm-dialog'); leaveTest.controller.setVisible(false); releaseChallenge(); await lateChallenge; await new Promise(resolve => setTimeout(resolve, 5));
check(leaveTest.scrollCount === 0, 'challenge completed after navigation away cannot steal focus or scroll'); leaveTest.controller.destroy();

// A delayed read must never overwrite a newer successful card action.
let readCount = 0, releaseRead;
const lateRead = new Promise(resolve => releaseRead = resolve);
const race = createTianji({ doc: null, fetcher: async (path, options) => {
  if (!options.method && ++readCount > 1) await lateRead;
  return { ok: true, status: 200, json: async () => options.method ? { ...active, encounter: { ...active.encounter, revision: 3 } } : active };
} });
await race.refresh(); const oldRead = race.refresh();
await race.handleClick({ target: { closest: () => ({ dataset: { tianji: 'end' } }) } });
releaseRead(); await oldRead;
check(race.status.encounter.revision === 3, 'late read cannot overwrite newer mutation revision'); race.destroy();

// Simulate history events only, without a browser: dismissed dialog commands must not survive Back.
const savedGlobals = Object.fromEntries(['history', 'location', 'addEventListener', 'removeEventListener'].map(key => [key, globalThis[key]]));
const events = new Map(); let cursor = 0; const stack = [{}];
globalThis.location = { href: 'http://isolated.test/#tianji' };
globalThis.addEventListener = (key, callback) => events.set(key, callback);
globalThis.removeEventListener = key => events.delete(key);
globalThis.history = { get state() { return stack[cursor]; }, pushState(value) { stack.splice(++cursor); stack.push(value); }, replaceState(value) { stack[cursor] = value; }, back() { setImmediate(() => { if (cursor) --cursor; events.get('popstate')?.(); }); }, forward() { if (cursor + 1 < stack.length) ++cursor; events.get('popstate')?.(); } };
try {
  const historyTest = harness(); await historyTest.controller.refresh(); await historyTest.click('join-review');
  check(history.state.nzTianjiDialog, 'opening a dialog creates a same-page Back entry');
  history.back(); await new Promise(resolve => setImmediate(resolve)); await historyTest.click('confirm-dialog');
  check(historyTest.requests.length === 1, 'Back cancels unconfirmed signup instead of submitting it');
  history.forward();
  check(!history.state.nzTianjiDialog, 'Forward clears obsolete modal history marker');
  await historyTest.click('join-review'); await historyTest.click('close-dialog'); await new Promise(resolve => setImmediate(resolve)); await historyTest.click('join-review');
  check(history.state.nzTianjiDialog, 'repeated open-close-open remains usable');
  historyTest.controller.destroy(); await new Promise(resolve => setImmediate(resolve));
} finally {
  for (const [key, value] of Object.entries(savedGlobals)) { if (value === undefined) delete globalThis[key]; else globalThis[key] = value; }
}
const oldMatchMedia = globalThis.matchMedia; let mediaChange; const media = { matches: true, addEventListener(key, fn) { mediaChange = fn; }, removeEventListener() {} };
globalThis.matchMedia = () => media;
try {
  const responsive = harness(active); await responsive.controller.refresh();
  check(responsive.controller.render().includes('tj-battle-hud'), '390px session uses compact battle HUD');
  media.matches = false; mediaChange(); check(!responsive.controller.render().includes('tj-battle-hud'), 'same session at1440px returns to desktop layout');
  media.matches = true; mediaChange(); check(responsive.controller.render().includes('tj-battle-hud'), 'same session at320px returns to mobile without losing encounter');
  check(responsive.controller.status.encounter.id === active.encounter.id, 'viewport change preserves active encounter'); responsive.controller.destroy();
} finally { if (oldMatchMedia === undefined) delete globalThis.matchMedia; else globalThis.matchMedia = oldMatchMedia; }
console.log(`✓ 天机大比前端 ${count} 项渲染与交互契约检查通过`);
