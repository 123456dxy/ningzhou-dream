// 法器固定数值验收：独立端口、临时账号与数据，夹具仅通过正式 import 接口装入。
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
const input = resolve(root, option('--exe', 'build-vs-current/Release/ningzhou_web.exe'));
const isRed = args.includes('--red');
const folder = join(root, 'tmp/artifact-fixed-browser-20261017'); mkdirSync(folder, { recursive: true });
const temporary = mkdtempSync(join(folder, isRed ? 'red-' : 'green-'));
const executable = join(temporary, 'artifact-fixed-qa.exe'); copyFileSync(input, executable);
for (const name of ['msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll']) {
  if (existsSync(join(dirname(input), name))) copyFileSync(join(dirname(input), name), join(temporary, name));
}
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
const base = `http://127.0.0.1:${port}`;
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const expected = { damage: [20, 40, 120, 450, 1800], shield: [20, 50, 150, 500, 2000], turn_shield: [3, 8, 25, 100, 400] };
const labels = { damage: '破阵', shield: '护体', turn_shield: '绵护', sharpness_burst: '锐势斩', poison_burst: '毒袭',
  fire_burst: '灼击', water_burst: '叠浪击', sword_burst: '归锋', soul_burst: '神识冲击', charge_burst: '镇岳' };
const shapes = { damage: 'sword', shield: 'bell', turn_shield: 'pearl' };
const slots = { damage: 'weapon', shield: 'armor', turn_shield: 'accessory' };
const bursts = { sharpness_burst: '金', poison_burst: '木', fire_burst: '火', water_burst: '水', sword_burst: '剑', soul_burst: '神', charge_burst: '土' };
for (const effect of Object.keys(bursts)) { shapes[effect] = 'sword'; slots[effect] = 'weapon'; }
const checks = [], timings = [], errors = [], serverLog = [];
const report = { ok: false, isRed, input, temporary, port, checks, timings, errors, serverLog,
  binarySha256: createHash('sha256').update(readFileSync(input)).digest('hex'),
  scope: '完整服隔离注册/建角/正式导入；真实桌面1440及手机390工坊和法器战斗交互；固定品阶数值。' };
