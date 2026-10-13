// 洞府独立页面：只展示后端快照，动作和报价由应用入口提交、后端再次校验。
import { esc, heading, panel } from './shell.mjs';

// 用途：安全展示后端数值，不把缺失报价默认为免费。
// 流程：灵田、储备、成熟时间及训练报价展示。
// 调用方式：传后端数值；缺失、非有限值显示占位。
// 返回值：数值文本；不计算成长、收益或收费规则。
function numberText(value) {
  if (value == null || value === '') return '—';
  const numeric = Number(value);
  return Number.isFinite(numeric) ? String(numeric) : '—';
}

// 用途：为数量或储备金额控件保留合法的本地输入。
// 流程：页面重绘时恢复用户尚未提交的选择。
// 调用方式：传草稿、后端容量上限及初始值；只限制输入范围。
// 返回值：正整数输入，不修改行囊、储备或服务器状态。
function inputCount(value, maximum, initial = 1) {
  const numeric = Number(value);
  const upper = Number(maximum);
  const chosen = Number.isSafeInteger(numeric) && numeric > 0 ? numeric : initial;
  return Number.isSafeInteger(upper) && upper > 0 ? Math.min(chosen, upper) : chosen;
}

// 用途：统一生成按服务端资格启用的按钮。
// 流程：返府、迁府、播种、收获、储备、清理与训练展示。
// 调用方式：传动作、标题和权威资格；附加属性只能来自模块内预先转义的字符串。
// 返回值：HTML按钮；未明确enabled=true时一律禁用，不执行动作。
function actionButton(action, label, qualification, attributes = '', className = '') {
  return `<button class="${esc(className)}" data-action="${esc(action)}" ${attributes}`
    + `${qualification?.enabled === true ? '' : ' disabled'}`
    + ` title="${esc(qualification?.reason || '')}">${esc(label)}</button>`;
}

// 用途：展示洞府所在地、是否在府和真实返府入口。
// 流程：洞府页面顶部位置及资格说明。
// 调用方式：传只读state；不引入进入/离开洞府旗标或位置推演。
// 返回值：位置横幅HTML，动作只带根应用约定的稳定标识。
function renderResidence(state) {
  const player = state.player || {};
  const access = state.cave?.access;
  const atHome = access?.atHome === true;
  const homeName = access?.homeName || player.caveName || '尚未安府';
  const hasHome = !!access?.homeLocation;
  const actions = [];
  if (hasHome && !atHome) {
    actions.push(actionButton('cave-return', '沿地图路线返府',
      { enabled: access?.canReturn === true, reason: access?.returnReason || access?.reason || '' }, '', 'primary'));
    if (access?.canReturn === true && access.returnMode)
      actions.push(`<span class="cave-action-cost">${esc(access.returnName || '')} · ${numberText(access.returnDays)} 天 · ${numberText(access.returnCost)} 灵石</span>`);
  }
  if (!atHome) {
    actions.push(actionButton('cave-move', hasHome ? '迁府至此' : '在此安府', access?.settle));
    actions.push(`<span class="cave-action-cost">${numberText(access?.settle?.costStones)} 灵石`
      + `${access?.settle?.reason ? ` · ${esc(access.settle.reason)}` : ''}</span>`);
  }
  return `<section class="cave-residence" aria-label="洞府所在地">`
    + `<span class="cave-seal" aria-hidden="true">府</span><div>`
    + `<span class="cave-eyebrow">${atHome ? '在府' : hasHome ? '外出' : '安府'} · ${esc(state.cave?.tierName || '修行居所')}</span>`
    + `<h3>${esc(homeName)}</h3><p>当前所在 ${esc(player.locationName || '—')}`
    + `${access?.reason ? ` · ${esc(access.reason)}` : ''}</p></div>`
    + `<div class="cave-entry-actions">${actions.join('') || '<span class="cave-action-cost">可在此管理灵田与修炼配给。</span>'}</div></section>`;
}

