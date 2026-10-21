// 浏览器保存下一次入场的奖励偏好；候选类型和实际奖励始终由后端决定。
import { esc } from './shell.mjs';

const storageKey = 'nz-desktop-dungeon-card-rewards';
const modes = new Set(['all', 'none', 'selected']);
let memoryPreference = { mode: 'all', daos: [] };

// 用途：约束浏览器草稿的结构；流程：读取、保存和展示；返回：独立对象，不按客户端目录计算奖励资格。
function normalizePreference(preference, singleDao = true) {
  const mode = modes.has(preference?.mode) ? preference.mode : 'all';
  const daos = mode === 'selected' && Array.isArray(preference?.daos)
    ? [...new Set(preference.daos.filter(id => typeof id === 'string' && /^[a-z][a-z0-9_-]{0,63}$/.test(id)))].slice(0, 32) : [];
  if (singleDao && daos.length > 1) return { mode: 'all', daos: [] };
  return { mode, daos };
}

// 用途：只读取得浏览器原草稿；流程：加载及旧多系重置提示；返回：原结构，调用边界须先规范化再使用。
function readStoredPreference() {
  try {
    const saved = globalThis.localStorage?.getItem(storageKey);
    if (saved != null) return JSON.parse(saved);
  } catch { /* 隐私模式或损坏偏好不阻止继续游玩。 */ }
  return memoryPreference;
}

// 用途：恢复下次入场的单系偏好；流程：刷新与打开入口；返回：独立草稿，旧多系明确回到全部，不默取首项。
export function loadDungeonCardRewardPreference() { return normalizePreference(readStoredPreference()); }

// 用途：记录用户显式修改的入场偏好；流程：模式/类型控件变更；返回：规范化副本，不修改当前秘境或角色存档。
export function saveDungeonCardRewardPreference(preference) {
  memoryPreference = normalizePreference(preference);
  try { globalThis.localStorage?.setItem(storageKey, JSON.stringify(memoryPreference)); }
  catch { /* 不能写浏览器存储时仍保留本会话的选择。 */ }
  return normalizePreference(memoryPreference);
}

// 用途：把权威候选和偏好转成摘要；流程：目录、地图与冻结探索；返回：纯文本，调用方插入HTML时须转义。
export function dungeonCardRewardSummary(preference, options) {
  // 已在途的旧多系快照只作如实展示；新入场单系限制由加载、保存及后端各自守住。
  const normalized = normalizePreference(preference, false);
  const modeName = (options?.modes || []).find(mode => mode.id === normalized.mode)?.name;
  if (normalized.mode === 'all') return modeName || '全部临时神通';
  if (normalized.mode === 'none') return modeName || '不获取临时神通';
  const names = (options?.daos || []).filter(dao => normalized.daos.includes(dao.id)).map(dao => dao.name);
  return `${modeName || '指定类型'}：${names.length ? names.join('、') : '尚未选择有效类别'}`;
}

// 用途：渲染入场前单系控件；流程：秘境目录；返回：只读HTML，不写浏览器偏好，不在前端推算奖励。
export function renderDungeonCardRewardPreference(options, preference = readStoredPreference()) {
  const availableModes = (options?.modes || []).filter(mode => modes.has(mode.id));
  if (!availableModes.length) return '';
  const normalized = normalizePreference(preference);
  const resetMultiple = normalizePreference(preference, false).daos.length > 1;
  const daos = (options?.daos || []).filter(dao => typeof dao.id === 'string' && /^[a-z][a-z0-9_-]{0,63}$/.test(dao.id));
  const selected = normalized.mode === 'selected';
  const hasSelection = daos.some(dao => normalized.daos.includes(dao.id));
  return `<section class="dungeon-card-preferences" data-dungeon-card-preferences>`
    + `<label class="dungeon-card-mode">临时神通奖励<select data-dungeon-card-mode aria-label="临时神通奖励模式">`
    + availableModes.map(mode => `<option value="${esc(mode.id)}" ${mode.id === normalized.mode ? 'selected' : ''}>${esc(mode.name)}</option>`).join('') + '</select></label>'
    + `<fieldset class="dungeon-card-daos"><legend>指定派系（单选）</legend>`
    + daos.map(dao => `<label><input type="radio" name="dungeon-card-dao" data-dungeon-card-dao value="${esc(dao.id)}" ${normalized.daos.includes(dao.id) ? 'checked' : ''} ${selected ? '' : 'disabled'}><span>${esc(dao.name)}</span></label>`).join('')
    + `${daos.length ? '' : '<small>当前没有可选的神通类型。</small>'}</fieldset>`
    + `<p class="dungeon-card-summary" data-dungeon-card-summary>${esc(dungeonCardRewardSummary(normalized, options))}</p>`
    + (resetMultiple ? '<p class="dungeon-card-warning" role="status">原多系奖励偏好已重置为全部，请重新选择一个派系。</p>' : '')
    + (selected && !hasSelection ? '<p class="dungeon-card-warning" role="status">请选择一个神通派系后再入境。</p>' : '')
    + '<small>仅筛选沿途获得或购买的临时神通；入境后固定，永久通关奖励不受影响。</small></section>';
}
