// Actual HTTP integration of the v2 API and pure view contracts (Node 18+).
// Run: node tools/v2-http-check.mjs
// Optional binary override: NZ_WEB_BINARY=/absolute/path/ningzhou_web
// Always starts its own server on an OS-selected port with disposable accounts,
// data, and feedback directories. Never connects to an existing server/browser.
// Edited export/import fixtures only reach year 50 and an ordinary battle quickly;
// they are NOT natural-gameplay progression or competitive anti-cheat evidence.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtemp, rm, access } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createApi, ApiError } from '../web/v2/api.mjs';
import { renderCreate } from '../web/v2/views/create.mjs';
import { renderHome } from '../web/v2/views/home.mjs';
import { renderBattle } from '../web/v2/views/battle.mjs';
import { renderTianji } from '../web/v2/views/tianji.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const binary = resolve(process.env.NZ_WEB_BINARY || join(root, 'build-v2', process.platform === 'win32' ? 'ningzhou_web.exe' : 'ningzhou_web'));
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const counts = { ordinary: 0, tianji: 0, ordinaryBattle: 0, replayed: 0, renderedCommands: 0, httpRequests: 0 };
let section = 'ordinary', lastCheck = 'server startup', lastRequestAt = 0;
function check(condition, label) {
  lastCheck = label;
  assert.ok(condition, label);
  counts[section]++;
}
function same(actual, expected, label) {
  lastCheck = label;
  // Avoid emitting actual request bodies, sessions, passwords or exported saves.
  assert.ok(JSON.stringify(actual) === JSON.stringify(expected), label);
  counts[section]++;
}