// 用途：以显示百分比说明每一批种苗的独立成长。
// 流程：灵田批次详情；只进行数值展示比例转换。
// 调用方式：传后端批次及该品阶所需成长量。
// 返回值：批次HTML，补种不与旧批次合并或共享进度。
function renderCohorts(plot) {
  const cohorts = Array.isArray(plot.cohorts) ? plot.cohorts : [];
  if (!cohorts.length) return '';
  return `<details class="cave-batch-details"><summary>${cohorts.length} 批种苗 · 各自成长</summary><ul>`
    + cohorts.map(cohort => {
      const required = Number(plot.requiredUnits), progress = Number(cohort.progressUnits);
      const percentage = required > 0 && Number.isFinite(progress)
        ? `${Math.max(0, Math.min(100, Math.round(progress / required * 100)))}%` : '—';
      return `<li>${numberText(cohort.count)} 份 · ${cohort.ready === true ? '已成熟' : `成长 ${percentage}`}</li>`;
    }).join('') + '</ul></details>';
}

// 用途：展示成熟时间、自动加速方案和储备收费状态。
// 流程：已种植灵田格的只读成长信息。
// 调用方式：传后端plot快照；费用和天数直接读取，不用客户端成长公式。
// 返回值：成长HTML，人在外面仍可查看自动生长与收费。
function renderGrowth(plot, garden) {
  if (!(Number(plot.seedCount) > 0)) return '';
  const hasGrowing = Number(plot.growingCount) > 0;
  const ready = plot.harvest?.enabled === true;
  return `<div class="cave-growth-line"><span>生长中 <strong>${numberText(plot.growingCount)} 份</strong></span>`
    + `<span>成熟种苗 <strong>${numberText(plot.readyCount)} 份</strong></span></div>`
    + `<p>成熟可收 ${numberText(plot.harvest?.harvestCount)} 份${plot.harvest?.reason ? ` · ${esc(plot.harvest.reason)}` : ''}</p>`
    + (hasGrowing ? `<p>按当前储备预计全部成熟约 <strong>${numberText(plot.estimatedEffectiveDays)} 天</strong>`
      + ` · ${plot.accelerationFunded === true ? '储备充足 · 自动加速' : '自然生长'}</p>`
      + `<details class="cave-batch-details"><summary>不同配给下的成熟时间</summary><ul>`
      + `<li>自然生长 ${numberText(plot.estimatedNaturalDays)} 天</li>`
      + `<li>灵气方案 ${numberText(plot.estimatedSpiritDays)} 天 · ${numberText(plot.spiritDailyCostStones)} 灵石 / 天</li>`
      + (garden.hasGreenBottle === true ? `<li>小绿瓶方案 ${numberText(plot.estimatedBottleDays)} 天 · ${numberText(plot.bottleDailyCostStones)} 灵石 / 天</li>` : '')
      + `<li>全部加速 ${numberText(plot.estimatedFullyAcceleratedDays)} 天 · ${numberText(plot.dailyCostStones)} 灵石 / 天</li></ul></details>` : '')
    + `<div class="cave-plot-actions">${actionButton('cave-garden-harvest', ready ? '收获成熟药材' : '尚不可收获',
      plot.harvest, `data-plot-id="${esc(plot.plotId)}"`, ready ? 'primary' : '')}</div>`
    + renderCohorts(plot);
}

// 用途：展示每格独立储备的金额输入与存取资格。
// 流程：自动加速付费储备管理。
// 调用方式：传plot快照及本地未提交金额，不隐式从背包扣款。
// 返回值：存取控件HTML；真正转账由根应用动作与后端校验。
function renderReserve(plot, amount) {
  const plotAttribute = `data-plot-id="${esc(plot.plotId)}"`;
  return `<div class="cave-reserve"><div class="cave-reserve-head">`
    + `<strong>本格储备 ${numberText(plot.reserveStones)} 灵石</strong><span>自动加速 ${numberText(plot.dailyCostStones)} 灵石 / 天</span></div>`
    + `<div class="cave-reserve-controls"><label>存取金额<input type="number" min="1" step="1" value="${inputCount(amount)}"`
    + ` data-cave-reserve-amount ${plotAttribute} inputmode="numeric" aria-label="本格储备存取灵石金额"></label>`
    + actionButton('cave-garden-deposit', '存入', plot.deposit, plotAttribute)
    + actionButton('cave-garden-withdraw', '取出', plot.withdraw, plotAttribute)
    + `</div><p>储备足够时按日自动加速；外出后也继续生长。${plot.deposit?.reason ? ` ${esc(plot.deposit.reason)}` : ''}`
    + `${plot.withdraw?.reason ? ` ${esc(plot.withdraw.reason)}` : ''}</p></div>`;
}

