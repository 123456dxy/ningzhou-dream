// 打印 JS 版炼丹/炼器的计算结果，供 C++ 版对照。
import { ensureArts, artBrewInfo, artForgeInfo, artPillEffect } from '../../michangsheng-demo/arts.mjs';
import { ART_RECIPES } from '../../michangsheng-demo/reference-data.mjs';

function makePlayer(overrides = {}) {
  const p = {
    name: '测试', gender: '女', age: 20, lifespan: 100, stage: 0, xp: 0,
    apt: 60, luck: 45, insight: 20, sword: 20, alchemy: 30, body: 2,
    health: 100, stones: 5000, herbs: 100, pills: 0, cores: 0,
    location: 'wuling', relations: {}, renown: 0, highestStage: 0,
    achievements: [], sect: null, traits: [], build: { budget: 10 }, arts: null,
    ...overrides,
  };
  const s = { player: p, world: { market: 1, herbs: 5000 } };
  ensureArts(s);
  return p;
}

// --- 炼丹 ---
console.log('=== 炼丹 ===');
for (const id of ['5001', '5010', '5020']) {
  const p = makePlayer();
  const b = artBrewInfo(p, id);
  if (!b) { console.log(id, 'null'); continue; }
  console.log(`${id} ${b.r.name} grade=${b.r.grade} cost=${b.cost} chance=${b.chance.toFixed(6)} count=${b.count} furnace=${b.f.grade}`);
}

// 不同丹术与门派
const p1 = makePlayer({ alchemy: 80, sect: 'lihuo' });
console.log('丹术80+离火门 chance=' + artBrewInfo(p1, '5001').chance.toFixed(6));

// --- 炼器 ---
console.log('=== 炼器 ===');
for (const combo of [
  ['iron', 'jade', 'wood', 'sword'],
  ['gold', 'crystal', 'soul', 'armor'],
  ['dragon', 'meteor', 'black', 'pearl'],
]) {
  const p = makePlayer({ stage: 6 });
  const b = artForgeInfo(p, ...combo);
  if (!b) { console.log(combo.join(','), 'null'); continue; }
  console.log(`${combo.join(',')} grade=${b.grade} util=${b.util.toFixed(6)} cost=${b.cost} bonus=${b.bonus} chance=${b.chance.toFixed(6)} dims=${JSON.stringify(b.dimensions)}`);
}

// 器道二层提高利用率
const p3 = makePlayer({ stage: 6 });
p3.arts.nodes = ['11-1', '11-2'];
console.log('器道二层 util=' + artForgeInfo(p3, 'iron', 'jade', 'wood', 'sword').util.toFixed(6));

// --- 丹药效果 ---
console.log('=== 丹药效果 ===');
for (const id of ['5001', '5005', '5030']) {
  const r = ART_RECIPES.find(x => x.id === id);
  if (!r) continue;
  const p = makePlayer({ stage: 3 });
  const e = artPillEffect(p, r);
  console.log(`${id} ${r.name} kind=${r.kind} value=${e.value} text=${e.text}`);
}
