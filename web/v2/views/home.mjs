import {esc,fmt,card,action,meter,uiButton,tag} from '../ui.mjs';
import {renderOutcome} from '../outcomes.mjs';

// The daily loop lives in one compact panel: state → plan → advance → result.
// HP is the engine's ordinary-battle opening reference, never health percent.
export function renderHome(s,ui={},data={},report,outcome) {
  const p=s.player,bt=s.breakthrough||{},plan=(s.plans||[]).find(x=>x.id===s.plan),job=s.job;
  const days=Math.max(1,Math.min(3600,Number(job?.remaining)||1));
  const reference=p.combatReference,battle=s.battle;
  const hp=battle?{current:Math.max(0,battle.hp),max:battle.max,label:'气血 · 本场实时'}:reference?{current:reference.hp,max:reference.hpMax,label:'气血 · 斗法开局参考'}:null;
  const selectDisabled=ui.busy||ui.reconciling||s.pending||s.battle||s.ended||s.dungeons?.run||s.tianji?.hasEncounter;
  const controls=job?`<div class="home-advance home-advance-job">${action(`继续 ${days} 天至${days===job.remaining?'完成':'下一段'}`,'advance',{days},{className:'primary'})}${action('过 1 天','advance',{days:1})}</div><p class="home-hint">完成本安排后即停，遇事暂停，不多过日子。</p>`:`<div class="home-advance">${action('过 1 天','advance',{days:1})}${action('过 30 天','advance',{days:30},{className:'primary'})}${action('过 1 年','advance',{days:360})}${action('过 10 年','advance',{days:3600})}</div><p class="home-hint">按所选计划推进，遇到抉择或斗法自动暂停。</p>`;
  const consumption=[bt.pillsConsumed?`凝元丹 × ${bt.pillsConsumed}`:'',bt.coresConsumed?`灵核 × ${bt.coresConsumed}`:'',bt.breakBuffConsumed?'已有突破丹药准备':''].filter(Boolean);
  return `<section class="home-control card" aria-label="日常修行操作台">
    <header class="home-heading"><h1>仙途日常</h1><span>${esc(p.locationName)} · ${esc(s.clock?.label||'')}</span></header>
    <div class="home-vitals">${hp?`<div class="home-hp">${meter(hp.current,hp.max,hp.label)}</div>`:'<p class="meta">气血开局参考未返回，请刷新服务器进度。</p>'}<div class="home-xp">${meter(p.xp,p.xpNeed,'修为 / 经验')}</div></div>
    <div class="home-growth"><span>资质 <b>${fmt(p.apt)}</b></span><span>悟性 <b>${fmt(p.insight)}</b></span>${uiButton('人物详情','character','','subtle')}</div>
    <div class="home-plan"><label for="daily-plan">${job?'后续日常计划':'日常计划'}</label><select id="daily-plan" data-ui="daily-plan" data-mutation="plan" aria-describedby="daily-plan-help" ${selectDisabled?'disabled':''}>${(s.plans||[]).map(x=>`<option value="${esc(x.id)}" ${x.id===s.plan?'selected':''}>${esc(x.name)}</option>`).join('')}</select></div>
    ${job?`<div class="home-job"><strong>${esc(job.name)}</strong><span>剩 ${fmt(job.remaining)} / ${fmt(job.total)} 天</span></div><p class="home-plan-preview" id="daily-plan-help">切换只改后续日常，不会中止当前安排。</p>`:`<p class="home-plan-preview" id="daily-plan-help">${esc(plan?.preview||plan?.desc||'选择一项计划，再推进时间。')}</p>`}
    ${controls}
    <div class="home-result-slot">${outcome?`<button class="home-receipt" data-ui="outcome-detail"><strong>${esc(outcome.label)}</strong><span>${esc((outcome.path==='advance'?[...outcome.changes.filter(x=>x.startsWith('修为 ')),...outcome.changes.filter(x=>!x.startsWith('修为 '))]:outcome.changes).slice(0,2).join(' · ')||outcome.text)}</span></button>`:report?`<button class="home-receipt" data-ui="outcome-detail">刚刚推进 ${fmt(report.daysPassed)} 天 · 查看结果</button>`:'<p class="meta">本次变化将在这里显示</p>'}</div>
  </section>
  <div class="home-secondary"><details class="card" data-disclosure-key="home-plan-detail"><summary>当前日常计划 · 完整说明</summary><p>${esc(plan?.name)}</p><p>${esc(plan?.desc)}</p><p>${esc(plan?.preview)}</p>${job?'<p>当前安排结束后才执行所选日常计划。</p>':''}</details>
    ${job?`<details class="card" data-disclosure-key="job-adjust"><summary>当前安排 · 更多操作</summary>${meter(job.elapsed??job.total-job.remaining,job.total,'安排进度')}${job.type==='socialElapsed'?'<p class="meta">往来已结算，剩余天数仍需履行，不能中止。</p>':`<p>中止可能不会退还已支付的费用。</p>${uiButton('中止安排','cancel-job')}`}</details>`:''}
    ${bt.next?`<details class="card" data-disclosure-key="home-breakthrough"><summary>突破 · ${esc(bt.next)}${bt.enabled?' · 可闭关':''}</summary><p>成功率 ${fmt((bt.chance||0)*100)}% · ${fmt(bt.cost)} 灵石${Number.isFinite(bt.days)?` · ${fmt(bt.days)} 天`:''}</p><p class="meta">持有 ${fmt(p.stones)} 灵石${Number.isFinite(bt.failureLoss)?` · 失败损失 ${fmt(bt.failureLoss*100)}% 修为`:''}</p>${consumption.length?`<p class="notice">开始时会消耗：${consumption.map(esc).join('、')}。</p>`:''}${bt.reason?`<p class="meta">${esc(bt.reason)}</p>`:''}${bt.enabled===undefined?'<p class="meta">当前服务未提供完整突破资格，请刷新至新版服务或从经典界面核对。</p>':''}${action('开始闭关突破','breakthrough',{}, {className:'primary wide',disabled:bt.enabled!==true,title:bt.reason})}</details>`:''}
    ${s.tide?.active?`<details class="card" data-disclosure-key="home-tide"><summary>兽潮来袭 · 查看天下变化</summary><p>${esc(s.tide.status)}</p>${uiButton('兽潮详情与迎战','navigate','map/adventures')}</details>`:''}
    <details class="card" data-disclosure-key="home-logs"><summary>岁月纪事 · ${(s.logs||[]).length} 则</summary><ol class="log-list">${(s.logs||[]).slice(0,90).map(l=>`<li><span class="meta">${esc(l.age!=null?l.age+' 岁':l.year!=null?'第 '+(l.year+1)+' 年':'')}</span><p>${esc(l.text||l.message||'')}</p></li>`).join('')}</ol></details>
    <details class="card" data-disclosure-key="home-business"><summary>俸禄、存档与随身事务</summary><p class="meta">${esc(p.realmName)} · ${fmt(p.age)} / ${fmt(p.lifespan)} 岁</p><div class="stack">${action(s.bonds?.stipendBalance>0?`领取俸禄 · ${fmt(s.bonds.stipendBalance)} 灵石`:'暂无可领俸禄','stipend',{}, {disabled:!(s.bonds?.stipendBalance>0),title:!p.sect?'尚未加入宗门':'当前账面没有待领俸禄'})}${uiButton('选择存档位置 / 设置','settings')}</div></details>
  </div>`;
}
