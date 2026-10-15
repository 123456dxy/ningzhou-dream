// 完整传承展示：目录和老师位置由后端提供，学习仍使用逐项请教入口。
// 用途：转义插入文本；流程：传承卡片渲染；传任意文字，返回安全HTML文本。
const esc = value => String(value ?? '').replace(/[&<>"']/g, character => ({
  '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
}[character]));

// 用途：定位当前流派的常态传承者；流程：流派建议；传状态和稳定流派ID，返回指引HTML，不发起赶路或学习。
export function renderTransmissionTeachers(state, buildId) {
  const teachers = (state.championTeachers || []).filter(teacher => teacher.buildId === buildId);
  if (!teachers.length) return '<p class="intro">这套进阶配装暂无完整传承老师，可通过已有渠道逐项获取组件。</p>';
  return '<div class="transmission-teachers"><b>完整传承老师</b>' + teachers.map(teacher =>
    `<div><span>${esc(teacher.name)} · ${esc(teacher.realmName)} · ${esc(teacher.locationName)}</span>`
    + (teacher.here ? `<button data-raction="meet" data-npc="${esc(teacher.id)}">当面请教</button>`
      : `<button data-action="dungeon-locate" data-id="${esc(teacher.location)}">在舆图定位</button>`)
    + '</div>').join('') + '</div>';
}

// 用途：说明老师可以传授的完整配装；流程：仙友详情；传当前人物与状态，返回目录和已学标记，实际费用/资格在下方请教条目显示。
export function renderNpcTransmission(npc, state) {
  const transmission = npc.transmission;
  if (!transmission) return '';
  const references = transmission.components || [
    ...(transmission.manuals || []).map(id => ({ id, kind: 'manual' })),
    ...(transmission.spells || []).map(id => ({ id, kind: 'spell' })),
  ];
  const entries = references.map(reference => {
    const lesson = (npc.learning || []).find(item => item.kind === reference.kind && item.id === reference.id);
    const catalog = reference.kind === 'manual' ? state.manualCatalog || [] : state.arts?.spellList || [];
    const name = reference.name || lesson?.name || catalog.find(item => item.id === reference.id)?.name || reference.id;
    const learned = lesson?.learned === true || (reference.kind === 'manual'
      ? (state.arts?.manualList || []).some(item => item.id === reference.id)
      : (state.arts?.knownSpells || []).includes(reference.id));
    return `<span class="build-component ${learned ? 'known' : 'missing'}">${esc(name)}<small>${learned ? '已学会' : '待请教'}${reference.stageName ? ` · 需${esc(reference.stageName)}` : ''}</small></span>`;
  }).join('');
  return `<details class="npc-learning" data-npc-transmission="${esc(npc.id)}"><summary>完整传承 · ${esc(transmission.name)}</summary>`
    + `<p>${esc(transmission.note)}</p><div class="build-components">${entries}</div>`
    + '<p>下方逐项请教显示实际境界与关系要求；学会后在功法与悟道中配装。</p></details>';
}
