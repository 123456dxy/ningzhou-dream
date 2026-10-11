// 检查 JS 版牌组构成与守势预览，用于核对 C++ 版。
import { ensureArts, artMods } from '../../michangsheng-demo/arts.mjs';
import { battleStart, battlePreview } from '../../michangsheng-demo/battle.mjs';

function makeGame(seed, spells) {
  const p = {
    name: '测试', gender: '女', age: 20, lifespan: 100, stage: 0, xp: 0,
    apt: 60, luck: 45, insight: 20, sword: 20, alchemy: 10, body: 2,
    health: 100, stones: 500, herbs: 20, pills: 0, cores: 0,
    location: 'wuling', relations: {}, renown: 0, highestStage: 0,
    achievements: [], sect: null, traits: ['sword'], build: { budget: 10 },
    arts: null,
  };
  const s = {
    version: 7, rng: seed >>> 0, seed: seed >>> 0, year: 0, eventSeq: 0, npcSeq: 0,
    ended: false, pending: null, plan: 'cultivate', flags: {}, logs: [], npcs: [],
    world: { prosperity: 72, market: 1, mineOwner: 'jinhong', herbs: 3200, coreStock: 120, pillStock: 90, weather: 'x', sects: {} },
    player: p,
  };
  ensureArts(s);
  p.arts.spells = spells;
  return s;
}

const ctx = {
  random: (s) => { s.rng = (Math.imul(s.rng, 1664525) + 1013904223) >>> 0; return s.rng / 4294967296; },
  log: () => {}, gainXP: () => {}, swordGain: () => {},
};

// 情形一：携带 ['strike','sword','water','wood']（我的测试脚本用的）
const s1 = makeGame(20261004, ['strike', 'sword', 'water', 'wood']);
battleStart(s1, { name: 'x', stage: 0, style: '剑' }, ctx);
const b1 = s1.player.arts.battle;
const c1 = {};
for (const c of b1.deck) c1[c.spell] = (c1[c.spell] || 0) + 1;
console.log('携带含 strike: deck=' + b1.deck.length + ' counts=' + JSON.stringify(c1));

// 守势在两种情形下的护盾
console.log('artMods.shield=' + artMods(s1.player).shield.toFixed(6) + ' max=' + b1.max);
const pvDefend = battlePreview(s1.player, b1, 'defend');
console.log('defend 预览: shield=' + pvDefend.shield + ' cost=' + pvDefend.cost);

const pvWater = battlePreview(s1.player, b1, 'water');
console.log('water 预览: shield=' + pvWater.shield + ' cost=' + pvWater.cost);

// 情形二：携带不含 strike（更贴近实际开局 ['strike','sword','water','wood'] 的默认）
const s2 = makeGame(20261004, ['sword', 'water', 'wood', 'fire']);
battleStart(s2, { name: 'x', stage: 0, style: '剑' }, ctx);
const b2 = s2.player.arts.battle;
const c2 = {};
for (const c of b2.deck) c2[c.spell] = (c2[c.spell] || 0) + 1;
console.log('携带不含 strike: deck=' + b2.deck.length + ' counts=' + JSON.stringify(c2));
