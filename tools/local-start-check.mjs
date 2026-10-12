// 用途：隔离验证本地启动、中文安装路径和公网默认鉴权；不连接现有玩家服务。
// 使用：NZ_WEB_BINARY 指定待测 EXE，然后 node tools/local-start-check.mjs。
// 数据与复制的程序只写到仓库 build/local-start-check 下，测试后保留以便排查。
import { spawn } from 'node:child_process';
import { access, copyFile, mkdir, mkdtemp, stat, symlink, writeFile } from 'node:fs/promises';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const binary = resolve(process.env.NZ_WEB_BINARY || join(root, 'build-backend-desktop', process.platform === 'win32' ? 'ningzhou_web.exe' : 'ningzhou_web'));
const fixtureParent = join(root, 'build', 'local-start-check');
await access(binary);
await mkdir(fixtureParent, { recursive: true });
const fixture = await mkdtemp(join(fixtureParent, 'run-'));
const install = join(fixture, '中文安装目录');
await mkdir(join(install, 'web', 'assets'), { recursive: true });
await writeFile(join(install, 'web', 'index.html'), '<!doctype html><title>本地启动验证</title><p>ningzhou-local-static</p>', 'utf8');
await writeFile(join(install, 'web', 'assets', '中文图标.txt'), 'ningzhou-unicode-asset', 'utf8');
await writeFile(join(install, 'web', 'assets', 'ascii.txt'), 'ningzhou-nested-ascii-asset', 'utf8');
await mkdir(join(install, 'web-sibling'), { recursive: true });
await writeFile(join(install, 'web-sibling', 'secret.txt'), 'must-not-be-served', 'utf8');
await symlink(join(install, 'web-sibling'), join(install, 'web', 'escaped'), 'junction');
const copiedBinary = join(install, process.platform === 'win32' ? 'ningzhou_web.exe' : 'ningzhou_web');
await copyFile(binary, copiedBinary);
const results = [];
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

// 用途：记录实际行为断言，继续收集其余独立问题；返回值：无。
function check(value, label) {
  results.push({ label, passed: !!value });
  console.log(`${value ? 'PASS' : 'FAIL'} ${label}`);
}

// 用途：申请当前空闲端口，不接触现有服务；返回值：端口号。
async function freePort() {
  const probe = createServer();
  await new Promise((resolve, reject) => { probe.once('error', reject); probe.listen(0, '127.0.0.1', resolve); });
  const port = probe.address().port;
  await new Promise((resolve, reject) => probe.close(error => error ? reject(error) : resolve()));
  return port;
}

// 用途：启动独占测试服务，监听日志确认服务已就绪；返回值：进程、URL 和日志读取器。
async function start(args, dataName) {
  const port = await freePort();
  const child = spawn(copiedBinary, ['--port', String(port), '--data', dataName, '--feedback', `${dataName}-feedback`, '--no-open', ...args], {
    cwd: install, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
  });
  let output = '', spawnError = null;
  child.stdout.on('data', chunk => { output += chunk.toString('utf8'); });
  child.stderr.on('data', chunk => { output += chunk.toString('utf8'); });
  child.on('error', error => { spawnError = error; });
  const base = `http://127.0.0.1:${port}`;
  for (let attempt = 0; attempt < 100; attempt++) {
    if (spawnError) throw spawnError;
    if (child.exitCode !== null) throw new Error(`Fixture service exited (${child.exitCode}): ${output}`);
    try { if ((await fetch(`${base}/api/mode`, { signal: AbortSignal.timeout(1000) })).ok) return { child, base, port, log: () => output }; } catch { /* 等待本测试进程完成监听。 */ }
    await delay(50);
  }
  child.kill();
  throw new Error(`Fixture service did not become ready: ${output}`);
}

// 用途：结束本测试创建的进程，不操作任何外部 PID；返回值：Promise<void>。
async function stop(server) {
  if (server.child.exitCode !== null) return;
  server.child.kill();
  await new Promise(resolve => {
    server.child.once('exit', resolve);
    setTimeout(resolve, 3000).unref();
  });
}