// 用途：展示同格同种药材的补种选择和数量范围。
// 流程：空灵田播种及已种田补充新批次。
// 调用方式：传权威剩余容量和材料目录、当前本地草稿。
// 返回值：播种控件HTML；不用展示名判断药材，不计算扣料或产出。
function renderPlantControls(plot, garden, choices) {
  const options = (garden.plantOptions || []).filter(option => !plot.herbId || option.herbId === plot.herbId);
  const selectedId = plot.herbId || (options.some(option => option.herbId === choices.herbId) ? choices.herbId
    : options.find(option => Number(option.ownedCount) > 0)?.herbId || options[0]?.herbId || '');
  const selected = options.find(option => option.herbId === selectedId);
  const capacity = Number(plot.remainingCapacity);
  const hasCapacity = Number.isSafeInteger(capacity) && capacity > 0;
  const count = inputCount(choices.count, hasCapacity ? Math.min(99, capacity) : 1);
  const qualification = { enabled: plot.plantEnabled === true && !!selected && hasCapacity,
    reason: plot.plantReason || (!hasCapacity ? '此格已满，收获后可以继续播种。' : '') };
  return `<div class="cave-plant-controls"><label>播种药材<select data-cave-plant-herb data-plot-id="${esc(plot.plotId)}"`
    + ` ${options.length && hasCapacity ? '' : 'disabled'}>${options.length ? options.map(option => `<option value="${esc(option.herbId)}"`
      + `${option.herbId === selectedId ? ' selected' : ''}>${esc(option.name)} · ${numberText(option.grade)} 阶 · 行囊 ${numberText(option.ownedCount)}</option>`).join('')
      : '<option value="">没有可播种药材</option>'}</select></label>`
    + `<label class="cave-plant-quantity">份数<input type="number" min="1" max="${hasCapacity ? Math.min(99, capacity) : 1}" step="1" value="${count}"`
    + ` data-cave-plant-count data-plot-id="${esc(plot.plotId)}" ${hasCapacity ? '' : 'disabled'} inputmode="numeric"></label>`
    + actionButton('cave-garden-plant', Number(plot.seedCount) > 0 ? '追加播种' : '播种', qualification,
      `data-plot-id="${esc(plot.plotId)}"`, 'primary') + '</div>'
    + `<p>本格还可播种 ${numberText(plot.remainingCapacity)} 份${selected ? ` · ${esc(selected.name)}自然成长 ${numberText(selected.baseGrowthDays)} 天` : ''}`
    + `${qualification.reason ? ` · ${esc(qualification.reason)}` : ''}</p>`;
}

// 用途：拼接一个灵田格的材料、独立批次、收获和储备信息。
// 流程：洞府灵田主面板。
// 调用方式：传后端plot、garden及本地输入，不写调用方数组或对象。
// 返回值：灵田卡HTML；清理动作由根入口确认，保留储备语义清晰可见。
function renderPlot(plot, garden, choices) {
  const planted = Number(plot.seedCount) > 0;
  const ready = Number(plot.readyCount) > 0;
  return `<article class="cave-plot ${planted ? ready ? 'is-ready' : '' : 'is-empty'}" data-cave-plot="${esc(plot.plotId)}">`
    + `<div class="cave-plot-head"><h4>${planted ? esc(plot.name || '药材') : '空灵田'}</h4>`
    + `<span class="tag">${planted ? `${numberText(plot.grade)} 阶 · 已种 ${numberText(plot.seedCount)} / ${numberText(garden.plantsPerPlotLimit)}` : `可容纳 ${numberText(garden.plantsPerPlotLimit)} 份`}</span></div>`
    + renderPlantControls(plot, garden, choices)
    + renderGrowth(plot, garden)
    + renderReserve(plot, choices.reserveAmount)
    + (planted ? `<div class="cave-plot-actions">${actionButton('cave-garden-clear', '清除种苗', plot.clear,
      `data-plot-id="${esc(plot.plotId)}"`, 'subtle')}<span class="cave-action-cost">放弃本格全部种苗，储备保留。</span></div>` : '')
    + '</article>';
}

