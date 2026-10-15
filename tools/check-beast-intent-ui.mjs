// 妖兽逐段意图的轻量纯展示检查；模拟字段不代替真实后端或浏览器结算验收。
import assert from 'node:assert/strict';
import { renderBeastIntent } from '../web/beast-intent.mjs';

let checks = 0;
// 用途：记录纯渲染断言；流程：妖兽意图展示回归；传条件和说明，失败立即抛出，不访问账号、端口或存档。
function check(value, description) {
  assert.ok(value, description); ++checks;
  console.log(`PASS ${description}`);
}

const fixture = {
  enemyBeast: true, beastEnergyBudget: 5, beastActionLimit: 3,
  beastActions: [
    { id: 'pounce', name: '扑击<script>', cost: 1, rawDamage: 41, damage: 0, blocked: 32, counterDamage: 8, executed: true },
    { id: 'flame', name: '吐息', cost: 2, rawDamage: 17, damage: 9, blocked: 2, burnDamage: 6, piercePercent: 25, executed: true },
    { id: 'carapace', name: '凝甲', cost: 1, shieldGain: 90, executed: false },
  ],
};
const before = JSON.stringify(fixture);
const html = renderBeastIntent(fixture);
check(html.includes('妖力预算 <b>5</b>') && html.includes('最多 <b>3</b> 次行动'), '妖力和行动上限直接显示后端值');
check((html.match(/data-beast-action=/g) || []).length === 3, '所有冻结行动按顺序分别展示');
check(html.includes('1. 扑击&lt;script&gt;') && !html.includes('<script>'), '行动名称安全转义');
check(html.includes('打出 41 · 气血伤害 0') && html.includes('盾挡 32'), '原始伤害和零实际损血同时可见，不在前端重算减伤');
check(html.includes('受到反震 8') && html.includes('穿盾 25%'), '反震和穿盾使用独立权威预演字段');
check(html.includes('施加灼烧 6 点 / 回合') && html.includes('打出 17 · 气血伤害 9'), '吐息直伤与后续灼烧清楚区分');
check(html.includes('data-executed="false"') && html.includes('不执行 · 预计战局将提前结束') && !html.includes('计划凝盾 90'), '死亡后剩余行动标记不执行且不误报其效果');
const utility = renderBeastIntent({ ...fixture, beastActions: [
  { id: 'defend', name: '凝甲', cost: 1, shieldGain: 23, healGain: 7, executed: true },
  { id: 'roar', name: '怒吼', cost: 1, weakenLayers: 1, chargeGain: 2, poisonDamage: 8, executed: true },
] });
check(utility.includes('计划凝盾 23') && utility.includes('最多回复 7 气血'), '冻结盾疗值明确标注计划或上限，避免假报实际容量收益');
check(utility.includes('计划虚弱 1 层') && utility.includes('计划蓄势 2 层') && utility.includes('毒息 8 点 / 回合'), '辅助与毒息分别显示');
check(renderBeastIntent({ ...fixture, enemyBeast: false }) === '', '普通修士不使用妖兽分段面板');
check(renderBeastIntent({ enemyBeast: true }) === '' && renderBeastIntent(null) === '', '旧单意图和空状态沿用既有展示');
check(JSON.stringify(fixture) === before, '纯展示不改变冻结计划或预演值');
console.log(`妖兽意图纯展示：${checks} 项通过。`);
