// 检查 JS 版五项开局天赋实际发放的物品，供 C++ 版对照。
import { ensureArts } from '../../michangsheng-demo/arts.mjs';
import { adventureBirth, ensureAdventure } from '../../michangsheng-demo/adventure.mjs';
import { ensureEconomy } from '../../michangsheng-demo/economy.mjs';

const ctx = {
  random: (s) => { s.rng = (Math.imul(s.rng, 1664525) + 1013904223) >>> 0; return s.rng / 4294967296; },
  log: () => {}, gainXP: () => {}, swordGain: () => {},
};

function makeGame(traits) {
  const p = {
    name: '测试', gender: '女', age: 20, lifespan: 100, stage: 0, xp: 0,
    apt: 60, luck: 45, insight: 20, sword: 20, alchemy: 10, body: 2,
    health: 100, stones: 500, herbs: 20, pills: 0, cores: 0,
    location: 'wuling', relations: {}, renown: 0, highestStage: 0,
    achievements: [], sect: null, traits, build: { budget: 20 }, arts: null,
  };
  const s = {
    version: 7, rng: 1, seed: 1, year: 0, eventSeq: 0, npcSeq: 0,
    ended: false, pending: null, plan: 'cultivate', flags: {}, logs: [], npcs: [],
    clock: { days: 0, ageBase: 16, carry: {}, job: null },
    world: { prosperity: 72, market: 1, mineOwner: 'jinhong', herbs: 3200, coreStock: 120, pillStock: 90, weather: 'x', sects: {} },
    player: p,
  };
  ensureArts(s);
  ensureEconomy(s);
  ensureAdventure(s);
  return s;
}

const cases = [
  ['forgeheir'],
  ['furnaceheir'],
  ['furnacescion'],
  ['heirblade'],
  ['artifactward'],
  ['sword'],           // 普通天赋不应发任何物品
];

for (const traits of cases) {
  const s = makeGame(traits);
  const before = { furnace: s.player.arts.furnace, gear: s.player.arts.gear.length, iron: s.player.arts.materials.iron || 0, books: s.economy.books.length, exp: s.player.arts.exp['器'] };
  adventureBirth(s, ctx);
  const a = s.player.arts;
  console.log(`${traits[0]}: 炉=${a.furnace} 法器数=${a.gear.length} 装备=${JSON.stringify(a.equipped)} 铁=${a.materials.iron || 0} 器道经验=${a.exp['器']} 抄本数=${s.economy.books.length}`);
  if (a.gear.length) {
    const g = a.gear[a.gear.length - 1];
    console.log(`         法器: ${g.name} slot=${g.slot} grade=${g.grade} bonus=${g.bonus} element=${g.element} craftCost=${g.craftCost} dims=${JSON.stringify(g.dimensions)} id=${g.id}`);
  }
}