// 用途：展示多格灵田与后台成长的共同规则。
// 流程：洞府独立页主体。
// 调用方式：传state与按plotId保存的本地输入草稿；权威状态缺失时明确占位。
// 返回值：灵田面板HTML，不对缺失状态进行客户端初始化。
function renderGarden(state, plantSelections) {
  const garden = state.cave?.garden;
  if (!garden) return panel('灵田', '', '<div class="empty">正在读取洞府灵田……</div>');
  const plots = Array.isArray(garden.plots) ? garden.plots : [];
  return panel('灵田', `${numberText(garden.plotCountUnlocked)} 格已解锁`, `<div class="cave-section-body">`
    + `<div class="cave-garden-summary"><span>每格最多<strong>${numberText(garden.plantsPerPlotLimit)} 份种苗</strong></span>`
    + `<span>每份成熟产出<strong>${numberText(garden.harvestMultiplier)} 份药材</strong></span>`
    + `<span>小绿瓶<strong>${garden.hasGreenBottle === true ? '已持有' : '未持有'}</strong></span></div>`
    + `<p class="cave-section-intro">同一格只种同种药材；追加播种形成独立批次，不继承已有成熟进度。</p>`
    + (garden.accessReason ? `<p class="cave-access-note">${esc(garden.accessReason)}</p>` : '')
    + `<div class="cave-plot-list">${plots.map(plot => renderPlot(plot, garden, plantSelections[plot.plotId] || {})).join('')
      || '<div class="empty">灵田状态尚未就绪。</div>'}</div>`
    + `<p class="cave-training-note">${esc(garden.growthCostRule || '')}</p>`
    + `<button class="subtle" data-action="goto-tab" data-tab="chronicle">前往岁月纪事安排时间 →</button>`
    + '</div>');
}

// 用途：把现有演武切磋以洞府训练场形式展示。
// 流程：洞府训练列表，实际动作仍由根应用调用原有经济入口。
// 调用方式：传后端training数组；名称、效果、费用、天数及拒绝原因均权威读取。
// 返回值：训练面板HTML；不自行构造敌人、奖励或资格。
function renderTraining(state) {
  const training = state.cave?.training;
  if (!Array.isArray(training) || !training.length) {
    return panel('训练场', '', '<div class="empty">正在读取训练安排……</div>');
  }
  return panel('训练场', '在府安排', `<div class="cave-section-body">${training.map(option => `
    <div class="cave-training-controls"><div class="cave-training-choice"><strong>${esc(option.name)}</strong>`
    + `<p class="cave-section-intro">${esc(option.desc)}</p></div>`
    + actionButton('cave-train', '开始训练', option, `data-id="${esc(option.id)}"`, 'primary') + '</div>'
    + `<div class="cave-training-quote"><span>${numberText(option.days)} 天 · ${numberText(option.cost)} 灵石</span>`
    + (option.reason ? `<p>${esc(option.reason)}</p>` : '') + '</div>').join('')}</div>`);
}

// 用途：生成与功法、悟道、工坊分开的洞府独立页面。
// 流程：桌面主导航选择洞府后，由根应用调用和绑定动作。
// 调用方式：传权威state及本地草稿，supplyHtml仅接受既有配给渲染器生成的可信HTML。
// 返回值：整页HTML；没有进入/离开旗标、单次加速或炼丹炼器入口，不产生网络请求。
export function renderCavePage(state, { supplyHtml = '', plantSelections = {} } = {}) {
  return `<div class="cave-page">`
    + heading('洞府', '安置修行居所，管理灵田、储备、配给与训练。', state.cave?.access?.atHome ? '在府' : '只读查看')
    + renderResidence(state)
    + `<div class="cave-main-column">${renderGarden(state, plantSelections)}${renderTraining(state)}</div>`
    + (supplyHtml ? `<div class="cave-supply-section">${supplyHtml}</div>` : '')
    + '</div>';
}
