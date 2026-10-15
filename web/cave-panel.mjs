// 洞府独立页面：只展示后端快照，动作和报价由应用入口提交、后端再次校验。
import { esc, heading, panel } from './shell.mjs';
import { renderQuantitySlider } from './quantity-controls.mjs';

// 用途：安全展示后端数值，不把缺失报价默认为免费。
// 流程：灵田、品阶、成熟时间及训练报价展示。
// 调用方式：传后端数值；缺失、非有限值显示占位。
// 返回值：数值文本；不计算成长、收益或收费规则。
function numberText(value) {
  if (value == null || value === '') return '—';
  const numeric = Number(value);
  return Number.isFinite(numeric) ? String(numeric) : '—';
}

// 用途：为种植数量控件保留合法的本地输入。
// 流程：页面重绘时恢复用户尚未提交的选择。
// 调用方式：传草稿、后端容量上限及初始值；只限制输入范围。
// 返回值：正整数输入，不修改行囊或服务器状态。
function inputCount(value, maximum, initial = 1) {
  const numeric = Number(value);
  const upper = Number(maximum);
  const chosen = Number.isSafeInteger(numeric) && numeric > 0 ? numeric : initial;
  return Number.isSafeInteger(upper) && upper > 0 ? Math.min(chosen, upper) : chosen;
}

// 用途：合并后端库存与本格容量，给播种控件提供当前份数上限。
// 流程：首次展示、药材切换、输入确认与动作提交共用同一范围。
// 调用方式：传当前格和所选药材快照；缺失或非法数量视为不可播种。
// 返回值：0 表示没有合法份数；不修改库存或计算种植结算。
export function cavePlantCountLimit(plot, option) {
  const capacity = Number(plot?.remainingCapacity), owned = Number(option?.ownedCount);
  return Number.isSafeInteger(capacity) && capacity > 0 && Number.isSafeInteger(owned) && owned > 0
    ? Math.min(capacity, owned) : 0;
}

// 用途：将播种草稿收束到当前可选的整数范围。
// 流程：份数失焦及提交前规范化；空白、非有限值与小于一的输入回到一份。
// 调用方式：传草稿与 cavePlantCountLimit 返回的上限。
// 返回值：合法正整数；无可播种库存时仅返回禁用控件的占位一份。
export function cavePlantCount(value, maximum) {
  const numeric = Number(value);
  return Math.max(1, Math.min(Math.max(1, maximum), Number.isFinite(numeric) ? Math.floor(numeric) : 1));
}

// 用途：统一生成按服务端资格启用的按钮。
// 流程：返府、迁府、播种、收获、品阶、清理与训练展示。
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
      return `<li>${numberText(cohort.count)} 份 · ${cohort.ready === true ? '已成熟' : `成长 ${percentage} · 预计 ${numberText(cohort.estimatedDays)} 天`}</li>`;
    }).join('') + '</ul></details>';
}

// 用途：展示永久成长速度和成熟时间。
// 流程：已种植灵田格的只读成长信息。
// 调用方式：传后端plot快照；速度和天数直接读取，不用客户端成长公式。
// 返回值：成长HTML，外出仍按同样日增生长，没有每日扣费。
function renderGrowth(plot, garden) {
  if (!(Number(plot.seedCount) > 0)) return '';
  const hasGrowing = Number(plot.growingCount) > 0;
  const ready = plot.harvest?.enabled === true;
  return `<div class="cave-growth-line"><span>生长中 <strong>${numberText(plot.growingCount)} 份</strong></span>`
    + `<span>成熟种苗 <strong>${numberText(plot.readyCount)} 份</strong></span></div>`
    + `<p>成熟可收 ${numberText(plot.harvest?.harvestCount)} 份${plot.harvest?.reason ? ` · ${esc(plot.harvest.reason)}` : ''}</p>`
    + `<p class="cave-growth-rate">每日成长 <strong>${numberText(plot.effectiveUnitsPerDay)}</strong> = 洞府自然 ${numberText(plot.naturalUnitsPerDay)}`
      + (garden.hasGreenBottle === true ? ` + 绿瓶阶位 ${numberText(plot.bottleBonusUnitsPerDay)}` : '') + '</p>'
    + (hasGrowing ? `<p>预计全部成熟约 <strong>${numberText(plot.estimatedEffectiveDays)} 天</strong></p>` : '')
    + `<div class="cave-plot-actions">${actionButton('cave-garden-harvest', ready ? '收获成熟药材' : '尚不可收获',
      plot.harvest, `data-plot-id="${esc(plot.plotId)}"`, ready ? 'primary' : '')}</div>`
    + renderCohorts(plot);
}

