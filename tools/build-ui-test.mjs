// Real-browser regression for character creation scroll and keyboard continuity.
// Start a separate server with its own working directory on port 18112 first.
// This test changes only the unfinished build form; it never starts a new game.
// Run with Node and Playwright (the bundled Codex Node runtime includes it).
import { createRequire } from 'node:module';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const port = argv.includes('--port') ? argv[argv.indexOf('--port') + 1] : '18112';
if (!/^\d+$/.test(port) || Number(port) < 18100 || Number(port) > 18199)
  throw new Error('建号交互验收只允许隔离测试端口 18100–18199（默认 18112）。');
const BASE = `http://127.0.0.1:${port}/`;
const SHOTS = join(ROOT, 'build', 'shots', 'build-ui');
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
async function position(page) {
  return page.evaluate(() => ({
    scroll: document.querySelector('.build-panel').scrollTop,
    focus: document.activeElement?.dataset.talent
      || (document.activeElement?.dataset.alloc && `${document.activeElement.dataset.alloc}:${document.activeElement.dataset.d}`)
      || document.activeElement?.id || document.activeElement?.tagName,
  }));
}
async function shot(page, name) {
  const file = join(SHOTS, name + '.png');
  await page.screenshot({ path: file }); shots.push(file);
}
mkdirSync(SHOTS, { recursive: true });
const browser = await playwright.chromium.launch({ executablePath, headless: true });
try {
  for (const width of [1440, 390]) {
    const page = await browser.newPage({ viewport: { width, height: width === 390 ? 844 : 1100 } });
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(BASE);
    await page.locator('#player-card .player-name, #bp-name').first().waitFor();
    if (!await page.locator('#bp-name').isVisible()) await page.locator('[data-action="new"]').click();
    await page.locator('#bp-name').waitFor();
    const talent = page.getByRole('button', { name: /^魂穿异世 / });
    await talent.scrollIntoViewIfNeeded(); await talent.focus();
    const talentId = await talent.getAttribute('data-talent');
    const before = await position(page);
    await shot(page, `${width}-talent-before`);
    await talent.click();
    const after = await position(page);
    check(before.scroll > 100 && Math.abs(after.scroll - before.scroll) <= 2,
      `${width}px: 选择下方天赋后留在原处`, { before, after });
    check(after.focus === talentId, `${width}px: 选择天赋后保留键盘焦点`, after);
    check(await talent.getAttribute('class').then(value => value.includes('picked')),
      `${width}px: 天赋被实际选中`, talentId);
    await shot(page, `${width}-talent-after`);
    await page.keyboard.press('Enter');
    check(await talent.getAttribute('class').then(value => !value.includes('picked')),
      `${width}px: 原控件可用键盘再次取消`, await position(page));
    const plus = page.locator('[data-alloc="apt"][data-d="-1"]');
    await plus.scrollIntoViewIfNeeded(); await plus.focus();
    const allocationBefore = await position(page);
    await plus.click();
    const allocationAfter = await position(page);
    check(Math.abs(allocationAfter.scroll - allocationBefore.scroll) <= 2,
      `${width}px: 属性加点不会跳回面板顶部`, { before: allocationBefore, after: allocationAfter });
    check(allocationAfter.focus === 'apt:-1', `${width}px: 属性加点后保留加号焦点`, allocationAfter);
    await page.keyboard.press('Enter');
    check(await plus.locator('..').locator('.av').innerText() === '2',
      `${width}px: 不用重新寻找控件就能连续加两点`, await position(page));
    await shot(page, `${width}-allocation-after`);
    check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2),
      `${width}px: 建号页没有横向溢出`, width);
    await page.getByRole('button', { name: '返回', exact: true }).click();
    await page.close();
  }
  check(errors.length === 0, '建号交互没有脚本错误', errors);
} catch (error) {
  check(false, '建号浏览器验收完成', error.stack || error.message);
} finally {
  await browser.close();
  const failed = results.filter(result => !result.passed);
  const report = { port: Number(port), checks: results.length, passed: results.length - failed.length, failed: failed.length, results, shots };
  writeFileSync(join(SHOTS, 'results.json'), JSON.stringify(report, null, 2), 'utf8');
  console.log(`Build UI: ${report.passed}/${report.checks} passed; ${report.failed} failed.`);
  if (failed.length) process.exitCode = 1;
}
