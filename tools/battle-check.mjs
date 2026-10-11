// 用 JS 版跑一场确定性的斗法，打印逐回合结果，供 C++ 版对照。
import { ensureArts, artStartBattle, artBattleTurn } from '../../michangsheng-demo/arts.mjs';

// 造一个可控的玩家状态
function makeGame(seed) {
  const p = {
    name: '测试', gender: '女', age: 20, lifespan: 100, stage: 0, xp: 0,
    apt: 60, luck: 45, insight: 20, sword: 20, alchemy: 10, body: 2,
    health: 100, stones: 500, herbs: 20, pills: 0, cores: 0,
    location: 'wuling', relations: {}, renown: 0, highestStage: 0,
    achievements: [], sect: null, traits: ['sword'], build: { budget: 10 },
    arts: null,
  };
  const s = {
    version: 5, rng: seed >>> 0, seed: seed >>> 0, year: 0, eventSeq: 0, npcSeq: 0,
    ended: false, pending: null, plan: 'cultivate', flags: {}, logs: [], npcs: [],
    world: { prosperity: 72, market: 1, mineOwner: 'jinhong', herbs: 3200, coreStock: 120, pillStock: 90, weather: '春和景明', sects: {} },
    player: p,
  };
  ensureArts(s);
  p.arts.spells = ['strike', 'gold', 'sword', 'water', 'wood', 'fire'];
  return s;
}

const ctx = {
  random: (s) => { s.rng = (Math.imul(s.rng, 1664525) + 1013904223) >>> 0; return s.rng / 4294967296; },
  log: () => {},
};

const s = makeGame(20261004);
artStartBattle(s, { name: '试招对手', stage: 0, style: '剑' }, ctx);
const b = s.player.arts.battle;
console.log(`start hp=${b.hp}/${b.max} enemyHp=${b.enemyHp}/${b.enemyMax} mana=${b.mana}/${b.maxMana} shield=${b.shield}`);

// 固定出招顺序，保证可复现
const plan = ['sword', 'strike', 'gold', 'defend', 'sword', 'water', 'fire', 'strike'];
for (let i = 0; i < plan.length && s.player.arts.battle; i++) {
  const r = artBattleTurn(s, plan[i], ctx);
  const bb = s.player.arts.battle;
  if (!bb) { console.log(`turn${i + 1} ${plan[i]} -> 斗法结束`); break; }
  console.log(`turn${i + 1} ${plan[i]} hp=${bb.hp}/${bb.max} enemyHp=${bb.enemyHp} mana=${bb.mana} shield=${bb.shield} qi=${bb.swordQi} turn=${bb.turn} ok=${r.ok}`);
}
const fin = s.player.arts.battle;
console.log('final:', fin ? 'battle ongoing' : `playerHealth=${s.player.health} wins=${s.player.arts.progress.wins} stones=${s.player.stones}`);
