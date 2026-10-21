// 五部位与层数法器验收。只启动本工具的新版EXE副本、随机端口和临时账号。
// 战斗层数/配装经正式import装入隔离夹具；不会访问玩家8106或玩家存档。
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
const args = process.argv.slice(2);
const option = (key, fallback) => args.includes(key) ? args[args.indexOf(key) + 1] : fallback;
assert.ok(args.includes('--exe'), '必须明确指定本轮成功构建的 --exe');
assert.ok(args.includes('--poison-multipliers') && args.includes('--fire-multipliers'), '必须传入本轮确认的毒与灼烧倍率表');
const multipliers = { poison_burst: option('--poison-multipliers', '').split(',').map(Number), fire_burst: option('--fire-multipliers', '').split(',').map(Number) };
assert.ok(Object.values(multipliers).every(values => values.length === 5 && values.every(value => Number.isFinite(value) && value > 0)));
const input = resolve(root, option('--exe'));
assert.ok(existsSync(input));
const directory = join(root, 'artifacts/beast-stack-artifact-20261018'); mkdirSync(directory, { recursive: true });
const temporary = mkdtempSync(join(directory, 'run-'));
const executable = join(temporary, 'beast-stack-artifact-qa.exe'); copyFileSync(input, executable);
for (const name of ['msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'])
  if (existsSync(join(dirname(input), name))) copyFileSync(join(dirname(input), name), join(temporary, name));
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
assert.ok(![8106, 8107].includes(port));
const base = `http://127.0.0.1:${port}`;
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const checks = [], errors = [], failures = [], timings = [], serverLog = [], screenshots = [];
const report = { ok: false, input, port, temporary, multipliers, checks, failures, errors, timings, serverLog, screenshots,
  binarySha256: createHash('sha256').update(readFileSync(input)).digest('hex'),
  limitations: ['配装、层数经正式import校验装入临时角色；不是完整自然养成或难度平衡测试。', '五部位浏览器结算覆盖城市猎妖；其他遭遇路径与八属性五阶全矩阵由C++规则测试补充。'] };
