// 秘境奖励偏好与遇前遮蔽专项冒烟：真实Edge、临时账号/目录/端口，不使用玩家服务或存档。
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
const executableInput = resolve(root, args.includes('--exe') ? args[args.indexOf('--exe') + 1] : 'build-vs-share/Release/ningzhou_web.exe');
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const output = join(root, 'build/diagnostics/dungeon-preferences-browser-20261016'); mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'run-'));
const executable = join(temporary, 'dungeon-preferences-qa.exe'); copyFileSync(executableInput, executable);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
const base = `http://127.0.0.1:${port}`;
const pause = milliseconds => new Promise(done => setTimeout(done, milliseconds));
const checks = [], timings = [], errors = [], posts = [], serverLog = [];
const report = { ok: false, executableInput, port, temporary, checks, timings, errors, posts, serverLog,
  scope: '单系偏好记忆/冻结及旧多系重置、未触发怪物遮蔽、none真实战后继续、旧冻结奖励、850px、双击和503恢复；不重复无关回归。' };
let child, exited, browser, context, page;

// 用途：记录本轮真实断言；流程：冒烟场景；返回：无，失败立即保留报告及界面。
function check(condition, message) { assert.ok(condition, message); checks.push(message); console.log(`PASS ${message}`); }
// 用途：启动专属完整服；流程：测试启动；返回：无，所有运行数据与随机端口隔离。
async function start() {
  child = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
    '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')], { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  exited = new Promise(done => child.once('exit', done));
  child.stdout.on('data', value => serverLog.push(String(value))); child.stderr.on('data', value => serverLog.push(String(value)));
  for (let attempt = 0; attempt < 200; attempt++) {
    try { if ((await fetch(base + '/api/mode')).ok) return; } catch { /* 仅等待本工具的服务。 */ }
    if (child.exitCode != null) throw new Error(`隔离服务提前退出 ${child.exitCode}`);
    await pause(50);
  }
  throw new Error('隔离服务未就绪');
}
// 用途：回收本工具进程；流程：finally；返回：无，不触碰其他端口或服务。
async function stop() {
  if (child && child.exitCode == null) { child.kill(); await Promise.race([exited, pause(5000)]); }
}

