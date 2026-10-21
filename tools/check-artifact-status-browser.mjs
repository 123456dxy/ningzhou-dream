// 法器单状态削层：本工具自建随机端口、临时账号和数据，仅通过正规 import 装载固定种子夹具。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { once } from 'node:events';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2), option = (key, fallback) => args.includes(key) ? args[args.indexOf(key) + 1] : fallback;
const input = resolve(root, option('--exe', 'build-vs2022/Release/ningzhou_web.exe'));
const fixtureFolder = resolve(root, option('--fixtures', 'tmp/artifact-status-browser-20261017/fixtures'));
const output = join(root, 'tmp/artifact-status-browser-20261017'); mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'run-'));
const executable = join(temporary, 'artifact-status-qa.exe'); copyFileSync(input, executable);
for (const file of ['msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll']) {
  const source = join(dirname(input), file); if (existsSync(source)) copyFileSync(source, join(temporary, file));
}
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
const base = `http://127.0.0.1:${port}`;
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const checks = [], timings = [], errors = [], serverLog = [];
const report = { ok: false, input, binarySha256: createHash('sha256').update(readFileSync(input)).digest('hex'),
  webHashes: Object.fromEntries(['web/artifact-status.mjs', 'web/app.mjs', 'web/v2/app.mjs', 'web/v2/views/battle.mjs']
    .map(path => [path, createHash('sha256').update(readFileSync(join(root, path))).digest('hex')])),
  port, temporary, checks, timings, errors, serverLog,
  scope: '正式 import 的固定种子隔离角色；五阶预览、1440桌面与390手机/v2的清浊及破护单选、取消、重复确认、过期拒绝恢复、空目标禁用、实际工坊文案。' };
const pause = milliseconds => new Promise(done => setTimeout(done, milliseconds));
let child, exited, browser, context, page;
function check(condition, name, details) { assert.ok(condition, `${name}${details == null ? '' : ': ' + JSON.stringify(details)}`); checks.push({ name, ...(details == null ? {} : { details }) }); console.log(`PASS ${name}`); }
const readFixture = name => JSON.parse(readFileSync(join(fixtureFolder, name + '.json'), 'utf8'));
function selector(mode, action, value) {
  return mode === 'mobile' ? `[data-ui="artifact-status-${action}"]${value ? `[data-value="${value}"]` : ''}`
    : `[data-action="artifact-status-${action}"]${value ? `[data-id="${value}"]` : ''}`;
}
async function start() {
  child = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
    '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')], { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  exited = new Promise(done => child.once('exit', done));
  child.stdout.on('data', chunk => serverLog.push(String(chunk))); child.stderr.on('data', chunk => serverLog.push(String(chunk)));
  for (let attempt = 0; attempt < 200; attempt++) {
    try { if ((await fetch(base + '/api/mode')).ok) return; } catch { /* Starting own server. */ }
    if (child.exitCode != null) throw new Error(`Own service exited ${child.exitCode}`);
    await pause(50);
  }
  throw new Error('Own isolated service failed to start');
}
async function request(path, body = {}, allowFailure = false) {
  await pause(65);
  const response = await context.request.post(`${base}/api/${path}`, { data: body });
  const result = await response.json();
  if (!allowFailure) assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
  return { response, result };
}
async function api(path, body = {}) { return (await request(path, body)).result; }
const exported = async () => JSON.parse((await api('export')).game);
const abilityFrom = (state, effect) => state.battle.artifacts.find(ability => ability.id.includes(`status-${effect}-`));
async function load(name, change) {
  const game = readFixture(name); if (change) change(game);
  await api('import', { game: JSON.stringify(game) });
  return (await api('state')).state;
}
async function open(mode) {
  await page.setViewportSize({ width: mode === 'mobile' ? 390 : 1440, height: mode === 'mobile' ? 844 : 1000 });
  await page.goto(base + (mode === 'mobile' ? '/v2.html' : '/') + `?statusqa=${Date.now()}`, { waitUntil: 'domcontentloaded' });
  await page.locator(mode === 'mobile' ? '[data-ui="combat-detail"][data-value="artifacts"]' : '[data-action="artifact-play"]').first().waitFor();
}
async function openSelection(mode, ability) {
  if (mode === 'mobile') {
    await page.locator('[data-ui="combat-detail"][data-value="artifacts"]').click();
    await page.locator(`[data-ui="artifact-status-open"][data-value="${ability.id}"]`).click();
  } else await page.locator(`[data-action="artifact-play"][data-id="${ability.id}"]`).click();
  await page.locator(selector(mode, 'select')).first().waitFor();
}
async function verifyLayout(mode, name) {
  const metrics = await page.locator(selector(mode, 'select')).evaluateAll(nodes => nodes.map(node => {
    const rect = node.getBoundingClientRect(); return { left: rect.left, right: rect.right, width: rect.width, screen: innerWidth };
  }));
  check(metrics.length > 0 && metrics.every(item => item.width > 40 && item.left >= -1 && item.right <= item.screen + 1), `${mode} ${name}单选项可读且在屏幕内`, metrics);
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), `${mode} ${name}页面无横向溢出`);
  await page.screenshot({ path: join(temporary, `${mode}-${name}.png`), fullPage: true });
}