// 用途：约束本地灵田筛选项，不把非法缓存值传播到控件。
// 流程：洞府纯展示入口接收根应用保存的筛选偏好。
// 调用方式：传 grade、search；默认一阶、空搜索，库存范围由后端目录决定。
// 返回值：独立筛选快照，不修改输入或服务器状态。
function normalizePlantFilters(filters) {
  const grade = String(filters?.grade ?? '1');
  return { grade: grade === 'all' || /^[1-6]$/.test(grade) ? grade : '1',
    search: String(filters?.search ?? '') };
}

// 用途：按品阶、名称缩小空田可选药材，后端目录仅列行囊已有材料。
// 流程：所有空灵田复用一次筛选结果；已种田另从原目录锁定同种。
// 调用方式：传权威材料目录及规范化筛选快照，只比较展示名称用于搜索。
// 返回值：原目录条目的筛选数组，不计算成长、扣费或产出。
function matchingPlantOptions(catalog, filters) {
  const search = filters.search.trim().toLocaleLowerCase();
  return catalog.filter(option => (filters.grade === 'all' || Number(option.grade) === Number(filters.grade))
    && (!search || String(option.name ?? '').toLocaleLowerCase().includes(search)));
}

// 用途：提供空田药材品阶、名称筛选，不展开全目录。
// 流程：灵田面板顶部；动作绑定与偏好保存由根应用负责。
// 调用方式：传权威目录、筛选快照与匹配数；目录品阶和当前选中品阶保持可见。
// 返回值：筛选 HTML，不产生请求或更新种植草稿。
function renderPlantFilters(catalog, filters, matchingCount) {
  const grades = [...new Set([...catalog.map(option => Number(option.grade)), Number(filters.grade)])]
    .filter(grade => Number.isInteger(grade) && grade >= 1 && grade <= 6).sort((left, right) => left - right);
  const gradeChoices = [{ value: 'all', label: '全部' }, ...grades.map(grade => ({ value: String(grade), label: `${grade} 阶` }))];
  return `<div class="cave-plant-filters"><div class="cave-plant-filter-row"><span>药材品阶</span>`
    + `<div class="cave-plant-grade-group" role="group" aria-label="灵田药材品阶">`
    + gradeChoices.map(choice => `<button type="button" class="${filters.grade === choice.value ? 'is-active' : ''}"`
      + ` data-cave-plant-grade="${choice.value}" aria-pressed="${filters.grade === choice.value}">${choice.label}</button>`).join('')
    + `</div></div><div class="cave-plant-filter-row"><label class="cave-plant-search">搜索药材`
    + `<input type="search" data-cave-plant-search value="${esc(filters.search)}" placeholder="输入药材名称" aria-label="搜索灵田药材"></label>`
    + `<span class="cave-plant-match-count" aria-live="polite">匹配 ${matchingCount} 种药材</span></div>`
    + `<p>仅列行囊已有药材。筛选用于空灵田；已种田固定显示同种药材，方便追加播种。</p></div>`;
}

// 用途：展示同格同种药材的补种选择和数量范围。
// 流程：空灵田播种及已种田补充新批次。
// 调用方式：传权威剩余容量和材料目录、当前本地草稿及全局筛选结果。
// 返回值：播种控件HTML；不用展示名判断药材，不计算扣料或产出。
function renderPlantControls(plot, garden, choices, matchingOptions) {
  const locked = !!plot.herbId;
  const catalog = Array.isArray(garden.plantOptions) ? garden.plantOptions : [];
  const lockedOption = locked ? catalog.find(option => option.herbId === plot.herbId) : null;
  const options = locked ? [lockedOption || { herbId: plot.herbId, name: plot.name || '药材',
    grade: plot.grade, ownedCount: 0 }] : matchingOptions;
  const selectedId = locked ? plot.herbId : options.some(option => option.herbId === choices.herbId)
    ? choices.herbId : options[0]?.herbId || '';
  const selected = options.find(option => option.herbId === selectedId);
  const capacity = Number(plot.remainingCapacity);
  const hasCapacity = Number.isSafeInteger(capacity) && capacity > 0;
  const maximum = cavePlantCountLimit(plot, selected);
  const count = cavePlantCount(choices.count, maximum);
  const hasSeeds = maximum > 0;
  const qualification = { enabled: plot.plantEnabled === true && !!selected && hasSeeds && hasCapacity,
    reason: plot.plantReason || (!hasCapacity ? '此格已满，收获后可以继续播种。'
      : !selected ? '没有符合当前筛选的药材。' : !hasSeeds ? '行囊中没有此药材，取得后可播种。' : '') };
  return `<div class="cave-plant-controls"><label>播种药材<select data-cave-plant-herb data-plot-id="${esc(plot.plotId)}"`
    + ` ${options.length && hasCapacity && !locked ? '' : 'disabled'}>${options.length ? options.map(option => `<option value="${esc(option.herbId)}"`
      + `${option.herbId === selectedId ? ' selected' : ''}>${esc(option.name)} · ${numberText(option.grade)} 阶 · 行囊 ${numberText(option.ownedCount)}</option>`).join('')
      : '<option value="">没有符合筛选的药材</option>'}</select></label>`
    + renderQuantitySlider({ value: count, maximum, enabled: maximum > 0 && qualification.enabled,
      label: '份数', className: 'cave-plant-quantity', attributes: `data-cave-plant-count data-plot-id="${esc(plot.plotId)}"` })
    + actionButton('cave-garden-plant', Number(plot.seedCount) > 0 ? '追加播种' : '播种', qualification,
      `data-plot-id="${esc(plot.plotId)}"`, 'primary') + '</div>'
    + `<p>${locked ? '本格仅可追加同种药材 · ' : ''}本格还可播种 ${numberText(plot.remainingCapacity)} 份${selected?.baseGrowthDays != null ? ` · ${esc(selected.name)}自然成长 ${numberText(selected.baseGrowthDays)} 天` : ''}`
    + `${qualification.reason ? ` · ${esc(qualification.reason)}` : ''}</p>`;
}

