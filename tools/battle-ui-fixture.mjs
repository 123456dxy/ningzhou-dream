// 使用隔离服务真实建号、导入与悬赏，再整理有效卡堆供 UI 验收。
import { mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const index = process.argv.indexOf('--port');
const port = index >= 0 ? process.argv[index + 1] : '18106';
if (!/^\d+$/.test(port) || ['8106', '8107'].includes(port)) throw new Error('只能使用隔离测试端口。');
const request = async (path, body = {}) => {
  const response = await fetch(`http://127.0.0.1:${port}/api/${path}`, {
    method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body),
  });
  const data = await response.json();
  if (!response.ok || data.error || data.ok === false) throw new Error(`${path}: ${data.error || response.status}`);
  return data;
};
await request('new', {seed:480317,name:'斗法验收',gender:'男',traits:[],allocation:{}});
let game = JSON.parse((await request('export')).game);
Object.assign(game.player, {stage:6,highestStage:6,xp:0,stones:50000,health:100,location:'fanghu'});
const extras = ['purge','swordFocus','qiFlow','skim','aegis','refresh'];
game.player.arts.spells = extras;
game.player.arts.knownSpells = [...new Set([...(game.player.arts.knownSpells || []),...extras])];
await request('import', {game:JSON.stringify(game)});
await request('wild', {id:'bandit'});
game = JSON.parse((await request('export')).game);
const battle = game.player.arts.battle;
if (!battle) throw new Error('真实悬赏未创建斗法。');
battle.hand = [...extras,'strike'].map(id => battle.deck.find(card => card.spell === id)?.uid);
if (battle.hand.some(uid => !uid)) throw new Error('真实卡组缺少验收神通。');
battle.draw = battle.deck.map(card => card.uid).filter(uid => !battle.hand.includes(uid));
battle.discard = []; battle.exhausted = []; battle.mana = 12;
battle.hp = battle.max; battle.enemyHp = battle.enemyMax = Math.max(2000,battle.enemyMax);
battle.shield = 0; battle.enemyShield = 0;
await request('import', {game:JSON.stringify(game)});
const actual = (await request('export')).game;
mkdirSync(join(root,'build'),{recursive:true});
writeFileSync(join(root,'build','battle-ui-fixture.json'),actual + '\n');
console.log('已通过真实服务生成 build/battle-ui-fixture.json，含可控增益、移除与攻击手牌。');