try {
  await start(); check(true, '随机端口的隔离完整服务启动');
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => {
    if (request.method() === 'POST') posts.push({ path: new URL(request.url()).pathname, body: request.postDataJSON() });
  });
  // 用途：调用临时角色的权威API；流程：夹具/结算检查；返回：成功JSON，失败保留具体调用名。
  async function api(path, body = {}) {
    await pause(90);
    const response = await context.request.post(base + '/api/' + path, { data: body });
    const result = await response.json();
    assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
    return result;
  }
  // 用途：从真实页面提交动作；流程：入场、前进及领奖；返回：响应和状态，记录交互耗时。
  async function action(locator, path, twice = false) {
    await pause(90);
    const started = performance.now();
    const pending = page.waitForResponse(response => new URL(response.url()).pathname === '/api/' + path && response.request().method() === 'POST');
    const [response] = await Promise.all([pending, twice ? locator.evaluate(button => { button.click(); button.click(); }) : locator.click()]);
    const result = await response.json();
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
    timings.push({ path, milliseconds: performance.now() - started });
    return { response, result };
  }
  // 用途：刷新到隔离秘境页面；流程：导入及偏好持久化检查；返回：无，等待导航与渲染完成。
  async function open() {
    await page.goto(base + '/?qa=' + Date.now() + '#dungeon', { waitUntil: 'domcontentloaded' });
    await page.locator('.main-tabs').waitFor();
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  }
  await api('login', { nickname: `CardQa${Date.now().toString(36).slice(-7)}`, password: 'isolated-card-preferences-qa' });
  await api('new', { name: '秘境偏好专项验收', gender: '男', portraitId: 'player-male', seed: 519104, traits: [], allocation: {} });
  const fixture = JSON.parse((await api('export')).game);
  const site = fixture.dungeons.sites.find(entry => entry.type === 'wild' && entry.revealed && !entry.cleared);
  assert.ok(site, '新角色应真实生成野外入口');
  fixture.pending = null; fixture.job = null; fixture.npcs = [];
  fixture.player.stage = fixture.player.highestStage = 12; fixture.player.lifespan = 2000;
  fixture.player.health = 100; fixture.player.location = site.location; fixture.player.stones = 10000;
  await api('import', { game: JSON.stringify(fixture) }); await open();
  const initial = (await api('state')).state;
  check(initial.dungeons.cardRewardOptions.modes.map(item => item.id).join(',') === 'all,none,selected'
    && ['metal', 'water'].every(id => initial.dungeons.cardRewardOptions.daos.some(item => item.id === id)),
  '真实API发布三种模式及金水稳定类型键');
  check(await page.locator('[data-dungeon-card-mode]').inputValue() === 'all', '新浏览器默认全部临时神通');
  await page.evaluate(() => localStorage.setItem('nz-desktop-dungeon-card-rewards', JSON.stringify({ mode: 'selected', daos: ['metal', 'water'] })));
  await open();
  check(await page.locator('[data-dungeon-card-mode]').inputValue() === 'all'
    && await page.locator('.dungeon-card-warning').innerText().then(text => text.includes('原多系奖励偏好已重置为全部')),
  '旧浏览器多系偏好重置全部并明确提示，没有默取首个派系');
  await page.locator('[data-dungeon-card-mode]').selectOption('none'); await open();
  check(await page.locator('[data-dungeon-card-mode]').inputValue() === 'none'
    && await page.locator('[data-dungeon-card-dao]:enabled').count() === 0, '不获取偏好刷新后保留且禁用派系选项');
  await page.setViewportSize({ width: 850, height: 1000 });
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '850px偏好控件与秘境目录无横向溢出');
  await page.screenshot({ path: join(temporary, 'preferences-none-850.png'), fullPage: true });
  await page.setViewportSize({ width: 1440, height: 1000 });

  const enter = () => page.locator(`[data-dungeon-site="${site.id}"] [data-action="dungeon-enter"]`);
  await page.route('**/api/dungeon/enter', route => route.fulfill({ status: 503, contentType: 'application/json',
    body: JSON.stringify({ ok: false, error: '隔离注入：请重试入境' }) }), { times: 1 });
  const frozenBeforeFailure = (await api('export')).game;
  let reply = await action(enter(), 'dungeon/enter');
  check(reply.response.status() === 503 && (await api('export')).game === frozenBeforeFailure
    && await enter().isEnabled(), '入境503不改存档且按钮恢复可重试');
  const beforeEnterPosts = posts.filter(item => item.path === '/api/dungeon/enter').length;
  reply = await action(enter(), 'dungeon/enter', true);
  let state = reply.result.state, run = state.dungeons.run;
  check(reply.response.ok() && run.phase === 'route' && run.cardRewardPreference.mode === 'none'
    && run.cardRewardPreference.daos.length === 0, '真实入场冻结none及空类型列表');
  check(posts.filter(item => item.path === '/api/dungeon/enter').length === beforeEnterPosts + 1
    && posts.findLast(item => item.path === '/api/dungeon/enter').body.cardRewardPreference.mode === 'none', '双击入场仅一次POST且携带真实浏览器偏好');
  check(await page.locator('[data-dungeon-card-frozen]').innerText().then(text => text.includes('不获取临时神通') && text.includes('途中不能更改'))
    && await page.locator('[data-dungeon-card-mode], [data-dungeon-card-dao]').count() === 0, '探索显示本次冻结偏好且不能途中修改');
  const rawEntry = JSON.parse((await api('export')).game);
  const hidden = run.nodes.filter(node => ['battle', 'combat', 'elite', 'boss'].includes(node.type) && !node.visited && node.id !== run.current);
  check(hidden.length > 0 && hidden.every(node => node.detailsRevealed === false
    && !node.encounterName && !node.encounterHint && ['斗法', '强敌', '守关者'].includes(node.name) && node.text.includes('进入后揭晓对手')),
  '未触发战斗/精英/首领API仅有通用类型风险，无怪名机制');
  const routeMarkup = await page.locator('.dungeon-route-panel').evaluate(element => element.outerHTML);
  const hiddenNames = rawEntry.dungeons.run.nodes.filter(node => hidden.some(item => item.id === node.id)).map(node => node.name);
  check(hiddenNames.every(name => !routeMarkup.includes(name)), '真实路线正文、title、aria与下一步说明没有冻结怪名');
  check(run.nodes.filter(node => node.type === 'event').every(node => node.eventName && node.eventHint), '奇遇主题和风险预告保留');
  const unchanged = (await api('export')).game; await api('state');
  check((await api('export')).game === unchanged, '奖励目录及遮蔽视图查询不推进时间或随机数');
  const first = run.nodes.find(node => node.available && node.type === 'battle'); assert.ok(first, '首层应有普通战斗');
  const originalDeckLength = run.deck.length;
  reply = await action(page.locator(`[data-dungeon-option="${first.id}"] [data-action="dungeon-choose"]`), 'dungeon/choose');
  state = reply.result.state; run = state.dungeons.run;
  const revealed = run.nodes.find(node => node.id === first.id);
  check(reply.response.ok() && state.battle && revealed.detailsRevealed && revealed.encounterName && revealed.encounterHint
    && state.battle.enemy === revealed.encounterName, '真实触发后当前怪物名称机制与实际战斗一致');
  check(await page.locator('[data-dungeon-current-preview]').innerText().then(text => text.includes(revealed.encounterName)), '当前遭遇已揭晓详情正常展示');
  await page.screenshot({ path: join(temporary, 'encounter-revealed.png'), fullPage: true });
  // 真实使用高境角色的普通手牌击败低阶入口，避免为UI流程刷随机遭遇或改战斗结算。
  for (let step = 0; state.battle && step < 45; step++) {
    const card = state.battle.hand.filter(item => item.playable && !item.requiresTarget)
      .sort((left, right) => right.damage - left.damage || right.rawDamage - left.rawDamage)[0];
    state = (await api('battle/turn', { id: card?.uid || 'end' })).state;
  }
  run = state.dungeons.run;
  check(!state.battle && run.phase === 'reward' && !run.cleared && run.rewards.length === 0
    && run.deck.length === originalDeckLength, 'none普通战真实胜利后不塞临时牌且原牌组不变');
  const legacyFixture = JSON.parse((await api('export')).game);
  await open();
  check(await page.locator('[data-dungeon-card-skipped]').innerText() === '已按偏好不提供临时神通，继续前行。'
    && await page.locator('[data-dungeon-rewards] .dungeon-reward-card').count() === 0, '空卡池明确解释并显示继续入口');
  reply = await action(page.locator('[data-dungeon-rewards] [data-action="dungeon-claim"][data-id="skip"]'), 'dungeon/claim');
  check(reply.response.ok() && reply.result.state.dungeons.run.phase === 'route', 'none真实点击继续可回到后续路线');

  // 旧在途快照使用缺省版本0，已冻结奖励不得被新的浏览器偏好追溯重算。
  delete legacyFixture.dungeons.run.cardRewardRulesVersion;
  delete legacyFixture.dungeons.run.cardRewardPreference;
  legacyFixture.dungeons.run.rewards = [{ id: 'legacy-card', kind: 'card', name: '金芒刺', desc: '旧在途已冻结临时神通',
    spell: 'gold', relic: '', manual: '', amount: 1, grade: 1 }];
  await api('import', { game: JSON.stringify(legacyFixture) }); await open();
  state = (await api('state')).state;
  check(state.dungeons.run.cardRewardPreference.mode === 'all'
    && state.dungeons.run.rewards.some(item => item.id === 'legacy-card' && item.spell === 'gold')
    && await page.locator('[data-action="dungeon-claim"][data-id="legacy-card"]').isEnabled(),
  '旧在途缺偏好字段恢复all，保留原冻结奖励且不受浏览器none追改');
  reply = await action(page.locator('[data-action="dungeon-claim"][data-id="legacy-card"]'), 'dungeon/claim');
  check(reply.response.ok() && reply.result.state.dungeons.run.deck.length === originalDeckLength + 1
    && reply.result.state.dungeons.run.phase === 'route', '旧冻结临时神通仍能真实领取并继续');

  await api('import', { game: JSON.stringify(fixture) }); await open();
  await page.locator('[data-dungeon-card-mode]').selectOption('selected');
  check(await page.locator('.dungeon-card-warning').innerText().then(text => text.includes('请选择一个')), '自选空列表明确提示，未静默退回全部');
  await page.locator('[data-dungeon-card-dao][value="metal"]').check();
  await page.locator('[data-dungeon-card-dao][value="water"]').check();
  check(await page.locator('[data-dungeon-card-dao][type="radio"]:checked').count() === 1
    && !await page.locator('[data-dungeon-card-dao][value="metal"]').isChecked()
    && await page.locator('[data-dungeon-card-dao][value="water"]').isChecked(), '真实先选金再选水只有水被选中，不能多系并选');
  await open();
  check(await page.locator('[data-dungeon-card-mode]').inputValue() === 'selected'
    && await page.locator('[data-dungeon-card-dao]:checked').evaluateAll(items => items.map(item => item.value).join(',')) === 'water',
  '指定水系在刷新后精确保留');
  await page.setViewportSize({ width: 850, height: 1000 });
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '850px自选单系控件可操作且没有横向溢出');
  await page.screenshot({ path: join(temporary, 'preferences-selected-850.png'), fullPage: true });
  await page.setViewportSize({ width: 1440, height: 1000 });
  reply = await action(enter(), 'dungeon/enter');
  run = reply.result.state.dungeons.run;
  check(reply.response.ok() && run.cardRewardPreference.mode === 'selected'
    && run.cardRewardPreference.daos.join(',') === 'water'
    && posts.findLast(item => item.path === '/api/dungeon/enter').body.cardRewardPreference.daos.join(',') === 'water', '真实入场payload与本次冻结水系一致，只有一个稳定类型键');
  const frozenBoss = reply.result.state.dungeons.sites.find(item => item.id === site.id).bossRewards;
  check(JSON.stringify(run.bossRewards) === JSON.stringify(frozenBoss), '临时神通筛选不更改永久通关主奖励');
  await open();
  check((await page.locator('[data-dungeon-card-frozen]').innerText()).includes('指定类型')
    && await page.locator('[data-dungeon-card-mode]').count() === 0, '刷新中的自选探索仍显示冻结配置');
  await page.screenshot({ path: join(temporary, 'selected-frozen-run.png'), fullPage: true });
  check(errors.length === 0, '本轮真实秘境交互没有脚本异常');
  report.ok = true;
} catch (error) {
  report.error = error.stack || error.message;
  if (page) await page.screenshot({ path: join(temporary, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally {
  if (browser) await browser.close(); await stop();
  writeFileSync(join(temporary, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ok: report.ok, checks: checks.length, port, evidence: temporary }, null, 2));
}