const pause = ms => new Promise(done => setTimeout(done, ms));
let child, exited, browser, context, page, created, materials, effects;
function check(condition, label, detail) {
  assert.ok(condition, `${label}${detail === undefined ? '' : ': ' + JSON.stringify(detail)}`);
  checks.push({ label, ...(detail === undefined ? {} : { detail }) }); console.log(`PASS ${label}`);
}
async function api(path, body = {}, allowFailure = false) {
  await pause(85);
  const response = await context.request.post(base + '/api/' + path, { data: body });
  const result = await response.json();
  if (!allowFailure) assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
  return result;
}
const exported = async () => JSON.parse((await api('export')).game);
const load = async game => (await api('import', { game: JSON.stringify(game) })).state;
async function scenario(label, callback) {
  console.log('SCENARIO ' + label);
  try { await callback(); } catch (error) {
    failures.push({ scenario: label, error: error.stack || String(error) });
    console.error(label + ': ' + error.message);
    await shot('failure-' + label).catch(() => {});
  }
}
async function shot(label) {
  const path = join(temporary, label + '.png'); await page.screenshot({ path, fullPage: true }); screenshots.push(path);
}
function gameFixture(grade = 1) {
  const game = structuredClone(created); game.pending = null; game.job = null;
  Object.assign(game.player, { stage: (grade - 1) * 3, highestStage: (grade - 1) * 3, stones: 1000000, lifespan: 2000, location: 'wuling' });
  game.player.arts.battle = null;
  return game;
}
function makeGear(effect, grade, material, power = 1) {
  return { id: `stack-${effect}-${grade}`, name: '层数催动验收剑', slot: 'weapon', element: material.element, polarity: material.polarity,
    grade, bonus: 0, inscribed: false, price: 0, craftCost: 33, crafted: true, origin: 'player', shape: 'sword',
    dimensions: [500, 500, 500, 500, 500], forgeVersion: 2, effectId: effect, inscriptionId: '', effectPower: power,
    forgeEnergy: 7, sourceMaterials: { [material.id]: 10 } };
}
async function battleFixture(effect, grade, stackCount = 12, duration = 3, empowered = false) {
  const game = gameFixture(grade), material = materials.find(item => item.grade === grade && item.element === (effect === 'poison_burst' ? '木' : '火') && item.family === 'metal');
  const gear = makeGear(effect, grade, material, empowered ? 3 : .1);
  if (empowered) { game.player.stage = game.player.highestStage = 12; game.player.arts.main = 'gold'; game.player.arts.manuals.gold = 5; }
  game.player.arts.gear = [gear]; game.player.arts.equipped = { weapon: gear.id };
  const prepared = await load(game);
  assert.ok(Number.isFinite(prepared.player.healthMax) && prepared.player.healthMax > 0, '后端应报价真实气血上限');
  game.player.health = prepared.player.healthMax;
  await load(game); const start = await api('eco', { type: 'hunt', id: '' });
  assert.ok(start.state.battle, '正式猎妖应创建斗法');
  const fixture = await exported(), battle = fixture.player.arts.battle;
  Object.assign(battle, { hp: battle.max, mana: battle.maxMana, shield: 0, enemyHp: battle.enemyMax, enemyShield: 0,
    enemyWeaken: 0, poison: effect === 'poison_burst' ? stackCount : 0, poisonTurns: effect === 'poison_burst' ? duration : 0,
    poisonDamage: effect === 'poison_burst' ? 91 : 0, burn: effect === 'fire_burst' ? stackCount : 0,
    burnTurns: effect === 'fire_burst' ? duration : 0, playerPoison: 0, playerPoisonTurns: 0,
    buff: empowered ? .9 : 0, discardPower: 0, water: 0, swordQi: 0, soul: 0, charge: 0 });
  const state = await load(fixture);
  return { game: await exported(), state, gear, grade, effect, stackCount, duration };
}
const abilityOf = fixture => fixture.state.battle.artifacts.find(item => item.id.includes(fixture.gear.id));
async function clickAction(button, label) {
  let count = 0;
  const listener = request => { if (new URL(request.url()).pathname === '/api/battle/turn' && request.method() === 'POST') count++; };
  page.on('request', listener); const started = performance.now();
  const pending = page.waitForResponse(response => new URL(response.url()).pathname === '/api/battle/turn');
  await button.evaluate(node => { node.click(); node.click(); });
  const response = await pending, result = await response.json(); await pause(250); page.off('request', listener);
  assert.ok(response.ok() && !result.error, result.error || response.status());
  check(count === 1, label + '重复点击只提交一次');
  timings.push({ action: label, milliseconds: performance.now() - started });
  return result;
}
async function openBattle(mode) {
  await page.setViewportSize({ width: mode === 'mobile' ? 390 : 1440, height: mode === 'mobile' ? 844 : 1000 });
  await page.goto(base + (mode === 'mobile' ? '/v2.html' : '/') + `?qa=${Date.now()}`, { waitUntil: 'domcontentloaded' });
  if (mode === 'mobile') await page.locator('[data-ui="combat-detail"][data-value="artifacts"]').click();
}
async function openWorkshop(mode) {
  await page.setViewportSize({ width: mode === 'mobile' ? 390 : 1440, height: mode === 'mobile' ? 844 : 1000 });
  await page.goto(base + (mode === 'mobile' ? '/v2.html' : '/') + `?qa=${Date.now()}` + (mode === 'mobile' ? '#/inventory/workshop' : '#workshop/forge'), { waitUntil: 'domcontentloaded' });
  if (mode === 'mobile') await page.locator('[data-ui="workshop-tab"][data-value="forge"]').click();
  await page.locator(mode === 'mobile' ? '[data-ui="forge-field"][data-field="shape"]' : '[data-forge-field][data-field="shape"]').waitFor();
}
async function fillWorkshop(mode, effect, material) {
  const field = mode === 'mobile' ? '[data-ui="forge-field"]' : '[data-forge-field]';
  const action = mode === 'mobile' ? 'data-ui' : 'data-action', id = mode === 'mobile' ? 'data-value' : 'data-id';
  const clear = page.locator(`[${action}="forge-clear"]`);
  if (await clear.isEnabled()) { const response = page.waitForResponse(reply => new URL(reply.url()).pathname === '/api/forge-preview' && reply.request().postDataJSON()?.materials?.every(id => !id)); await clear.click(); await response; }
  await page.locator(`${field}[data-field="shape"]`).selectOption('sword');
  await page.locator(`[${action}="forge-slot"]`).first().click();
  const filter = mode === 'mobile' ? '[data-ui="forge-filter"]' : '[data-forge-filter]';
  await page.locator(`${filter}[data-field="grade"]`).selectOption(String(material.grade));
  await page.locator(`${filter}[data-field="element"]`).selectOption(material.element);
  await page.locator(`${filter}[data-field="family"]`).selectOption(material.family);
  const fill = page.waitForResponse(reply => new URL(reply.url()).pathname === '/api/forge-preview' && reply.request().postDataJSON()?.materials?.every(id => id === material.id));
  await page.locator(`[${action}="forge-fill"][${id}="${material.id}"]`).click(); await fill;
  const preview = page.waitForResponse(reply => new URL(reply.url()).pathname === '/api/forge-preview' && reply.request().postDataJSON()?.effect === effect);
  await page.locator(`${field}[data-field="effect"]`).selectOption(effect);
  const result = await (await preview).json();
  await page.waitForFunction(text => document.querySelector('.forge-result p')?.textContent === text, result.info.effectText);
  return result.info;
}

