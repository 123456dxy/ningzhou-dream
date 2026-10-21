import { esc } from './shell.mjs';

export const isArtifactStatus = ability => ability?.targetMode === 'status-one';
const statusTargets = ability => Array.isArray(ability?.targets) ? ability.targets : [];
const targetSignature = ability => JSON.stringify(statusTargets(ability));

export function artifactStatusSnapshot(battle, ability) {
  return { id:ability?.id, expectedTurn:battle?.turn, expectedStatusToken:ability?.statusToken,
    targetSignature:targetSignature(ability) };
}

export function artifactStatusIsCurrent(battle, snapshot) {
  const ability=battle?.artifacts?.find(item=>item.id===snapshot?.id);
  return !!battle && isArtifactStatus(ability) && ability.playable===true
    && typeof snapshot?.expectedStatusToken==='string' && snapshot.expectedStatusToken.length>0
    && snapshot.expectedTurn===battle.turn && snapshot.expectedStatusToken===ability.statusToken
    && snapshot.targetSignature===targetSignature(ability);
}

export function artifactStatusRequest(battle, ability, selected, snapshot, { busy=false }={}) {
  if (busy || !isArtifactStatus(ability) || ability.id!==snapshot?.id
      || !artifactStatusIsCurrent(battle,snapshot)) return null;
  const current=battle.artifacts.find(item=>item.id===ability.id);
  if (typeof selected!=='string' || !selected || !statusTargets(current).some(item=>item.id===selected)) return null;
  return {id:current.id,target:selected,expectedTurn:snapshot.expectedTurn,expectedStatusToken:snapshot.expectedStatusToken};
}

export function renderArtifactStatus(battle, ability, selected=null, { mode='desktop',busy=false }={}) {
  const targets=statusTargets(ability), chosen=targets.find(item=>item.id===selected);
  const attrs=(action,value='')=>mode==='mobile'
    ? `data-ui="artifact-status-${action}" data-value="${esc(value)}"`
    : `data-action="artifact-status-${action}" data-id="${esc(value)}"`;
  const blocked=busy || ability?.playable!==true;
  return `<p class="artifact-redraw-description">${esc(ability?.preview?.text || '选择一种状态，按法器品阶削除层数。')} 确认才消耗 ${esc(ability?.cost || 0)} 灵力${Number(ability?.healthCost)>0 ? `和 ${esc(ability.healthCost)} 真实气血` : ''}。</p>`
    + `<div class="artifact-redraw-cards">${targets.map(target=>`<button type="button" class="artifact-redraw-card artifact-status-target${selected===target.id ? ' selected' : ''}" ${attrs('select',target.id)} aria-pressed="${selected===target.id}"${blocked ? ' disabled' : ''}><strong>${selected===target.id ? '✓ ' : ''}${esc(target.name)}</strong><span>当前 ${esc(target.layers)} ${esc(target.unit)}</span><span>可削 ${esc(target.removeLayers)} ${esc(target.unit)} · 剩余 ${esc(target.remainingLayers)} ${esc(target.unit)}</span>${target.description ? `<small>${esc(target.description)}</small>` : ''}</button>`).join('') || '<p>当前没有可削除的状态。</p>'}</div>`
    + `<div class="artifact-redraw-actions"><button type="button" ${attrs('cancel')}${busy ? ' disabled' : ''}>取消</button><button type="button" class="primary" ${attrs('confirm')}${blocked || !chosen ? ' disabled' : ''}>${chosen ? `确认削除「${esc(chosen.name)}」` : '先选择一种状态'}</button></div>`;
}