try {
  await start();
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(15000); page.on('pageerror', error => errors.push(error.message));
  await api('login', { nickname: `StatusQa${Date.now().toString(36).slice(-7)}`, password: 'isolated-status-qa' });
  for (const effect of ['cleanse', 'dispel']) for (let grade = 1; grade <= 5; grade++) {
    const state = await load(`${effect}-${grade}`), ability = abilityFrom(state, effect);
    const expected = effect === 'cleanse' ? [5, 10, 25, 45, 80][grade - 1] : grade;
    check(ability.targetMode === 'status-one' && ability.statusToken && ability.playable, `${effect}${grade}阶有后端单状态选项与快照令牌`);
    const target = ability.targets.find(item => item.id === (effect === 'cleanse' ? 'poison' : 'guard'));
    check(target?.removeLayers === expected && target.remainingLayers === target.layers - expected,
      `${effect}${grade}阶真实预览固定削${expected}层`, target);
    check(ability.targets.length === (effect === 'cleanse' ? 4 : 2), `${effect}${grade}阶只列可移除状态`);
  }
  for (const mode of ['desktop', 'mobile']) {
    for (const effect of ['cleanse', 'dispel']) {
      let state = await load(`${effect}-1`), ability = abilityFrom(state, effect);
      await open(mode); const beforeCancel = await exported();
      await openSelection(mode, ability);
      check(await page.locator(selector(mode, 'confirm')).isDisabled(), `${mode} ${effect}未选目标不可确认`);
      await page.locator(selector(mode, 'select', effect === 'cleanse' ? 'poison' : 'guard')).click();
      await verifyLayout(mode, effect);
      await page.locator(selector(mode, 'cancel')).click();
      check(JSON.stringify(await exported()) === JSON.stringify(beforeCancel), `${mode} ${effect}取消不扣灵力、不改状态或随机数`);
      await openSelection(mode, ability);
      await page.locator(selector(mode, 'select', effect === 'cleanse' ? 'poison' : 'guard')).click();
      const before = await exported(), beforeBattle = before.player.arts.battle;
      let actionRequests = 0;
      const listener = request => { if (new URL(request.url()).pathname === '/api/battle/turn' && request.method() === 'POST') actionRequests++; };
      page.on('request', listener);
      const startTime = performance.now(), pending = page.waitForResponse(response => new URL(response.url()).pathname === '/api/battle/turn');
      const button = page.locator(selector(mode, 'confirm')); await button.scrollIntoViewIfNeeded();
      const box = await button.boundingBox();
      await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2, { clickCount: 2, delay: 30 });
      const response = await pending, result = await response.json();
      check(response.ok() && result.ok !== false && !result.error, `${mode} ${effect}真实确认成功`, result.error);
      await pause(250); page.off('request', listener);
      const after = await exported(), battle = after.player.arts.battle;
      check(actionRequests === 1 && battle.mana === beforeBattle.mana - ability.cost && battle.played === beforeBattle.played + 1,
        `${mode} ${effect}双击只发一次并只扣一次资源`, { actionRequests, beforeMana: beforeBattle.mana, afterMana: battle.mana });
      if (effect === 'cleanse') check(battle.enemyCards.poison === beforeBattle.enemyCards.poison - 5
        && battle.enemyCards.burn === beforeBattle.enemyCards.burn && battle.lock === beforeBattle.lock && battle.enemyWeaken === beforeBattle.enemyWeaken,
        `${mode} 清浊只削毒5层，其他三类负面保留`);
      else check(Math.abs(battle.enemyCards.guard - (beforeBattle.enemyCards.guard - .01)) < 1e-9
        && battle.enemyCards.discardPower === beforeBattle.enemyCards.discardPower && battle.enemyCards.buff === beforeBattle.enemyCards.buff
        && battle.enemyCards.reduction === beforeBattle.enemyCards.reduction, `${mode} 破护只削守势1层，锐势及本场积累保留`);
      timings.push({ mode, effect, milliseconds: performance.now() - startTime });
    }
    const state = await load('cleanse-1'), ability = abilityFrom(state, 'cleanse');
    await open(mode); await openSelection(mode, ability); await page.locator(selector(mode, 'select', 'poison')).click();
    const changed = await exported(); changed.player.arts.battle.enemyCards.poison--;
    await api('import', { game: JSON.stringify(changed) });
    const frozen = (await api('export')).game;
    const staleResponse = page.waitForResponse(response => new URL(response.url()).pathname === '/api/battle/turn');
    await page.locator(selector(mode, 'confirm')).click();
    const rejected = await staleResponse, rejectedBody = await rejected.json();
    check(!rejected.ok() && rejectedBody.error && (await api('export')).game === frozen,
      `${mode} 过期状态确认被拒绝且无部分扣费`, { status: rejected.status(), error: rejectedBody.error });
    await pause(350);
    await openSelection(mode, abilityFrom((await api('state')).state, 'cleanse'));
    check((await page.locator(selector(mode, 'select', 'poison')).innerText()).includes('89'), `${mode} 错误后恢复新状态并能重新选择`);
    await page.locator(selector(mode, 'cancel')).click();
    const empty = await load('cleanse-1', game => {
      const battle = game.player.arts.battle; battle.enemyCards.poison = battle.enemyCards.poisonDamage = battle.enemyCards.poisonTurns = 0;
      battle.enemyCards.burn = battle.enemyCards.burnTurns = battle.playerPoison = battle.playerPoisonTurns = battle.lock = battle.enemyWeaken = 0;
    });
    check(!abilityFrom(empty, 'cleanse').playable && abilityFrom(empty, 'cleanse').targets.length === 0, `${mode} 无可清负面时后端禁用法器`);
    await open(mode);
    if (mode === 'mobile') {
      await page.locator('[data-ui="combat-detail"][data-value="artifacts"]').click();
      check(await page.locator('[data-ui="artifact-status-open"]').isDisabled(), '手机无可清状态时实际施展入口禁用');
    } else check(await page.locator('[data-action="artifact-play"]').isDisabled(), '桌面无可清状态时实际施展入口禁用');
    const permanentOnly = await load('dispel-1', game => {
      game.player.arts.battle.enemyCards.guard = game.player.arts.battle.enemyCards.discardPower = 0;
    });
    check(!abilityFrom(permanentOnly, 'dispel').playable && abilityFrom(permanentOnly, 'dispel').targets.length === 0,
      `${mode} 仅本场增伤减伤存在时破护仍禁用，不把永久积累当目标`);
  }
  await load('workshop');
  const catalog = await api('forge-options');
  const materials = catalog.materials || catalog.options?.materials;
  check(Array.isArray(materials) && materials.length > 0, '工坊读取正式材料目录');
  for (const effect of ['cleanse', 'dispel']) for (let grade = 1; grade <= 5; grade++) {
    const material = materials.find(item => item.grade === grade && item.element === '金' && item.family === 'metal');
    assert.ok(material, `${grade}阶金矿材料`);
    const quote = (await api('forge-preview', { version: 2, materials: Array(10).fill(material.id), main: material.id, shape: 'bell', effect, inscription: '' })).info;
    check(quote.enabled && quote.grade === grade && quote.effectText.includes(String(effect === 'cleanse' ? [5, 10, 25, 45, 80][grade - 1] : grade))
      && !/清除自身中毒、灼烧、悬灵与临时削弱|清除对手临时增伤、守势、额外减伤与锐势/.test(quote.effectText), `${effect}${grade}阶工坊使用新固定削层文案`, quote.effectText);
  }
  await page.setViewportSize({ width: 1440, height: 1000 }); await page.goto(base + '/#workshop/forge', { waitUntil: 'domcontentloaded' });
  await page.locator('[data-forge-workshop]').waitFor();
  await page.locator('[data-forge-field][data-field="shape"]').selectOption('bell');
  await page.locator('[data-action="forge-slot"]').first().click();
  await page.locator('[data-action="forge-fill"]').first().click();
  for (const effect of ['cleanse', 'dispel']) {
    const quoted = page.waitForResponse(response => new URL(response.url()).pathname === '/api/forge-preview'
      && response.request().postDataJSON()?.effect === effect);
    await page.locator('[data-forge-field][data-field="effect"]').selectOption(effect);
    const quoteResponse = await quoted; assert.ok(quoteResponse.ok());
    await page.waitForFunction(effect => document.querySelector('.forge-result h3')?.textContent === (effect === 'cleanse' ? '清浊' : '破护')
      && /层/.test(document.querySelector('.forge-result p')?.textContent || ''), effect);
    await page.locator('.forge-result').screenshot({ path: join(temporary, `workshop-${effect}.png`) });
    check(/层/.test(await page.locator('.forge-result p').first().innerText()), `真实工坊${effect}显示固定削层效果`);
  }
  check(errors.length === 0, '桌面及手机真实交互没有页面脚本异常', errors);
  report.ok = true;
} catch (error) {
  report.error = error.stack || error.message;
  if (page) await page.screenshot({ path: join(temporary, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally {
  if (browser) await browser.close();
  if (child && child.exitCode == null) { child.kill(); await Promise.race([exited, pause(5000)]); }
  writeFileSync(join(temporary, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ok: report.ok, checks: checks.length, port, evidence: temporary }, null, 2));
}
