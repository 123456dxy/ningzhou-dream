// 丹毒统一规则专项冒烟；真实Edge、随机端口及临时角色，不读取玩家存档或复用运行服务。
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
const output = join(root, 'build/diagnostics/pill-poison-browser-20261016'); mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'run-'));
const executable = join(temporary, 'pill-poison-qa.exe'); copyFileSync(executableInput, executable);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
const base = `http://127.0.0.1:${port}`;
const checks = [], timings = [], errors = [], serverLog = [], catalogueExamples = [];
const report = { ok: false, executableInput, port, temporary, checks, timings, errors, serverLog, catalogueExamples,
  scope: '目录分类、低高境预览、实际点击服丹、阈值、正规保存及进程重启；不重复战斗和绿瓶等无关回归。' };
const poisonByKind = { heal: 1, xp: 1, lure: 1, repel: 1, dao: 2, points: 2, apt: 2, insight: 2,
  body: 2, heart: 2, mind: 2, speed: 2, combat: 4, breakthrough: 4, life: 4, detox: 0 };
const staleHint = /越阶服用额外增加2丹毒|额外获得30丹毒|境界不足[^；。]*丹毒\+30/;
const nickname = `PoisonQa${Date.now().toString(36).slice(-7)}`, password = 'isolated-poison-qa';
const pause = milliseconds => new Promise(done => setTimeout(done, milliseconds));
let child, exited, browser, context, page;
// 用途：记录真实验证结果；流程：本工具各场景；返回：无，失败立即停止并保留证据。
function check(condition, message) { assert.ok(condition, message); checks.push(message); console.log(`PASS ${message}`); }
// 用途：启动本工具拥有的完整服务；流程：首次运行及持久化重启；返回：无，数据与端口均隔离。
async function start() {
  child = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
    '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')], { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  exited = new Promise(done => child.once('exit', done));
  child.stdout.on('data', chunk => serverLog.push(String(chunk))); child.stderr.on('data', chunk => serverLog.push(String(chunk)));
  for (let attempt = 0; attempt < 200; attempt++) {
    try { if ((await fetch(base + '/api/mode')).ok) return; } catch { /* 等待本工具的服务。 */ }
    if (child.exitCode != null) throw new Error(`隔离服务提前退出 ${child.exitCode}`);
    await pause(50);
  }
  throw new Error('隔离服务未就绪');
}
// 用途：只停止本工具创建的进程；流程：重启验证与finally；返回：无，不操作其他监听进程。
async function stop() {
  if (child && child.exitCode == null) { child.kill(); await Promise.race([exited, pause(5000)]); }
}
try {
  await start(); check(true, '隔离完整服务启动');
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  // 用途：调用临时角色的真实API；流程：夹具导入与动作核对；返回：响应和JSON，显式拒绝场景由调用方检查。
  async function request(path, body = {}, allowedFailure = false) {
    await pause(90);
    const response = await context.request.post(`${base}/api/${path}`, { data: body });
    const result = await response.json();
    if (!allowedFailure) assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
    return { response, result };
  }
  async function api(path, body = {}) { return (await request(path, body)).result; }
  // 用途：夹具导入后读取真实行囊；流程：服丹与UI核对；返回：已加载且解除忙碌的页面。
  async function openInventory() {
    await page.goto(`${base}/?qa=${Date.now()}#inventory`, { waitUntil: 'domcontentloaded' });
    await page.locator('.main-tabs').waitFor();
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  }
  // 用途：点击行囊丹药而非旁路修改；流程：实际服用与耗时检查；返回：后端真实结果及更新后的界面。
  async function consume(id) {
    const item = page.locator(`[data-inventory-item="${id}"]`);
    const details = item.locator('.inventory-pill-details');
    if (await details.getAttribute('open') === null) await details.locator(':scope > summary').click();
    const text = await details.innerText(); check(!staleHint.test(text), `${id}真实服丹面板没有旧越阶加毒提示`);
    const startTime = performance.now();
    const pending = page.waitForResponse(response => new URL(response.url()).pathname === '/api/use-pill' && response.request().method() === 'POST');
    await item.locator('[data-action="use-pill"]').click();
    const response = await pending, result = await response.json();
    assert.ok(response.ok() && result.ok !== false && !result.error, result.error || `服用 ${id}`);
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
    timings.push({ id, milliseconds: performance.now() - startTime });
    return result.state;
  }
  await api('login', { nickname, password });
  await api('new', { name: '丹毒专项验收', gender: '男', portraitId: 'player-male', seed: 216601,
    traits: [], allocation: {} });
  const original = JSON.parse((await api('export')).game);
  // 用途：形成独立可服丹状态；流程：低高境、阈值与旧丹毒验证；返回：仍由正规import校验的候选存档。
  function fixture(stage = 0, poison = 0) {
    const game = structuredClone(original); game.pending = null; game.job = null; game.npcs = [];
    game.player.stage = game.player.highestStage = stage; game.player.xp = 0; game.player.lifespan = 2000;
    game.player.health = 70; game.player.stones = 100000; game.player.location = 'wuling';
    game.player.apt = 90; game.player.insight = 30;
    game.player.arts.poison = poison; game.player.arts.used = {}; game.player.arts.combatPrep = {}; game.player.arts.battle = null;
    game.player.arts.potions = { '5502': 1, '5207': 1, '5005': 1, '5519': 1 };
    return game;
  }
  for (const stage of [0, 12]) {
    await api('import', { game: JSON.stringify(fixture(stage)) });
    const state = (await api('state')).state;
    const wrong = state.pillCatalog.filter(pill => poisonByKind[pill.kind] === undefined
      || pill.basePoison !== poisonByKind[pill.kind] || pill.poison !== poisonByKind[pill.kind]);
    check(wrong.length === 0, `${stage === 0 ? '炼气' : '化神'}完整丹药目录基础丹毒与当前丹毒统一按功能1/2/4/0`);
    check(new Set(state.pillCatalog.map(pill => pill.grade)).size === 6, '目录覆盖一至六品阶，品阶不改变丹毒档位');
    check(state.pillCatalog.every(pill => !staleHint.test([pill.usage, pill.baseEffect, pill.catalogEffect].join(' '))),
      `${stage === 0 ? '低境' : '高境'}目录不保留越阶+2或旧高阶+30提示`);
    catalogueExamples.push(...[1, 2, 3, 4, 5, 6].map(grade => {
      const pill = state.pillCatalog.find(entry => entry.grade === grade && entry.kind === 'xp');
      return { stage, id: pill?.id, grade, basePoison: pill?.basePoison, poison: pill?.poison, overTier: pill?.overTier };
    }));
    check(state.poison.detoxPerYear === 2 && state.poison.threshold === 20, '基础年化毒2点与修炼影响阈值20不变');
    const snapshot = (await api('export')).game; await api('state');
    check((await api('export')).game === snapshot, '目录查询不服丹、不改变丹毒、资源或随机数');
  }
  await api('import', { game: JSON.stringify(fixture()) }); await openInventory();
  let before = (await api('state')).state;
  const highPill = before.pillCatalog.find(pill => pill.id === '5502');
  check(highPill.overTier && highPill.poison === 1 && highPill.poisonDelta === 1, '炼气服六阶修为丹预览只有1丹毒，无额外越阶惩罚');
  let state = await consume('5502');
  check(state.poison.current === 1 && state.player.xp === before.player.xp + highPill.effectValue
    && (state.arts.potions['5502'] || 0) === 0, '真实点击高阶修为丹仅加1毒并兑现原药效');
  before = state;
  state = await consume('5207');
  check(state.poison.current === 3 && state.player.insight > before.player.insight && (state.arts.potions['5207'] || 0) === 0,
    '真实点击永久悟性丹加2毒且属性实际增加');
  state = await consume('5005');
  check(state.poison.current === 7 && (state.arts.potions['5005'] || 0) === 0, '真实点击战斗丹加4毒且消耗一枚');
  await page.screenshot({ path: join(temporary, 'poison-1-2-4.png'), fullPage: true });
  const detox = state.pillCatalog.find(pill => pill.id === '5519');
  check(detox.overTier && detox.basePoison === 0 && detox.poison === 0 && detox.poisonAfter === 0,
    '超阶解毒丹零新增毒，只按原化毒效果减少丹毒');
  state = await consume('5519');
  check(state.poison.current === 0 && (state.arts.potions['5519'] || 0) === 0, '真实点击解毒丹从7降到0且不产生额外毒');
  await page.setViewportSize({ width: 850, height: 1000 });
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '850px行囊丹毒展示没有横向溢出');
  await page.screenshot({ path: join(temporary, 'poison-inventory-850.png'), fullPage: true });

  await api('import', { game: JSON.stringify(fixture(0, 80)) });
  const threshold = (await api('state')).state;
  check(!threshold.pillCatalog.find(pill => pill.id === '5502').enabled
    && threshold.pillCatalog.find(pill => pill.id === '5519').enabled, '丹毒80仍禁止普通服丹，允许化毒丹');
  const frozen = (await api('export')).game;
  const blocked = await request('use-pill', { id: '5502' }, true);
  check(!blocked.response.ok() && (await api('export')).game === frozen, '丹毒80绕过界面服普通丹仍拒绝，资源及丹毒不变');

  await api('import', { game: JSON.stringify(fixture(9, 73)) }); await api('save', { slot: 1 });
  check((await api('state')).state.poison.current === 73, '旧累计丹毒73正规保存时不因新规则重算');
  await stop(); await start(); await api('login', { nickname, password });
  const restored = (await api('state')).state;
  check(restored.poison.current === 73 && restored.poison.detoxPerYear === 2, '完整服务重启续玩保留旧累计丹毒73及年化毒规则');
  check(errors.length === 0, '真实服丹页面没有脚本异常'); report.ok = true;
} catch (error) {
  report.error = error.stack || error.message;
  if (page) await page.screenshot({ path: join(temporary, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally {
  if (browser) await browser.close(); await stop();
  writeFileSync(join(temporary, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ok: report.ok, checks: checks.length, port, evidence: temporary }, null, 2));
}
