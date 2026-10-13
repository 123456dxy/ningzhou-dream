// 兽潮真实 DOM 交互验收，API 明确模拟。只启动隔离端口，不操作玩家服务或已打开的浏览器。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createServer } from 'node:net';
import { once } from 'node:events';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const output = join(root, 'build', 'tide-ui-regressions');
mkdirSync(output, { recursive: true });
const require = createRequire('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/package.json');
const { chromium } = require('playwright');
const executablePath = ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(resolve => reservation.close(resolve));
const fixture = spawn(process.execPath, [join(root, 'tools/ui-fixture-20261012.mjs'), '--port', String(port), '--root', root], { cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
const base = `http://127.0.0.1:${port}`, results = [], requests = [], errors = [];
let browser;
function check(value, label) { results.push({ label, passed: !!value }); assert.ok(value, label); console.log('PASS ' + label); }
try {
  let initial;
  for (let attempt = 0; attempt < 100; ++attempt) {
    try { initial = (await (await fetch(base + '/api/state')).json()).state; if (initial) break; }
    catch { await new Promise(resolve => setTimeout(resolve, 50)); }
  }
  assert.ok(initial, '隔离模拟服务已启动');
  const state = structuredClone(initial);
  state.player.stones = 2000;
  state.tide = {
    eventId: 'tide-dom-fixture', active: true, settled: false, severity: 1, daysLeft: 60, durationDays: 60,
    playerScore: 0, playerRank: 0, totalScore: 123, wavesFought: 0, wavesWon: 0,
    battlefields: [0, 1, 2, 3].map((id) => ({ id, name: ['炼气战场', '筑基战场', '金丹战场', '元婴战场'][id], pointsPerKill: [1, 3, 9, 27][id], enabled: id < 2, reason: id >= 2 ? '境界尚未达到所选战场要求。' : '' })),
    leaderboard: [{ actorId: 'player', name: '桌面验收', realm: '筑基初期', score: 0, rank: 0, isPlayer: true }, { actorId: 'elite', name: '赤焰高手', realm: '筑基后期', score: 123, rank: 1, elite: true }],
    market: { open: false, items: [] },
  };
  browser = await chromium.launch({ executablePath, headless: true });
  const context = await browser.newContext({ viewport: { width: 1440, height: 1080 } });
  await context.route('**/api/**', async route => {
    const path = new URL(route.request().url()).pathname;
    if (path === '/api/mode') return route.fulfill({ json: { ok: true, local: true, nickname: '兽潮 DOM 模拟' } });
    if (path === '/api/state') return route.fulfill({ json: { ok: true, hasGame: true, state } });
    if (!['/api/tide/fight', '/api/battle/turn', '/api/tide/market/buy'].includes(path)) return route.continue();
    const body = route.request().postDataJSON(); requests.push({ path, body });
    if (path === '/api/tide/fight') {
      state.battle = { kind: 'tide', enemy: '兽潮妖兽', enemyStage: 3, enemyBeast: true, hp: 100, max: 100, enemyHp: 100, enemyMax: 100,
        turn: 0, hand: [], log: [], cooldowns: {}, artifacts: [], shield: 0, enemyShield: 0, draw: 10, discard: 0, mana: 4, maxMana: 4, nextMana: 4, manaCap: 12, lethal: true, played: 0, intentName: '蓄势', intentRaw: 10 };
    } else if (path === '/api/battle/turn') {
      state.battle = null;
      Object.assign(state.tide, { active: false, settled: true, daysLeft: 0, playerScore: 3, playerRank: 2, rewardStones: 500, rewardPaid: true });
      state.tide.market = { open: true, daysLeft: 30, discountPercent: 50, minDiscountPercent: 50, priceBasis: '当前声望下普通坊市购入价', items: [
        { kind: 'herb', id: 'h3', name: '三阶灵药', grade: 3, basePrice: 100, unitPrice: 50, stock: 3, owned: 0, enabled: true },
        { kind: 'material', id: 'm4', name: '四阶仙材', grade: 4, basePrice: 300, unitPrice: 150, stock: 0, enabled: false, reason: '已售罄' },
      ] };
    } else {
      const item = state.tide.market.items.find(item => item.id === body.id && item.kind === body.kind);
      assert.ok(item && body.eventId === state.tide.eventId && item.unitPrice === body.expectedUnitPrice && item.stock >= body.count, '模拟交易收到当前权威商品报价');
      item.stock -= body.count; item.owned += body.count; state.player.stones -= body.count * item.unitPrice;
    }
    return route.fulfill({ json: { ok: true, state, text: '模拟动作完成' } });
  });
  const page = await context.newPage(); page.on('pageerror', error => errors.push(error.message));
  await page.goto(base + '/#chronicle');
  await page.locator('[data-battlefield="0"]').waitFor();
  check(await page.locator('[data-action="tide-fight"]').count() === 4, '真实 DOM 四个战場入口');
  check((await page.locator('.tide-panel').innerText()).includes('60 天'), '真实 DOM 天倒计时');
  check(await page.locator('[data-battlefield="2"]').isDisabled(), '高境界战场受权威门槛禁用');
  await page.locator('.tide-goldboard summary').click();
  check(await page.locator('.tide-ranking-scroll').isVisible(), '金榜可点击展开');
  check((await page.locator('.tide-ranking-scroll').innerText()).includes('完整流派'), '特殊 NPC 构筑标记可见');
  await page.locator('[data-battlefield="1"]').click();
  await page.locator('.tide-combat-clock').waitFor();
  check(requests.at(-1).body.battlefield === 1, '选定筑基战场使用数字权威 ID 请求');
  check((await page.locator('.combat-round').innerText()).includes('每回合一天') && !(await page.locator('.combat-round').innerText()).includes('月历暂停'), '兽潮战斗不显示月历暂停');
  check((await page.locator('.tide-combat-clock').innerText()).includes('快速击杀'), '战中明确快速击杀耗时');
  await page.locator('[data-action="end-turn"]').click();
  await page.locator('[data-action="tide-market-open"]').waitFor();
  check((await page.locator('.tide-reward').innerText()).includes('已到账'), '兽潮结束后保留结榜与到账说明');
  await page.locator('[data-action="tide-market-open"]').click();
  await page.locator('[data-tide-market-grade]').waitFor();
  check(await page.locator('[data-tide-market-grade]').inputValue() === '3', '战后市场默认三阶');
  check((await page.locator('.tide-market-rule').innerText()).includes('当前声望') && (await page.locator('.tide-market-rule').innerText()).includes('50%'), '正常声望价基准与五折底线可见');
  await page.locator('[data-tide-quantity="herb:h3"]').fill('2');
  await page.locator('[data-tide-market-grade]').focus();
  await page.locator('[data-action="tide-market-buy"]').click();
  check(requests.at(-1).path === '/api/tide/market/buy' && requests.at(-1).body.count === 2 && requests.at(-1).body.expectedUnitPrice === 50 && requests.at(-1).body.eventId === 'tide-dom-fixture', '真实购买携带数量、当前报价、赛事来源');
  await page.locator('.tide-market-row').filter({ hasText: '已有 2' }).waitFor();
  check((await page.locator('.tide-market-row').innerText()).includes('库存 1'), '购买后刷新库存与已持有量');
  await page.locator('[data-tide-market-grade]').selectOption('4');
  check(await page.locator('[data-action="tide-market-buy"]').isDisabled() && (await page.locator('.tide-market-row').innerText()).includes('已售罄'), '售罄商品无法发起购买');
  await page.reload(); await page.locator('[data-tide-market-grade]').waitFor();
  check(await page.locator('[data-tide-market-grade]').inputValue() === '4', '品阶偏好刷新后保留');
  await page.locator('[data-tide-market-kind="herb"]').click();
  check((await page.locator('.tide-market-list').innerText()).includes('暂无材料'), '类型与品阶筛选可组合');
  await page.locator('[data-tide-market-kind="material"]').click();
  await page.screenshot({ path: join(output, 'tide-market-desktop.png'), fullPage: true });
  check(errors.length === 0, '没有浏览器脚本错误');
  console.log(`兽潮真实 DOM 检查：${results.length}/${results.length} 通过（API 明确模拟）`);
} finally {
  writeFileSync(join(output, 'results.json'), JSON.stringify({ api: 'mock', results, errors, requests }, null, 2));
  if (browser) await browser.close();
  fixture.kill();
}
