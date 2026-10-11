// 用 Node 直接加载 JS 版的数据模块，导出成 JSON 供 C++ 头文件生成使用。
// 这样不依赖正则解析，数据一定和 JS 版一致。
// 用法: node tools/dump-data.mjs > tools/data-dump.json

import { ART_HERBS, ART_RECIPES, ART_FURNACES } from '../../michangsheng-demo/reference-data.mjs';
import { BUILD_MANUALS, BUILD_SPELLS, BUILD_GUIDES } from '../../michangsheng-demo/build-data.mjs';
import { ART_MATERIALS, ART_SHAPES, ART_QUESTS, ART_ACHIEVEMENTS,
         ART_DAOS, ART_NODES, ART_MANUALS, ART_SPELLS } from '../../michangsheng-demo/arts.mjs';
import { BIRTH_TALENTS, ADV_ORIGINS, ADV_KINDS } from '../../michangsheng-demo/adventure.mjs';
import { SWORD_REALMS } from '../../michangsheng-demo/sword.mjs';
import { TALENTS } from '../../michangsheng-demo/sim.mjs';

const out = {
  herbs: ART_HERBS,
  recipes: ART_RECIPES,
  furnaces: ART_FURNACES,
  buildManuals: BUILD_MANUALS,
  buildSpells: BUILD_SPELLS,
  buildGuides: BUILD_GUIDES,
  // 洞府相关的表
  daos: ART_DAOS,
  nodes: ART_NODES,
  materials: ART_MATERIALS,
  shapes: ART_SHAPES,
  // ART_QUESTS 里带函数（requires 是谓词），只导出可序列化的字段
  quests: ART_QUESTS.map(q => ({
    id: q.id, name: q.name, start: q.start, end: q.end,
    stage: q.stage ?? 0, maxStage: q.maxStage ?? null,
    location: q.location, desc: q.desc, reward: q.reward,
    price: q.price, visits: q.visits ?? null,
    requires: typeof q.requires === 'string' ? q.requires : null,
    battle: !!q.battle,
  })),
  achievements: ART_ACHIEVEMENTS,
  // 基础功法与神通的完整定义（含每层系数）
  manuals: ART_MANUALS,
  spells: ART_SPELLS,

  // ---- Demo06 / Demo07 新增 ----
  // 完整天赋表（39 项）
  talents: TALENTS.map(t => ({
    id: t.id, name: t.name, tag: t.tag, cost: t.cost, desc: t.desc,
    stat: t.stat ?? null, value: t.value ?? 0,
  })),
  // 五项会发放实际物品的开局天赋
  birthTalents: BIRTH_TALENTS.map(t => ({
    id: t.id, name: t.name, tag: t.tag, cost: t.cost, desc: t.desc,
  })),
  // 起源文本（含五项新天赋）
  advOrigins: ADV_ORIGINS,
  // 游历遇事类型
  advKinds: ADV_KINDS,
  // 七重剑道境界
  swordRealms: SWORD_REALMS,
};

process.stdout.write(JSON.stringify(out, null, 1));
