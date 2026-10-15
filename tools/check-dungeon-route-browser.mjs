// 秘境真实桌面冒烟：独立EXE/端口/账号；仅导入隔离夹具，不接触玩家服务。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
// 用途：读取独立EXE位置；流程：工具启动；调用方式：参数名与缺省值；返回值：命令行指定值或缺省值。
const argument = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const output = join(root, 'build/diagnostics/dungeon-route-browser-20261015');
mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'run-'));
const executable = join(temporary, 'dungeon-route-qa.exe');
copyFileSync(resolve(root, argument('--exe', 'build-vs-share/Release/ningzhou_web.exe')), executable);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
assert.ok(![8107, 8108, 8109, 8110, 8111, 8112, 8113].includes(port));
const base = `http://127.0.0.1:${port}`;
const server = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
  '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')], { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
const checks = [], timings = [], errors = [], posts = [], serverLog = [];
server.stdout.on('data', value => serverLog.push(String(value))); server.stderr.on('data', value => serverLog.push(String(value)));
let browser, context, page, oldServer;
// 用途：记录冒烟断言；流程：本工具；调用方式：传条件及说明；返回值：无，失败抛错并保存现场。
function check(condition, message) { assert.ok(condition, message); checks.push(message); console.log(`PASS ${message}`); }
try {
  let ready = false;
  for (let attempt = 0; attempt < 200; attempt++) {
    try { ready = (await fetch(base + '/api/mode')).ok; } catch { /* 只等待本工具的新服务。 */ }
    if (ready) break;
    await new Promise(done => setTimeout(done, 50));
  }
  check(ready, '隔离完整游戏服务启动');
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST') posts.push(new URL(request.url()).pathname); });
  // 用途：对临时账号请求真实API；流程：夹具与权威状态核对；调用方式：路径与请求体；返回值：成功JSON，否则抛错。
  async function api(path, body = {}) {
    await new Promise(done => setTimeout(done, 90));
    const response = await context.request.post(base + '/api/' + path, { data: body });
    const result = await response.json();
    assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
    return result;
  }
  // 用途：从真实界面提交一次或双击动作；流程：探索与领奖；调用方式：定位器、接口路径和双击开关；返回值：后端结果。
  async function action(locator, path, twice = false) {
    await new Promise(done => setTimeout(done, 90));
    const start = performance.now();
    const pending = page.waitForResponse(response => new URL(response.url()).pathname === '/api/' + path && response.request().method() === 'POST');
    const [response] = await Promise.all([pending, twice ? locator.evaluate(button => { button.click(); button.click(); }) : locator.click()]);
    const result = await response.json();
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
    timings.push({ path, milliseconds: performance.now() - start });
    return { response, result };
  }
  // 用途：重新载入权威状态；流程：导入夹具后与刷新检查；调用方式：页签名；返回值：无，等主导航可操作。
  async function open(hash = 'dungeon') {
    await page.goto(base + '/?qa=' + Date.now() + '#' + hash, { waitUntil: 'domcontentloaded' });
    await page.locator('.main-tabs').waitFor();
  }
  await api('login', { nickname: `Dungeon${Date.now().toString(36).slice(-7)}`, password: 'isolated-dungeon-qa' });
  await api('new', { name: '秘境界面验收', gender: '男', portraitId: 'player-male', seed: 519104, traits: [], allocation: {} });
  const fixture = JSON.parse((await api('export')).game);
  const site = fixture.dungeons.sites.find(entry => entry.type === 'wild' && entry.revealed && !entry.cleared);
  assert.ok(site, '新角色必须真实生成野外入口');
  fixture.pending = null; fixture.job = null; fixture.npcs = [];
  fixture.player.stage = fixture.player.highestStage = 3; fixture.player.lifespan = 250;
  fixture.player.health = 100; fixture.player.location = site.location; fixture.player.stones = 10000;
  await api('import', { game: JSON.stringify(fixture) });
  const quoted = (await api('state')).state.dungeons.sites.find(entry => entry.id === site.id);
  check(quoted.sweep?.enabled && quoted.sweep.days === 1, '新后端真实提供高一大境界的一天扫荡报价');
  await open();
  const entry = page.locator(`[data-dungeon-site="${site.id}"]`);
  check(await entry.locator('[data-action="dungeon-enter"]').isEnabled()
    && await entry.locator('[data-action="dungeon-sweep"]').isEnabled(), '同一入口正常探索和扫荡均可使用');
  check(typeof quoted.rewardHint === 'string' && quoted.rewardHint.length > 0
    && await entry.locator('[data-dungeon-reward-hint]').innerText() === quoted.rewardHint, '目录显示后端实际入口境界奖励档');
  const preview = entry.locator('.dungeon-boss-preview');
  check(await preview.getAttribute('open') === null, '真实目录通关奖励默认折叠');
  await preview.locator('summary').click(); check(await preview.locator('li').count() >= 1, '点击可展开真实冻结通关奖励');
  await preview.locator('summary').click();
  const frozen = (await api('export')).game; await open(); await api('state');
  check((await api('export')).game === frozen, '刷新与重复状态读取不消耗秘境、不改存档');
  let reply = await action(page.locator(`[data-dungeon-site="${site.id}"] [data-action="dungeon-enter"]`), 'dungeon/enter');
  check(reply.response.ok() && reply.result.state.dungeons.run.phase === 'route', '真实正常入境保留逐层路线');
  const realRun = reply.result.state.dungeons.run;
  check(await page.locator('.dungeon-run-heading [data-dungeon-reward-hint]').innerText() === realRun.rewardHint,
    '探索头部显示后端实际奖励档，不按玩家越境重算');
  check(typeof realRun.routeSummary === 'string' && realRun.routeSummary.length > 0
    && realRun.nodes.some(node => node.encounterName && node.encounterHint), '后端生成路线摘要与怪物机制预告');
  check(await page.locator('[data-dungeon-option]').count() === realRun.nodes.filter(node => node.available).length, '页面可达路线对照与后端资格数量一致');
  await page.setViewportSize({ width: 850, height: 1000 });
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '850px秘境路线无横向溢出');
  await page.screenshot({ path: join(temporary, 'route-850.png'), fullPage: true });
  // 只在隔离存档标记首层已完成，跳过不属于此UI任务的战斗；后续事件仍用真实选择与结算。
  const eventFixture = JSON.parse((await api('export')).game);
  const fixtureNodes = eventFixture.dungeons.run.nodes;
  const first = fixtureNodes.find(node => node.row === 0
    && node.next.some(id => fixtureNodes.some(next => next.id === id && next.type === 'event')));
  assert.ok(first, '真实首层路线必须有一处连接次层奇遇');
  first.visited = true; eventFixture.dungeons.run.current = first.id; eventFixture.dungeons.run.row = 0;
  eventFixture.dungeons.run.phase = 'route'; eventFixture.dungeons.run.elapsedDays = 1;
  await api('import', { game: JSON.stringify(eventFixture) }); await open();
  const eventNode = (await api('state')).state.dungeons.run.nodes.find(node => node.available && node.type === 'event');
  assert.ok(eventNode, '受控合法路线应连接真实奇遇节点');
  check(await page.locator(`[data-dungeon-option="${eventNode.id}"]`).innerText().then(text => text.includes(eventNode.eventName) && text.includes(eventNode.eventHint)), '真实奇遇名称与风险在入场前可读');
  reply = await action(page.locator(`[data-dungeon-option="${eventNode.id}"] [data-action="dungeon-choose"]`), 'dungeon/choose');
  check(reply.response.ok() && reply.result.state.dungeons.run.phase === 'reward'
    && reply.result.state.dungeons.run.rewards.some(reward => reward.kind === 'eventChoice')
    && await page.locator('[data-dungeon-rewards] .dungeon-reward-card').count() >= 2, '真实奇遇沿用奖励阶段并展示不同选择及后端代价说明');
  await page.screenshot({ path: join(temporary, 'event-850.png'), fullPage: true });
  const eventBefore = reply.result.state.dungeons.run;
  const eventChoice = eventBefore.rewards.find(reward => reward.kind === 'eventChoice' && reward.enabled !== false && reward.healthCost > 0)
    || eventBefore.rewards.find(reward => reward.kind === 'eventChoice' && reward.enabled !== false);
  assert.ok(eventChoice);
  reply = await action(page.locator(`[data-action="dungeon-claim"][data-id="${eventChoice.id}"]`), 'dungeon/claim');
  check(reply.response.ok() && reply.result.state.dungeons.run.phase === 'route', '真实点击事件选项后完成结算并恢复路线选择');
  if (eventChoice.healthCost > 0) check(reply.result.state.dungeons.run.health === eventBefore.health - eventChoice.healthCost,
    '真实事件气血代价与显示的后端报价一致');
  await api('import', { game: JSON.stringify(fixture) }); await open();
  await page.route('**/api/dungeon/sweep', route => route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, error: '隔离注入：请重试扫荡' }) }), { times: 1 });
  const beforeError = (await api('export')).game;
  reply = await action(page.locator(`[data-dungeon-site="${site.id}"] [data-action="dungeon-sweep"]`), 'dungeon/sweep');
  check(reply.response.status() === 503 && (await api('export')).game === beforeError
    && await page.locator(`[data-dungeon-site="${site.id}"] [data-action="dungeon-sweep"]`).isEnabled(), '扫荡503后存档不变且可重试');
  const countBefore = posts.filter(path => path === '/api/dungeon/sweep').length;
  reply = await action(page.locator(`[data-dungeon-site="${site.id}"] [data-action="dungeon-sweep"]`), 'dungeon/sweep', true);
  const swept = reply.result.state.dungeons.run;
  check(reply.response.ok() && swept.cleared && swept.phase === 'reward' && swept.elapsedDays === 1 && !reply.result.state.battle,
    '真实扫荡直接进入冻结通关奖励且只累计一天');
  check(posts.filter(path => path === '/api/dungeon/sweep').length === countBefore + 1, '双击扫荡只发一次POST');
  check(swept.stones === 0 && swept.lootItems.length === 0 && swept.relics.length === 0,
    '非定向野外扫荡没有伪造沿途灵石物资遗物');
  check(await page.locator('[data-dungeon-rewards] .dungeon-reward-card').count() === swept.rewards.length
    && await page.locator('.dungeon-run-vitals [data-action="dungeon-leave"]').isDisabled(), '主奖励数量以冻结候选为准且必须先选择');
  const stones = swept.rewards.find(reward => reward.kind === 'stones'); assert.ok(stones);
  reply = await action(page.locator(`[data-action="dungeon-claim"][data-id="${stones.id}"]`), 'dungeon/claim');
  check(reply.response.ok() && reply.result.state.dungeons.run.phase === 'complete', '手选真实主奖励后进入通关结算');
  await page.screenshot({ path: join(temporary, 'sweep-complete-850.png'), fullPage: true });
  reply = await action(page.locator('.dungeon-complete [data-action="dungeon-leave"]'), 'dungeon/leave');
  check(reply.response.ok() && !reply.result.state.dungeons.run, '扫荡领奖后真实离境');
  let finished = JSON.parse((await api('export')).game);
  if (finished.clock.days === fixture.clock.days && finished.job?.remaining === 1) {
    // 真实领域事件可能暂停离境耗时；只在该状态出现时走既有继续入口，不伪造必有工作。
    check(!finished.pending && !finished.player.arts.battle, '隔离夹具的剩余一天未被待处理事件阻塞');
    await open();
    check(await page.locator('[data-dungeon-elapsed-continuation] [data-action="advance"]').getAttribute('data-days') === '1', '真实剩余耗时时目录提供继续一天');
    await action(page.locator('[data-dungeon-elapsed-continuation] [data-action="advance"]'), 'advance');
    finished = JSON.parse((await api('export')).game);
  }
  check(finished.clock.days === fixture.clock.days + 1 && !finished.job, '真实离境恰结算一天，不重复推进');
  const sameRealm = structuredClone(fixture); sameRealm.player.stage = sameRealm.player.highestStage = site.stage; sameRealm.player.lifespan = 100;
  await api('import', { game: JSON.stringify(sameRealm) }); await open();
  check(await page.locator(`[data-dungeon-site="${site.id}"] [data-action="dungeon-sweep"]`).isDisabled()
    && (await page.locator(`[data-dungeon-site="${site.id}"] .dungeon-sweep-reason`).innerText()).length > 2, '同大境界扫荡禁用且显示真实拒绝原因');
  // 旧版入口及路线必须由旧EXE真实生成；只在临时账号调整地点，不手造旧版节点。
  const oldExecutable = join(temporary, 'previous-version-qa.exe');
  copyFileSync(resolve(root, argument('--legacy-exe', 'build/diagnostics/dungeon-v2-20261015/previous-version-web.exe')), oldExecutable);
  const oldReservation = createServer(); oldReservation.listen(0, '127.0.0.1'); await once(oldReservation, 'listening');
  const oldPort = oldReservation.address().port; await new Promise(done => oldReservation.close(done));
  assert.ok(oldPort !== port && ![8107, 8108, 8109, 8110, 8111, 8112, 8113].includes(oldPort));
  const oldBase = `http://127.0.0.1:${oldPort}`;
  oldServer = spawn(oldExecutable, ['--host', '127.0.0.1', '--port', String(oldPort), '--no-open', '--web', join(root, 'web'),
    '--data', join(temporary, 'previous-data'), '--feedback', join(temporary, 'previous-feedback')], { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  oldServer.stdout.on('data', value => serverLog.push('[previous] ' + String(value)));
  oldServer.stderr.on('data', value => serverLog.push('[previous] ' + String(value)));
  let oldReady = false;
  for (let attempt = 0; attempt < 200; attempt++) {
    try { oldReady = (await fetch(oldBase + '/api/mode')).ok; } catch { /* 仅等本工具的旧版临时服务。 */ }
    if (oldReady) break;
    await new Promise(done => setTimeout(done, 50));
  }
  check(oldReady, '独立旧版临时服务启动，不连接现有玩家服务');
  const oldContext = await browser.newContext();
  // 用途：请求旧版临时服务；流程：生成真实旧档；调用方式：旧API路径与请求体；返回值：成功JSON。
  async function oldApi(path, body = {}) {
    await new Promise(done => setTimeout(done, 90));
    const response = await oldContext.request.post(oldBase + '/api/' + path, { data: body });
    const result = await response.json();
    assert.ok(response.ok() && result.ok !== false && !result.error, `previous ${path}: ${result.error || response.status()}`);
    return result;
  }
  await oldApi('login', { nickname: `Legacy${Date.now().toString(36).slice(-8)}`, password: 'isolated-previous-qa' });
  await oldApi('new', { name: '旧秘境迁移验收', gender: '男', portraitId: 'player-male', seed: 519104, traits: [], allocation: {} });
  const oldFixture = JSON.parse((await oldApi('export')).game);
  const oldSite = oldFixture.dungeons.sites.find(entry => entry.type === 'wild' && entry.revealed && !entry.cleared);
  assert.ok(oldSite);
  oldFixture.pending = null; oldFixture.job = null; oldFixture.npcs = []; oldFixture.player.location = oldSite.location;
  await oldApi('import', { game: JSON.stringify(oldFixture) }); await oldApi('dungeon/enter', { id: oldSite.id });
  const oldGame = (await oldApi('export')).game, oldRun = JSON.parse(oldGame).dungeons.run;
  check(!Object.hasOwn(oldRun, 'layoutVersion') && oldRun.nodes.length > 0, '旧版真实入境存档缺layoutVersion且包含原路线');
  await oldContext.close(); oldServer.kill(); oldServer = null;
  await api('import', { game: oldGame });
  const migrated = JSON.parse((await api('export')).game).dungeons.run;
  check(migrated.layoutVersion === 1, '新版真实导入旧run缺字段默认路线版本1');
  assert.deepEqual(migrated.nodes, oldRun.nodes);
  check(true, '旧版所有节点、名称、连线与访问状态原样保留');
  await open();
  const migratedState = (await api('state')).state.dungeons.run;
  check(await page.locator('[data-dungeon-option]').count() === migratedState.nodes.filter(node => node.available).length,
    '旧路线在新前端可显示合法可达选项');
  const oldFirst = migratedState.nodes.find(node => node.available); assert.ok(oldFirst);
  reply = await action(page.locator(`[data-dungeon-option="${oldFirst.id}"] [data-action="dungeon-choose"]`), 'dungeon/choose');
  check(reply.response.ok() && reply.result.state.dungeons.run.current === oldFirst.id && !!reply.result.state.battle,
    '旧版路线在新版可真实继续进入首层斗法');
  check(errors.length === 0, '全流程无浏览器脚本错误');
  writeFileSync(join(temporary, 'report.json'), JSON.stringify({ checks, timings, errors, serverLog }, null, 2));
  console.log(`SUMMARY ${checks.length} checks, evidence=${temporary}`);
} catch (error) {
  writeFileSync(join(temporary, 'failure.json'), JSON.stringify({ error: error.stack, checks, timings, errors, serverLog }, null, 2));
  if (page) await page.screenshot({ path: join(temporary, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally { if (browser) await browser.close(); if (oldServer) oldServer.kill(); server.kill(); }