// 用途：拼接一个灵田格的材料、独立批次与收获信息。
// 流程：洞府灵田主面板。
// 调用方式：传后端plot、garden及本地输入，不写调用方数组或对象。
// 返回值：灵田卡HTML；清理动作由根入口确认种苗损失。
function renderPlot(plot, garden, choices, matchingOptions) {
  const planted = Number(plot.seedCount) > 0;
  const ready = Number(plot.readyCount) > 0;
  return `<article class="cave-plot ${planted ? ready ? 'is-ready' : '' : 'is-empty'}" data-cave-plot="${esc(plot.plotId)}">`
    + `<div class="cave-plot-head"><h4>${planted ? esc(plot.name || '药材') : '空灵田'}</h4>`
    + `<span class="tag">${planted ? `${numberText(plot.grade)} 阶 · 已种 ${numberText(plot.seedCount)} / ${numberText(garden.plantsPerPlotLimit)}` : `可容纳 ${numberText(garden.plantsPerPlotLimit)} 份`}</span></div>`
    + renderPlantControls(plot, garden, choices, matchingOptions)
    + renderGrowth(plot, garden)
    + (planted ? `<div class="cave-plot-actions">${actionButton('cave-garden-clear', '清除种苗', plot.clear,
      `data-plot-id="${esc(plot.plotId)}"`, 'subtle')}<span class="cave-action-cost">放弃本格全部种苗。</span></div>` : '')
    + '</article>';
}

// 用途：展示多格灵田与后台成长的共同规则。
// 流程：洞府独立页主体。
// 调用方式：传state、按plotId保存的本地输入和全局药材筛选；权威状态缺失时明确占位。
// 返回值：灵田面板HTML，不对缺失状态进行客户端初始化。
function renderGarden(state, plantSelections, plantFilters) {
  const garden = state.cave?.garden;
  if (!garden) return panel('灵田', '', '<div class="empty">正在读取洞府灵田……</div>');
  const plots = Array.isArray(garden.plots) ? garden.plots : [];
  const catalog = Array.isArray(garden.plantOptions) ? garden.plantOptions : [];
  const filters = normalizePlantFilters(plantFilters);
  const matchingOptions = matchingPlantOptions(catalog, filters);
  return panel('灵田', `${numberText(garden.plotCountUnlocked)} 格已解锁`, `<div class="cave-section-body">`
    + `<div class="cave-garden-summary"><span>每格最多<strong>${numberText(garden.plantsPerPlotLimit)} 份种苗</strong></span>`
    + `<span>每份成熟产出<strong>${numberText(garden.harvestMultiplier)} 份药材</strong></span>`
    + '</div>'
    + `<p class="cave-section-intro">同一格只种同种药材；追加播种形成独立批次，不继承已有成熟进度。</p>`
    + (garden.accessReason ? `<p class="cave-access-note">${esc(garden.accessReason)}</p>` : '')
    + renderPlantFilters(catalog, filters, matchingOptions.length)
    + `<div class="cave-plot-list">${plots.map(plot => renderPlot(plot, garden, plantSelections[plot.plotId] || {}, matchingOptions)).join('')
      || '<div class="empty">灵田状态尚未就绪。</div>'}</div>`
    + `<p class="cave-training-note">${esc(garden.growthCostRule || '')}</p>`
    + `<button class="subtle" data-action="goto-tab" data-tab="chronicle">前往岁月纪事安排时间 →</button>`
    + '</div>');
}

