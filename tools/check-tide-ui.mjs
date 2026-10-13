// 纯展示回归：使用明确的模拟权威字段，不代替 C++ 兽潮结算测试。
import assert from 'node:assert/strict';
import { renderTidePanel, renderTideMarket, renderTideBattleNotice, shouldRenderTide } from '../web/tide-panel.mjs';
import { renderTidePanel as legacyRender } from '../web/friends.mjs';

let checks = 0;
function check(value, label) { assert.ok(value, label); ++checks; }
const tide = {
  eventId: 7, active: true, settled: false, severity: 2, daysLeft: 17, durationDays: 60,
  playerScore: 42, playerRank: 3, totalScore: 1111, wavesFought: 9, wavesWon: 7,
  battlefield: 0, battlefields: [
    { id: 0, name: '炼气战场', pointsPerKill: 1, enabled: true },
    { id: 1, name: '筑基战场', pointsPerKill: 3, enabled: true },
    { id: 2, name: '金丹战场', pointsPerKill: 9, enabled: false, reason: '境界尚未达到金丹初期。' },
    { id: 3, name: '元婴战场', pointsPerKill: 27, enabled: false, reason: '境界尚未达到元婴初期。' },
  ],
  leaderboard: [{ actorId: 'player', name: '道友<script>', realm: '筑基初期', score: 42, rank: 3, isPlayer: true },
    { actorId: 'elite', name: '流派高手', realm: '筑基后期', score: 60, rank: 1, elite: true }],
  market: { open: false, items: [] },
};
const active = renderTidePanel(tide, { enabled: true });
check(active.includes('17 天') && !active.includes('17 个月'), '倒计时使用后端剩余天数');
check(active.includes('42') && active.includes('1111'), '本人成绩与全州积分可见');
check((active.match(/data-action="tide-fight"/g) || []).length === 4, '四个境界战场分别有入口');
check(/data-battlefield="2"[^>]*disabled/.test(active) && active.includes('境界尚未达到金丹初期'), '未开放战场禁用并说明原因');
check(active.includes('27 积分') && active.includes('1 积分'), '击杀积分取自战场定义');
check(active.includes('道友&lt;script&gt;') && !active.includes('<script>'), '金榜姓名安全转义');
check(active.includes('流派高手') && active.includes('完整流派'), '特殊修士显示完整流派标记');
check(legacyRender === renderTidePanel, 'friends旧导出保持同一个实现');
check(!shouldRenderTide({ active: false }) && shouldRenderTide({ active: false, settled: true }), '结榜后保留事件卡');
const ended = { ...tide, active: false, settled: true, rewardStones: 5000, rewardPaid: true,
  market: { open: true, daysLeft: 20, discountPercent: 50, minDiscountPercent: 50, priceBasis: '当前声望下普通坊市购入价', items: [
    { kind: 'herb', id: 'h3', name: '三阶灵药', grade: 3, basePrice: 100, unitPrice: 50, stock: 3, enabled: true },
    { kind: 'material', id: 'm4', name: '四阶仙材', grade: 4, basePrice: 300, unitPrice: 150, stock: 0, enabled: false, reason: '已售罄' },
  ] } };
const settled = renderTidePanel(ended);
check(settled.includes('5000') && settled.includes('已到账') && !settled.includes('data-action="tide-fight"'), '期末自动奖到账且不重复发迎战入口');
check(settled.includes('data-action="tide-market-open"'), '结榜卡提供战后坊市入口');
const state = { tide: ended, player: { stones: 500 } };
const market = renderTideMarket(state, { grade: '3', quantities: { 'herb:h3': 2 } });
check(market.includes('三阶灵药') && !market.includes('四阶仙材'), '战后坊市按品阶筛选');
check(market.includes('正常价') && market.includes('100') && market.includes('排名价') && market.includes('50'), '明确正常价与排名后实价');
check(market.includes('当前声望') && market.includes('最低') && market.includes('50%'), '折扣基准和五折底线可见');
check(market.includes('data-count="2"') && market.includes('data-unit-price="50"') && market.includes('data-event-id="7"'), '交易按钮携带当前报价及赛事编号');
const empty = renderTideMarket(state, { grade: '4' });
check(/data-action="tide-market-buy"[^>]*disabled/.test(empty) && empty.includes('已售罄'), '缺库存禁用购买');
check(renderTideMarket({ tide: { ...ended, market: { open: false } } }).includes('尚未开放或已结束'), '市场关闭清晰显示');
check(renderTideBattleNotice({ kind: 'tide' }, tide).includes('快速击杀') && renderTideBattleNotice({ kind: 'spar' }, tide) === '', '仅兽潮战斗提示每回合一天');
console.log(`兽潮纯展示检查：${checks}/${checks} 通过（模拟字段）`);
