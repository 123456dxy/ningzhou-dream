import { esc, fmt, card, uiButton, empty, tag } from '../ui.mjs';
import { matchesBuildPlan, renderBuildPortraits, renderOpeningPreview } from '../../build-preview.mjs';

export function renderCreate(ui, data) {
  const build = ui.build || {};
  const options = data.buildOptions;
  const check = data.buildCheck;
  if (!options) return card('开辟这一世', empty(ui.buildOptionsError || '正在向引擎读取天赋与先天气运……')
    + `<div class="row">${uiButton('返回', 'cancel-build')}${ui.buildOptionsError ? uiButton('重新读取', 'build-options-retry') : ''}</div>`);
  const locked = matchesBuildPlan(ui.buildPlan, build);
  const editingDisabled = locked || ui.buildOptionsLoading || ui.busy;
  const disabled = editingDisabled ? 'disabled' : '';
  const groups = [...new Set((options.talents || []).map(talent => talent.category))];
  const identity = card('道号与出身', `
    <div class="field"><label for="life-name">这一世的名字</label>
      <input id="life-name" data-input="build-name" value="${esc(build.name || '')}" maxlength="12" placeholder="为自己写下道号" ${disabled}></div>
    <div class="field"><label for="life-gender">性别</label><select id="life-gender" data-input="build-gender" ${disabled}>
      <option ${build.gender === '女' ? 'selected' : ''}>女</option><option ${build.gender === '男' ? 'selected' : ''}>男</option></select></div>
    <p class="notice">先天资质 ${fmt(options.innate?.apt)} · 悟性 ${fmt(options.innate?.insight)}</p>
    <p class="meta">种子 ${esc(build.seed)} · 锁定后按同一种子与方案开局。</p>
    <button class="btn" data-ui="build-reroll" ${editingDisabled || ui.buildChecking ? 'disabled' : ''}>换个种子</button>`);
  const portraits = card('角色立绘', `<p class="meta">只改变外观，不改变门派与属性。</p>`
    + renderBuildPortraits(options, build.portraitId, build.gender, editingDisabled));
  const attributes = card('分配属性', `<div class="list">${(options.stats || []).map(stat => `
    <div class="alloc-row"><div><strong>${esc(stat.name)}</strong><p class="meta">${esc(stat.desc)}</p></div>
      <div class="stepper"><button data-ui="build-alloc" data-value="${esc(stat.id)}" data-delta="-1"
        aria-label="减少${esc(stat.name)}" ${editingDisabled || !(build.allocation?.[stat.id] > 0) ? 'disabled' : ''}>−</button>
        <span>${fmt(build.allocation?.[stat.id] || 0)}</span>
        <button data-ui="build-alloc" data-value="${esc(stat.id)}" data-delta="1" aria-label="增加${esc(stat.name)}"
          ${editingDisabled || (build.allocation?.[stat.id] || 0) >= stat.max ? 'disabled' : ''}>＋</button></div></div>`).join('')}</div>`);
  const talents = card('选择天赋', `<p class="meta">磨砺类天赋返还点数。选好后锁定，预览立即生效的属性及后续触发效果。</p>
    ${groups.map(group => `<details ${group === groups[0] ? 'open' : ''}>
      <summary>${esc(group)} · ${(options.talents || []).filter(talent => talent.category === group).length} 项</summary>
      <div class="talent-list">${(options.talents || []).filter(talent => talent.category === group).map(talent => `
        <button class="talent ${(build.traits || []).includes(talent.id) ? 'selected' : ''}" data-ui="build-trait"
          data-value="${esc(talent.id)}" aria-pressed="${(build.traits || []).includes(talent.id)}" ${disabled}>
          <span class="row"><strong>${esc(talent.name)}</strong>${tag(talent.cost + ' 点')}</span>
          <span class="meta">${esc(talent.effect)}</span></button>`).join('')}</div></details>`).join('')}`);
  const canLock = !ui.busy && !ui.buildOptionsLoading && !ui.buildChecking && check?.valid === true && !!build.name?.trim();
  return `<div class="page-heading"><div><p class="eyebrow">一页新的人生</p><h1>开辟这一世</h1></div>${uiButton('返回', 'cancel-build')}</div>
    <div class="build-layout"><div class="stack">${identity}${portraits}${attributes}</div><div class="stack">${talents}</div></div>
    ${locked ? renderOpeningPreview(ui.buildPlan.preview) : ''}
    <div class="build-summary card"><div id="build-check">
      <strong>${ui.buildChecking ? '正在校验……' : `尚余 ${fmt(check?.remain ?? options.budget)} / ${fmt(check?.budget ?? options.budget)} 点`}</strong>
      <p class="meta">${esc(check?.error || (locked ? '方案已锁定，可解锁后继续修改。' : '先锁定并核对开局效果，再开始这一世。'))}</p></div>
      ${locked ? `<button class="btn" data-ui="build-unlock" ${ui.busy ? 'disabled' : ''}>解锁修改</button>`
        : `<button class="btn" data-ui="build-lock" ${canLock ? '' : 'disabled'}>锁定方案并预览</button>`}
      <button class="btn primary" data-ui="start-life" ${locked && canLock ? '' : 'disabled'}>开始这一世</button></div>`;
}
