// 兽潮桌面展示：时间、积分、金榜与战后交易均读取服务端，不在这里推演奖励规则。
import { esc } from './shell.mjs';
import { quantityValue, renderQuantitySlider } from './quantity-controls.mjs';

// 用途：安全显示服务端计数；参与兽潮面板与交易报价；传数值，返回非负整数。
function count(value) { return Number.isFinite(Number(value)) ? Math.max(0, Math.floor(Number(value))) : 0; }

// 用途：判断纪事是否应保留兽潮入口；参与纪事渲染；传兽潮状态，返回是否显示。
export function shouldRenderTide(tide) { return tide?.active === true || tide?.market?.open === true; }

// 用途：展示后端配置的排名奖励，不在浏览器计算或补发灵石和声望。
// 流程：进行中金榜及战后结榜；参数：只读兽潮快照；返回值：奖励分档说明HTML，旧响应缺规则时显示结算提示。
function renderRewardRules(tide) {
  const rules = Array.isArray(tide.rewardRules) ? tide.rewardRules : [];
  if (!rules.length) return '<p class="tp-hint">正积分才有金榜奖励，奖励金额与声望以本期期末结算为准。</p>';
  let previousRank = 0;
  const bands = rules.map(rule => {
    const maximumRank = count(rule.maxRank);
    const label = maximumRank === 0 ? '其余正积分参战者' : previousRank + 1 === maximumRank
      ? `第 ${maximumRank} 名` : `第 ${previousRank + 1}～${maximumRank} 名`;
    previousRank = maximumRank;
    return `${label}：${count(rule.rewardYears)} 年参战境界标准俸禄，声望 +${count(rule.rewardRenown)}`;
  });
  return `<div class="tide-reward-rules"><p class="tp-hint">${bands.map(esc).join('；')}。同分并列，按参战时境界折算灵石。</p><p class="tp-hint">仅入场或撤离、没有击杀积分时不获保底奖；期末自动发放，无须重复领取。</p></div>`;
}

// 用途：说明后端给出的阶梯库存与解锁门槛；参与迎战前规划和战后交易。
// 使用方式：传只读兽潮快照；返回值：安全HTML，不在前端重新计算库存或改变购买资格。
function renderStockRules(tide) {
  const market = tide?.market;
  const rules = Array.isArray(market?.stockRules) ? market.stockRules : [];
  if (!rules.length) return '';
  return `<div class="tide-stock-rules"><p class="tp-note">全州总积分每累计 ${count(market.pointsPerStock)} 分增加一档库存；解锁更高阶后，已解锁低阶材料继续增加，单项库存有上限。</p>${rules.map(rule => `<p class="tp-note">${count(rule.grade)} 阶材料：全州累计 ${count(rule.unlockScore)} 分解锁 · ${!tide.settled && count(tide.totalScore) < count(rule.unlockScore) ? '尚未解锁' : `${tide.settled ? '按当前规则计算供应' : '当前积分预计供应'} ${count(rule.initialStock)} 份 / 种`}</p>`).join('')}${tide.settled ? '<p class="tp-note">商品剩余库存以已生成记录为准；已生成库存不会按新规则重新补货。</p>' : ''}</div>`;
}