const pause = milliseconds => new Promise(done => setTimeout(done, milliseconds));
let child, exited, browser, context, page;
function check(condition, name, detail) {
  assert.ok(condition, `${name}${detail === undefined ? '' : ': ' + JSON.stringify(detail)}`);
  checks.push({ name, ...(detail === undefined ? {} : { detail }) }); console.log(`PASS ${name}`);
}
async function api(path, body = {}) {
  await pause(75);
  const response = await context.request.post(`${base}/api/${path}`, { data: body });
  const result = await response.json();
  assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
  return result;
}
const exported = async () => JSON.parse((await api('export')).game);
async function load(game) { return (await api('import', { game: JSON.stringify(game) })).state; }
async function start() {
  child = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
    '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')],
  { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  exited = new Promise(done => child.once('exit', done));
  child.stdout.on('data', data => serverLog.push(String(data))); child.stderr.on('data', data => serverLog.push(String(data)));
  for (let attempt = 0; attempt < 200; attempt++) {
    try { if ((await fetch(base + '/api/mode')).ok) return; } catch { /* own service starting */ }
    if (child.exitCode !== null) throw new Error(`Own process exited ${child.exitCode}`);
    await pause(50);
  }
  throw new Error('Own service startup timed out');
}
async function workshop(mode, effect, material) {
  const mobile = mode === 'mobile';
  await page.setViewportSize({ width: mobile ? 390 : 1440, height: mobile ? 844 : 1000 });
  await page.goto(base + (mobile ? '/v2.html#/inventory/workshop' : '/#workshop/forge'), { waitUntil: 'domcontentloaded' });
  const field = mobile ? '[data-ui="forge-field"]' : '[data-forge-field]';
  if (mobile) {
    const forgeTab = page.locator('[data-ui="workshop-tab"][data-value="forge"]');
    await forgeTab.waitFor(); await forgeTab.click();
  }
  await page.locator(`${field}[data-field="shape"]`).waitFor();
  const action = mobile ? 'data-ui' : 'data-action', id = mobile ? 'data-value' : 'data-id';
  const clearButton = page.locator(`[${action}="forge-clear"]`);
  if (await clearButton.isEnabled()) {
    const cleared = page.waitForResponse(response => new URL(response.url()).pathname === '/api/forge-preview'
      && response.request().postDataJSON()?.materials?.every(id => !id));
    await clearButton.click(); await cleared;
  }
  await page.locator(`${field}[data-field="shape"]`).selectOption(shapes[effect]);
  await page.locator(`[${action}="forge-slot"]`).first().click();
  const filter = mobile ? '[data-ui="forge-filter"]' : '[data-forge-filter]';
  await page.locator(`${filter}[data-field="grade"]`).selectOption('1');
  await page.locator(`${filter}[data-field="element"]`).selectOption(material.element);
  await page.locator(`${filter}[data-field="family"]`).selectOption('metal');
  const filled = page.waitForResponse(response => new URL(response.url()).pathname === '/api/forge-preview'
    && response.request().postDataJSON()?.materials?.every(id => id === material.id));
  await page.locator(`[${action}="forge-fill"][${id}="${material.id}"]`).click();
  const filledBody = await (await filled).json();
  await page.waitForFunction(text => document.querySelector('.forge-result p')?.textContent === text, filledBody.info.effectText);
  const started = performance.now();
  const pending = page.waitForResponse(response => new URL(response.url()).pathname === '/api/forge-preview'
    && response.request().postDataJSON()?.effect === effect);
  await page.locator(`${field}[data-field="effect"]`).selectOption(effect);
  const response = await pending; assert.ok(response.ok());
  const body = await response.json();
  await page.waitForFunction(({ label, text }) => document.querySelector('.forge-result h3')?.textContent === label
    && document.querySelector('.forge-result p')?.textContent === text, { label: labels[effect], text: body.info.effectText });
  const text = await page.locator('.forge-result p').first().innerText();
  timings.push({ mode, action: 'workshop-' + effect, milliseconds: performance.now() - started });
  await page.locator('.forge-result').screenshot({ path: join(temporary, `${mode}-workshop-${effect}.png`) });
  if (effect === 'damage') await page.screenshot({ path: join(temporary, `${mode}-workshop-full.png`), fullPage: true });
  return text;
}
function makeGear(effect, grade, material, power = 1) {
  return { id: `fixed-${effect}-${grade}`, name: `${labels[effect]}验收器`, slot: slots[effect], element: material.element, polarity: '天阳',
    grade, bonus: 0, inscribed: false, price: 0, craftCost: 33, crafted: true, shape: shapes[effect],
    dimensions: [500, 500, 500, 500, 500], forgeVersion: 2, effectId: effect, inscriptionId: '', effectPower: power,
    forgeEnergy: 7, sourceMaterials: { [material.id]: 10 } };
}
async function beginBattle(created, effect, grade, material, empowered = false) {
  const game = structuredClone(created), gear = makeGear(effect, grade, material, empowered ? 3 : .1);
  Object.assign(game.player, { stage: empowered ? 12 : (grade - 1) * 3, stones: 100000 });
  game.player.arts.gear = [gear]; game.player.arts.equipped = { [gear.slot]: gear.id };
  if (empowered) { game.player.arts.main = 'gold'; game.player.arts.manuals.gold = 5; }
  await load(game);
  const result = await api('eco', { type: 'hunt', id: '' });
  assert.ok(result.state.battle, '正式猎妖行动应创建斗法');
  const battleGame = await exported(), battle = battleGame.player.arts.battle;
  Object.assign(battle, { hp: battle.max, shield: effect === 'turn_shield' ? battle.shield : 0, mana: battle.maxMana,
    enemyShield: 0, enemyWeaken: 0, playerPoison: 0, playerPoisonTurns: 0,
    poison: 0, poisonTurns: 0, poisonDamage: 0, burn: 0, burnTurns: 0, water: 0, swordQi: 0, soul: 0, charge: 0, buff: empowered ? .9 : 0,
    discardPower: empowered && !(effect in bursts) ? .25 : 0 });
  await load(battleGame);
  return { game: await exported(), state: (await api('state')).state, gear };
}
async function battleUi(mode, fixture, effect) {
  const mobile = mode === 'mobile';
  await load(fixture.game);
  await page.setViewportSize({ width: mobile ? 390 : 1440, height: mobile ? 844 : 1000 });
  await page.goto(base + (mobile ? '/v2.html' : '/'), { waitUntil: 'domcontentloaded' });
  if (mobile) await page.locator('[data-ui="combat-detail"][data-value="artifacts"]').click();
  const ability = fixture.state.battle.artifacts.find(item => item.id.includes(fixture.gear.id));
  assert.ok(ability, `正式战斗目录有${effect}`);
  const button = mobile ? page.locator('[data-command="battle/turn"]').filter({ hasText: '施展法宝' })
    : page.locator(`[data-action="artifact-play"][data-id="${ability.id}"]`);
  await button.waitFor();
  const text = mobile ? await button.locator('..').innerText() : await button.locator('..').innerText();
  check(text.includes('20'), `${mode} 法器即时战果显示20点`, text);
  await page.screenshot({ path: join(temporary, `${mode}-battle-${effect}.png`), fullPage: true });
  if (effect === 'damage') {
    const frozen = (await api('export')).game;
    await page.route('**/api/battle/turn', route => route.fulfill({ status: 503, contentType: 'application/json',
      body: JSON.stringify({ ok: false, error: '隔离验收临时503' }) }), { times: 1 });
    const failed = page.waitForResponse(response => new URL(response.url()).pathname === '/api/battle/turn');
    await button.click(); await failed;
    await pause(250);
    if (mobile) {
      await page.locator('[data-ui="close-sheet"]').click();
      await page.locator('[data-ui="combat-detail"][data-value="artifacts"]').click();
    }
    check((await api('export')).game === frozen && await button.isEnabled(), `${mode} 法器技术错误不扣费且能恢复重试`);
  }
  const before = await exported(), old = before.player.arts.battle;
  let count = 0;
  const listener = request => { if (new URL(request.url()).pathname === '/api/battle/turn' && request.method() === 'POST') count++; };
  page.on('request', listener);
  const started = performance.now(), pending = page.waitForResponse(response => new URL(response.url()).pathname === '/api/battle/turn');
  await button.evaluate(node => { node.click(); node.click(); });
  const response = await pending, result = await response.json(); assert.ok(response.ok() && !result.error, result.error);
  await pause(250); page.off('request', listener);
  const next = (await exported()).player.arts.battle;
  check(count === 1 && next.mana === old.mana - ability.cost && next.played === old.played + 1,
    `${mode} ${labels[effect]}重复点击只提交一次及扣费一次`, { count, beforeMana: old.mana, afterMana: next.mana });
  const isDamage = slots[effect] === 'weapon';
  check(isDamage ? old.enemyHp - next.enemyHp === 20 : next.shield - old.shield === 20,
    `${mode} ${labels[effect]}实际结算20点，与预览一致`, { preview: ability, actual: isDamage ? old.enemyHp - next.enemyHp : next.shield - old.shield });
  timings.push({ mode, action: 'battle-' + effect, milliseconds: performance.now() - started });
}

