// 真正的桌面浏览器验收：独立完整服务器、临时账号、实际C++ API，无mock。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const value = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const binary = resolve(root, value('--exe', 'build-backend-desktop/ningzhou_web.exe'));
const port = Number(value('--port', '18133'));
assert.ok(Number.isInteger(port) && port >= 1024 && port <= 65535 && ![8107, 8108].includes(port), '只使用独立测试端口');
assert.ok(existsSync(binary), '需要本轮完整Web程序');
const edge = value('--browser-path', process.env.CHROMIUM_PATH);
assert.ok(!edge || existsSync(edge), '指定浏览器程序必须存在');
const packagePath = value('--playwright-module', process.env.PLAYWRIGHT_MODULE || 'playwright');
const require = createRequire(import.meta.url);
const { chromium } = require(packagePath);
const output = join(root, 'build', 'merge-20261013');
mkdirSync(join(output, 'qa-data'), { recursive: true });
const temporary = mkdtempSync(join(output, 'qa-data', 'cave-browser-'));
// 使用独立程序副本，浏览器验收不会锁住正在重新编译的生产构建目标。
const isolatedBinary = join(temporary, 'cave-qa-server.exe');
copyFileSync(binary, isolatedBinary);
const base = `http://127.0.0.1:${port}`;
let server, browser, page;
let checks = 0;
const serverLog = [], pageErrors = [], consoleErrors = [], failedRequests = [], screenshots = [];

// 用途：记录实际浏览器/API检查的结果。
// 流程：隔离桌面验收；失败立即抛错，finally清理本脚本进程。
// 使用方式：传布尔结果和中文描述；不输出凭证或完整存档。
// 返回值：无，通过时增加检查计数。
function check(condition, label) {
  assert.ok(condition, label);
  checks += 1;
  console.log(`PASS ${label}`);
}

// 用途：确认指定测试端口未被其他进程占用。
// 流程：启动隔离服务前的资源边界检查。
// 使用方式：只绑定127.0.0.1，完成后立即释放，不停止已有服务器。
// 返回值：Promise，端口占用时明确失败。
async function verifyPort() {
  const reservation = createServer();
  reservation.listen(port, '127.0.0.1');
  await once(reservation, 'listening');
  await new Promise(resolveClose => reservation.close(resolveClose));
}

// 用途：截取实际页面状态供人工视觉核验。
// 流程：桌面洞府、外出和制作价格验收。
// 使用方式：传文件名；先等待字体和可见图片加载完成。
// 返回值：无，PNG保存在独立构建输出目录。
async function capture(name) {
  await page.evaluate(async () => {
    await document.fonts.ready;
    await Promise.all([...document.images].map(image => image.decode().catch(() => {})));
  });
  const path = join(output, name);
  await page.screenshot({ path, fullPage: true });
  screenshots.push(path);
}

