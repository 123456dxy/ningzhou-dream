// 用 JS 版跑一场确定性的卡牌斗法，打印逐次出牌结果，供 C++ 版对照。
import { ensureArts } from '../../michangsheng-demo/arts.mjs';
import { battleStart, battleTurn, battlePreview } from '../../michangsheng-demo/battle.mjs';

const ctx = {
  random: (s) => { s.rng = (Math.imul(s.rng, 1664525) + 1013904223) >>> 0; return s.rng / 4294967296; },
  log: () => {}, gainXP: () => {}, swordGain: () => {},
};

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
    version: 7, rng: seed >>> 0, seed: seed >>> 0, year: 0, eventSeq: 0, npcSeq: 0,
    ended: false, pending: null, plan: 'cultivate', flags: {}, logs: [], npcs: [],
    world: { prosperity: 72, market: 1, mineOwner: 'jinhong', herbs: 3200, coreStock: 120, pillStock: 90, weather: '春和景明', sects: {} },
    player: p,
  };
  ensureArts(s);
  p.arts.spells = ['strike', 'sword', 'water', 'wood'];
  return s;
}

const s = makeGame(20261004);
battleStart(s, { name: '试招对手', stage: 0, style: '剑' }, ctx);
const b = s.player.arts.battle;

console.log(`start hp=${b.hp}/${b.max} enemyHp=${b.enemyHp}/${b.enemyMax} mana=${b.mana}/${b.maxMana} shield=${b.shield} qi=${b.swordQi}`);
console.log(`deck=${b.deck.length} hand=${b.hand.length} draw=${b.draw.length} intent=${b.intent.kind}/${b.intent.raw}`);

// 打印起手牌（按 uid 顺序）与预览，便于逐张核对
console.log('hand uids: ' + JSON.stringify(b.hand));
console.log('hand spells: ' + JSON.stringify(b.hand.map(uid => b.deck.find(c => c.uid === uid).spell)));

// 逐张预览手牌，检查预览与真实结算一致
for (const uid of [...b.hand]) {
  const card = b.deck.find(c => c.uid === uid);
  const pv = battlePreview(s.player, b, card.spell);
  console.log(`preview ${card.spell} cost=${pv.cost} dmg=${pv.damage} shield=${pv.shield} heal=${pv.heal} energy=${pv.energy} qi=${pv.qi}`);
}

// 依次出牌：灵击 -> 御剑诀 -> 结束回合 -> 灵击 ...
const plan = ['strike', 'sword', 'end', 'strike', 'water', 'end', 'strike', 'end'];
for (let i = 0; i < plan.length; i++) {
  const r = battleTurn(s, plan[i], ctx);
  const bb = s.player.arts.battle;
  if (!bb) { console.log(`step${i + 1} ${plan[i]} -> 斗法结束`); break; }
  console.log(`step${i + 1} ${plan[i]} hp=${bb.hp}/${bb.max} enemyHp=${bb.enemyHp} mana=${bb.mana}/${bb.maxMana} shield=${bb.shield} qi=${bb.swordQi} turn=${bb.turn} played=${bb.played} hand=${bb.hand.length} draw=${bb.draw.length} discard=${bb.discard.length} ok=${r.ok}`);
}
const fin = s.player.arts.battle;
console.log('final: ' + (fin ? 'ongoing' : `playerHealth=${s.player.health} wins=${s.player.arts.progress.wins}`));