// 用途：展示小绿瓶永久阶位、一次性投入与专属灵材秘境。
// 流程：洞府有天赋时展示；传后端报价，不在客户端计算费用、资格、成长或周期。
// 返回值：完整面板HTML；无天赋为空，实际升阶/开境由根动作提交expectedGrade。
function renderGreenBottle(state) {
  const bottle = state.cave?.greenBottle;
  if (bottle?.hasTalent !== true) return '';
  const upgrade = bottle.upgrade || {};
  const materials = (upgrade.materials || []).map(material => `<li>${esc(material.name)}：需要 ${numberText(material.required)} 份 · 已有 ${numberText(material.owned)} 份${material.missing > 0 ? ` · 缺 ${numberText(material.missing)} 份` : ''}</li>`).join('');
  const periods = (upgrade.maturityDaysBefore || []).map(before => {
    const after = (upgrade.maturityDaysAfter || []).find(entry => entry.grade === before.grade);
    return `<li>${numberText(before.grade)}品：${numberText(before.days)} 天 → ${numberText(after?.days)} 天</li>`;
  }).join('');
  const gradeAttribute = `data-bottle-grade="${esc(bottle.currentGrade)}"`;
  return panel('小绿瓶', `${numberText(bottle.currentGrade)} / 6 阶`, `<div class="cave-section-body" data-green-bottle-panel>`
    + `<p>当前瓶阶永久日增 <strong>+${numberText(bottle.currentDailyBonus)}</strong> · 灵田合计日增 <strong>${numberText(bottle.currentUnitsPerDay)}</strong></p>`
    + (bottle.currentGrade < 6 && bottle.nextGrade != null ? `<p>升至 ${numberText(bottle.nextGrade)} 阶：瓶阶日增 +${numberText(bottle.nextDailyBonus)}，灵田合计日增 ${numberText(bottle.nextUnitsPerDay)}。</p>`
      + `<p>一次性投入 ${numberText(upgrade.stoneCost)} 灵石 · 当前持有 ${numberText(upgrade.ownedStones)} 灵石</p>`
      + (materials ? `<ul>${materials}</ul>` : '')
      + `<details class="cave-batch-details"><summary>升阶前后成熟周期</summary><ul>${periods}</ul><small>以当前洞府、空进度一批种苗为参照；已有种苗按剩余成长另算。</small></details>` : '<p>灵性已恢复至六阶，持续提供永久成长。</p>')
    + actionButton('green-bottle-upgrade', '投入大量灵石，为小绿瓶恢复些许灵性', upgrade, gradeAttribute, 'primary')
    + (upgrade.reason ? `<p class="cave-access-note">${esc(upgrade.reason)}</p>` : '')
    + `<details class="cave-batch-details" data-green-bottle-dungeons><summary>寻找升阶灵材</summary>`
    + (bottle.dungeons || []).map(dungeon => `<div class="cave-bottle-dungeon"><div><strong>${esc(dungeon.name)}</strong>`
      + `<p>通关获得 ${esc(dungeon.materialName)} ×${numberText(dungeon.rewardCount)}</p>`
      + (dungeon.reason ? `<small>${esc(dungeon.reason)}</small>` : '') + '</div>'
      + actionButton('green-bottle-dungeon', '进入秘境', dungeon, `${gradeAttribute} data-material-grade="${esc(dungeon.grade)}"`) + '</div>').join('')
    + '</details></div>');
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
// 调用方式：传权威state及本地草稿和plantFilters；supplyHtml仅接受既有配给渲染器生成的可信HTML。
// 返回值：整页HTML；没有进入/离开旗标、单次加速或炼丹炼器入口，不产生网络请求。
export function renderCavePage(state, { supplyHtml = '', plantSelections = {}, plantFilters = {} } = {}) {
  return `<div class="cave-page">`
    + heading('洞府', '安置修行居所，管理灵田、闭关配给与训练。', state.cave?.access?.atHome ? '在府' : '只读查看')
    + renderResidence(state)
    + `<div class="cave-main-column">${renderGreenBottle(state)}${renderGarden(state, plantSelections, plantFilters)}${renderTraining(state)}</div>`
    + (supplyHtml ? `<div class="cave-supply-section">${supplyHtml}</div>` : '')
    + '</div>';
}