try {
  await verifyPort();
  server = spawn(isolatedBinary, ['--host', '127.0.0.1', '--port', String(port), '--no-open',
    '--web', join(root, 'web'), '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')],
  { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  server.stdout.on('data', chunk => serverLog.push(chunk.toString()));
  server.stderr.on('data', chunk => serverLog.push(chunk.toString()));
  let ready = false;
  for (let attempt = 0; attempt < 200; attempt += 1) {
    if (server.exitCode !== null) throw new Error('隔离Web程序提前退出');
    try { const response = await fetch(base + '/api/mode'); await response.arrayBuffer(); ready = response.ok; if (ready) break; }
    catch { await new Promise(resolveWait => setTimeout(resolveWait, 50)); }
  }
  check(ready, '独立127.0.0.1:18133完整服务启动');
  browser = await chromium.launch({ ...(edge ? { executablePath: edge } : { channel: 'msedge' }), headless: true });
  const context = await browser.newContext({ viewport: { width: 1600, height: 1000 }, locale: 'zh-CN' });
  page = await context.newPage();
  page.setDefaultTimeout(20000);
  page.on('pageerror', error => pageErrors.push(error.message));
  page.on('console', message => { if (message.type() === 'error') consoleErrors.push(message.text()); });
  page.on('requestfailed', request => failedRequests.push(`${request.method()} ${request.url()} ${request.failure()?.errorText}`));
  page.on('response', response => {
    if (response.status() >= 400) failedRequests.push(`${response.status()} ${response.request().method()} ${response.url()}`);
  });

  // 用途：通过真实服务器执行测试账号的API调用。
  // 流程：注册、建角、fixture导入、实际结果核对。
  // 使用方式：传相对API路径及可选请求正文；共享浏览器cookie。
  // 返回值：真实JSON响应；错误明确抛出，不将失败响应冒充成功。
  const api = async (path, body) => {
    const response = body === undefined ? await context.request.get(`${base}/api/${path}`)
      : await context.request.post(`${base}/api/${path}`, { data: body });
    const data = await response.json();
    assert.ok(response.ok() && data.ok !== false && !data.error, `${path}: ${data.error || response.status()}`);
    return data;
  };

  // 用途：点击真实DOM动作并等待对应POST及UI解除忙碌。
  // 流程：播种、储备和返府功能验收。
  // 使用方式：传Playwright locator和实际API路径，不拦截或替换响应。
  // 返回值：真实动作JSON，失败立即中止。
  const clickAction = async (locator, path) => {
    const pending = page.waitForResponse(response => response.url() === `${base}/api/${path}`
      && response.request().method() === 'POST');
    await locator.click();
    const response = await pending;
    const data = await response.json();
    assert.ok(response.ok() && data.ok !== false && !data.error, `DOM ${path}: ${data.error || response.status()}`);
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
    return data;
  };

  const nickname = `CaveQa${Date.now().toString(36).slice(-8)}`;
  await api('login', { nickname, password: 'isolated-cave-qa' });
  await api('new', { name: '洞府验收机器人', gender: '男', portraitId: 'player-male', seed: 313373,
    traits: ['greenBottle'], allocation: {} });
  const initial = (await api('state')).state;
  check(initial.cave?.garden?.plantsPerPlotLimit === 99, '本轮真实服务器具备99种苗灵田协议');
  const herb = initial.herbCatalog.find(item => item.id === '6001');
  assert.ok(herb, '真实药材目录包含一阶种苗');
  const fixture = JSON.parse((await api('export')).game);
  fixture.player.cave = 'wuling';
  fixture.player.caveTier = 2;
  fixture.player.qiSupply = 1;
  fixture.player.renown = 80;
  fixture.player.stones = 100000;
  fixture.player.arts.herbs = Object.fromEntries(initial.herbCatalog.map(item => [item.name, 50]));
  const emptyStockHerb = initial.herbCatalog.find(item => item.id === '6002');
  assert.ok(emptyStockHerb, '真实目录包含库存筛选对照药材');
  fixture.player.arts.herbs[emptyStockHerb.name] = 0;
  fixture.player.arts.herbs[herb.name] = 300;
  fixture.player.herbs = Object.values(fixture.player.arts.herbs).reduce((sum, count) => sum + count, 0);
  fixture.player.arts.materials = Object.fromEntries(initial.materialCatalog.filter(item => item.grade <= 2).map(item => [item.id, 50]));
  await api('import', { game: JSON.stringify(fixture) });
  await page.goto(base + '/#cave', { waitUntil: 'domcontentloaded' });
  await page.locator('.cave-page').waitFor();
  check(await page.locator('.main-tabs [data-tab="arts"]').innerText().then(text => text.includes('功法与悟道')), '功法与悟道保持独立导航');
  check(await page.locator('.cave-residence').innerText().then(text => text.includes('武陵城') && text.includes('在府')), '真实洞府页显示归属所在地与在府状态');
  check(await page.locator('[data-cave-plot]').count() === 8, '聚灵洞府显示八格灵田');
  check(await page.locator('.cave-page [data-action="cave-enter"],.cave-page [data-action="cave-leave"],.cave-page [data-action="goto-workshop-forge"]').count() === 0, '洞府没有虚构进入旗标或重复工坊入口');

  await page.locator('.main-tabs [data-tab="chronicle"]').click();
  const planResponse = page.waitForResponse(response => response.url() === `${base}/api/plan` && response.request().method() === 'POST');
  await page.locator('[data-plan-select]').selectOption('gather');
  const changedPlan = await planResponse;
  check(changedPlan.request().postDataJSON().plan === 'gather'
    && (await changedPlan.json()).state.plan === 'gather', '真实日常安排使用plan字段并成功切换为采药');
  await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  check(await page.locator('[data-plan-select]').inputValue() === 'gather', '日常安排重绘后保持服务端确认的选择');
  await api('plan', { plan: initial.plan });
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.locator('[data-plan-select]').waitFor();
  check(await page.locator('[data-plan-select]').inputValue() === initial.plan, '重新读取后显示已恢复的真实计划');
  await page.locator('.main-tabs [data-tab="cave"]').click();

  let emptyPlot = page.locator('[data-cave-plot="plot-2"]');
  check(await page.locator('[data-cave-plant-grade="1"]').getAttribute('aria-pressed') === 'true',
    '灵田默认一阶且只列行囊已有材料');
  check((await emptyPlot.locator('[data-cave-plant-herb] option').allTextContents()).every(text => text.includes('1 阶'))
    && await emptyPlot.locator(`[data-cave-plant-herb] option[value="${emptyStockHerb.id}"]`).count() === 0,
    '大量材料时空田选项按品阶和真实库存缩小');
  await page.locator('[data-cave-plant-grade="6"]').click();
  check((await emptyPlot.locator('[data-cave-plant-herb] option').allTextContents()).every(text => text.includes('6 阶')),
    '六阶品阶按钮真实切换空田药材');
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.locator('.cave-page').waitFor();
  check(await page.locator('[data-cave-plant-grade="6"]').getAttribute('aria-pressed') === 'true',
    '刷新后保留灵田品阶偏好');
  await page.locator('[data-cave-plant-grade="1"]').click();
  await page.locator('[data-cave-plant-search]').fill(herb.name);
  check(await emptyPlot.locator('[data-cave-plant-herb] option').count() === 1
    && await emptyPlot.locator('[data-cave-plant-herb]').inputValue() === herb.id, '搜索药材名称直接定位到匹配材料');
  await page.locator('[data-cave-plant-search]').fill('没有这种药材');
  check(await emptyPlot.locator('[data-cave-plant-herb]').inputValue() === ''
    && await emptyPlot.locator('[data-action="cave-garden-plant"]').isDisabled(), '搜索无匹配时不能播种隐藏的旧选择');
  check(await page.evaluate(name => {
    const input = document.querySelector('[data-cave-plant-search]');
    input.dispatchEvent(new CompositionEvent('compositionstart', { bubbles: true }));
    input.value = name.slice(0, 1);
    input.dispatchEvent(new InputEvent('input', { bubbles: true, isComposing: true, inputType: 'insertCompositionText' }));
    const retained = input === document.querySelector('[data-cave-plant-search]');
    input.value = name;
    input.dispatchEvent(new CompositionEvent('compositionend', { bubbles: true, data: name }));
    return retained && document.querySelector('[data-cave-plant-search]').value === name;
  }, herb.name), '中文组词期间保留搜索控件，确认后应用搜索');
  await page.locator('[data-cave-plant-search]').fill('');

  let plot = page.locator('[data-cave-plot="plot-1"]');
  await plot.locator('[data-cave-plant-herb]').selectOption(herb.id);
  await plot.locator('[data-cave-plant-count]').fill('99');
  await clickAction(plot.locator('[data-action="cave-garden-plant"]'), 'cave/garden/plant');
  let state = (await api('state')).state;
  check(state.cave.garden.plots[0].seedCount === 99 && state.cave.garden.plots[0].remainingCapacity === 0,
    'DOM播种99份实际进入服务器批次并达到容量上限');
  check(await plot.locator('[data-action="cave-garden-plant"]').isDisabled(), '满格补种按钮真实禁用');
  await page.locator('[data-cave-plant-grade="6"]').click();
  await page.locator('[data-cave-plant-search]').fill('没有这种药材');
  check(await plot.locator('[data-cave-plant-herb]').inputValue() === herb.id
    && await plot.locator('[data-cave-plant-herb]').isDisabled(), '已种田固定原药材，不被其它品阶或搜索隐藏');
  await page.locator('[data-cave-plant-search]').fill('');
  await page.locator('[data-cave-plant-grade="1"]').click();
  const purseBefore = state.player.stones;
  await plot.locator('[data-cave-reserve-amount]').fill('2000');
  await clickAction(plot.locator('[data-action="cave-garden-deposit"]'), 'cave/garden/reserve/deposit');
  state = (await api('state')).state;
  check(state.cave.garden.plots[0].reserveStones === 2000 && state.player.stones === purseBefore - 2000,
    'DOM存入从实际背包扣款并增加本格储备');
  await plot.locator('[data-cave-reserve-amount]').fill('100');
  await clickAction(plot.locator('[data-action="cave-garden-withdraw"]'), 'cave/garden/reserve/withdraw');
  state = (await api('state')).state;
  check(state.cave.garden.plots[0].reserveStones === 1900 && state.player.stones === purseBefore - 1900,
    'DOM取回真实储备且余额准确');
  check((await plot.innerText()).includes(`${state.cave.garden.plots[0].dailyCostStones} 灵石 / 天`), '页面显示真实每日自动加速费用');
  check(await page.locator('[data-action="cave-garden-accelerate"]').count() === 0, '没有单次加速收费按钮');
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), '1600px桌面洞府无横向溢出');
  await capture('cave-desktop-at-home.png');

  const offsite = JSON.parse((await api('export')).game);
  offsite.player.location = 'yifeng';
  offsite.rng = 1;
  await api('import', { game: JSON.stringify(offsite) });
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.locator('.cave-residence').filter({ hasText: '外出' }).waitFor();
  check(await plot.locator('[data-action="cave-garden-deposit"]').isDisabled()
    && await plot.locator('[data-action="cave-garden-withdraw"]').isDisabled()
    && await page.locator('[data-action="cave-train"]').isDisabled(), '外出时可查看但管理和训练受服务端资格阻断');
  check((await plot.innerText()).includes('1900 灵石') && (await plot.innerText()).includes('自动加速'), '外出仍显示本格储备和后台自动成长');
  await capture('cave-desktop-away.png');
  const daysBeforeReturn = (await api('state')).state.clock.days;
  const returnResponse = await clickAction(page.locator('[data-action="cave-return"]'), 'travel');
  const afterReturn = (await api('state')).state;
  check(returnResponse.ok === true && afterReturn.player.location === 'wuling', 'DOM返府通过真实walk赶路抵达原洞府');
  check(afterReturn.clock.days > daysBeforeReturn, '返府实际消耗世界日而非直接改地点');
  check(afterReturn.cave.garden.plots[0].cohorts[0].progressUnits > 0
    && afterReturn.cave.garden.plots[0].reserveStones < 1900, '真实赶路期间灵田成长并扣本格加速储备');

  await page.locator('.main-tabs [data-tab="workshop"]').click();
  await page.locator('[data-craft-pricing="potion"]').first().waitFor();
  const pillPricing = page.locator('[data-craft-pricing="potion"]').first();
  await pillPricing.locator('summary').click();
  check((await pillPricing.innerText()).includes('材料市价 1.5 倍') && (await pillPricing.innerText()).includes('材料市价 3 倍'), '真实工坊丹药定价可展开并明确1.5/3倍材料口径');
  await capture('workshop-pill-pricing-desktop.png');
  await page.locator('[data-subtab="workshop"][data-sub="forge"]').click();
  await page.locator('[data-craft-pricing="gear"]').first().waitFor();
  const gearPricing = page.locator('[data-craft-pricing="gear"]').first();
  await gearPricing.locator('summary').click();
  check((await gearPricing.innerText()).includes('基础出售价') && (await gearPricing.innerText()).includes('额外出售加成'), '真实炼器预览可展开材料定价与额外出售加成');
  await capture('workshop-gear-pricing-desktop.png');
  await page.locator('.main-tabs [data-tab="market"]').click();
  await page.locator('[data-shop="potion"]').click();
  await page.locator('[data-craft-pricing="potion"]').first().waitFor();
  check(await page.locator('[data-craft-pricing="potion"]').count() > 0, '真实坊市丹药具备材料定价明细');
  check(pageErrors.length === 0, '真实页面没有pageerror');
  check(consoleErrors.length === 0, '真实页面没有console error');
  check(failedRequests.length === 0, '真实页面没有404或失败请求');
  console.log(`真实桌面浏览器验收：${checks} 项通过。`);
} catch (error) {
  if (page && !page.isClosed()) await capture('cave-browser-failure.png').catch(() => {});
  throw error;
} finally {
  await browser?.close();
  if (server && server.exitCode === null) {
    const closed = once(server, 'exit');
    server.kill();
    await closed;
  }
  writeFileSync(join(temporary, 'server.log'), serverLog.join(''), 'utf8');
  writeFileSync(join(output, 'cave-browser-report.json'), JSON.stringify({ checks, port, binary, isolatedBinary, browser: edge || 'msedge',
    temporary, pageErrors, consoleErrors, failedRequests, screenshots }, null, 2), 'utf8');
}