// 用途：生成可滚动金榜；参与进行中及期末面板；传权威榜单，返回安全HTML。
function renderLeaderboard(tide) {
  const rows = Array.isArray(tide.leaderboard) ? tide.leaderboard : [];
  if (!rows.length) return '<p class="tp-note">本期金榜尚无战绩。</p>';
  return `<details class="tide-goldboard" ${tide.settled ? 'open' : ''}><summary>兽潮金榜 · ${rows.length} 位修士${tide.active ? ' · 实时战绩' : ' · 最终排名'}</summary>`
    + '<p class="tp-note">全州按积分排名，同分并列，各境界不分榜。流派高手拥有完整功法与神通配合。</p>'
    + (tide.npcPace ? `<p class="tp-note">修士间歇参战：普通修士${count(tide.npcPace.normalChanceDenominator) ? `每日有 1 / ${count(tide.npcPace.normalChanceDenominator)} 概率出战，` : ''}本期最多击杀 ${count(tide.npcPace.normalMaxKills)} 只；流派高手${count(tide.npcPace.eliteChanceDenominator) ? `每日有 1 / ${count(tide.npcPace.eliteChanceDenominator)} 概率出战，` : ''}本期最多击杀 ${count(tide.npcPace.eliteMaxKills)} 只。高境界战场每只妖兽的积分更多。</p>` : '')
    + '<div class="tide-ranking-scroll"><table><thead><tr><th scope="col">名次</th><th scope="col">修士</th><th scope="col">参战境界</th><th scope="col">积分</th></tr></thead><tbody>'
    + rows.map(row => `<tr class="${row.isPlayer ? 'tide-self' : ''}"><td>${count(row.rank) || '—'}</td><th scope="row">${esc(row.name)}${row.isPlayer ? '<span class="tide-row-tag">你</span>' : ''}${row.elite ? '<span class="tide-row-tag">完整流派</span>' : ''}<small>${esc(row.sectName || row.sect || '散修')}</small></th><td>${esc(row.realm)}</td><td>${count(row.score)}</td></tr>`).join('')
    + '</tbody></table></div></details>';
}

// 用途：展示迎战、期末奖励与金榜；参与纪事首页；传兽潮与存档提示，返回面板HTML，兼容旧状态。
export function renderTidePanel(tide, checkpoint) {
  if (!tide) return '';
  if (!shouldRenderTide(tide)) return `<div class="tide-panel calm"><div class="tp-head"><strong>宁州暂安</strong><span class="tp-tag">无兽潮</span></div><p class="tp-note">${esc(tide.status || '各处险地未见异动。')}</p></div>`;
  const severityName = ['初起', '猖獗', '滔天'][Math.min(2, Math.max(0, count(tide.severity) - 1))];
  const days = tide.daysLeft != null ? `${count(tide.daysLeft)} 天` : `${count(tide.monthsLeft)} 个月`;
  const battlefields = Array.isArray(tide.battlefields) ? tide.battlefields : [];
  const fallen = (tide.fallen || []).slice(0, 8);
  const actions = !tide.active ? '' : battlefields.length
    ? `<div class="tide-battlefields">${battlefields.map(field => `<div class="tide-battlefield"><button data-action="tide-fight" data-battlefield="${count(field.id)}" ${field.enabled === true ? '' : 'disabled'} title="${esc(field.reason || '')}"><strong>${esc(field.name)}</strong><span>每只 ${count(field.pointsPerKill)} 积分</span></button>${field.reason ? `<small>${esc(field.reason)}</small>` : ''}</div>`).join('')}</div>`
    : '<div class="tp-actions"><button class="primary" data-action="tide-fight">迎战兽潮</button></div>';
  return `<section class="tide-panel ${tide.active ? `sev${Math.min(3, Math.max(1, count(tide.severity)))}` : 'calm'}" aria-label="兽潮战况">
    <div class="tp-head"><strong>${tide.active ? `兽潮${esc(severityName)}` : '兽潮结榜'}</strong><span class="tp-tag">${tide.active ? `还剩 ${days}` : '本期已结束'}</span></div>
    <p class="tp-note">${esc(tide.status || '')}</p>
    <div class="tp-stats"><span>你的积分 <b>${count(tide.playerScore)}</b></span><span>你的名次 <b>${tide.playerRank > 0 ? `第 ${count(tide.playerRank)} 名` : '尚未参战'}</b></span><span>全州总积分 <b>${count(tide.totalScore)}</b></span><span>击退 <b>${count(tide.wavesWon)} / ${count(tide.wavesFought)}</b> 场</span>${tide.active ? `<span>全图危险 <b>+${count(tide.dangerBonus)}</b></span>` : ''}</div>
    ${actions}
    ${tide.active ? `<p class="tp-hint">兽潮固定持续 ${count(tide.durationDays) || 60} 天。每完成一回合消耗一天，快速击杀也至少消耗一天；战场境界越高，每只妖兽积分越多。</p><p class="tp-hint">迎战战败会陨落。${checkpoint?.enabled ? '可在结局选择接受死亡或回到最近存档，回档会撤回保存后的进度。' : '请留意气血、敌方意图与撤离机会。'}</p><button class="subtle" data-action="tide-hunker">固守不出</button>` : ''}
    ${renderRewardRules(tide)}
    ${tide.settled ? `<p class="tide-reward"><strong>金榜奖励 ${count(tide.rewardStones)} 灵石 · 声望 +${count(tide.rewardRenown)}</strong><span>${count(tide.playerScore) === 0 ? '未获得击杀积分，本期不发奖' : tide.rewardPaid ? '已到账' : '以期末结算为准'} · 期末自动发放，无须重复领取。</span></p>` : ''}
    ${renderStockRules(tide)}
    ${tide.market?.open ? `<div class="tp-actions"><button class="primary" data-action="tide-market-open">进入兽潮坊市</button><span>尚余 ${count(tide.market.daysLeft)} 天 · 高阶库存由本期全州总积分决定</span></div>` : ''}
    ${renderLeaderboard(tide)}
    ${fallen.length ? `<p class="tp-fallen">此役已知陨落：${fallen.map(esc).join('、')}${count(tide.npcDeaths) > fallen.length ? ' 等' : ''} · 共 ${count(tide.npcDeaths)} 人</p>` : ''}
  </section>`;
}

