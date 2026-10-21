// 本轮玩法验收：只创建本工具拥有的完整服务、临时目录与随机测试账号。
// 必须明确传入本轮成功构建的二进制；不连接8106/8107，不读取玩家存档。
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
const scenarioFilter = args.includes('--only') ? args[args.indexOf('--only') + 1].split(',') : [];
assert.ok(args.includes('--exe'), '必须用 --exe 明确指定本轮已经成功构建的新版EXE');
const input = resolve(root, args[args.indexOf('--exe') + 1]);
assert.ok(existsSync(input), '本轮EXE尚不存在');
const folder = join(root, 'artifacts/gameplay-usability-20261018'); mkdirSync(folder, { recursive: true });
const temporary = mkdtempSync(join(folder, 'run-'));
const executable = join(temporary, 'gameplay-usability-qa.exe'); copyFileSync(input, executable);
for (const name of ['msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll']) {
  if (existsSync(join(dirname(input), name))) copyFileSync(join(dirname(input), name), join(temporary, name));
}
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
assert.ok(![8106, 8107].includes(port));
const base = `http://127.0.0.1:${port}`;
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const checks = [], timings = [], failures = [], errors = [], serverLog = [], screenshots = [], network = [];
const report = { ok: false, input, port, temporary, scenarioFilter, checks, timings, failures, errors, serverLog, screenshots, network,
  binarySha256: createHash('sha256').update(readFileSync(input)).digest('hex'),
  limitations: ['高境界及物资夹具由新角色合法导出结构经正式import校验，不代表长期自然养成。', '四连胜奖励及恢复由主任务的定向HTTP/规则测试补充。'] };
const pause = ms => new Promise(done => setTimeout(done, ms));
let child, exited, browser, context, page, created, catalog;
function check(condition, name, detail) {
  assert.ok(condition, `${name}${detail === undefined ? '' : ': ' + JSON.stringify(detail)}`);
  checks.push({ name, ...(detail === undefined ? {} : { detail }) }); console.log(`PASS ${name}`);
}
async function api(path, body = {}, allowFailure = false) {
  await pause(75);
  const response = await context.request.post(`${base}/api/${path}`, { data: body });
  const result = await response.json();
  if (!allowFailure) assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
  return result;
}
const exported = async () => JSON.parse((await api('export')).game);
const load = async game => (await api('import', { game: JSON.stringify(game) })).state;
async function shot(name) {
  const path = join(temporary, `${name}.png`); await page.screenshot({ path, fullPage: true }); screenshots.push(path);
  const viewport = join(temporary, `${name}-viewport.png`); await page.screenshot({ path: viewport }); screenshots.push(viewport);
}
async function run(name, callback) {
  if (scenarioFilter.length && !scenarioFilter.some(filter => name.includes(filter))) return;
  console.log(`SCENARIO ${name}`);
  try { await callback(); }
  catch (error) {
    failures.push({ scenario: name, reproduction: name, error: error.stack || error.message });
    await shot(`failure-${name}`).catch(() => {}); console.error(`${name}: ${error.message}`);
  }
}
async function open(mode, classic, mobile) {
  const small = mode === 'mobile';
  await page.setViewportSize({ width: small ? 390 : 1440, height: small ? 844 : 1000 });
  const fresh = `?qa=${Date.now()}`;
  await page.goto(base + (small ? `/v2.html${fresh}#/${mobile}` : `/${fresh}#${classic}`), { waitUntil: 'domcontentloaded' });
  await page.locator(small ? '.bottom-nav, .mobile-bottom, .app-header' : '.main-tabs').first().waitFor({ timeout: 25000 });
  await page.waitForFunction(() => document.readyState === 'complete' && document.body.getAttribute('aria-busy') !== 'true');
  await pause(180);
  if (!small && classic === 'arts/spells') await page.locator('[data-subtab="arts"][data-sub="spells"]').click();
}
async function noOverflow(name) {
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), `${name}没有横向溢出`);
}
async function action(path, callback, name) {
  let count = 0;
  const listener = request => { if (new URL(request.url()).pathname === `/api/${path}` && request.method() === 'POST') count++; };
  page.on('request', listener);
  const started = performance.now();
  const pending = page.waitForResponse(response => new URL(response.url()).pathname === `/api/${path}` && response.request().method() === 'POST').catch(error => error);
  try { await callback(); } catch (error) { page.off('request', listener); throw error; }
  const response = await pending; if (response instanceof Error) { page.off('request', listener); throw response; }
  const result = await response.json();
  const responseMilliseconds = performance.now() - started;
  await pause(220); page.off('request', listener);
  timings.push({ action: name, milliseconds: responseMilliseconds, duplicateObservationMilliseconds: 220, postCount: count });
  assert.ok(response.ok() && result.ok !== false && !result.error, `${name}: ${result.error || response.status()}`);
  return { result, count };
}
function fixture(stage = 0) {
  const game = structuredClone(created); game.pending = null; game.job = null; game.npcs = [];
  Object.assign(game.player, { stage, highestStage: stage, stones: 1000000, health: 100, lifespan: 2000, location: 'wuling' });
  Object.assign(game.player.arts, { battle: null, poison: 0, used: {}, combatPrep: {} });
  return game;
}