let server;
try {
  // 显式传入公网 host 也不能让免登录本地模式向网络开放。
  server = await start(['--local', '--host', '0.0.0.0'], 'local-data');
  check(server.log().includes(`http://127.0.0.1:${server.port}/`), '--local 仅监听 127.0.0.1');
  const mode = await (await fetch(`${server.base}/api/mode`)).json();
  check(mode.local === true, '--local 明确返回本地模式');
  const page = await fetch(`${server.base}/`);
  check(page.ok && (await page.text()).includes('ningzhou-local-static'), '中文安装路径默认 web 根目录可读取首页');
  const asset = await fetch(`${server.base}/assets/${encodeURIComponent('中文图标.txt')}`);
  check(asset.ok && (await asset.text()) === 'ningzhou-unicode-asset', '中文文件名静态资源可读取');
  const asciiAsset = await fetch(`${server.base}/assets/ascii.txt`);
  check(asciiAsset.ok && (await asciiAsset.text()) === 'ningzhou-nested-ascii-asset', '多级路径的英文静态资源可读取');
  const state = await fetch(`${server.base}/api/state`);
  const stateBody = await state.json();
  check(state.ok && stateBody.hasGame === true && !!stateBody.state, '本地模式无需登录且能默认开局');
  for (const path of ['login', 'whoami', 'feedback', 'leaderboard', 'tianji/board', 'tianji/status']) {
    check((await fetch(`${server.base}/api/${path}`)).status === 404, `本地模式关闭 ${path}`);
  }
  let tianjiExists = false;
  try { tianjiExists = (await stat(join(install, 'local-data', 'tianji'))).isDirectory(); } catch (error) { if (error.code !== 'ENOENT') throw error; }
  check(!tianjiExists, '本地启动和 state 请求不创建天机数据目录');
  check((await fetch(`${server.base}/%2e%2e/secret.txt`)).status === 404, '静态目录不能向上读取文件');
  check((await fetch(`${server.base}/escaped/secret.txt`)).status === 404, '符号链接不能读取根目录同名前缀的相邻目录');
  await stop(server);
  // 存在损坏的多人数据时，本地启动应完全不读取它，而非只隐藏多人按钮。
  await mkdir(join(install, 'poisoned-local', 'tianji'), { recursive: true });
  await writeFile(join(install, 'poisoned-local', 'tianji', 'store.json'), '{invalid-multiplayer-store', 'utf8');
  server = await start(['--local'], 'poisoned-local');
  const isolatedState = await fetch(`${server.base}/api/state`);
  const isolatedBody = await isolatedState.json();
  check(server.log().includes(`http://127.0.0.1:${server.port}/`), '仅传 --local 时也保持本机监听');
  check(isolatedState.ok && isolatedBody.hasGame === true, '本地开局不依赖多人存储可用性');
  check(!server.log().includes('天机大比存储损坏'), '本地启动不读取既有天机 Store');
  check(!isolatedBody.state?.tianji, '本地状态不附加天机窗口数据');
  await stop(server);
  server = await start(['--host', '127.0.0.1'], 'public-data');
  const publicMode = await (await fetch(`${server.base}/api/mode`)).json();
  check(publicMode.local === false, '不加 --local 保持公网默认模式');
  check((await fetch(`${server.base}/api/state`)).status === 401, '公网默认模式保留登录鉴权');
  check((await fetch(`${server.base}/api/tianji/board`)).status === 200, '公网默认模式保留天机公开榜单');
} finally {
  if (server) await stop(server);
}
const failures = results.filter(result => !result.passed);
await writeFile(join(fixture, 'results.json'), JSON.stringify({ binary, results }, null, 2), 'utf8');
console.log(`${results.length - failures.length}/${results.length} passed; fixture: ${fixture}`);
if (failures.length) process.exitCode = 1;
