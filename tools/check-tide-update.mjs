// 本轮兽潮纯展示检查；加 --exe 可进行真实C++服务器、独立账号和浏览器交互冒烟。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { renderTidePanel, renderTideMarket, shouldRenderTide } from '../web/tide-panel.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const argument = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const results = [];

// 用途：记录独立断言；参与纯展示和真实浏览器验收；传条件和说明，返回无，失败立即抛错。
function check(condition, label) {
  assert.ok(condition, label);
  results.push({ label, passed: true });
  console.log(`PASS ${label}`);
}

const fixture = {
  active: true, settled: false, durationDays: 60, daysLeft: 20, playerScore: 3000, totalScore: 3000,
  npcPace: { normalChanceDenominator: 6, eliteChanceDenominator: 4, normalMaxKills: 9, eliteMaxKills: 12 },
  rewardRules: [{ maxRank: 1, rewardYears: 50, rewardRenown: 50 }, { maxRank: 3, rewardYears: 25, rewardRenown: 20 },
    { maxRank: 10, rewardYears: 15, rewardRenown: 10 }, { maxRank: 0, rewardYears: 5, rewardRenown: 1 }],
  leaderboard: [{ actorId: 'player', name: '测试<修士>', realm: '筑基后期', rank: 1, score: 3000, isPlayer: true }],
  market: { open: false, pointsPerStock: 300, stockRules: [{ grade: 3, unlockScore: 300, initialStock: 10 },
    { grade: 4, unlockScore: 3000, initialStock: 1 }, { grade: 5, unlockScore: 5700, initialStock: 0 }] },
};
const before = JSON.stringify(fixture);
const active = renderTidePanel(fixture);
check(active.includes('50 年') && active.includes('25 年') && active.includes('15 年') && active.includes('5 年'), '四档俸禄年数从API读取');
check(active.includes('声望 +50') && active.includes('声望 +20') && active.includes('声望 +10') && active.includes('声望 +1'), '四档声望奖励明确显示');
check(active.includes('1 / 6') && active.includes('1 / 4') && active.includes('9 只') && active.includes('12 只'), '每日出战概率与本期击杀上限从API读取');
check(active.includes('累计 300 分') && active.includes('3000 分解锁') && active.includes('5700 分解锁'), '积分库存步长与高阶解锁门槛可见');
check(active.includes('当前积分预计供应 10 份') && active.includes('当前积分预计供应 1 份') && active.includes('尚未解锁'), '战期显示后端预估库存并区分锁定高阶');
check(!active.includes('30 / 20 / 10 / 3') && active.includes('测试&lt;修士&gt;'), '删除旧奖励文案且安全转义榜单姓名');
check(JSON.stringify(fixture) === before, '纯展示不改变调用者状态');
const changed = structuredClone(fixture);
changed.rewardRules[0].rewardYears = 51;
changed.rewardRules[0].rewardRenown = 52;
changed.market.pointsPerStock = 333;
changed.npcPace.normalChanceDenominator = 7;
const changedHtml = renderTidePanel(changed);
check(changedHtml.includes('51 年') && changedHtml.includes('声望 +52') && changedHtml.includes('每 333 分增加 1 份')
  && changedHtml.includes('1 / 7'), '模拟后端参数变化直接反映到展示，无前端第二份配置');
const ended = { ...fixture, active: false, settled: true, rewardStones: 9600, rewardRenown: 49, rewardPaid: true,
  market: { ...fixture.market, open: true, daysLeft: 30, discountPercent: 50, minDiscountPercent: 50, items: [
    { kind: 'herb', id: 'h3', name: '三阶灵草', grade: 3, unitPrice: 50, stock: 10, enabled: true },
    { kind: 'herb', id: 'h5', name: '五阶灵草', grade: 5, unitPrice: 50, stock: 0, enabled: false, reason: '已售罄' },
  ] } };
const endedHtml = renderTidePanel(ended);
check(endedHtml.includes('9600 灵石 · 声望 +49') && endedHtml.includes('已到账'), '结榜使用保存的实际奖金和声望，不用规则重新覆盖旧结果');
const market = renderTideMarket({ tide: ended, player: { stones: 10000 } }, { grade: 'all' });
check(market.includes('按当前规则计算供应 10 份') && market.includes('低阶库存继续增加')
  && market.includes('商品剩余库存以已生成记录为准'), '交易说明当前规则供应与真实库存记录的区别，低阶继续累增');
