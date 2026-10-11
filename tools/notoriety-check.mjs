// 确认恶名刚好 40 时的行为
import { ensureArts } from '../../michangsheng-demo/arts.mjs';
import { ensureSociety, socInitPayroll, socAccrue } from '../../michangsheng-demo/society.mjs';

function makeGame(sect, stage, days, notoriety = 0) {
  const p = {
    name: '测试', gender: '女', age: 20, lifespan: 100, stage, xp: 0,
    apt: 60, luck: 45, insight: 20, sword: 20, alchemy: 10, body: 2,
    health: 100, stones: 500, herbs: 20, pills: 0, cores: 0,
    location: 'wuling', relations: {}, renown: 0, highestStage: stage,
    achievements: [], sect, traits: ['sword'], build: { budget: 10 }, arts: null,
  };
  const s = {
    version: 5, rng: 1, seed: 1, year: Math.floor(days / 360), eventSeq: 0, npcSeq: 0,
    ended: false, pending: null, plan: 'cultivate', flags: {}, logs: [], npcs: [],
    clock: { days, ageBase: 16, carry: {}, job: null },
    world: { prosperity: 72, market: 1, mineOwner: 'jinhong', herbs: 3200, coreStock: 120, pillStock: 90, weather: '春和景明', sects: {} },
    player: p,
  };
  ensureArts(s);
  ensureSociety(s);
  s.bonds.notoriety = notoriety;
  return s;
}

for (const n of [0, 39, 40, 41]) {
  const s = makeGame('jinhong', 0, 0, n);
  socInitPayroll(s);
  s.clock.days = 60;   // 第 2 个月
  socAccrue(s);
  console.log(`恶名=${n} balance=${s.bonds.payroll.balance} credited=${s.bonds.payroll.credited} lastMonth=${s.bonds.payroll.lastMonth}`);
}
