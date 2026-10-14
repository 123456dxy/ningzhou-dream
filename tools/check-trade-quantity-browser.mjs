// 完整桌面一键交易/数量滑条冒烟。使用随机端口、临时账号和临时存档；不碰玩家8108服务。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..'), args = process.argv.slice(2);
const arg = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const binary = resolve(root, arg('--exe', 'build-vs-share/Release/ningzhou_web.exe'));
assert.ok(existsSync(binary), '需要本轮新编译的完整Web程序');
const directory = join(root, 'build', 'trade-quantity-20261014'); mkdirSync(directory, { recursive: true });
const temporary = mkdtempSync(join(directory, 'browser-')), isolated = join(temporary, 'trade-qa.exe');
copyFileSync(binary, isolated);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(close => reservation.close(close));
assert.ok(![8107, 8108].includes(port), '玩家端口不得用于自动测试');
const base = `http://127.0.0.1:${port}`;
const require = createRequire(import.meta.url);
const { chromium } = require(arg('--playwright-module', process.env.PLAYWRIGHT_MODULE || 'C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
const results = [], timings = [], errors = [], serverLog = [], requests = [];
let server, browser, context, page;
// 用途：保留可核对断言结果；流程：本轮真实服务器冒烟；用法：传条件与说明；返回：无，失败立即抛出。
function check(value, label) { assert.ok(value, label); results.push({ label, passed: true }); console.log(`PASS ${label}`); }
try {
  server = spawn(isolated, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
    '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')],
    { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  server.stdout.on('data', chunk => serverLog.push(chunk.toString())); server.stderr.on('data', chunk => serverLog.push(chunk.toString()));
  let ready = false;
  for (let attempt = 0; attempt < 200; attempt++) {
    if (server.exitCode !== null) throw new Error('隔离服务器提前退出');
    try { ready = (await fetch(base + '/api/mode')).ok; if (ready) break; } catch {}
    await new Promise(done => setTimeout(done, 50));
  }
  check(ready, '隔离完整服务器启动');
  browser = await chromium.launch({ executablePath: arg('--browser-path', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'), headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN' });
  page = await context.newPage(); page.setDefaultTimeout(20000); page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST') requests.push({ url: request.url(), body: request.postDataJSON() }); });
  // 用途：准备及查询隔离角色；流程：真实HTTP夹具；用法：传路径和可选正文；返回：成功JSON，拒绝立即失败。
  async function api(path, body) {
    const response = body === undefined ? await context.request.get(`${base}/api/${path}`)
      : await context.request.post(`${base}/api/${path}`, { data: body });
    const result = await response.json(); assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`); return result;
  }
  await api('login', { nickname: `TradeQa${Date.now().toString(36)}`, password: 'trade-isolated-qa' });
  await api('new', { name: '一键交易验收', gender: '男', portraitId: 'player-male', seed: 514017, traits: [], allocation: {} });
  const initial = JSON.parse((await api('export')).game), initialState = (await api('state')).state;
  const pill = initialState.pillCatalog.find(item => item.grade === 1 && item.pricing?.valid !== false);
  assert.ok(pill, '一阶丹药夹具存在');
  const fixture = structuredClone(initial); fixture.player.stage = 5; fixture.player.xp = 0; fixture.player.lifespan = 250; fixture.player.stones = 1000000;
  fixture.player.arts.potions[pill.id] = 20; fixture.player.renown = 0;
  fixture.clock.days = 59; fixture.clock.ageBase = fixture.player.age; fixture.year = 0;
  fixture.plan = 'rest'; fixture.job = null; fixture.pending = null; fixture.player.arts.battle = null;
  Object.assign(fixture.tide, { active: true, startYear: 0, severity: 1, rulesVersion: 1, eventId: 'trade-tide-qa',
    startDay: 0, endDay: 60, durationDays: 60, lastProcessedDay: 59, monthsLeft: 1, playerJoined: true,
    playerScore: 3000, playerEntryStage: 5, playerRank: 0, totalScore: 0, settled: false, rewardPaid: false,
    rewardStones: 0, rewardRenown: 0, participants: [], marketStock: [], marketExpiresDay: -1 });
  await api('import', { game: JSON.stringify(fixture) }); await api('advance', { days: 1 });
  const tideState = (await api('state')).state;
  const material = tideState.tide.market.items.find(item => item.grade === 3 && item.maxPurchase?.enabled);
  assert.ok(material, '兽潮三阶有可购买材料');
  await page.goto(base + '/#chronicle', { waitUntil: 'domcontentloaded' });
  await page.locator('[data-action="tide-market-open"]').click(); await page.locator('.tide-market').waitFor();
  const row = page.locator('.tide-market-row').filter({ has: page.locator(`[data-action="tide-market-buy-max"][data-kind="${material.kind}"][data-id="${material.id}"]`) });
  const slider = row.locator('[data-tide-quantity]');
  check(await slider.getAttribute('type') === 'range' && Number(await slider.getAttribute('max')) === material.maxPurchase.count, '真实兽潮滑条采用后端最大份数');
  await slider.evaluate(input => { window.__rangeNode = input; input.value = '2'; input.dispatchEvent(new Event('input', { bubbles: true })); });
  check(await row.locator('[data-quantity-value]').innerText() === '2'
    && await row.locator('[data-quantity-submit]').getAttribute('data-count') === '2'
    && (await row.locator('[data-quantity-total]').innerText()).includes(String(material.unitPrice * 2)), '拖动即时同步数量、按钮提交和总价');
  await slider.focus(); await page.keyboard.press('ArrowRight');
  check(await slider.evaluate(input => window.__rangeNode === input) && await row.locator('[data-quantity-value]').innerText() === '3', '键盘调量后仍为同一DOM节点');
  const sliderBox = await slider.boundingBox();
  await page.mouse.move(sliderBox.x + sliderBox.width * .3, sliderBox.y + sliderBox.height / 2);
  await page.mouse.down(); await page.mouse.move(sliderBox.x + sliderBox.width * .8, sliderBox.y + sliderBox.height / 2, { steps: 6 }); await page.mouse.up();
  check(await slider.evaluate(input => window.__rangeNode === input && Number(input.value) > 3)
    && Number(await row.locator('[data-quantity-value]').innerText()) === Number(await slider.inputValue())
    && await row.locator('[data-quantity-submit]').getAttribute('data-count') === await slider.inputValue(), '真实鼠标连续拖动可完成并保留节点与提交数量');
  const buyButton = row.locator('[data-action="tide-market-buy-max"]');
  check((await buyButton.innerText()).includes(`${material.maxPurchase.count} 份`) && (await buyButton.innerText()).includes(String(material.maxPurchase.totalCost)), '一键买满展示真实份数及总价');
  const buyStart = performance.now(), buyResponse = page.waitForResponse(response => response.url() === `${base}/api/tide/market/buy`);
  const buyPostsBefore = requests.filter(request => request.url.endsWith('/tide/market/buy')).length;
  await buyButton.evaluate(button => { button.click(); button.click(); });
  const bought = await (await buyResponse).json(); assert.ok(!bought.error && bought.ok !== false, '买满成功');
  await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  timings.push({ label: '兽潮一键买满至界面恢复', milliseconds: performance.now() - buyStart });
  const purchased = bought.state.tide.market.items.find(item => item.id === material.id && item.kind === material.kind);
  check(bought.state.player.stones === tideState.player.stones - material.maxPurchase.totalCost
    && purchased.owned === material.owned + material.maxPurchase.count && purchased.stock === material.stock - material.maxPurchase.count, '真实买满扣费、到账与库存相符');
  check(requests.filter(request => request.url.endsWith('/tide/market/buy')).length - buyPostsBefore === 1, '双击买满只发送一次POST');
  check(await row.locator('[data-action="tide-market-buy-max"]').isDisabled(), '买尽库存后按钮禁用');
  await page.setViewportSize({ width: 850, height: 900 }); await page.screenshot({ path: join(temporary, 'tide-market-850.png'), fullPage: true });
  await row.scrollIntoViewIfNeeded(); await page.screenshot({ path: join(temporary, 'tide-market-850-viewport.png'), fullPage: false });
  check(await row.locator('[data-action="tide-market-buy-max"]').isVisible()
    && await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth), '850px桌面交易入口可见且无整页横向溢出');

  const saleFixture = structuredClone(initial); saleFixture.player.stones = 10000; saleFixture.player.renown = 0;
  saleFixture.player.arts.potions[pill.id] = 20; saleFixture.economy.marketCash = 1000000; saleFixture.economy.sold = {};
  await api('import', { game: JSON.stringify(saleFixture) });
  const saleState = (await api('state')).state, sale = saleState.pillCatalog.find(item => item.id === pill.id).maxSale;
  check(sale.enabled && sale.count === 8 && sale.totalGain === sale.count * sale.unitPrice && sale.limitReason.length > 0, '20枚丹药报价受本月8枚需求限制并解释原因');
  await page.goto(base + '/?qa=sale#inventory', { waitUntil: 'domcontentloaded' });
  const details = page.locator(`[data-inventory-pill-details="${pill.id}"]`); await details.locator('summary').first().click();
  const sellButton = details.locator('[data-action="sell-max"]'); await sellButton.waitFor();
  check(await sellButton.innerText() === '一键售出' && (await details.locator('[data-trade-market-cash]').innerText()).includes(String(saleState.world?.marketCash ?? saleState.marketCash)),
    '行囊售出仅显示简单按钮，旁边直接显示坊市剩余收购资金');
  let failNext = true;
  await page.route('**/api/sell', async route => {
    if (failNext) { failNext = false; await route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, error: '隔离验收：临时503' }) }); }
    else await route.continue();
  });
  await sellButton.click(); await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  check((await api('state')).state.arts.potions[pill.id] === 20 && await sellButton.isEnabled(), '技术错误不扣库存且一键售出可重试');
  const sellStart = performance.now(), sellResponse = page.waitForResponse(response => response.url() === `${base}/api/sell`);
  const sellPostsBefore = requests.filter(request => request.url.endsWith('/sell')).length;
  await sellButton.evaluate(button => { button.click(); button.click(); });
  const sold = await (await sellResponse).json(); assert.ok(!sold.error && sold.ok !== false, '售尽成功');
  await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  timings.push({ label: '丹药一键售出至界面恢复', milliseconds: performance.now() - sellStart });
  check(sold.state.arts.potions[pill.id] === 12 && sold.state.player.stones === saleState.player.stones + sale.totalGain
    && sold.gain === sale.totalGain, '实际一次售出8枚且总收益等于报价');
  check(requests.filter(request => request.url.endsWith('/sell')).length - sellPostsBefore === 1, '双击售出只发送一次POST');
  const limited = structuredClone(saleFixture); limited.economy.marketCash = sale.unitPrice * 3;
  await api('import', { game: JSON.stringify(limited) });
  const limitedSale = (await api('state')).state.pillCatalog.find(item => item.id === pill.id).maxSale;
  check(limitedSale.count === 3 && limitedSale.limitReason.length > 0, '资金只能买3枚时服务端最大出售数量为3');
  await page.goto(base + '/?qa=limited#inventory', { waitUntil: 'domcontentloaded' }); await details.locator('summary').first().click();
  await page.screenshot({ path: join(temporary, 'pill-sale-850.png'), fullPage: true });
  const limitedResponse = page.waitForResponse(response => response.url() === `${base}/api/sell`); await details.locator('[data-action="sell-max"]').click();
  const limitedSold = await (await limitedResponse).json();
  check(limitedSold.state.arts.potions[pill.id] === 17 && limitedSold.gain === limitedSale.totalGain, '资金限制的3枚真实售出');
  await page.goto(base + '/#market', { waitUntil: 'domcontentloaded' });
  await page.locator('[data-shop="herb"]').click();
  const ordinarySlider = page.locator('[data-qtyinput]').first(); await ordinarySlider.waitFor();
  await ordinarySlider.evaluate(input => { window.__ordinaryNode = input; input.value = '5'; input.dispatchEvent(new Event('input', { bubbles: true })); input.dispatchEvent(new Event('change', { bubbles: true })); });
  const ordinaryRow = ordinarySlider.locator('xpath=ancestor::*[@data-quantity-row][1]');
  check(await ordinarySlider.evaluate(input => window.__ordinaryNode === input)
    && await ordinaryRow.locator('[data-quantity-submit]').getAttribute('data-count') === '5'
    && (await ordinaryRow.locator('[data-quantity-total]').innerText()).includes(String(Number(await ordinaryRow.locator('[data-quantity-submit]').getAttribute('data-unit-price')) * 5)), '普通坊市滑条变更同步总价且不重建节点');
  await api('import', { game: JSON.stringify(saleFixture) }); await page.goto(base + '/?qa=workshop#workshop', { waitUntil: 'domcontentloaded' });
  const workshopSale = page.locator('.pill-row').filter({ has: page.locator(`[data-action="sell-max"][data-id="${pill.id}"]`) });
  await workshopSale.waitFor();
  check((await workshopSale.locator('[data-trade-market-cash]').innerText()).includes('1000000')
    && await workshopSale.locator('[data-action="sell-max"]').innerText() === '一键售出', '工坊丹药出售处也直接显示坊市资金与简单一键按钮');
  await workshopSale.scrollIntoViewIfNeeded(); await page.screenshot({ path: join(temporary, 'workshop-sale-850-viewport.png') });
  const gearFixture = structuredClone(saleFixture);
  gearFixture.player.arts.gear = [{ id: 'trade-qa-sword', name: '验收法剑', slot: 'weapon', element: '剑', polarity: '天阳',
    grade: 1, bonus: .12, inscribed: false, price: 100, craftCost: 0, crafted: true, shape: 'sword', dimensions: [] }];
  gearFixture.player.arts.equipped = { weapon: 'trade-qa-sword' };
  await api('import', { game: JSON.stringify(gearFixture) }); await page.goto(base + '/?qa=gear#inventory', { waitUntil: 'domcontentloaded' });
  const gearCard = page.locator('[data-inventory-item="trade-qa-sword"]'); await gearCard.waitFor();
  check(await gearCard.locator('[data-action="sell"][data-kind="gear"]').isDisabled()
    && (await gearCard.locator('[data-trade-market-cash]').innerText()).includes('1000000'), '已装备法器出售禁用且资金直接显示在处置区');
  const unequipResponse = page.waitForResponse(response => response.url() === `${base}/api/gear/unequip`);
  await gearCard.locator('[data-action="gear/unequip"]').click(); const unequipped = await (await unequipResponse).json();
  await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  const gearQuote = unequipped.state.gears.find(item => item.id === 'trade-qa-sword').actions.sell;
  check(gearQuote.enabled && await gearCard.locator('[data-action="sell"][data-kind="gear"]').isEnabled(), '卸下法器后出售资格自动恢复');
  await gearCard.scrollIntoViewIfNeeded(); await page.screenshot({ path: join(temporary, 'gear-sale-850-viewport.png') });
  const gearResponse = page.waitForResponse(response => response.url() === `${base}/api/sell`);
  await gearCard.locator('[data-action="sell"][data-kind="gear"]').click(); const gearSold = await (await gearResponse).json();
  check(!gearSold.error && !gearSold.state.gears.some(item => item.id === 'trade-qa-sword')
    && gearSold.state.player.stones === unequipped.state.player.stones + gearQuote.returnStones, '桌面法器真实出售移除法器并按权威报价入账');
  const herb = initialState.herbCatalog.find(item => item.grade === 1);
  const plantFixture = structuredClone(initial); plantFixture.player.stones = 10000; plantFixture.player.cave = plantFixture.player.location;
  plantFixture.player.arts.herbs = { [herb.name]: 8 }; plantFixture.player.herbs = 8;
  await api('import', { game: JSON.stringify(plantFixture) }); await page.goto(base + '/?qa=plant#cave', { waitUntil: 'domcontentloaded' });
  const plantSlider = page.locator('[data-cave-plant-count]').first(); await plantSlider.waitFor();
  check(await plantSlider.getAttribute('type') === 'range' && await plantSlider.getAttribute('max') === '8', '真实灵田滑条1至拥有8份');
  await plantSlider.evaluate(input => { input.value = '5'; input.dispatchEvent(new Event('input', { bubbles: true })); });
  const plot = plantSlider.locator('xpath=ancestor::*[@data-cave-plot][1]');
  const plantResponse = page.waitForResponse(response => response.url() === `${base}/api/cave/garden/plant`);
  await plot.locator('[data-action="cave-garden-plant"]').click(); const planted = await (await plantResponse).json();
  assert.ok(!planted.error && planted.ok !== false, '播种成功');
  check(planted.state.arts.herbs[herb.name] === 3 && planted.state.cave.garden.plots[0].seedCount === 5, '拖动播种5份真实扣除5材料');
  check(errors.length === 0, '完整桌面浏览器没有脚本异常');
  console.log(`本轮一键交易与滑条真实冒烟 ${results.length} 项通过；报告 ${temporary}`);
} finally {
  await browser?.close();
  if (server && server.exitCode === null) { const closed = once(server, 'exit'); server.kill(); await closed; }
  writeFileSync(join(temporary, 'server.log'), serverLog.join(''), 'utf8');
  writeFileSync(join(temporary, 'report.json'), JSON.stringify({ binary, port, results, timings, errors }, null, 2), 'utf8');
}