// Extract the real generated button payloads, not hand-maintained endpoint mocks.
// View helpers generate double-quoted attributes and escape precisely these five
// entities. Browser DOM/event-handler acceptance is a separate suite.
function decode(value) {
  return value.replace(/&(amp|lt|gt|quot|#39);/g, (_, key) => ({ amp: '&', lt: '<', gt: '>', quot: '"', '#39': "'" })[key]);
}
function commands(html) {
  assert.ok(!html.includes('undefined'), 'live view must not render undefined');
  return [...html.matchAll(/<button\b[^>]*>/g)].flatMap(([tag]) => {
    const attrs = Object.fromEntries([...tag.matchAll(/([\w-]+)="([^"]*)"/g)].map(([, key, value]) => [key, decode(value)]));
    if (!attrs['data-command']) return [];
    return [{ path: attrs['data-command'], body: JSON.parse(attrs['data-body']), disabled: /\sdisabled(?:\s|>|=)/.test(tag) }];
  });
}
function command(html, path, predicate = () => true) {
  const found = commands(html).find(item => item.path === path && !item.disabled && predicate(item.body));
  check(!!found, `live view exposes enabled ${path} command`);
  counts.renderedCommands++;
  return found;
}
const startButtonDisabled = html => /\sdisabled(?:\s|>|=)/.test(html.match(/<button\b[^>]*data-ui="start-life"[^>]*>/)?.[0] || 'missing disabled');
const storage = () => {
  const values = new Map();
  return { getItem: key => values.get(key) ?? null, setItem: (key, value) => values.set(key, String(value)) };
};

function client(base, nickname) {
  const cookies = new Map(), saved = storage(), requests = [], busy = [];
  let drop = null, received = null;
  const fetcher = async (relative, options) => {
    assert.ok(relative.startsWith('/api/'), 'adapter accepts only local API paths');
    const path = relative.slice(5);
    const headers = new Headers(options.headers);
    if (cookies.size) headers.set('Cookie', [...cookies].map(([key, value]) => `${key}=${value}`).join('; '));
    // Keep only gameplay command metadata in memory, never auth credentials.
    if (!['login', 'logout'].includes(path)) requests.push({ path, method: options.method, body: options.body ? JSON.parse(options.body) : null });
    // Pace this test below the unchanged server's per-IP rate limit. This is
    // not a response retry; a rejected or uncertain request reaches createApi.
    await delay(Math.max(0, 75 - (Date.now() - lastRequestAt)));
    lastRequestAt = Date.now();
    counts.httpRequests++;
    const response = await fetch(base + relative, { ...options, headers });
    const setCookies = response.headers.getSetCookie?.() || [response.headers.get('set-cookie')].filter(Boolean);
    for (const cookie of setCookies) {
      const match = /^([^=;]+)=([^;]*)/.exec(cookie);
      if (match) match[2] ? cookies.set(match[1], match[2]) : cookies.delete(match[1]);
    }
    if (drop?.path === path && response.ok) {
      const value = await response.clone().json();
      if (drop.when(value)) {
        drop = null;
        received = value;
        // The real server already committed; simulate losing its response only.
        await response.text();
        throw new TypeError('simulated response loss after real server commit');
      }
    }
    return response;
  };
  const makeApi = () => createApi({ fetcher, storage: saved, timeout: 10000, onBusy: value => busy.push(value) });
  let api = makeApi();
  return {
    nickname, requests, busy,
    get api() { return api; },
    recreate() { api = makeApi(); },
    lose(path, when = () => true) { assert.equal(drop, null); drop = { path, when }; received = null; },
    get lostResponse() { return received; },
    get dropPending() { return !!drop; },
    read: (path, body = {}, method = 'GET') => api.read(path, body, method),
    mutate: (path, body = {}, options = {}) => api.mutate(path, body, { account: nickname, ...options }),
    run: item => { assert.ok(!item.disabled, 'must not execute a disabled button'); return api.mutate(item.path, item.body, { account: nickname }); },
    login: () => api.mutate('login', { nickname, password: 'local-test-only-v2' }),
    state: async () => (await api.read('state', {}, 'GET')).state,
    status: () => api.read('tianji/status', {}, 'GET'),
    export: async () => JSON.parse((await api.read('export', {}, 'GET')).game),
  };
}
const view = (state, status, ui = {}) => renderTianji(state, ui, { tianji: status });
function latestMutation(c, path) {
  return [...c.requests].reverse().find(request => request.path === path && request.method === 'POST');
}
async function retryLost(c, path, before, projection = value => value) {
  const pending = structuredClone(c.api.pendingFor(c.nickname));
  check(pending?.path === path && /^[A-Za-z0-9_.-]{1,64}$/.test(pending.body.requestId), 'uncertain committed command retains generated request ID');
  const sent = structuredClone(latestMutation(c, path));
  c.recreate();
  same(c.api.pendingFor(c.nickname), pending, 'pending command survives API recreation');
  await c.mutate(path, { deliberatelyIgnored: true }, { retry: true });
  same(latestMutation(c, path), sent, 'retry sends identical HTTP method, endpoint and JSON body');
  same(projection(await c.status()), projection(before), 'exact replay leaves durable encounter and scoring unchanged');
  check(c.api.pendingFor(c.nickname) === null && !c.api.busy, 'successful retry clears uncertainty and mutation lock');
  counts.replayed++;
}
async function loseAndRetry(c, item, project) {
  c.lose(item.path);
  let lost = false;
  try { await c.run(item); } catch (error) { lost = error instanceof ApiError && error.uncertain; }
  check(lost && !!c.lostResponse, 'transport loss occurs after a real successful mutation');
  const committed = await c.status();
  await retryLost(c, item.path, committed, project);
  return await c.status();
}
async function tianjiFixture(c) {
  const game = await c.export();
  game.year = 49;
  game.clock.days = 49 * 360;
  game.player.age = game.clock.ageBase + 49;
  game.player.lifespan = Math.max(2000, game.player.lifespan);
  game.player.stage = 0;
  game.player.highestStage = Math.max(0, game.player.highestStage || 0);
  game.player.location = 'tianji';
  game.job = null;
  game.pending = null;
  game.player.arts.battle = null;
  game.checkpoint.enabled = false;
  game.checkpoint.snapshot = '';
  await c.mutate('import', { game: JSON.stringify(game) });
  check((await c.status()).window.year === 50, 'disposable edited import reaches displayed year 50');
}

await access(binary);
const temporary = await mkdtemp(join(tmpdir(), 'ningzhou-v2-http-'));
let server, serverError;
try {
  const reservation = createServer();
  reservation.listen(0, '127.0.0.1');
  await once(reservation, 'listening');
  const port = reservation.address().port;
  await new Promise((resolve, reject) => reservation.close(error => error ? reject(error) : resolve()));
  const base = `http://127.0.0.1:${port}`;
  server = spawn(binary, ['--host', '127.0.0.1', '--port', String(port), '--web', join(root, 'web'), '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback'), '--no-open'], { cwd: temporary, stdio: 'ignore' });
  server.once('error', error => { serverError = error; });
  let ready = false;
  for (let i = 0; i < 200; i++) {
    if (serverError || server.exitCode !== null) throw new Error('disposable HTTP server failed to start');
    try { const response = await fetch(base + '/api/whoami', { signal: AbortSignal.timeout(1000) }); await response.arrayBuffer(); ready = true; break; }
    catch { await delay(50); }
  }
  check(ready, 'fresh disposable HTTP server is ready');
  const a = client(base, 'V2HttpQA'), b = client(base, 'V2HttpQB');
  for (const [index, c] of [a, b].entries()) {
    await c.login();
    check((await c.read('state')).hasGame === false, 'new account has no fabricated starting character');
    const options = await c.read('build-options', { seed: 1100 + index }, 'POST');
    const build = { name: `前端验收${index + 1}`, gender: '女', seed: options.innate.seed, traits: [], allocation: {} };
    const valid = await c.read('build-check', build, 'POST');
    check(valid.valid === true && !startButtonDisabled(renderCreate({ build }, { buildOptions: options, buildCheck: valid })), 'server-valid build enables real create view');
    const invalid = await c.read('build-check', { traits: [], allocation: Object.fromEntries(options.stats.map(stat => [stat.id, options.maxPoints])) }, 'POST');
    check(invalid.valid === false && startButtonDisabled(renderCreate({ build }, { buildOptions: options, buildCheck: invalid })), 'server-invalid build disables real create view');
    const created = await c.mutate('new', build);
    check(created.state?.player.name === build.name, 'v2 new request creates the chosen life');
    check((await c.read('whoami')).nickname === c.nickname, 'cookie adapter isolates authenticated account');
  }
  const ordinaryInitial = await a.state();
  const beforeDays = (await a.export()).clock.days;
  const advance = command(renderHome(ordinaryInitial, {}, {}, null), 'advance', body => body.days === 1);
  const advanced = await a.run(advance);
  check((await a.export()).clock.days > beforeDays || advanced.state.pending || advanced.state.battle, 'rendered advance command changes server time or creates an interruption');
  check(advanced.report && Number.isFinite(advanced.report.daysPassed), 'advance returns report contract used by home view');
  check(renderHome(await a.state(), {}, {}, advanced.report).includes('data-ui="settings"'), 'home opens explicit slot selection instead of blind slot-one overwrite');
  check((await a.read('slots')).slots.some(slot => slot.slot === 1 && !slot.exists), 'chosen test slot is empty before saving');
  await a.mutate('save', {slot:1});
  const saved = await a.export();
  check((await a.read('slots')).slots.some(slot => slot.slot === 1 && slot.exists), 'manual save is visible in live slot listing');
  await a.mutate('new', { name: '临时替换', gender: '女', seed: 9090, traits: [], allocation: {} });
  await a.mutate('load', { slot: 1 });
  const restored = await a.export();
  same(restored.player, saved.player, 'load restores saved player');
  same(restored.clock, saved.clock, 'load restores saved world clock');
  const exported = await a.read('export');
  check(exported.format === 'ningzhou-dream' && typeof exported.game === 'string', 'export has the downloadable save envelope');
  await a.mutate('import', { game: exported.game, legacy: exported.legacy });
  same((await a.export()).player, restored.player, 'unmodified export/import preserves ordinary player state');
  check((await b.state()).player.name === '前端验收2', 'first account save/load/import does not replace second account');
  const longBefore=await b.state();
  const tenYears=command(renderHome(longBefore, {}, {}, null),'advance',body=>body.days===3600);
  same(tenYears.body,{days:3600},'ten-year UI preserves default auto-stop rather than overriding interruptions');
  const longAdvance=await b.run(tenYears);
  check(longAdvance.report.daysPassed>=0&&longAdvance.report.daysPassed<=3600,'ten-year request respects the authoritative day budget');
  check(longAdvance.report.stopReason==='done'?longAdvance.report.daysPassed===3600:longAdvance.report.stopReason==='event'?!!longAdvance.state.pending:longAdvance.report.stopReason==='battle'?!!longAdvance.state.battle:['ended','restored','error'].includes(longAdvance.report.stopReason),'ten-year action completes or stops for the returned authoritative reason');
  console.log(`v2 HTTP ordinary: ${counts.ordinary} checks passed`);

  section = 'tianji';
  await tianjiFixture(a);
  await tianjiFixture(b);
  let status = await a.status();
  check(status.canJoin && status.phase === 'eligible', 'live Tianji eligibility is returned for fixture');
  status = await loseAndRetry(a, command(view(await a.state(), status), 'tianji/join'), value => value.entry);
  const aid = status.entry.id;
  await b.run(command(view(await b.state(), await b.status()), 'tianji/join'));
  const bid = (await b.status()).entry.id;
  status = await a.status();
  check(status.opponents.some(opponent => opponent.id === bid && opponent.canChallenge), 'second account appears as a real challengeable shadow');
  const aWorld = await a.export(), bWorld = await b.export();
  await b.mutate('logout');
  status = await loseAndRetry(a, command(view(await a.state(), status), 'tianji/challenge', body => body.targetId === bid), value => value.encounter);
  check(status.encounter.state === 'active' && status.encounter.battle, 'rendered challenge starts live battle against offline shadow');
  const activeHtml = view(await a.state(), status);
  check(!commands(activeHtml).some(item => item.path === 'battle/turn'), 'Tianji view never emits ordinary battle mutation path');
  const initialActions = commands(activeHtml).filter(item => item.path === 'tianji/action');
  check(initialActions.length > 1 && initialActions.every(item => item.body.encounterId === status.encounter.id && item.body.expectedRevision === status.encounter.revision), 'all live combat buttons carry current encounter and revision');
  const pendingHtml = view(await a.state(), status, { tianjiPending: true });
  check(commands(pendingHtml).filter(item => item.path.startsWith('tianji/')).every(item => item.disabled), 'uncertain-state view disables all tournament mutations');

  function nextAction(current) {
    const all = commands(view(aWorld, current)).filter(item => item.path === 'tianji/action' && !item.disabled);
    const cards = current.encounter.battle.hand.filter(card => card.playable !== false && !card.requiresTarget && card.kind !== 'exhaust').sort((x, y) => (y.damage || 0) - (x.damage || 0));
    const chosen = cards.map(card => all.find(item => item.body.id === card.uid)).find(Boolean) || all.find(item => item.body.id === 'end');
    check(!!chosen, 'live battle exposes next legal card or end-turn command');
    counts.renderedCommands++;
    return chosen;
  }
  const first = nextAction(status);
  check(status.encounter.battle.hand.some(card => card.uid === first.body.id), 'first live combat command uses the rendered card UID');
  const revision = status.encounter.revision;
  status = await loseAndRetry(a, first, value => ({ encounter: value.encounter, entry: value.entry }));
  check(status.encounter.revision === revision + 1, 'lost-and-replayed card commits exactly one revision');

  // Complete a manual battle from live rendered cards. Lose only its terminal
  // response to verify settlement replay and score idempotency through createApi.
  a.lose('tianji/action', result => result.encounter?.state === 'settled');
  let turns = 0, terminalLost = false;
  while (status.encounter.state === 'active' && turns < 240) {
    const item = nextAction(status);
    try { await a.run(item); }
    catch (error) {
      if (!(error instanceof ApiError && error.uncertain && a.lostResponse?.encounter?.state === 'settled')) throw error;
      terminalLost = true;
    }
    status = await a.status();
    turns++;
    if (terminalLost) {
      await retryLost(a, item.path, status, value => ({ encounter: value.encounter, entry: value.entry }));
      break;
    }
  }
  check(status.encounter.state === 'settled' && terminalLost && !a.dropPending, 'manual live card battle reaches a replayed durable settlement');
  check(['win', 'loss', 'draw'].includes(status.encounter.result), 'natural battle settlement returns an engine outcome');
  check(status.entry.score === status.entry.wins * 3 + status.entry.draws && status.entry.wins + status.entry.losses + status.entry.draws === 1, 'settlement scores exactly one challenge with server rules');
  const settledCommands = commands(view(await a.state(), status));
  check(settledCommands.filter(item => item.path === 'tianji/action').every(item => item.disabled), 'settled view exposes no enabled old card action');
  check(!settledCommands.some(item => item.path === 'tianji/challenge' && item.body.targetId === bid && !item.disabled), 'settled opponent cannot be challenged again from view');
  same(await a.export(), aWorld, 'tournament preserves challenger personal world');
  await b.login();
  same(await b.export(), bWorld, 'offline shadow combat preserves opponent personal world');
  const board = await a.read('tianji/board');
  const row = board.list.find(item => item.id === aid);
  check(row && row.score === status.entry.score && row.pending === 0, 'public board reflects completed server score');
  const boardHtml = renderTianji(await a.state(), { tianjiTab: 'board' }, { tianjiBoard: board });
  check(boardHtml.includes(a.nickname) && boardHtml.includes(b.nickname), 'board view renders both real accounts');
  check(counts.replayed === 4, 'join, challenge, card action and settlement each survive exact replay');
  console.log(`v2 HTTP Tianji: ${counts.tianji} checks passed, ${turns + 1} live battle actions, ${counts.replayed} committed-response replays`);

  section = 'ordinaryBattle';
  await b.mutate('new', { name: '普通斗法验收', gender: '女', seed: 20261011, traits: [], allocation: {} });
  const fixture = await b.export();
  fixture.player.stage = fixture.player.highestStage = 6;
  fixture.player.location = 'fanghu';
  fixture.player.arts.spells = ['swordFocus', 'qiFlow', 'skim', 'aegis'];
  fixture.job = fixture.pending = fixture.player.arts.battle = null;
  fixture.checkpoint.enabled = false;
  fixture.checkpoint.snapshot = '';
  await b.mutate('import', { game: JSON.stringify(fixture) });
  await b.mutate('wild', { id: 'bandit' });
  let battleState = await b.state();
  check(!!battleState.battle, 'disposable ordinary fixture reaches a real bounty battle');
  let ordinaryCommands = commands(renderBattle(battleState));
  const card = battleState.battle.hand.find(item => item.playable !== false && !item.requiresTarget && item.kind !== 'exhaust' && ordinaryCommands.some(button => button.body.id === item.uid && !button.disabled));
  check(!!card, 'ordinary live hand exposes playable unique card UID');
  const cardAction = command(renderBattle(battleState), 'battle/turn', body => body.id === card.uid);
  const mana = battleState.battle.mana;
  await b.run(cardAction);
  battleState = await b.state();
  check(!battleState.battle || battleState.battle.mana !== mana || !battleState.battle.hand.some(item => item.uid === card.uid), 'rendered ordinary card command changes live battle');
  if (battleState.battle) {
    await b.run(command(renderBattle(battleState), 'battle/turn', body => body.id === 'end'));
    battleState = await b.state();
    if (battleState.battle) await b.run(command(renderBattle(battleState), 'battle/turn', body => body.id === 'escape'));
  }
  check(!(await b.state()).battle, 'ordinary rendered exit command ends the bounty battle');
  for (const c of [a, b]) check(!c.api.busy && c.busy.every((value, index) => value === (index % 2 === 0)) && c.busy.length % 2 === 0, 'all real mutations balance busy/unlock transitions');
  console.log(`v2 HTTP ordinary battle: ${counts.ordinaryBattle} checks passed`);
  console.log(JSON.stringify({ suite: 'v2-http-check', passed: counts.ordinary + counts.tianji + counts.ordinaryBattle, failed: 0, ...counts, isolation: 'fresh temporary server and two disposable accounts; edited fixtures, not natural year-50 progression' }));
} catch (error) {
  console.error(JSON.stringify({ suite: 'v2-http-check', failed: 1, section, lastCheck, error: error.message, status: error instanceof ApiError ? error.status : undefined }));
  process.exitCode = 1;
} finally {
  if (server && server.exitCode === null && !serverError) {
    const exited = once(server, 'exit');
    server.kill('SIGTERM');
    const timer = setTimeout(() => server.kill('SIGKILL'), 5000);
    await exited;
    clearTimeout(timer);
  }
  await rm(temporary, { recursive: true, force: true });
}
