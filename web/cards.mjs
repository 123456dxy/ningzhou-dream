// 宁州一梦 · C++ 版 — 斗法卡面文案
//
// 对应 JS cards-ui.mjs 的 cardKind / cardRule。
// 40 张神通、20 种 kind，之前界面上只显示一个名字和伤害数字，
// 玩家不知道「蓄势」「震荡」「穿盾」到底是什么意思。

'use strict';

// 20 种 kind → 中文标签
const KIND_LABEL = {
  shield: '护盾', guard: '护盾', heal: '治疗', mana: '聚气', float: '聚气',
  charge: '蓄势', water: '蓄水', poison: '施毒', soul: '震荡', fire: '灼烧',
  sword: '剑术', human: '剑术', wave: '爆发', heavy: '爆发',
  poisonBurst: '爆发', soulBurst: '爆发', bodyBurst: '爆发',
  steal: '夺灵', drain: '穿盾', attack: '攻击',
  // 灵力经营（参考杀戮尖塔）
  store: '蓄灵', flow: '聚气', burst: '夺灵',
  draw: '抽牌', foresee: '预谋',
  exhaust: '本场移除', redraw: '换手', empower: '增伤', ward: '减伤', fireBurst: '引爆',
};

export function cardKind(card) {
  return KIND_LABEL[card?.kind] || '神通';
}

// 每张卡的规则说明
const KIND_RULE = {
  sword: '按当前剑气增伤；施术后剑气 +1（功法可追加），上限 8。',
  human: '按当前剑气增伤；消耗全部剑气与当前 8% 气血（不致死）。',
  wave: '每层水势增伤，施术后消耗全部水势。',
  heavy: '每层重势增伤 12%，施术后消耗全部重势。',
  poisonBurst: '每层毒追加伤害，施术后清除毒。',
  soulBurst: '每层震荡增伤 15%，施术后消耗震荡。',
  bodyBurst: '体魄、重势增伤；出牌前有 3 灵力时额外增伤 15%。',
  mana: '恢复灵力；卡牌进入弃牌堆，本回合不能再抽回。',
  float: '恢复 2 灵力；随后两回合灵力回复 −1。',
  shield: '护盾可叠加，最高气血 30%；敌方行动后清零。',
  guard: '护盾可叠加；本回合额外减伤 10%，总减伤最高 35%。',
  heal: '恢复气血，单卡最高气血 14%；冷却按回合结算。',
  charge: '获得重势，上限 4 层，供爆发神通消耗。',
  water: '水势 +2，上限 6 层，供水系爆发消耗。',
  poison: '叠毒，上限 6 层；回合结束结算，持续 3 回合。',
  soul: '叠加震荡，上限 6 层，供神识爆发消耗。',
  fire: '附加灼烧；回合结束结算，持续 3 回合。',
  steal: '命中后恢复灵力。',
  drain: '无视敌方护盾。',
  attack: '直接攻击；木→火→土→金→水→木相生连击增伤。',
  // ---- 灵力经营：每条都有代价，不能白拿 ----
  store: '把本回合剩余灵力封存到下回合（有上限）；代价是本回合灵力清零。',
  flow: '此后每回合灵力 +1（本场持续）；每次施术令当前气血上限下降 8%，本场结束后恢复。',
  burst: '立刻获得灵力；代价是此后每回合灵力 −1（本场持续）。',
  draw: '本回合立刻多抽牌；卡牌本身也要花灵力，等于用灵力换手牌。',
  foresee: '下回合多抽牌；本回合不出力，把资源留给下一波。',
  exhaust: '选择一种其它神通，将其全部副本从本场手牌、抽牌堆和弃牌堆中移除；下一场恢复。',
  redraw: '弃掉其它手牌，再重抽相同数量 +1 张；弃牌仍会在牌堆重洗后出现。',
};