try {
  await start();
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(15000); page.on('pageerror', error => errors.push(error.message));
  await api('login', { nickname: `FixedQa${Date.now().toString(36).slice(-7)}`, password: 'isolated-fixed-qa' });
  await api('new', { name: '法器定值验收', gender: '男', portraitId: 'player-male', seed: 519104, traits: [], allocation: {} });
  const created = await exported(); writeFileSync(join(temporary, 'created.json'), JSON.stringify(created));
  const catalog = await api('forge-options');
  const materials = catalog.materials || catalog.options?.materials;
  check(Array.isArray(materials) && materials.length > 0, '真实创建账号角色并读取正式炼器目录');
  const workshopGame = structuredClone(created);
  workshopGame.player.stage = 12;
  workshopGame.player.stones = 1000000;
  workshopGame.player.arts.exp['器'] = 1000;
  for (const material of materials) workshopGame.player.arts.materials[material.id] = 50;
  await load(workshopGame);
  const quotes = [];
  for (const effect of Object.keys(expected)) for (let grade = 1; grade <= 5; grade++) {
    const material = materials.find(item => item.grade === grade && item.element === '金' && item.family === 'metal');
    assert.ok(material, `${grade}阶金矿`);
    const info = (await api('forge-preview', { version: 2, materials: Array(10).fill(material.id), main: material.id,
      shape: shapes[effect], effect, inscription: '' })).info;
    quotes.push({ effect, grade, expected: expected[effect][grade - 1], text: info.effectText, gradeActual: info.grade });
    if (!isRed) check(info.enabled && info.grade === grade && info.effectText.includes(String(expected[effect][grade - 1]))
      && !/战力伤害基数|气血上限[^，。]*%/.test(info.effectText), `${effect}${grade}阶工坊明确固定值`, info.effectText);
  }
  report.quotes = quotes;
  const material = materials.find(item => item.grade === 1 && item.element === '金' && item.family === 'metal');
  if (isRed) {
    const text = await workshop('desktop', 'damage', material);
    check(/战力伤害基数/.test(text), 'RED：旧版真实工坊伤害仍显示战力百分比，与用户20点固定值要求不符', text);
    report.expectedFailure = '旧实现 mfBattleDamageBasis(Actor.stage) * .65 * Power 等倍率仍存在；不是单纯浏览器缓存问题。';
    if (args.includes('--probe')) {
      const probe = await beginBattle(created, 'damage', 1, material, true);
      writeFileSync(join(temporary, 'battle-probe.json'), JSON.stringify(probe));
      report.probe = probe.state.battle.artifacts;
      await load(workshopGame);
      report.mobileProbe = await workshop('mobile', 'shield', material);
    }
  } else {
    for (const mode of ['desktop', 'mobile']) for (const effect of Object.keys(expected)) {
      await load(workshopGame);
      const text = await workshop(mode, effect, material);
      check(text.includes(String(expected[effect][0])) && !/战力伤害基数|气血上限[^，。]*%/.test(text), `${mode} 真实工坊${labels[effect]}展示固定值`, text);
      check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), `${mode} ${labels[effect]}无横向溢出`);
    }
    const battleCases = [];
    for (const effect of ['damage', 'shield']) for (let grade = 1; grade <= 5; grade++) {
      const material = materials.find(item => item.grade === grade && item.element === '金' && item.family === 'metal');
      const values = [];
      for (const empowered of [false, true]) {
        const fixture = await beginBattle(created, effect, grade, material, empowered);
        const ability = fixture.state.battle.artifacts.find(item => item.id.includes(fixture.gear.id));
        assert.ok(ability?.playable, `${effect}${grade}阶可施展`);
        const preview = effect === 'damage' ? ability.rawDamage : ability.shield;
        check(preview === expected[effect][grade - 1], `${effect}${grade}阶${empowered ? '高境界强功法及增伤' : '最低可用境界'}仍为品阶固定值`, ability);
        const before = fixture.game.player.arts.battle;
        await api('battle/turn', { id: ability.id });
        const after = (await exported()).player.arts.battle;
        const actual = effect === 'damage' ? before.enemyHp - after.enemyHp : after.shield - before.shield;
        check(actual === expected[effect][grade - 1] && actual === preview, `${effect}${grade}阶真实结算与预览一致`, { actual, preview });
        values.push({ empowered, preview, actual });
        if (grade === 1 && empowered) battleCases.push({ effect, fixture });
      }
      check(values[0].actual === values[1].actual, `${effect}${grade}阶不随境界、增伤、人物功法或材料效力变化`, values);
    }
    for (let grade = 1; grade <= 5; grade++) {
      const material = materials.find(item => item.grade === grade && item.element === '金' && item.family === 'metal');
      const fixture = await beginBattle(created, 'turn_shield', grade, material, true);
      const before = fixture.game.player.arts.battle;
      check(before.shield === expected.turn_shield[grade - 1] && before.artifactState?.roundShield === expected.turn_shield[grade - 1],
        `绵护${grade}阶真实入战固定${expected.turn_shield[grade - 1]}盾并冻结点数`, { shield: before.shield, snapshot: before.artifactState });
      before.shield = 0;
      await load(fixture.game); await api('battle/turn', { id: 'end' });
      const after = (await exported()).player.arts.battle;
      check(after && after.shield === expected.turn_shield[grade - 1], `绵护${grade}阶下一轮仍提供固定盾`, { shield: after?.shield });
    }
    for (const [effect, element] of Object.entries(bursts)) {
      // 毒袭/灼击为纯状态伤害，空状态拒绝；分品阶、保留状态与拒绝边界由
      // check-beast-stack-artifact-browser.mjs验收，不能继续套用零蓄积20基础直伤断言。
      if (effect === 'poison_burst' || effect === 'fire_burst') continue;
      const material = materials.find(item => item.grade === 1 && item.element === element && item.family === 'metal');
      assert.ok(material, `${element}属性矿物`);
      const fixture = await beginBattle(created, effect, 1, material, true);
      const ability = fixture.state.battle.artifacts.find(item => item.id.includes(fixture.gear.id));
      check(ability?.playable && ability.rawDamage === 20 && !/战力伤害基数/.test(ability.description),
        `${labels[effect]}零蓄积时一阶固定20基础伤害`, ability);
      const before = fixture.game.player.arts.battle;
      await api('battle/turn', { id: ability.id });
      const after = (await exported()).player.arts.battle;
      check(before.enemyHp - after.enemyHp === 20, `${labels[effect]}真实结算未遗漏旧战力倍率`);
    }
    for (const mode of ['desktop', 'mobile']) for (const { effect, fixture } of battleCases) await battleUi(mode, fixture, effect);
  }
  check(errors.length === 0, '真实浏览器没有页面脚本异常', errors);
  report.ok = true;
} catch (error) {
  report.error = error.stack || error.message;
  if (page) await page.screenshot({ path: join(temporary, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally {
  if (browser) await browser.close();
  if (child && child.exitCode === null) { child.kill(); await Promise.race([exited, pause(5000)]); }
  writeFileSync(join(temporary, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ok: report.ok, isRed, checks: checks.length, port, evidence: temporary }, null, 2));
}
