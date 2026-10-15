// 本地发行目录冒烟：临时端口、独立数据、真实自动建角与导航；不使用玩家运行目录。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { mkdtempSync, mkdirSync, copyFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { resolve, join } from 'node:path';

const args = process.argv.slice(2);
// 用途：读取独立工具参数；流程：检查启动；返回：明确参数或缺省值。
const argument = (key, fallback) => args.includes(key) ? args[args.indexOf(key) + 1] : fallback;
const root = resolve('.'), packageRoot = resolve(argument('--package', 'dist/ningzhou-local-20261015'));
const output = join(root, 'build/diagnostics/local-package-browser-20261015'); mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'run-'));
const executable = join(temporary, 'local-release-qa.exe'); copyFileSync(join(packageRoot, 'ningzhou_web.exe'), executable);
const { chromium } = createRequire(import.meta.url)('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
assert.ok(port > 0 && ![8107,8108,8109,8110,8111,8112,8113].includes(port));
const base = `http://127.0.0.1:${port}`;
const server = spawn(executable, ['--local', '--no-open', '--port', String(port), '--web', join(packageRoot, 'web'), '--data', join(temporary, 'data')],
  { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
const checks = [], errors = [], serverLog = [], timings = [];
server.stdout.on('data', value => serverLog.push(String(value))); server.stderr.on('data', value => serverLog.push(String(value)));
let browser;
// 用途：记录真实断言；流程：发行目录浏览器冒烟；返回：失败即抛出，不写玩家数据。
function check(condition, message) { assert.ok(condition, message); checks.push(message); console.log(`PASS ${message}`); }
try {
  let ready = false;
  for (let attempt = 0; attempt < 200; attempt++) {
    try { ready = (await fetch(base + '/api/mode')).ok; } catch { /* 独立服务正在启动。 */ }
    if (ready) break;
    if (server.exitCode !== null) throw new Error('本地发行程序提前退出');
    await new Promise(done => setTimeout(done, 50));
  }
  check(ready, '发行目录真实EXE启动');
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  const page = await context.newPage(); page.setDefaultTimeout(15000); page.on('pageerror', error => errors.push(error.message));
  await page.goto(base, { waitUntil: 'domcontentloaded' }); await page.locator('.main-tabs').waitFor();
  await page.waitForFunction(() => document.querySelector('.character-name,.side-name,.player-name') || document.body.textContent.includes('本地道友'));
  const mode = await (await context.request.get(base + '/api/mode')).json();
  const state = (await (await context.request.get(base + '/api/state')).json()).state;
  check(mode.local && state?.player?.stage === 0, '正式本地模式自动创建炼气角色');
  check(state.arts.spells.join('/') === 'gold/fire/water/gather', '实际新角携带金芒/焰火/水盾/聚气');
  check(['sword','wood','skim','refresh','purge'].every(id => state.arts.knownSpells.includes(id)), '原入门已知神通保留');
  for (const tab of ['arts', 'cave', 'market', 'inventory', 'chronicle']) {
    const started = performance.now(); await page.locator(`.main-tabs [data-tab="${tab}"]`).click();
    await page.waitForFunction(value => location.hash === '#' + value, tab);
    timings.push({ tab, milliseconds: performance.now() - started });
  }
  check(timings.length === 5, '功法/洞府/坊市/行囊/纪事真实导航均可操作');
  const selection = page.locator('[data-plan-select]');
  const change = page.waitForResponse(response => response.url().endsWith('/api/plan') && response.request().method() === 'POST');
  await selection.selectOption('rest'); await change;
  await page.waitForFunction(() => document.querySelector('[data-plan-select]')?.value === 'rest');
  check((await (await context.request.get(base + '/api/state')).json()).state.plan === 'rest', '日常安排真实选择后端与界面一致');
  await page.screenshot({ path: join(temporary, 'desktop-starter.png'), fullPage: true });
  await page.setViewportSize({ width: 850, height: 1000 }); await page.locator('.main-tabs [data-tab="arts"]').click();
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '850px桌面无整页横向溢出');
  await page.screenshot({ path: join(temporary, 'narrow-arts.png'), fullPage: true });
  check(!(await context.request.get(base + '/battle-lab.html')).ok(), '正式资源不暴露独立试炼页');
  check(errors.length === 0, '正式本地页面无脚本异常');
  writeFileSync(join(temporary, 'report.json'), JSON.stringify({ checks, timings, errors, serverLog, packageRoot }, null, 2));
  console.log(`SUMMARY ${checks.length} checks; evidence=${temporary}`);
} catch (error) {
  writeFileSync(join(temporary, 'failure.json'), JSON.stringify({ error: error.stack, checks, timings, errors, serverLog }, null, 2)); throw error;
} finally { if (browser) await browser.close(); server.kill(); }