// 用途：明确兽潮战斗的日历成本；参与斗法页；传战斗及兽潮状态，返回提示或空串。
export function renderTideBattleNotice(battle, tide) {
  if (battle?.kind !== 'tide') return '';
  return `<p class="tide-combat-clock"><strong>兽潮尚余 ${count(tide?.daysLeft)} 天</strong><span>每完成一回合消耗一天；快速击杀也至少消耗一天。击杀获得对应战场积分。</span></p>`;
}

// 用途：独立展示战后三至五阶药材与器材及权威报价；参与兽潮坊市页。
// 调用方式：传状态和本地筛选/数量，拒绝展示其他类别与低阶目录；返回交易HTML，不修改后端资格或价格。
export function renderTideMarket(state, { kind = 'all', grade = '3', quantities = {} } = {}) {
  const tide = state?.tide, market = tide?.market;
  if (!market?.open) return '<div class="empty">兽潮坊市尚未开放或已结束。兽潮结束后开放 30 天，按全州积分供给高阶材料。</div>';
  const catalog = (market.items || []).filter(item => ['herb', 'material'].includes(item.kind)
    && Number.isInteger(Number(item.grade)) && Number(item.grade) >= 3 && Number(item.grade) <= 5);
  const items = catalog.filter(item => (kind === 'all' || item.kind === kind) && (grade === 'all' || Number(item.grade) === Number(grade)));
  const grades = [...new Set([3, 4, 5, ...catalog.map(item => count(item.grade))])].sort((a, b) => a - b);
  const controls = `<div class="tide-market-controls"><div class="shop-tabs">${[['all', '全部材料'], ['herb', '炼丹药材'], ['material', '炼器材料']].map(([id, name]) => `<button data-tide-market-kind="${id}" class="${kind === id ? 'active' : ''}">${name}</button>`).join('')}</div><label>品阶 <select data-tide-market-grade><option value="all" ${grade === 'all' ? 'selected' : ''}>全部品阶</option>${grades.map(value => `<option value="${value}" ${String(value) === String(grade) ? 'selected' : ''}>${value} 阶</option>`).join('')}</select></label></div>`;
  const rows = items.map(item => {
    const key = `${item.kind}:${item.id}`, stock = count(item.stock);
    const maximumQuote = item.maxPurchase;
    const maximum = maximumQuote ? count(maximumQuote.count) : Math.min(99, stock);
    const quantity = quantityValue(quantities[key], Math.max(1, maximum));
    const unitPrice = count(item.unitPrice), total = unitPrice * quantity;
    const affordable = Number(state.player?.stones) >= total;
    const stockRule = Array.isArray(market.stockRules) ? market.stockRules.find(rule => count(rule.grade) === count(item.grade)) : null;
    const isLocked = stock <= 0 && stockRule && count(tide.totalScore) < count(stockRule.unlockScore);
    const permitted = item.enabled === true && maximum > 0 && !isLocked && (!maximumQuote || maximumQuote.enabled === true);
    const enabled = permitted && stock >= quantity && unitPrice > 0 && affordable;
    const reason = isLocked ? `尚未解锁：本期全州总积分需达到 ${count(stockRule.unlockScore)} 分` : maximumQuote?.reason || item.reason || (!stock ? '已售罄' : !affordable ? `还差 ${Math.max(0, total - count(state.player?.stones))} 灵石` : '');
    const maximumEnabled = maximumQuote?.enabled === true && maximum > 0 && Number.isSafeInteger(maximumQuote.totalCost)
      && maximumQuote.totalCost > 0 && Number.isSafeInteger(maximumQuote.unitPrice) && maximumQuote.unitPrice > 0;
    const attributes = `data-event-id="${esc(tide.eventId)}" data-kind="${esc(item.kind)}" data-id="${esc(item.id)}"`;
    return `<div class="tide-market-row" data-quantity-row><div><strong class="item-name">${esc(item.name)}</strong><p class="item-sub">${count(item.grade)} 阶 · 库存 ${stock} · 已有 ${count(item.owned)}</p>${reason ? `<p class="market-item-reason">${esc(reason)}</p>` : ''}</div><div class="tide-market-price"><span>正常价 ${count(item.basePrice)} 灵石</span><strong>排名价 ${unitPrice} 灵石</strong><small data-quantity-total>合计 ${total} 灵石</small></div>`
      + renderQuantitySlider({ value: quantity, maximum, enabled: permitted, className: 'tide-market-quantity', attributes: `data-tide-quantity="${esc(key)}"` })
      + `<div class="trade-purchase-actions"><button class="buy" data-action="tide-market-buy" ${attributes} data-count="${quantity}" data-unit-price="${unitPrice}" data-quantity-submit data-balance="${count(state.player?.stones)}" data-permitted="${permitted}" data-block-reason="${esc(reason)}" ${enabled ? '' : 'disabled'}>${enabled ? `买入 · ${total} 灵石` : esc(reason || '暂不可购买')}</button>`
      + `<button class="buy primary" data-action="tide-market-buy-max" ${attributes} data-count="${maximum}" data-unit-price="${count(maximumQuote?.unitPrice)}" ${maximumEnabled ? '' : 'disabled'} title="${esc(maximumQuote?.reason || reason || '')}">${maximumEnabled ? `一键买满 · ${maximum} 份 / ${count(maximumQuote.totalCost)} 灵石` : '一键买满 · 暂不可购买'}</button></div></div>`;
  }).join('');
  return `<section class="tide-market" aria-label="兽潮坊市"><p class="tide-market-rule">尚余 <strong>${count(market.daysLeft)} 天</strong> · 你的排名价为正常价的 <strong>${count(market.discountPercent)}%</strong>；最低为正常价的 <strong>${count(market.minDiscountPercent) || 50}%</strong>。正常价采用${esc(market.priceBasis || '当前声望下普通坊市购入价')}，排名优惠在此价格上计算，并受底线限制。</p><p class="tp-note">本期全州总积分 ${count(tide.totalScore)}，总积分越高，可售材料越多；买完不补货，坊市关闭后无法购买。</p>${renderStockRules(tide)}${controls}<div class="tide-market-list">${rows || '<div class="empty">所选品阶或类型暂无材料，请切换筛选。</div>'}</div></section>`;
}
