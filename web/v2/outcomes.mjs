import {esc,fmt,tag,uiButton} from './ui.mjs';
const labels={'arts/lifebound':'设置本命','ascension-tournament/enter':'升仙大会报名','tianji-shop/buy':'执事交易','pills/use':'批量服丹',advance:'推进岁月',plan:'调整计划',buy:'购入物品',sell:'出售物品','use-pill':'服用丹药',equip:'装备法器','gear/unequip':'卸下法器','gear/salvage':'分解法器','gear/inscribe':'铭刻法器',brew:'炼丹',craft:'炼丹',forge:'炼器','arts/learn':'修习功法','arts/main':'调整主修','arts/aux':'调整辅修','arts/spell':'调整神通','arts/node':'领悟道法','arts/focus':'调整大道',travel:'赶路','location-service':'当地事务',social:'道友往来',greet:'见礼',gift:'赠礼','npc-learn':'请教学问','npc-take':'换取物品',save:'保存进度',load:'读取存档',import:'导入行卷','checkpoint/restore':'回到存档','cancel-job':'中止安排',breakthrough:'闭关突破','legacy/settle':'结算传承',story:'机缘抉择',wild:'野外悬赏',expedition:'后山探访','tide/hunker':'固守','tide/fight':'迎战兽潮'};
// A receipt belongs to the operation workspace that submitted it, not the next page visited.
export function outcomeWorkspace(ui={}) {
  const route=ui.route||'home';
  if(route==='inventory'){
    const raw=ui.inventoryTab||'bag',aliases={potions:'bag',herbs:'bag',items:'bag',brew:'workshop',forge:'workshop',furnace:'workshop'},tab=aliases[raw]||raw;
    return `inventory/${tab}${tab==='workshop'?'/'+(['brew','forge','furnace'].includes(raw)?raw:ui.workshopTab||'brew'):''}`;
  }
  if(route==='arts')return `arts/${ui.artsTab==='builds'?'loadout':ui.artsTab||'loadout'}`;
  if(route==='social')return `social/${ui.social||'friends'}/${ui.social==='tianji'?ui.tianjiTab||'hall':ui.friendTask||'social'}`;
  if(route==='map')return `map/${ui.mapTab||'places'}`;
  return route;
}
export function outcomeForWorkspace(outcome,ui) {
  return outcome?.workspace===outcomeWorkspace(ui)?outcome:null;
}
export function makeOutcome(before,result,path){const after=result.state;if(!after)return null;const changes=[];const delta=(label,a,b,suffix='')=>{if(Number.isFinite(a)&&Number.isFinite(b)&&b!==a)changes.push(`${label} ${b>a?'+':''}${fmt(b-a)}${suffix}`);};
  if(before){if(before.plan!==after.plan)changes.push(`日常计划：${(after.plans||[]).find(x=>x.id===after.plan)?.name||after.plan}`);delta('历时',before.clock?.days,after.clock?.days,' 天');for(const [key,label,suffix] of [['stones','灵石',''],['xp','修为',''],['health','气血',' 点'],['renown','声望',''],['apt','资质',''],['insight','悟性','']])delta(label,before.player?.[key],after.player?.[key],suffix);delta('悟道点',before.arts?.points,after.arts?.points);
    for(const [key,label] of [['potions','丹药'],['herbs','药材'],['materials','材料']]){const old=before.arts?.[key]||{},next=after.arts?.[key]||{};for(const id of new Set([...Object.keys(old),...Object.keys(next)])){const amount=(next[id]||0)-(old[id]||0);if(!amount)continue;const definition=[...(after.pillCatalog||[]),...(after.market||[]),...(after.brewOptions||[])].find(x=>x.id===id||x.name===id);const name=definition?.name||(key==='herbs'?id:label);changes.push(`${name} ${amount>0?'+':''}${fmt(amount)}`);}}
    for(const m of after.arts?.manualList||[]){const old=(before.arts?.manualList||[]).find(x=>x.id===m.id);if(m.level!==(old?.level||0))changes.push(`${m.name} 第 ${fmt(m.level)} 层`);}
    const beforeGear=(before.gears||[]).filter(x=>x.equipped).map(x=>x.id).join(','),nowGear=(after.gears||[]).filter(x=>x.equipped);if(beforeGear!==nowGear.map(x=>x.id).join(','))changes.push(`当前装备：${nowGear.map(x=>x.name).join('、')||'无'}`);
    if(JSON.stringify(before.arts?.spells)!==JSON.stringify(after.arts?.spells))changes.push(`携带神通已更新，共 ${after.arts?.spells?.length||0} 种`);
  }
  if(Number.isFinite(result.relationDelta)&&result.relationDelta)changes.push(`好感 ${result.relationDelta>0?'+':''}${fmt(result.relationDelta)}`);
  const latest=after.logs?.[0],old=before?.logs?.[0];const newLog=latest&&(latest.id!==old?.id||latest.text!==old?.text)?latest.text:'';if(before?.job&&!after.job)changes.push('当前安排已完成或中止');if(after.job&&before?.job?.name!==after.job.name)changes.push(`${after.job.name} · 尚余 ${fmt(after.job.remaining)} 天`);if(before?.battle&&!after.battle)changes.push('本场斗法已结束');
  let text=result.text||newLog||'已按服务器最新状态更新。';
  if(path==='craft'){const elapsed=after.clock?.days-before?.clock?.days;const passed=Number.isFinite(elapsed)?`实际经过 ${fmt(Math.max(0,elapsed))} 天；`:'';const status=after.job?`当前安排尚余 ${fmt(after.job.remaining)} 天。`:after.ended?'此生已结束。':'当前炼丹安排已结束。';text=passed+status+(newLog?' '+newLog:'');}
  return {label:labels[path]||'操作结果',text,changes,report:result.report||null,clock:after.clock?.label||'',path};
}
export function renderOutcome(outcome){if(!outcome)return '';const ordered=outcome.path==='advance'?[...outcome.changes.filter(x=>x.startsWith('修为 ')),...outcome.changes.filter(x=>!x.startsWith('修为 '))]:outcome.changes;const brief=ordered.slice(0,2).join(' · ')||outcome.text.slice(0,72);return `<details class="notice outcome" data-disclosure-key="outcome" role="status" aria-label="上次操作结果"><summary><strong>${esc(outcome.label)}</strong><span>${esc(brief)}${!outcome.changes.length&&outcome.text.length>72?'…':''}</span></summary><p>${esc(outcome.text)}</p>${outcome.changes.length?`<div class="outcome-chips">${outcome.changes.map(tag).join('')}</div>`:''}${(outcome.report?.highlights||[]).map(x=>`<p>${esc(x)}</p>`).join('')}${outcome.path==='arts/learn'?uiButton('查看功法并搭配','navigate','arts/manuals'):outcome.path==='npc-learn'||outcome.path==='library/spell'?uiButton('查看已学道法','navigate','arts/loadout'):outcome.path==='battle/turn'?uiButton('查看健康与下一段安排','navigate','home'):''}${uiButton('收起这条回执','dismiss-outcome','','subtle')}</details>`;}