check(market.includes('尚未解锁：本期全州总积分需达到 5700 分') && !market.includes('已售罄'), '未解锁高阶不误标售罄');
check(!shouldRenderTide({ active: false, settled: true, market: { open: false } }), '战后坊市到期不继续占用纪事界面');
const legacyHtml = renderTidePanel({ active: true, daysLeft: 10, leaderboard: [] });
check(legacyHtml.includes('以本期期末结算为准') && !legacyHtml.includes('30 / 20'), '旧响应缺规则时提示结算，不展示写死旧奖励');
const oldStock = structuredClone(ended);
oldStock.totalScore = 0;
oldStock.market.stockRules.forEach(rule => { rule.initialStock = 0; });
const oldStockHtml = renderTideMarket({ tide: oldStock, player: { stones: 10000 } }, { grade: '3' });
check(oldStockHtml.includes('按当前规则计算供应 0 份') && oldStockHtml.includes('库存 10')
  && /data-action="tide-market-buy"[^>]*>买入/.test(oldStockHtml), '旧已结算库存大于零时依然可购，不用新门槛覆盖历史库存');

if (!args.includes('--exe')) {
  console.log(`兽潮本轮纯展示检查：${results.length} 项通过（模拟字段，非实际结算）。`);
} else {
  const binary = resolve(root, argument('--exe'));
  assert.ok(existsSync(binary), '必须提供本轮完整Web程序');
  const output = join(root, 'build', 'tide-update-20261014');
  mkdirSync(output, { recursive: true });
  const temporary = mkdtempSync(join(output, 'browser-'));
  const isolatedBinary = join(temporary, 'tide-qa-server.exe');
  copyFileSync(binary, isolatedBinary);
  const reservation = createServer(); reservation.listen(0, '127.0.0.1');
  await once(reservation, 'listening');
  const port = reservation.address().port;
  await new Promise(resolveClose => reservation.close(resolveClose));
  assert.ok(![8107, 8108].includes(port), '拒绝使用玩家端口');
  const base = `http://127.0.0.1:${port}`;
  const require = createRequire(import.meta.url);
  const modulePath = argument('--playwright-module', process.env.PLAYWRIGHT_MODULE || 'playwright');
  const { chromium } = require(modulePath);
  const edge = argument('--browser-path', process.env.CHROMIUM_PATH || 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe');
  const serverLog = [], errors = [], timings = [];
  let server, browser, page;
  try {
    server = spawn(isolatedBinary, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
      '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')],
    { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    server.stdout.on('data', chunk => serverLog.push(chunk.toString()));
    server.stderr.on('data', chunk => serverLog.push(chunk.toString()));
    let ready = false;
    for (let attempt = 0; attempt < 200; ++attempt) {
      if (server.exitCode !== null) throw new Error('隔离程序提前退出');
      try { ready = (await fetch(base + '/api/mode')).ok; if (ready) break; }
      catch { await new Promise(resolveWait => setTimeout(resolveWait, 50)); }
    }
    check(ready, '独立服务器启动，无需停止玩家8108服务');
    browser = await chromium.launch({ executablePath: edge, headless: true });
    const context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN' });
    page = await context.newPage(); page.setDefaultTimeout(20000);
    page.on('pageerror', error => errors.push(error.message));

    // 用途：通过真实服务器操作独立账号；传相对API路径和可选正文，返回JSON；拒绝时明确失败。
    async function api(path, body) {
      const response = body === undefined ? await context.request.get(`${base}/api/${path}`)
        : await context.request.post(`${base}/api/${path}`, { data: body });
      const data = await response.json();
      assert.ok(response.ok() && data.ok !== false && !data.error, `${path}: ${data.error || response.status()}`);
      return data;
    }
    await api('login', { nickname: `TideQa${Date.now().toString(36)}`, password: 'isolated-tide-qa' });
    await api('new', { name: '兽潮桌面验收', gender: '男', portraitId: 'player-male', seed: 514014, traits: [], allocation: {} });
    const game = JSON.parse((await api('export')).game);
    game.player.stage = 5; game.player.xp = 0; game.player.lifespan = 250; game.player.stones = 10000;
    game.clock.days = 59; game.clock.ageBase = game.player.age; game.year = 0;
    game.plan = 'rest'; game.job = null; game.pending = null; game.player.arts.battle = null;
    Object.assign(game.tide, { active: true, startYear: 0, severity: 1, rulesVersion: 1, eventId: 'desktop-tide-qa',
      startDay: 0, endDay: 60, durationDays: 60, lastProcessedDay: 59, monthsLeft: 1, playerJoined: true,
      playerScore: 5700, playerEntryStage: 5, playerRank: 0, totalScore: 0, settled: false, rewardPaid: false,
      rewardStones: 0, rewardRenown: 0, participants: [], marketStock: [], marketExpiresDay: -1 });
    game.tide.participants = game.npcs.filter(npc => npc.alive && !npc.beast && npc.combatProfileId)
      .map(npc => ({ actorId: npc.id, name: npc.name, sect: npc.sect, combatProfileId: npc.combatProfileId,
        entryStage: npc.stage, score: 0, kills: 0, rank: 0, elite: true }));
    await api('import', { game: JSON.stringify(game) });
    const state = (await api('state')).state;
    check(state.tide.rewardRules.map(rule => rule.rewardYears).join('/') === '50/25/15/5'
      && state.tide.rewardRules.map(rule => rule.rewardRenown).join('/') === '50/20/10/1', '真实API发送四档新俸禄与声望规则');
    check(state.tide.npcPace.normalChanceDenominator === 6 && state.tide.npcPace.eliteChanceDenominator === 4,
      '真实API出战频率字段为普通1/6精英1/4');
    check(state.tide.market.pointsPerStock === 300 && state.tide.market.stockRules.map(rule => rule.unlockScore).join('/') === '300/3000/5700',
      '真实API阶梯库存门槛300/3000/5700');
    check(state.tide.market.stockRules.map(rule => rule.incrementScore).join('/') === '300/900/2700',
      '真实API高阶库存采用更慢的独立增长间隔');
    const champions = state.tide.leaderboard.filter(row => row.elite === true);
    check(champions.length > 0 && champions.every(row => row.buildName), '真实金榜提供合法高手身份及具体流派名');
    const queryBefore = (await api('export')).game;
    await api('state'); await api('state');
    check((await api('export')).game === queryBefore, '两次完整state查询不参战不推进RNG不发奖');
    const pageStart = performance.now();
    await page.goto(base + '/#chronicle', { waitUntil: 'domcontentloaded' });
    await page.locator('.tide-panel').waitFor();
    timings.push({ label: '首次可见兽潮面板', milliseconds: performance.now() - pageStart });
    check((await page.locator('.tide-reward-rules').innerText()).includes('25 年')
      && (await page.locator('.tide-stock-rules').innerText()).includes('5700 分解锁'), '真实DOM可见新奖励与库存门槛');
    await page.locator('.tide-goldboard > summary').click();
    check((await page.locator('.tide-goldboard').innerText()).includes('1 / 6'), '真实金榜展开后显示参战概率');
    await page.locator('.tide-champions summary').click();
    check((await page.locator('.tide-champions').innerText()).includes(champions[0].buildName)
      && (await page.locator('.tide-ranking-scroll').innerText()).includes('流派高手'), '真实金榜高手可定位并显示流派名与徽标');
    const advanceResponse = page.waitForResponse(response => response.url() === `${base}/api/advance` && response.request().method() === 'POST');
    const clickStart = performance.now();
    await page.locator('[data-action="advance-step"][data-days="1"]').click();
    const advanced = await (await advanceResponse).json();
    assert.ok(advanced.ok !== false && !advanced.error, '真实DOM过一天动作成功');
    await page.locator('.tide-reward').waitFor();
    timings.push({ label: '过一天到结榜可见', milliseconds: performance.now() - clickStart });
    check(advanced.state.tide.settled && advanced.state.tide.rewardPaid && advanced.state.tide.rewardRenown === 50,
      '真实推进到期结榜并发放榜首声望');
    check((await page.locator('.tide-reward').innerText()).includes(`声望 +${advanced.state.tide.rewardRenown}`)
      && (await page.locator('.tide-reward').innerText()).includes(String(advanced.state.tide.rewardStones)), '实际结榜奖励与API一致显示');
    await page.locator('[data-action="tide-market-open"]').click();
    await page.locator('.tide-market').waitFor();
    check((await page.locator('.tide-stock-rules').innerText()).includes('按当前规则计算供应')
      && (await page.locator('.tide-stock-rules').innerText()).includes('商品剩余库存以已生成记录为准'), '真实进入坊市明确当前规则供应与历史库存区别');
    await page.locator('[data-tide-market-grade]').selectOption('5');
    check(await page.locator('.tide-market-row').count() > 0, '五阶达到门槛有材料上架可切换筛选');
    await page.setViewportSize({ width: 960, height: 900 });
    await page.screenshot({ path: join(temporary, 'tide-market-narrow-desktop.png'), fullPage: true });
    await page.screenshot({ path: join(temporary, 'tide-market-narrow-desktop-viewport.png'), fullPage: false });
    check(await page.locator('[data-tide-market-grade]').isVisible(), '窄桌面保留可操作品阶筛选');
    check(errors.length === 0, '真实浏览器没有脚本异常');
    console.log(`兽潮本轮检查：${results.length} 项通过；实际C++服务器，未模拟API。`);
  } finally {
    await browser?.close();
    if (server && server.exitCode === null) { const closed = once(server, 'exit'); server.kill(); await closed; }
    writeFileSync(join(temporary, 'server.log'), serverLog.join(''), 'utf8');
    writeFileSync(join(temporary, 'report.json'), JSON.stringify({ results, timings, errors, binary, port }, null, 2), 'utf8');
  }
}
