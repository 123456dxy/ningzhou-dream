import { esc, heading, panel } from './shell.mjs';
import { actionRow } from './economy.mjs';
import { renderSectTasks } from './sects.mjs';

// 用途：在任务入口集中展示城中、宗门、悬赏与支线。
// 流程：按当前单选分类读取服务端行动条件，生成可执行按钮与原有耗时说明。
// 调用方式：桌面任务页面传入 state、eco-options、宗门目录和分类。
// 返回值：任务页 HTML；支线先打开阅读器，不默认替玩家选择。
export function renderTaskPanel(state, options, sects, sub = 'city') {
  const categories = [['city', '城中任务'], ['sect', '宗门任务'], ['wild', '悬赏'], ['story', '支线']];
  const tabs = `<div class="subtabs">${categories.map(([id, name]) => `<button class="subtab ${sub === id ? 'on' : ''}" data-subtab="economy" data-sub="${id}">${name}</button>`).join('')}</div>`;
  let body;
  if (sub === 'sect') body = renderSectTasks(state, options, sects);
  else if (sub === 'wild') body = `<p class="panel-note">战败会陨落。${state.checkpoint?.enabled ? '意外死亡后可选择回到最近存档，回档会撤回保存之后的进度。' : '请留意双方气血与敌方意图。'}</p><div class="arts-grid">`
    + (state.wildTasks || []).map(item => `<button class="card" data-action="wild" data-id="${esc(item.id)}" ${item.enabled === false ? 'disabled' : ''}><strong>${esc(item.name)}</strong><p>${esc(item.desc)}</p><div class="card-foot">${item.days} 天 · 3 灵石 · 赏金 ${item.fee}${item.reason ? ` · ${esc(item.reason)}` : ''}</div></button>`).join('') + `</div>`;
  else if (sub === 'story') body = `<div class="arts-grid">` + (state.stories || []).map(item => `<button class="card ${item.available ? '' : 'dead'}" data-action="story" data-id="${esc(item.id)}" ${item.available || item.done ? '' : 'disabled'}><strong>${esc(item.name)}</strong><p>${esc(item.desc)}</p><div class="card-foot">${esc(item.done ? '已落笔 · 点击阅读' : item.available ? `${item.days} 天 · 点击阅读` : item.reason)}</div></button>`).join('') + `</div>`;
  else body = (options || []).filter(item => !item.sect && item.id !== 'sect' && !item.id?.startsWith('sect-')).map(actionRow).join('') || '<div class="empty">正在读取任务；城中可接更多差事。</div>';
  return heading('任务', '按所在地点与身份接取任务，或查看悬赏和支线。', state.player.locationName) + tabs
    + panel(categories.find(([id]) => id === sub)?.[1] || '城中任务', '', `<div class="task-content">${body}</div>`);
}