export function cardRule(card) {
  if (!card) return '';
  if (card.description) return card.description;
  // 两张有特殊规则的神通
  if (card.id === 'goldblade')
    return '重势增伤并消耗全部重势；无视敌方 50% 护盾。';
  if (card.id === 'array')
    return '获得护盾、额外减伤 10%（总减伤上限 35%），恢复 1 灵力。';
  return KIND_RULE[card.kind] || '';
}

// 将实际战斗状态连同持续范围显示出来；不据卡名推测是否已经生效。
export function battleStatusRows(b) {
  const rows = [];
  const add = (label, value, scope, tone = 'good') => rows.push({ label, value, scope, tone });
  const percent = value => `${Math.round(value * 100)}%`;
  if (b.swordQi > 0) add('剑气', `${b.swordQi} / 8 层`, '本场积累；部分剑击消耗');
  if (b.charge > 0) add('重势', `${b.charge} / 4 层`, '爆发神通消耗后清空');
  if (b.water > 0) add('水势', `${b.water} / 6 层`, '水系爆发消耗后清空');
  if (b.swordSequence > 0) add('连续剑牌', `${b.swordSequence} 张`, '本回合累计；非剑牌会打断');
  if (b.discardPower > 0) add('弃牌锐势', `下次直接攻击 +${percent(b.discardPower)}`, '只消耗一次；治疗与抽牌不消耗');
  if (b.buff) add('攻击增伤', `${b.buff > 0 ? '+' : ''}${percent(b.buff)}`, '本场持续，可叠加', b.buff > 0 ? 'good' : 'bad');
  if (b.effectiveReduction != null && b.effectiveReduction > 0) add('总减伤', percent(b.effectiveReduction), '当前实际减伤，已计入功法与守势');
  else if (b.reduction) add('战斗减伤', percent(Math.abs(b.reduction)), '本场持续');
  if (b.guard > 0) add('守势', `减伤 +${percent(b.guard)}`, '本回合；敌方行动后重置');
  if (b.manaBonus > 0) add('聚气', `每回合灵力 +${b.manaBonus}`, '本场持续，可叠加');
  if (b.manaPenalty > 0) add('夺灵代价', `每回合灵力 −${b.manaPenalty}`, '本场持续，可叠加', 'bad');
  if (b.carryCap > 0 || b.carryMana > 0) add('封存灵力', `${b.carryMana || 0} / ${b.carryCap || 0} 点`, '仅下回合解封一次');
  if (b.drawNext > 0) add('预谋', `下回合多抽 ${b.drawNext} 张`, '仅下回合生效，可叠加');
  if (b.lock > 0) add('锁灵', `每回合灵力 −1 · 剩 ${b.lock} 回合`, '悬灵术的代价', 'bad');
  const weakened = b.weakened ?? b.enemyWeaken ?? 0;
  if (weakened > 0) add('扰灵', `${weakened} 层${b.weakenedPenalty != null ? ` · 下次直接攻击 −${percent(b.weakenedPenalty)}` : ''}`, b.weakenedDescription || '下一次直接攻击会削弱；治疗与抽牌不消耗', 'bad');
  if (b.hpCostPct > 0) add('聚气损耗', `累计施术代价 ${b.hpCostPct}% · 当前上限 ${b.max}`, '每次按当前上限减少 8%，本场结束恢复', 'bad');
  if (b.playerPoison > 0) add('受毒', `每回合 ${b.playerPoison} 点 · 剩 ${b.playerPoisonTurns} 回合`, '结束回合时结算', 'bad');
  return rows;
}

// 卡面上要显示的伤害/护盾/治疗数字
export function cardNumbers(card) {
  const out = [];
  if (card.damage > 0) out.push(`伤 ${card.damage}`);
  if (card.shield > 0) out.push(`盾 ${card.shield}`);
  if (card.heal > 0) out.push(`疗 ${card.heal}`);
  if (card.qi > 0) out.push(`势 ${card.qi}`);
  return out.join(' · ');
}