try {
  child = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'), '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')], { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  exited = new Promise(done => child.once('exit', done));
  child.stdout.on('data', value => serverLog.push(String(value))); child.stderr.on('data', value => serverLog.push(String(value)));
  for (let attempt = 0; attempt < 200; attempt++) {
    try { if ((await fetch(base + '/api/mode')).ok) break; } catch { /* own server starting */ }
    if (child.exitCode !== null) throw new Error('Own server exited ' + child.exitCode);
    if (attempt === 199) throw new Error('Own server startup timeout');
    await pause(50);
  }
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(15000); page.on('pageerror', error => errors.push(error.message));
  await api('login', { nickname: 'StackQa' + Date.now().toString(36), password: 'isolated-stack-qa' });
  await api('new', { name: '五部位层数验收', gender: '男', portraitId: 'player-male', seed: 5182026, traits: [], allocation: {} });
  created = await exported(); const catalog = await api('forge-options'); materials = catalog.materials; effects = catalog.effects;
  console.log('OWN_PREVIEW ' + base);

  await scenario('catalog-and-readonly', async () => {
    const bodyparts = materials.filter(item => ['skin', 'bone', 'claw', 'eye', 'heart'].includes(item.part));
    check(bodyparts.length === 400 && bodyparts.every(item => !item.acquisitionPending), '五阶八属性阴阳共400种五部位目录正式开放');
    check(bodyparts.every(item => !item.buyable && !/尚待|后续开放|未开放/.test(item.source)), '妖兽部位来源已明确且不是商店货物');
    const game = gameFixture(5); for (const material of materials) game.player.arts.materials[material.id] = 50;
    await load(game); const before = (await api('export')).game;
    const state = (await api('state')).state, bodypartIds = new Set(bodyparts.map(item => item.id));
    const ordinaryMinerals = state.market.filter(item => item.kind === 'material' && item.id.startsWith('fm-'));
    const stewardMinerals = state.tianjiSteward.offers.filter(item => item.kind === 'material' && item.itemId.startsWith('fm-'));
    check(ordinaryMinerals.length > 0 && ordinaryMinerals.every(item => item.grade <= 2 && !bodypartIds.has(item.id)), '普通坊市仍供一二阶矿物并排除五部位');
    check(stewardMinerals.length > 0 && stewardMinerals.every(item => item.grade >= 3 && !bodypartIds.has(item.itemId)), '天机阁执事仍供三至五阶矿物并排除五部位');
    for (const effect of ['poison_burst', 'fire_burst']) {
      const material = materials.find(item => item.grade === 3 && item.element === (effect === 'poison_burst' ? '木' : '火') && item.family === 'metal');
      await api('forge-preview', { version: 2, materials: Array(10).fill(material.id), main: material.id, shape: 'sword', effect, inscription: '' });
      await api('state'); await api('forge-options');
    }
    check((await api('export')).game === before, '查询目录成器和战斗预览不改变完整存档或RNG');
  });

  for (const mode of ['desktop', 'mobile']) for (const effect of ['poison_burst', 'fire_burst']) await scenario(`forge-${mode}-${effect}`, async () => {
    const game = gameFixture(3); game.player.arts.exp['器'] = 1000;
    for (const material of materials) game.player.arts.materials[material.id] = 50;
    await load(game); await openWorkshop(mode);
    const material = materials.find(item => item.grade === 3 && item.element === (effect === 'poison_burst' ? '木' : '火') && item.family === 'metal');
    const info = await fillWorkshop(mode, effect, material);
    check(/层|灼烧/.test(info.effectText) && !/清空全部|并清空|剩余回合的伤害/.test(info.effectText), `${mode}${effect}真实成器页已显示层数伤害保留状态规则`, info.effectText);
    check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), `${mode}${effect}工坊无横向溢出`);
    await shot(`forge-${mode}-${effect}`);
  });

  await scenario('five-grade-preview-and-execute', async () => {
    for (const effect of ['poison_burst', 'fire_burst']) for (let grade = 1; grade <= 5; grade++) {
      const expected = Math.round(12 * multipliers[effect][grade - 1]);
      const values = [];
      for (const empowered of [false, true]) {
        const fixture = await battleFixture(effect, grade, 12, 3, empowered), ability = abilityOf(fixture);
        check(ability?.playable && ability.rawDamage === expected, `${effect}${grade}阶${empowered ? '高境界强效' : '最低境界低效'}层数倍率正确`, { expected, actual: ability?.rawDamage });
        check(ability.breakdown?.statusLayers === 12 && ability.breakdown.damagePerLayer === multipliers[effect][grade - 1]
          && ability.breakdown.statusDamage === expected && ability.breakdown.consumesStatus === false,
          `${effect}${grade}阶后端明细为当前状态量乘固定倍率且不消耗状态`, ability.breakdown);
        const before = fixture.game.player.arts.battle;
        await api('battle/turn', { id: ability.id }); const after = (await exported()).player.arts.battle;
        check(after && before.enemyHp - after.enemyHp === ability.damage, `${effect}${grade}阶真实结算等于预览`, { preview: ability.damage, actual: after ? before.enemyHp - after.enemyHp : 'battle-ended' });
        check(after.poison === before.poison && after.poisonTurns === before.poisonTurns && after.poisonDamage === before.poisonDamage && after.burn === before.burn && after.burnTurns === before.burnTurns, `${effect}${grade}阶催动不消耗状态层数持续时间或毒伤缓存`);
        values.push(ability.rawDamage);
      }
      check(values[0] === values[1], `${effect}${grade}阶倍率不跟人物战力或材料效力变化`);
      const shorter = await battleFixture(effect, grade, 12, 1);
      check(abilityOf(shorter).rawDamage === expected, `${effect}${grade}阶不乘剩余持续回合`);
    }
  });

  await scenario('missing-or-expired-status-atomic-reject', async () => {
    for (const effect of ['poison_burst', 'fire_burst']) for (const [count, duration] of [[0, 0], [12, 0]]) {
      const fixture = await battleFixture(effect, 1, count, duration), ability = abilityOf(fixture);
      check(ability?.playable === false && Boolean(ability.reason), `${effect}无状态或已过期时预览禁用`, { count, duration, reason: ability?.reason });
      const before = (await api('export')).game;
      const rejected = await api('battle/turn', { id: ability.id }, true);
      check(Boolean(rejected.error) && (await api('export')).game === before, `${effect}无状态或已过期时真实拒绝不扣费`, { count, duration, error: rejected.error });
    }
  });

  await scenario('battle-readonly-purity', async () => {
    await battleFixture('poison_burst', 3);
    const before = (await api('export')).game;
    for (let repeat = 0; repeat < 3; repeat++) { await api('state'); await api('forge-options'); }
    check((await api('export')).game === before, '真实妖兽战斗连续预演不改变状态层数或掉落RNG快照');
  });

  for (const mode of ['desktop', 'mobile']) for (const effect of ['poison_burst', 'fire_burst']) await scenario(`battle-${mode}-${effect}`, async () => {
    const fixture = await battleFixture(effect, 3), ability = abilityOf(fixture);
    await openBattle(mode);
    const button = mode === 'mobile' ? page.locator('[data-command="battle/turn"]').filter({ hasText: '施展法宝' }) : page.locator(`[data-action="artifact-play"][data-id="${ability.id}"]`);
    await button.waitFor(); const text = await button.locator('..').innerText();
    check(text.includes(String(ability.rawDamage)) && !/并清空|清空全部|剩余回合的伤害/.test(text), `${mode}${effect}战斗面板数值与保留层数说明一致`, text);
    await shot(`battle-${mode}-${effect}`);
    const frozen = (await api('export')).game;
    await page.route('**/api/battle/turn', route => route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, error: '隔离验收503' }) }), { times: 1 });
    const failed = page.waitForResponse(response => new URL(response.url()).pathname === '/api/battle/turn'); await button.click(); await failed; await pause(350);
    if (mode === 'mobile') { await page.locator('[data-ui="close-sheet"]').click(); await page.locator('[data-ui="combat-detail"][data-value="artifacts"]').click(); }
    check((await api('export')).game === frozen && await button.isEnabled(), `${mode}${effect}技术失败不扣费并可恢复`);
    const before = (await exported()).player.arts.battle;
    await clickAction(button, `${mode}${effect}`); const after = (await exported()).player.arts.battle;
    check(before.enemyHp - after.enemyHp === ability.damage && after.poison === before.poison && after.burn === before.burn, `${mode}${effect}真实鼠标施展伤害等于预览且保留层数`);
    check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), `${mode}${effect}战斗无横向溢出`);
  });

  await scenario('hunt-win-five-bodyparts', async () => {
    const game = gameFixture(1); game.player.arts.materials = {};
    const material = materials.find(item => item.grade === 1 && item.element === '金' && item.family === 'metal');
    const gear = makeGear('damage', 1, material); game.player.arts.gear = [gear]; game.player.arts.equipped = { weapon: gear.id };
    await load(game); await api('eco', { type: 'hunt', id: '' });
    const battleGame = await exported(), battle = battleGame.player.arts.battle;
    check(battle.enemyBeast === true, '城市猎妖创建真实妖兽身份而非无属性修士');
    // 冻结真实猎妖目标；仅把该隔离目标置于可斩杀血线，验证正式动作和唯一胜利结算。
    report.limitations.push('浏览器五部位领奖将已冻结的真实猎妖目标调整至1气血，再由真实法器点击击杀；不代表自然战斗胜率。');
    battle.enemyHp = 1; battle.enemyShield = 0; battle.enemyWeaken = 0; battle.mana = battle.maxMana;
    const state = await load(battleGame), ability = state.battle.artifacts.find(item => item.id.includes(gear.id));
    await openBattle('desktop'); await shot('hunt-before-reward');
    await clickAction(page.locator(`[data-action="artifact-play"][data-id="${ability.id}"]`), '猎妖胜利');
    const after = await exported(), entries = Object.entries(after.player.arts.materials).filter(([, count]) => count > 0);
    check(!after.player.arts.battle && entries.length === 5, '真实猎妖胜利发五种不同部位');
    const drops = entries.map(([id, count]) => ({ ...materials.find(item => item.id === id), count }));
    check(drops.every(item => item.count === 1) && ['skin', 'bone', 'claw', 'eye', 'heart'].every(part => drops.some(item => item.part === part)), '每只妖兽皮鳞妖骨牙爪妖目妖心各一份', drops.map(item => ({ id: item.id, part: item.part, count: item.count })));
    const expectedElement = battle.enemyDao === '体' ? '气' : battle.enemyDao;
    const expectedGrade = Math.max(1, Math.min(5, Math.floor(battle.enemyStage / 3) + 1));
    check(drops.every(item => item.grade === expectedGrade && item.element === expectedElement) && new Set(drops.map(item => item.polarity)).size === 1, '五部位品阶属性阴阳一致并对应已冻结妖兽', { enemyDao: battle.enemyDao, enemyStage: battle.enemyStage, drops: drops.map(item => ({ grade: item.grade, element: item.element, polarity: item.polarity })) });
    check(drops.every(item => !['metal', 'stone'].includes(item.part)), '妖兽胜利不再掉商店矿物');
    report.hunt = { frozen: battle, drops }; await shot('hunt-after-reward');
    await openWorkshop('desktop'); await page.locator('[data-action="forge-slot"]').first().click();
    await page.locator('[data-forge-filter][data-field="grade"]').selectOption('1');
    await page.locator('[data-forge-filter][data-field="element"]').selectOption(drops[0].element);
    await page.locator('[data-forge-filter][data-field="family"]').selectOption('');
    await page.locator('[data-forge-filter][data-field="part"]').selectOption('skin');
    const row = page.locator('.forge-material').filter({ hasText: drops.find(item => item.part === 'skin').name }).first();
    check((await row.innerText()).includes('持有 1') && !/后续开放|尚待/.test(await row.innerText()), '战利品已进入真实工坊持有量及获取来源');
    await shot('hunt-bodypart-in-workshop');
  });
  check(errors.length === 0, '所有真实页面无脚本异常', errors);
  report.ok = failures.length === 0;
} catch (error) { report.error = error.stack || String(error); if (page) await shot('fatal').catch(() => {}); }
finally {
  if (browser) await browser.close();
  if (child && child.exitCode === null) { child.kill(); await Promise.race([exited, pause(5000)]); }
  writeFileSync(join(temporary, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ok: report.ok, checks: checks.length, failures: failures.length, port, evidence: temporary }, null, 2));
  if (!report.ok) process.exitCode = 1;
}
