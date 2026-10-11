// 为隔离测试服务生成真实战斗存档；会重置指定服务的角色。
// node tools/battle-fixture.mjs --port 18106
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = process.argv.slice(2);
const port = Number(args[args.indexOf('--port') + 1]);
if (!args.includes('--port') || !Number.isInteger(port) || port < 1024 || port > 65535 || [8106, 8107].includes(port))
  throw new Error('请用 --port 指定隔离测试端口，不能使用正式游戏端口 8106/8107。');
const base = `http://127.0.0.1:${port}/api/`;
async function call(path, input) {
  const response = await fetch(base + path, input === undefined ? {} : {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(input),
  });
  const result = await response.json();
  if (!response.ok || result.error || result.ok === false)
    throw new Error(`${path}: ${result.error || response.status}`);
  return result;
}

await call('new', { name: '战斗界面回归', traits: [], seed: 20261011 });
const game = JSON.parse((await call('export')).game);
const spells = ['purge', 'swordFocus', 'qiFlow', 'skim', 'aegis', 'refresh'];
game.player.stage = game.player.highestStage = 6;
game.player.lifespan = 500 + game.player.lifeBonus;
game.player.location = 'fanghu';
game.player.arts.spells = spells;
await call('import', { game: JSON.stringify(game) });
await call('wild', { id: 'bandit' });
const fixture = JSON.parse((await call('export')).game);
const battle = fixture.player.arts.battle;
if (!battle || battle.lethal) throw new Error('未进入可退出的真实斗法。');
const hand = spells.map(id => battle.deck.find(card => card.spell === id)?.uid);
if (hand.some(uid => !uid)) throw new Error('真实牌组缺少测试神通。');
battle.hand = hand;
battle.draw = battle.deck.filter(card => !hand.includes(card.uid)).map(card => card.uid);
battle.discard = [];
battle.exhausted = [];
battle.cooldowns = {};
battle.mana = 12;
battle.played = 0;
// 再经正式读档校验，确保四个牌堆与规则完整合法。
await call('import', { game: JSON.stringify(fixture) });
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
mkdirSync(join(root, 'build'), { recursive: true });
const path = join(root, 'build', 'battle-ui-fixture.json');
writeFileSync(path, (await call('export')).game, 'utf8');
console.log(`已生成并校验：${path}`);