try {
  child = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
    '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')],
  { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  exited = new Promise(done => child.once('exit', done));
  child.stdout.on('data', data => serverLog.push(String(data))); child.stderr.on('data', data => serverLog.push(String(data)));
  for (let attempt = 0; attempt < 200; attempt++) {
    try { if ((await fetch(base + '/api/mode')).ok) break; } catch { /* own service starting */ }
    if (child.exitCode !== null) throw new Error(`Own process exited ${child.exitCode}`);
    if (attempt === 199) throw new Error('Own service startup timed out');
    await pause(50);
  }
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(12000); page.on('pageerror', error => errors.push(error.message));
  const requestStarts = new WeakMap();
  page.on('request', request => requestStarts.set(request, performance.now()));
  page.on('requestfailed', request => network.push({ path: new URL(request.url()).pathname, failed: request.failure()?.errorText, milliseconds: performance.now() - (requestStarts.get(request) || performance.now()) }));
  page.on('response', response => { const path = new URL(response.url()).pathname; if (path.startsWith('/api/') || response.status() >= 400) network.push({ path, status: response.status(), milliseconds: performance.now() - (requestStarts.get(response.request()) || performance.now()) }); });
  await api('login', { nickname: `UseQa${Date.now().toString(36)}`, password: 'isolated-gameplay-qa' });
  await api('new', { name: '玩法隔离验收', gender: '男', portraitId: 'player-male', seed: 734, traits: ['artifactHeir'], allocation: {} });
  created = await exported(); writeFileSync(join(temporary, 'created.json'), JSON.stringify(created));
  catalog = await api('forge-options');
  console.log(`OWN_PREVIEW ${base}`);

  await run('http-readonly-purity-payload', async () => {
    const before = (await api('export')).game;
    const state = (await api('state')).state;
    const recipeId = state.brewOptions[0].id;
    const stateSamples = [];
    for (let iteration = 0; iteration < 3; iteration++) {
      await pause(75);
      const started = performance.now();
      const response = await context.request.post(base + '/api/state', { data: {} });
      const bytes = await response.body();
      assert.ok(response.ok()); stateSamples.push({ bytes: bytes.length, milliseconds: performance.now() - started });
      await api('ascension-tournament'); await api('tianji-shop');
      await api('pills/use-quote', { id: '5502', count: 1 });
      await api('alchemy/quote', { id: recipeId, count: 3 });
    }
    check((await api('export')).game === before, 'HTTP连续三轮state/大会/货架/服丹/炼丹报价保持完整game逐字不变');
    report.stateSamples = stateSamples;
    console.log('STATE_PAYLOAD ' + JSON.stringify(stateSamples));
  });

  await run('birth-invitation-desktop', async () => {
    const gear = created.player.arts.gear;
    check(gear.length === 2 && gear.every(item => item.forgeVersion === 2 && item.grade === 2), '护道遗宝出生两件二品V2法器', gear);
    check(gear.some(item => item.effectId === 'damage') && gear.some(item => item.effectId === 'turn_shield'), '遗宝主动直伤及每轮盾使用原有槽位');
    await open('desktop', 'home', 'home');
    const decline = page.locator('[data-action="choose"][data-id="decline"]');
    await decline.waitFor(); await shot('desktop-first-year-invitation');
    await action('choose', () => decline.click(), '首年邀请先行整备');
    check(!(await exported()).pending, '先行整备真实解除邀请阻塞');
    const enter = page.locator('[data-action="ascension-tournament"]'); await enter.waitFor();
    await action('ascension-tournament/enter', () => enter.evaluate(node => { node.click(); node.click(); }), '入口真实报名');
    let state = (await api('state')).state;
    check(state.battle && state.ascensionTournament.active, '入口报名真实进入大会首战');
    const ability = state.battle.artifacts.find(item => item.rawDamage === 40);
    check(ability?.playable && (await exported()).player.arts.battle.shield === 8, '炼气遗宝40点主动伤害可用且入战8盾', ability);
    const before = (await exported()).player.arts.battle;
    await action('battle/turn', () => page.locator(`[data-action="artifact-play"][data-id="${ability.id}"]`).click(), '大会首战真实催动遗宝');
    const after = (await exported()).player.arts.battle;
    check(before.enemyHp - after.enemyHp === 40, '真实斗法直伤40与预览一致');
    await action('battle/turn', () => page.locator('[data-action="end-turn"]').click(), '大会真实结束回合');
    check((await exported()).player.arts.battle.shield === 8, '遗宝下一回合实际补充固定8盾');
    await shot('desktop-tournament-first-battle');
    const escape = page.locator('[data-action="escape"]');
    await action('battle/turn', () => escape.click(), '大会认输');
    state = (await api('state')).state;
    check(!state.battle && !state.ended && state.ascensionTournament.attempted && !state.ascensionTournament.enabled, '认输不致死，本届不能重复参赛');
    const beforeDuplicate = (await api('export')).game;
    const duplicate = await api('ascension-tournament/enter', {}, true);
    check(Boolean(duplicate.error) && (await api('export')).game === beforeDuplicate, '绕过按钮重复报名仍拒绝且状态不变');
  });

  await run('birth-invitation-mobile', async () => {
    await load(created); await open('mobile', 'home', 'home');
    const decline = page.locator('[data-command="choose"]').filter({ hasText: '先行整备' });
    await decline.waitFor(); await shot('mobile-first-year-invitation');
    await action('choose', () => decline.click(), '390首年邀请先行整备');
    await action('ascension-tournament/enter', () => page.locator('[data-command="ascension-tournament/enter"]').click(), '390大会入口报名');
    await page.locator('[data-ui="combat-detail"][data-value="artifacts"]').click();
    const ability = page.locator('[data-command="battle/turn"]').filter({ hasText: '施展法宝' }); await ability.waitFor();
    check((await ability.locator('..').innerText()).includes('40'), '390遗宝法宝面板固定40伤害可见');
    await shot('mobile-tournament-artifact');
    const before = (await exported()).player.arts.battle;
    await action('battle/turn', () => ability.click(), '390首战真实催动遗宝');
    const after = (await exported()).player.arts.battle;
    check(before.enemyHp - after.enemyHp === 40 && before.shield === 8, '390真实遗宝40伤8盾与桌面一致');
    const close = page.locator('[data-ui="close-sheet"]'); if (await close.isVisible()) await close.click();
    await page.locator('[data-ui="combat-detail"][data-value="more"]').click();
    await action('battle/turn', () => page.locator('[data-command="battle/turn"]').filter({ hasText: /退出切磋|认输/ }).click(), '390大会真实认输');
    const state = (await api('state')).state;
    check(!state.battle && !state.ended && !state.ascensionTournament.enabled, '390认输后同届不可重复');
    await noOverflow('390大会');
  });

  for (const mode of ['desktop', 'mobile']) {
    await run(`pill-batch-${mode}`, async () => {
      const game = fixture(); game.player.arts.potions = { '5502': 20, '5207': 10 };
      await load(game); await open(mode, 'inventory', 'inventory');
      const section = page.locator('[data-pill-batch="5502"]');
      if (mode === 'desktop') {
        const details = page.locator('[data-inventory-item="5502"] .inventory-pill-details');
        await details.locator(':scope > summary').click();
      } else {
        const select = page.locator('select[data-ui="inventory-item"]'); await select.waitFor(); await select.selectOption('potions:5502');
      }
      const number = section.locator('input[type="number"]'), range = section.locator('input[type="range"]');
      await number.waitFor(); await page.waitForFunction(() => !document.querySelector('[data-pill-batch="5502"] input[type="number"]')?.disabled);
      // 真实服务先算2枚报价但延迟返回；随后选3，旧响应不得覆盖新选择。
      await page.route('**/api/pills/use-quote', async route => {
        const response = await route.fetch(); await pause(700); await route.fulfill({ response });
      }, { times: 1 });
      const stale = page.waitForRequest(request => new URL(request.url()).pathname === '/api/pills/use-quote');
      await range.focus(); await range.press('Home'); await range.press('ArrowRight'); await stale;
      await range.press('ArrowRight');
      await page.waitForFunction(() => !document.querySelector('[data-pill-batch="5502"] [data-pill-batch-use]')?.disabled);
      await pause(800);
      check(await section.evaluate(node => node._pillQuote?.requestedCount === 3), `${mode}迟到的2枚报价不会覆盖3枚选择`);
      check(await number.inputValue() === '3', `${mode}滑条与数量同步`);
      const before = await exported();
      const used = await action('pills/use', () => section.locator('[data-pill-batch-use]').evaluate(node => { node.click(); node.click(); }), `${mode}批量服3枚及重复点击`);
      const after = await exported();
      check(used.count === 1 && before.player.arts.potions['5502'] - after.player.arts.potions['5502'] === 3, `${mode}一次POST仅扣3枚`);
      await shot(`${mode}-pill-batch-success`); await noOverflow(`${mode}批量服丹`);
      // 重新展开后注入一次未提交的503，证明错误后真实恢复。
      if (mode === 'desktop') {
        const details = page.locator('[data-inventory-item="5502"] .inventory-pill-details');
        if (await details.getAttribute('open') === null) await details.locator(':scope > summary').click();
      }
      await page.waitForFunction(() => !document.querySelector('[data-pill-batch="5502"] [data-pill-batch-use]')?.disabled);
      const frozen = (await api('export')).game;
      // 放慢503后的真实目录恢复，稳定暴露“报价先返回、动作锁尚未解除”的窗口。
      // 这里只延迟已有只读响应；不能让启用按钮靠静默吞点击代替忙碌状态。
      if (mode === 'mobile') await page.route('**/api/eco-options', async route => {
        const response = await route.fetch(); await pause(2000); await route.fulfill({ response });
      }, { times: 1 });
      await page.route('**/api/pills/use', route => route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, error: '隔离验收503，请重试' }) }), { times: 1 });
      const failed = page.waitForResponse(response => new URL(response.url()).pathname === '/api/pills/use');
      await section.locator('[data-pill-batch-use]').click(); await failed; await pause(250);
      check((await api('export')).game === frozen, `${mode}503不扣丹不加毒`);
      if (mode === 'desktop') {
        const details = page.locator('[data-inventory-item="5502"] .inventory-pill-details');
        if (await details.getAttribute('open') === null) await details.locator(':scope > summary').click();
      }
      await page.waitForFunction(() => !document.querySelector('[data-pill-batch="5502"] [data-pill-batch-use]')?.disabled);
      await action('pills/use', () => section.locator('[data-pill-batch-use]').click(), `${mode}错误后真实重试成功`);
      // 下方详情真实滚动/展开保持位置；只读报价不应重绘到页顶。
      await section.scrollIntoViewIfNeeded();
      const scrollBefore = await page.evaluate(() => scrollY);
      await number.fill('1'); await number.dispatchEvent('input'); await pause(450);
      const scrollAfter = await page.evaluate(() => scrollY);
      check(Math.abs(scrollAfter - scrollBefore) < 4, `${mode}重新报价不回页顶`, { scrollBefore, scrollAfter });
    });

    await run(`spells-lifebound-${mode}`, async () => {
      const game = fixture(3); game.player.arts.lifeboundKind = 'retain'; game.player.arts.lifeboundSpell = '';
      let state = await load(game);
      const spellCatalog = (await api('arts/spells')).spells || [];
      const fixtureSpells = spellCatalog.filter(item => !item.mandatory && !['strike', 'defend', 'skim', 'refresh', 'purge'].includes(item.id) && Number(item.stage || 0) <= 3).slice(0, state.arts.spellSlots || 4).map(item => item.id);
      if (fixtureSpells.length) {
        game.player.arts.knownSpells = [...new Set([...game.player.arts.knownSpells, ...fixtureSpells])];
        game.player.arts.spells = fixtureSpells; state = await load(game);
      }
      await open(mode, 'arts/spells', 'arts/spells');
      const slot = page.locator('.lifebound-slot'); await slot.waitFor();
      check(await slot.count() === 1 && (await slot.innerText()).includes('保留'), `${mode}独立唯一本命槽保留筑基路线`);
      const buttons = mode === 'mobile' ? '[data-command="arts/lifebound"]' : '[data-action="lifebound"]';
      for (const id of ['strike', 'defend', 'strike']) {
        const button = mode === 'mobile' ? slot.locator(buttons).filter({ hasText: id === 'strike' ? '普通攻击' : '普通防御' }) : slot.locator(`${buttons}[data-id="${id}"]`);
        await action('arts/lifebound', () => button.click(), `${mode}本命改为${id}`);
        check((await exported()).player.arts.lifeboundSpell === id, `${mode}本命实际存档为${id}`);
      }
      const scroll = page.locator('[data-ui-scroll="equipped-spells"]');
      const info = await scroll.evaluate(node => ({ overflow: getComputedStyle(node).overflowY, maxHeight: getComputedStyle(node).maxHeight, client: node.clientHeight, height: node.scrollHeight }));
      check(['auto', 'scroll'].includes(info.overflow) && info.maxHeight !== 'none', `${mode}携带列表具有独立滚动容器`, info);
      if (info.height > info.client + 2) {
        await scroll.scrollIntoViewIfNeeded();
        const before = await page.evaluate(() => scrollY);
        await scroll.hover(); await page.mouse.wheel(0, 500); await pause(200);
        const after = await scroll.evaluate(node => ({ inner: node.scrollTop, outer: scrollY }));
        check(after.inner > 0 && Math.abs(after.outer - before) < 4, `${mode}鼠标滚轮真实滚动携带栏且页面不跟滚`, after);
      } else report.limitations.push(`${mode}当前合法携带数量未超出栏高，已验证滚动容器但未触发内滚。`);
      await shot(`${mode}-spells-lifebound`); await noOverflow(`${mode}神通及本命`);
    });

    await run(`alchemy-${mode}`, async () => {
      const game = fixture(3); game.player.arts.exp['丹'] = 1000; game.player.arts.durability = 100;
      let state = await load(game);
      for (const herb of state.herbCatalog || []) game.player.arts.herbs[herb.name] = 1000;
      for (const recipe of state.brewOptions || []) for (const ingredient of recipe.batchQuotes?.[3]?.ingredients || []) game.player.arts.herbs[ingredient.name] = 1000;
      state = await load(game); await open(mode, 'workshop/brew', 'inventory/workshop');
      if (mode === 'mobile') await page.locator('[data-ui="workshop-tab"][data-value="brew"]').click();
      const select = page.locator(mode === 'mobile' ? '[data-ui="brew-batch"]' : '[data-brew-batch]'); await select.selectOption('3');
      const summary = page.locator('.alchemy-batch-summary'); await summary.waitFor();
      check((await summary.innerText()).includes('本批 3 炉') && (await summary.innerText()).includes('开炉费用'), `${mode}整批三炉费用报价`);
      check(await page.locator('.alchemy-ingredients [role="row"]').count() > 1, `${mode}真实展示整批材料需有缺`);
      check((await page.locator('body').innerText()).includes('一次投入整批材料与费用'), `${mode}明确一次整批投入文案`);
      await shot(`${mode}-alchemy-batch-3`); await noOverflow(`${mode}炼丹整批报价`);
      const recipeId = await page.locator('[data-selected-recipe]').getAttribute('data-selected-recipe');
      const quoteResult = await api('alchemy/quote', { id: recipeId, count: 3 });
      const quote = quoteResult.quote || quoteResult;
      check(quote.enabled, `${mode}三炉真实夹具具备开炉资格`, quote.reason);
      const before = await exported();
      const craft = page.locator(mode === 'mobile' ? '[data-command="craft"]' : `[data-action="craft"][data-id="${recipeId}"]`);
      const result = await action('craft', () => craft.evaluate(node => { node.click(); node.click(); }), `${mode}三炉整批真实开炉`);
      const after = await exported();
      check(result.count === 1 && before.player.stones - after.player.stones === quote.stoneCost, `${mode}三炉一次POST扣整批费用一次`, { count: result.count, stoneCost: quote.stoneCost, actual: before.player.stones - after.player.stones });
      check(quote.ingredients.every(item => before.player.arts.herbs[item.name] - after.player.arts.herbs[item.name] === item.required), `${mode}开炉一次投入整批材料`);
    });

    await run(`market-steward-${mode}`, async () => {
      const state = await load(fixture(3)); await open(mode, 'market', 'inventory/market');
      const ordinary = state.market.filter(item => item.kind === 'material');
      check(ordinary.length >= 10 && ordinary.every(item => item.grade <= 2), `${mode}普通坊市新矿材并限制低阶`, ordinary.map(item => ({ id: item.id, grade: item.grade })));
      const stewardTab = page.locator(mode === 'mobile' ? '[data-ui="market-kind-tab"]' : '[data-shop="steward"]');
      if (mode === 'mobile') await stewardTab.selectOption('steward'); else await stewardTab.click();
      const host = page.locator('[data-steward-shop]'); await host.waitFor();
      for (const steward of state.tianjiSteward.stewards.filter(item => mode === 'desktop' || item.isHere)) check((await host.innerText()).includes(steward.name) && (await host.innerText()).includes(steward.realm), `${mode}执事${steward.name}姓名境界可见`);
      const row = host.locator('[data-steward-row]').first(), count = row.locator('[data-steward-count]');
      await count.fill('2'); await count.dispatchEvent('input');
      await page.waitForFunction(() => !document.querySelector('[data-steward-row] [data-steward-buy]')?.disabled);
      const before = await exported();
      const result = await action('tianji-shop/buy', () => row.locator('[data-steward-buy]').evaluate(node => { node.click(); node.click(); }), `${mode}执事固定价买2件`);
      const body = result.result;
      check(result.count === 1 && (await exported()).player.stones < before.player.stones, `${mode}执事真实购买扣费且重复点击只一次`, body.quote);
      await shot(`${mode}-steward-purchased`); await noOverflow(`${mode}执事`);
    });

    await run(`forge-illustrations-${mode}`, async () => {
      const game = fixture(12), materials = catalog.materials || catalog.options?.materials;
      for (const item of materials) game.player.arts.materials[item.id] = 50;
      game.player.arts.exp['器'] = 1000; await load(game); await open(mode, 'workshop/forge', 'inventory/workshop');
      if (mode === 'mobile') await page.locator('[data-ui="workshop-tab"][data-value="forge"]').click();
      const select = page.locator(mode === 'mobile' ? '[data-ui="forge-field"][data-field="shape"]' : '[data-forge-field][data-field="shape"]');
      await select.waitFor();
      const shapes = catalog.shapes || catalog.options?.shapes; check(shapes.length === 11, '正式工坊目录11器型');
      for (const shape of shapes) {
        await select.selectOption(shape.id);
        await page.waitForFunction(id => { const image = document.querySelector('.forge-vessel-art img[data-artifact-illustration]'); return image?.complete && image.naturalWidth > 0 && image.src.includes(`/${id}-v1.png`); }, shape.id);
        check(true, `${mode}工坊${shape.name}PNG真实加载`);
      }
      await shot(`${mode}-forge-illustrations`); await noOverflow(`${mode}11器型`);
      // 真实放入二阶金矿，选择破阵效果，核对成器固定40文案。
      const item = materials.find(item => item.grade === 2 && item.element === '金' && item.family === 'metal');
      const info = (await api('forge-preview', { version: 2, materials: Array(10).fill(item.id), main: item.id, shape: 'sword', effect: 'damage', inscription: '' })).info;
      check(info.grade === 2 && info.effectText.includes('40') && !/战力伤害基数/.test(info.effectText), `${mode}成器预览二品直伤固定40`, info.effectText);
      await select.selectOption('sword');
      const key = mode === 'mobile' ? 'data-ui' : 'data-action', idKey = mode === 'mobile' ? 'data-value' : 'data-id';
      await page.locator(`[${key}="forge-slot"]`).first().click();
      const filter = mode === 'mobile' ? '[data-ui="forge-filter"]' : '[data-forge-filter]';
      await page.locator(`${filter}[data-field="grade"]`).selectOption('2');
      await page.locator(`${filter}[data-field="element"]`).selectOption('金');
      await page.locator(`${filter}[data-field="family"]`).selectOption('metal');
      await page.locator(`[${key}="forge-fill"][${idKey}="${item.id}"]`).click();
      const field = mode === 'mobile' ? '[data-ui="forge-field"]' : '[data-forge-field]';
      await page.locator(`${field}[data-field="effect"]`).selectOption('damage');
      await page.waitForFunction(() => /40/.test(document.querySelector('.forge-result p')?.textContent || ''));
      check((await page.locator('.forge-result').innerText()).includes('40'), `${mode}真实投料后工坊UI显示固定40`);
      await shot(`${mode}-forge-fixed-40`);
    });
  }
  check(errors.length === 0, '真实浏览器没有脚本异常', errors);
  report.ok = failures.length === 0;
} catch (error) {
  report.error = error.stack || error.message;
  if (page) await shot('fatal-error').catch(() => {});
} finally {
  if (browser) await browser.close();
  if (child && child.exitCode === null) { child.kill(); await Promise.race([exited, pause(5000)]); }
  writeFileSync(join(temporary, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ok: report.ok, checks: checks.length, failures: failures.length, port, evidence: temporary }, null, 2));
  if (!report.ok) process.exitCode = 1;
}
