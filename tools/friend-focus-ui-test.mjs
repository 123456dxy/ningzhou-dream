// Real-browser regression: a selected local NPC must be reachable on small screens.
// Requires an isolated game with local NPCs on port 18112; never changes game state.
import { createRequire } from 'node:module';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const port = argv.includes('--port') ? argv[argv.indexOf('--port') + 1] : '18112';
if (!/^\d+$/.test(port) || Number(port) < 18100 || Number(port) > 18199)
  throw new Error('道友详情验收只允许隔离测试端口 18100–18199（默认 18112）。');
const SHOTS = join(ROOT, 'build', 'shots', 'friend-focus-ui');
let playwright;
try { playwright = createRequire(import.meta.url)('playwright'); }
catch { playwright = createRequire(process.execPath)('playwright'); }
const executablePath = [
  'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
  'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
].find(existsSync);
if (!executablePath) throw new Error('需要 Chrome 或 Edge 进行浏览器验收。');
const results = [], shots = [], errors = [];
function check(passed, name, details) {
  results.push({ passed: !!passed, name, details });
  console.log(`${passed ? 'PASS' : 'FAIL'} ${name}: ${JSON.stringify(details)}`);
}
mkdirSync(SHOTS, { recursive: true });
const browser = await playwright.chromium.launch({ executablePath, headless: true });
try {
  for (const width of [1440, 390]) {
    const page = await browser.newPage({ viewport: { width, height: width === 390 ? 844 : 1100 }, reducedMotion: 'reduce' });
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(`http://127.0.0.1:${port}/`);
    await page.locator('#player-card .player-name').waitFor();
    await page.locator('.main-tabs [data-tab="map"]').click();
    await page.locator('[data-subtab="map"][data-sub="friends"]').click();
    const person = page.locator('#workspace [data-person]').first();
    const npc = await person.getAttribute('data-person');
    const bag = page.waitForResponse(response => response.url().includes('/api/npc-bag'));
    await person.click(); await bag;
    await page.locator('.friend-panel').waitFor();
    const geometry = await page.locator('.friend-panel').evaluate(element => {
      const r = element.getBoundingClientRect();
      return { top: r.top, bottom: r.bottom, width: innerWidth, height: innerHeight, scroll: document.documentElement.scrollWidth };
    });
    check(geometry.top >= -2 && geometry.top < geometry.height - 120,
      `${width}px: 点道友后能直接看到详情标题和首个行动`, geometry);
    const greet = page.locator(`.friend-panel [data-faction="greet"][data-npc="${npc}"]`);
    check(await greet.isVisible() && (await greet.isEnabled() || await greet.innerText() === '已见过礼'),
      `${width}px: 选中的道友显示可见的见礼入口或已见礼状态`, npc);
    check(geometry.scroll <= geometry.width + 2, `${width}px: 道友详情不横向溢出`, geometry);
    const file = join(SHOTS, `${width}-selected-friend.png`);
    await page.screenshot({ path: file }); shots.push(file);
    await page.close();
  }
  check(errors.length === 0, '道友详情没有脚本错误', errors);
} catch (error) {
  check(false, '道友详情浏览器验收完成', error.stack || error.message);
} finally {
  await browser.close();
  const failed = results.filter(result => !result.passed);
  const report = { port: Number(port), checks: results.length, passed: results.length - failed.length, failed: failed.length, results, shots };
  writeFileSync(join(SHOTS, 'results.json'), JSON.stringify(report, null, 2), 'utf8');
  console.log(`Friend Focus UI: ${report.passed}/${report.checks} passed; ${report.failed} failed.`);
  if (failed.length) process.exitCode = 1;
}
