// 丹药改版的真实HTTP回归；独立临时目录、随机端口和临时角色。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, mkdtempSync } from 'node:fs';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const input = resolve(root, args.includes('--exe') ? args[args.indexOf('--exe') + 1] : 'build-vs2022/Debug/ningzhou_web.exe');
const temporary = mkdtempSync(join(tmpdir(), 'ningzhou-pill-revision-'));
const binary = join(temporary, 'pill-revision.exe'); copyFileSync(input, binary);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
const child = spawn(binary, ['--host', '127.0.0.1', '--port', String(port), '--web', join(root, 'web'),
  '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback'), '--no-open'],
  { cwd: temporary, windowsHide: true, stdio: 'ignore' });
const exited = once(child, 'exit');
let cookie = '', failures = 0;
const wait = ms => new Promise(done => setTimeout(done, ms));
async function call(path, body) {
  await wait(90);
  const response = await fetch(`http://127.0.0.1:${port}/api/${path}`, body === undefined ? { headers: { cookie } } : {
    method: 'POST', headers: { 'Content-Type': 'application/json', cookie }, body: JSON.stringify(body),
  });
  const setCookie = response.headers.get('set-cookie'); if (setCookie) cookie = setCookie.split(';')[0];
  const result = await response.json();
  assert.ok(response.ok && result.ok !== false && !result.error, `${path}: ${result.error || response.status}`);
  return result;
}
function check(condition, message) {
  console.log(`${condition ? 'PASS' : 'FAIL'} ${message}`); if (!condition) failures++;
}
try {
  for (let attempt = 0; attempt < 200; attempt++) {
    try { if ((await fetch(`http://127.0.0.1:${port}/api/mode`)).ok) break; } catch {}
    assert.equal(child.exitCode, null, '隔离服务应保持运行'); await wait(50);
  }
  await call('login', { nickname: `PillQa${Date.now().toString(36)}`, password: 'isolated-pill-revision' });
  await call('new', { name: '丹药改版验收', seed: 20261016, traits: [], allocation: {} });
  const original = JSON.parse((await call('export')).game);
  if (args.includes('--migration-only')) {
    const legacy = structuredClone(original); legacy.healthRulesVersion = 1; legacy.player.health = 100;
    const npc = legacy.npcs.find(row => row.alive && !row.beast);
    npc.bag = { 'potion:5519': 2, '5519': 1, 'potion:5420': 3,
      'potion:5318': 1, 'herb:诱妖草': 1, 'herb:6324': 1, 'herb:幻心草': 5 };
    await call('import', { game: JSON.stringify(legacy) });
    const loadedNpc = JSON.parse((await call('export')).game).npcs.find(row => row.id === npc.id);
    check(loadedNpc.bag['potion:5420'] === 6 && Object.keys(loadedNpc.bag).length === 2,
      '旧NPC前缀/裸ID化毒丹正确合并，退休探索物资清退');
    for (const id of ['5318', '5319']) {
      const game = structuredClone(original); game.healthRulesVersion = 1; game.npcs = []; game.pending = null;
      Object.assign(game.player, { stage: 6, highestStage: 6, lifespan: 500, health: 100, stones: 100, herbs: 0 });
      Object.assign(game.player.arts, { herbs: {}, nodes: [], furnace: 'furnace-5', durability: 37 });
      game.job = { actionType: 'brew', kind: 'brew', destination: id, name: '旧在途炼丹',
        total: 120, remaining: 60, started: 0, cost: 50, paid: true, gift: 3, batch: 3,
        craftOutput: 1, chance: 0.5, fee: 0 };
      await call('import', { game: JSON.stringify(game) });
      const loaded = JSON.parse((await call('export')).game);
      check(loaded.job == null && loaded.player.stones === 486 && loaded.player.herbs === 9,
        `${id}退休制作取消并退款50加三份112折价，返还九份辅材`);
    }
  } else {
  {
    const game = structuredClone(original);
    const npc = game.npcs.find(row => row.alive && !row.beast);
    assert.ok(npc, '隔离新角色有真实NPC');
    npc.bag = {}; npc.gear = []; npc.equipped = {};
    npc.pillState ??= {}; npc.pillState.poison = 80;
    await call('import', { game: JSON.stringify(game) });
    const before = (await call('export')).game;
    await call('npc-bag', { npc: npc.id });
    await call('npc-bag', { npc: npc.id });
    check((await call('export')).game === before, '连续NPC行囊查询不写库存不改RNG与服丹状态');
  }
  if (!args.includes('--npc-only')) {
  for (const [id, expected] of [['5001', 40], ['5101', 200], ['5201', 800], ['5301', 2000], ['5501', null]]) {
    const game = structuredClone(original); game.pending = null; game.job = null; game.npcs = [];
    Object.assign(game.player, { stage: 12, highestStage: 12, health: 1, lifespan: 2000, xp: 0 });
    Object.assign(game.player.arts, { poison: 0, potions: { [id]: 1 }, used: {}, battle: null });
    await call('import', { game: JSON.stringify(game) });
    const before = (await call('state')).state;
    check(before.player.healthMax > 2000, '化神气血上限使用真实点数，足以区分高阶疗伤档位');
    const preview = before.pillCatalog.find(row => row.id === id);
    const after = (await call('use-pill', { id })).state;
    const expectedHealth = expected === null ? after.player.healthMax : Math.min(after.player.healthMax, 1 + expected);
    check(after.player.health === expectedHealth, `${id}实际恢复${expected ?? '全部'}气血（得到${after.player.health}）`);
    check(preview.effectValue === after.player.health - before.player.health, `${id}服用预览与真实恢复一致`);
  }
  {
    const game = structuredClone(original); game.pending = null; game.job = null; game.npcs = [];
    Object.assign(game.player, { stage: 12, highestStage: 12, health: 1, lifespan: 2015, lifeBonus: 15, pillLifeBonus: 0, body: 0 });
    Object.assign(game.player.arts, { poison: 0, potions: { '5119': 1, '5515': 1 }, used: {}, baseHealthBonus: 0, foundationHealthBonus: 0,
      hpBonus: 0, gear: [], equipped: {}, aux: [], nodes: [], battle: null });
    await call('import', { game: JSON.stringify(game) });
    const before = (await call('state')).state;
    const life = (await call('use-pill', { id: '5119' })).state;
    check(life.player.lifespan === 2025, '续命丹真实增加10年且保留15年天赋');
    const body = (await call('use-pill', { id: '5515' })).state;
    check(body.player.healthMax - before.player.healthMax === 96, '最高体魄丹40基础值实际增加96气血上限');
  }
  const state = (await call('state')).state;
  check(state.pillCatalog.length === 123 && new Set(state.pillCatalog.map(row => row.kind)).size === 14, '有效丹药目录123味14用途');
  check(['5519', '5318', '5319'].every(id => !state.pillCatalog.some(row => row.id === id)), '退休丹药不再出现在有效目录');
  check(state.poison.detoxPerYear === 5, '自然年度化毒统一5点');
  }
  }
  console.log(JSON.stringify({ temporary, port, failures }));
  process.exitCode = failures ? 1 : 0;
} finally {
  if (child.exitCode === null) child.kill();
  await Promise.race([exited, wait(5000)]);
}
