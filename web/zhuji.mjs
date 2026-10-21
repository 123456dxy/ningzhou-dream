const Elements = ['gold', 'wood', 'water', 'fire', 'earth'];
const CompassOrder = ['wood', 'fire', 'earth', 'gold', 'water'];
const Names = { gold: '金', wood: '木', water: '水', fire: '火', earth: '土' };
const Rituals = [['basic', '基础凝液'], ['same', '同源凝练'], ['generate', '相生合炼'], ['five', '五行合一'], ['convert', '调息换气'], ['gather', '聚气待发']];
const State = { View: null, Host: null, Hooks: {}, Kind: 'basic', Source: null, SelectedId: null, Busy: false,
  Confirm: null, Error: '', Keep: null, KeepRevision: null, Pending: null, QuoteSerial: 0, Quote: null, Route: 'elements', Pills: 0, Core: false,
  Scope: '', Epoch: 0, Operation: 0 };
const Escape = Value => String(Value ?? '').replace(/[&<>"']/g, Character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[Character]));
const NumberText = Value => Number.isFinite(Value) ? Value.toLocaleString('zh-CN', { maximumFractionDigits: 1 }) : '—';
const Busy = () => State.Busy || Boolean(State.Hooks.isBusy?.());
const Choice = Kind => State.View?.choices?.find(Item => Item.action.kind === Kind);
const Disabled = Value => Value ? ' disabled' : '';

// Also call before accepting new/load/import/logout, including a rollback of the same life.
export function mfResetZhuji() {
  ++State.Epoch; ++State.Operation; ++State.QuoteSerial;
  Object.assign(State, { View: null, Host: null, Hooks: {}, Scope: '', Kind: 'basic', Source: null, SelectedId: null,
    Busy: false, Confirm: null, Error: '', Keep: null, KeepRevision: null, Pending: null, Quote: null, Route: 'elements', Pills: 0, Core: false });
}

export function mfAcceptZhuji(View, { replace = false } = {}) {
  if (!View || typeof View !== 'object') return;
  if (State.View && (State.View.lifeId || '') !== (View.lifeId || '')) mfResetZhuji();
  if (replace || !State.View || View.revision >= State.View.revision || View.status === 'unstarted') {
    if (State.View?.revision !== View.revision) { State.Source = null; State.Quote = null; State.Keep = null; State.KeepRevision = null; ++State.QuoteSerial; }
    State.View = View;
  }
}

function mfReward(Reward) {
  return Reward ? `永久基础气血 <strong>＋${NumberText(Reward.baseHpGain)}</strong> · 裸乘区斗法等效 ＋${NumberText(Reward.battleHpEquivalent)}<small>实际气血上限还经过功法、体魄乘区。</small>` : '';
}

function mfPrepare(View) {
  return `<div class="zhuji-preparation"><p>两种方式由你自选，成功后的表现永久决定本世根基，不能补修。失败或收功后可重新准备。</p>
    <div class="zhuji-route-options">${[['elements', '五行凝练', '八轮抽气，组合五行术式。60分可定型。', '本命固有：所选神通第一副本优先进入起手。'], ['meridians', '周天冲穴', '沿完整经脉冲穴，贯通任、督、冲三关后回丹田。', '本命保留：所选神通第一副本回合末留在手中。']].map(([Id, Name, Text, Reward]) => `<button type="button" data-zhuji-route="${Id}" aria-pressed="${State.Route === Id}"${Disabled(Busy())}><strong>${Name}</strong><span>${Text}</span><small>${Reward}</small></button>`).join('')}</div>
    <div class="zhuji-prep-fields"><label>筑基丹（凝元丹） <input data-zhuji-pills type="number" min="0" max="${Math.min(View.pillMaximum ?? 6, View.pillAvailable || 0)}" step="1" value="${State.Pills}"${Disabled(Busy())}></label>
    <label><input data-zhuji-core type="checkbox"${State.Core ? ' checked' : ''}${Disabled(Busy() || !View.coreAvailableCount)}> 携带一枚灵核</label></div>
    ${View.preparedPill ? '<p class="zhuji-muted">已服的当前境界突破丹计入1份筑基准备，本次最多再选5枚；不重复扣除已服丹药。</p>' : ''}
    <p class="zhuji-muted">需 ${View.cost} 灵石 · 闭关 ${View.days} 天 · 天赋按角色实际生效。筑基丹每局最多6枚。准备与闭关时间在失败或收功后不退。</p>
    <button type="button" class="primary" data-zhuji-command="start"${Disabled(Busy() || !View.canStart)}>确认准备并入关</button>
    ${View.startReason ? `<p class="zhuji-muted">${Escape(View.startReason)}</p>` : ''}</div>`;
}

function mfElementChoice(Element) {
  const Choices = State.View?.choices || [];
  if (State.Kind === 'five') return Choices.find(Item => Item.action.kind === 'five');
  if (State.Kind === 'convert') return Choices.find(Item => Item.action.kind === 'convert' && Item.action.spend?.[0] === State.Source && Item.action.targetId === Element);
  return Choices.find(Item => Item.action.kind === State.Kind && Item.action.spend?.[0] === Element);
}

function mfElements(View) {
  const Total = Object.values(View.resources || {}).reduce((Sum, Count) => Sum + Count, 0);
  const Overflow = Total > View.carryLimit && View.round < 8;
  const Quote = State.Quote;
  return `<div class="zhuji-elements-layout"><section><div class="zhuji-rituals">${Rituals.map(([Id, Name]) => `<button type="button" data-zhuji-kind="${Id}" aria-pressed="${State.Kind === Id}"${Disabled(Busy())}>${Name}</button>`).join('')}</div>
    <div class="zhuji-compass" aria-label="五行罗盘">${CompassOrder.map((Element, Index) => {
      const Angle = (Index * 72 - 90) * Math.PI / 180; const X = 50 + Math.cos(Angle) * 35; const Y = 50 + Math.sin(Angle) * 35;
      const Selected = State.Source === Element; const Resolved = mfElementChoice(Element);
      const CanUse = State.Kind === 'convert' && !State.Source ? View.choices.some(Item => Item.action.kind === 'convert' && Item.action.spend[0] === Element && Item.quote.enabled) : Resolved?.quote.enabled;
      return `<button type="button" class="zhuji-element ${Element}${Selected ? ' selected' : ''}" style="left:${X}%;top:${Y}%" data-zhuji-element="${Element}" aria-label="${Names[Element]}灵气${View.resources?.[Element] || 0}枚" title="${Escape(Resolved?.quote.effectText || Resolved?.quote.reason || '选择换气来源，不扣灵气')}"${Disabled(Busy() || (!CanUse && State.Kind !== 'convert'))}><span>${Names[Element]}</span><strong>${View.resources?.[Element] || 0}</strong></button>`;
    }).join('')}<button type="button" class="zhuji-compass-center" data-zhuji-element="center"${Disabled(Busy() || State.Kind !== 'five' || !Choice('five')?.quote.enabled)}>五行<br>合一</button></div>
    <p class="zhuji-center-note">${State.Kind === 'convert' ? State.Source ? `${Names[State.Source]}气已暂存于界面，点目标才扣气。<button type="button" data-zhuji-cancel-source>取消</button>` : '先点来源，再点目标；取消不扣气。' : '选择术式，再点五行直接施展。悬停或聚焦可查看后端报价。'}</p></section>
    <aside class="zhuji-detail"><h3>第${View.round} / 8轮</h3><p>本轮尚余 <strong>${View.actionPoints}</strong> 次操作</p><p>${mfReward(View.projectedReward)}</p>
    ${Quote ? `<p role="status">${Escape(Quote.effectText || Quote.reason)}</p>` : ''}
    <details class="zhuji-rules"><summary>凝练规则</summary><p class="zhuji-muted">基础凝液：同系2气得3分；同源3气得6分；相生两系各2气得8分；合一每系1气得12分。聚气同系2气，下轮随机返3气。每轮调息、聚气各一次。</p>
    <p class="zhuji-muted">自然抽气：6、6、8、4、6、8、4、6。每点五行各20%，进入下一轮才抽定；跨轮最多保留${View.carryLimit}气。</p></details>
    ${Overflow ? `<div class="zhuji-keep"><p>余气${Total}枚。请输入每系保留量，合计不超过${View.carryLimit}。</p>${Elements.map(Element => `<label>${Names[Element]}<input data-zhuji-keep="${Element}" type="number" min="0" max="${View.resources[Element]}" step="1" value="${State.Keep?.[Element] ?? View.resources[Element]}"${Disabled(Busy())}></label>`).join('')}</div>` : ''}
    <button type="button" data-zhuji-command="next"${Disabled(Busy())}>${View.round === 8 ? '八轮定型' : '进入下一轮'}</button>
    <button type="button" class="primary" data-zhuji-command="seal"${Disabled(Busy() || !Choice('seal')?.quote.enabled)}>提前定型 · 永久</button>
    <button type="button" data-zhuji-command="withdraw"${Disabled(Busy())}>收功离关</button></aside></div>`;
}

function mfMeridians(View) {
  const Nodes = View.nodes || []; const ById = new Map(Nodes.map(Node => [Node.id, Node]));
  const Selected = ById.get(State.SelectedId); const SelectedChoice = View.choices?.find(Item => Item.action.targetId === State.SelectedId);
  return `<div class="zhuji-meridian-layout"><section class="zhuji-map-scroll"><div class="zhuji-map"><div class="zhuji-meditator" aria-hidden="true"><svg viewBox="0 0 200 250"><defs><radialGradient id="zhuji-aura"><stop stop-color="#c3ddbd" stop-opacity=".6"/><stop offset="1" stop-color="#dbe8c9" stop-opacity="0"/></radialGradient><linearGradient id="zhuji-robe" x2="0" y2="1"><stop stop-color="#d1dfcc"/><stop offset="1" stop-color="#b8cdb2"/></linearGradient></defs><ellipse cx="100" cy="135" rx="98" ry="113" fill="url(#zhuji-aura)"/><ellipse cx="100" cy="228" rx="76" ry="10" fill="#b7c7ad" opacity=".28"/><path d="M89 48Q85 25 100 22Q115 25 112 48Z" fill="#7d9380"/><path d="M81 48Q78 71 92 79L108 79Q123 68 118 48Q99 40 81 48" fill="#e7debf"/><path d="M86 77L77 94Q56 102 57 136L44 178L79 188L121 188L156 177L143 135Q144 101 123 94L113 77L101 96Z" fill="url(#zhuji-robe)" stroke="#96b295" stroke-width="2"/><path d="M78 99L99 150L120 99M99 150L98 186M72 125L61 166L88 180M129 125L139 165L113 180" fill="none" stroke="#98b595" stroke-width="2"/><path d="M63 165Q46 178 22 205Q14 226 43 231L101 216L160 231Q186 229 178 209Q155 182 137 166L119 181L101 188L81 180Z" fill="#c0d1b7" stroke="#98b295" stroke-width="2"/><path d="M30 216Q63 203 99 215M171 218Q139 205 101 215M88 178Q98 167 113 179Q101 191 88 178" fill="none" stroke="#98b295" stroke-width="2"/><circle cx="100" cy="155" r="12" fill="#f9e9a9" opacity=".7"/><circle cx="100" cy="155" r="4" fill="#fff8cf"/></svg></div><svg class="zhuji-network-lines" viewBox="0 0 100 100" preserveAspectRatio="none" aria-hidden="true">${(View.edges || []).map(Edge => {
    const From = ById.get(Edge.from), To = ById.get(Edge.to);
    return From && To ? `<path class="${Edge.locked ? 'locked' : From.opened && To.opened ? 'opened' : ''}" d="M${From.x} ${From.y}L${To.x} ${To.y}"/>` : '';
  }).join('')}</svg>${Nodes.map(Node => `<button type="button" class="zhuji-node ${Node.opened ? 'opened' : ''} ${Node.isCurrent ? 'current' : ''} ${Node.isAdjacent ? 'adjacent' : ''} ${Node.revealed ? '' : 'unknown'} ${Node.id === State.SelectedId ? 'selected' : ''}" style="left:${Node.x}%;top:${Node.y}%" data-zhuji-node="${Escape(Node.id)}" aria-label="${Escape(Node.label)}${Node.isCurrent ? '，真气所在' : ''}" aria-pressed="${Node.id === State.SelectedId}"><i>${Node.isCurrent ? '◉' : Node.opened ? '✓' : Node.kind === 'gate' ? '关' : '·'}</i><span>${Escape(Node.label)}</span></button>`).join('')}</div></section>
    <aside class="zhuji-detail"><h3>${Escape(Selected?.label || '择穴而行')}</h3><p>真气 <strong>${View.qi}</strong> · 三关 ${View.gates.filter(Gate => Gate.opened).length} / 3</p>
    ${Selected?.revealed ? `<p>${!SelectedChoice?.quote.effectText && Selected.effectText ? `${Escape(Selected.effectText)}<br>` : ''}得分 ${Selected.points} · 剩余冲穴 ${Selected.attemptsLeft} / ${Selected.maxAttempts}</p>` : ''}
    ${SelectedChoice ? `<p>${Escape(SelectedChoice.quote.effectText || SelectedChoice.quote.reason)}</p><p>耗气 ${SelectedChoice.quote.qiCost} · ${SelectedChoice.quote.successPercent}%成功率</p><button type="button" class="primary" data-zhuji-selected${Disabled(Busy() || !SelectedChoice.quote.enabled)}>${SelectedChoice.action.kind === 'move' ? '沿经脉回走' : '确认冲穴'}</button>` : `<p class="zhuji-muted">点选只预览，冲击按钮才扣气。任选相邻穴位，冲开任、督、冲各一关，再回丹田归元。</p>`}
    <details class="zhuji-rules"><summary>冲穴规则</summary><p class="zhuji-muted">窥见照见两层；蓄势使下一目标成功率+20%，同穴失败可续；聚气回12气；通络开启近道。失败扣气和次数，留在原穴，普通穴2次、主关3次。已通穴回走耗1气。</p></details>
    <button type="button" data-zhuji-command="restore" title="仅恢复刚失败穴位次数，不返还真气"${Disabled(Busy() || !Choice('restore')?.quote.enabled)}>灵核 · 恢复次数</button>
    <button type="button" class="primary" data-zhuji-command="seal"${Disabled(Busy() || !Choice('seal')?.quote.enabled)}>归元定型 · 永久</button><small>${Escape(Choice('seal')?.quote.reason || '归元额外20分，超过100分每分+1基础气血。')}</small>
    <button type="button" data-zhuji-command="withdraw"${Disabled(Busy())}>收功离关</button></aside></div>`;
}

function mfResult(View) {
  const Success = View.status === 'success';
  return `<div class="zhuji-result"><h3>${Success ? '道基已成 · 永久定型' : '本次收功'}</h3><p>最终根基 <strong>${View.score}</strong> / 120</p><p>${Success ? mfReward(View.reward) : '本次没有永久奖励。损失修为，准备与闭关时间不退，可重新准备。'}</p>
    ${Success ? `<section class="lifebound-result"><p>已解锁本命${View.lifeboundKind === 'innate' ? '固有' : '保留'}槽位${View.lifeboundSpell ? `，当前神通：${Escape(View.lifeboundName || '已选择神通')}` : ''}。可在神通界面选择或更换，只强化首张副本。</p><button type="button" class="primary" data-action="goto-lifebound" data-ui="navigate" data-value="arts/spells">前往神通 · 设置本命</button></section>` : mfPrepare(View)}</div>`;
}

export function mfRenderZhuji(View, Options = {}) {
  mfAcceptZhuji(View, { replace: true }); if (!State.View) return '';
  View = State.View;
  if (!View.available && !['success', 'preparing', 'active'].includes(View.status)) return '';
  const Active = View.status === 'active';
  const Content = View.status === 'unstarted' ? mfPrepare(View) : View.status === 'preparing' ? '<p>正在完成90天入关准备。若遇事暂停，请处理当前事件或斗法，再继续安排；准备完成后进入所选玩法。</p>' : Active ? View.route === 'elements' ? mfElements(View) : mfMeridians(View) : mfResult(View);
  return `<section class="zhuji-panel" aria-busy="${Busy() || Boolean(Options.busy)}"><header><div><small>炼气化液 · 此世道基</small><h2>${View.route === 'meridians' ? '周天冲穴' : View.route === 'elements' ? '五行凝练' : '筑基 · 自择道途'}</h2></div>${Active ? `<strong class="zhuji-score">${View.score}<small> / 120 分</small></strong>` : ''}</header>${Content}
    ${State.Error ? `<p class="zhuji-error" role="alert">${Escape(State.Error)} <button type="button" data-zhuji-refresh>核对当前状态</button>${State.Pending ? '<button type="button" data-zhuji-retry>重试同一动作</button>' : ''}</p>` : ''}
    ${View.history?.length ? `<details class="zhuji-history"><summary>修行手记</summary><ol>${View.history.slice(-12).map(Text => `<li>${Escape(Text)}</li>`).join('')}</ol></details>` : ''}
    ${State.Confirm ? `<div class="zhuji-confirm-backdrop"><section class="zhuji-confirm" role="dialog" aria-modal="true" aria-label="确认筑基操作"><h3>${Escape(State.Confirm.title)}</h3><p>${Escape(State.Confirm.text)}</p><button type="button" data-zhuji-confirm${Disabled(Busy())}>确认</button><button type="button" data-zhuji-cancel${Disabled(Busy())}>再想一想</button></section></div>` : ''}</section>`;
}

function mfRender() { if (State.Host?.isConnected) State.Host.innerHTML = mfRenderZhuji(State.View); }
function mfCurrent(Ticket) {
  return Ticket.Epoch === State.Epoch && Ticket.Scope === State.Scope && Ticket.Operation === State.Operation;
}
function mfBegin() {
  return { Epoch: State.Epoch, Scope: State.Scope, Operation: ++State.Operation, Hooks: State.Hooks, LifeId: State.View?.lifeId || '' };
}
async function mfRequest(Body, Hooks = State.Hooks) {
  if (Hooks.request) return await Hooks.request(Body);
  const Response = await fetch('/api/zhuji', { method: Body ? 'POST' : 'GET', headers: { 'Content-Type': 'application/json' }, ...(Body ? { body: JSON.stringify(Body) } : {}) });
  const Data = await Response.json();
  if (!Response.ok || Data.error) throw new Error(Data.error || '连接未完成，请核对当前状态。');
  return Data;
}
function mfAcceptResponse(Data, Ticket) {
  if (!mfCurrent(Ticket)) return false;
  const View = Data?.state?.zhuji || Data?.zhuji;
  if (View && (View.lifeId || '') !== Ticket.LifeId) return false;
  if (Data?.zhuji) mfAcceptZhuji(Data.zhuji);
  if (Data?.state?.zhuji) mfAcceptZhuji(Data.state.zhuji);
  if (Data?.state) Ticket.Hooks.onState?.(Data.state);
  return mfCurrent(Ticket);
}
async function mfCommit(Body, Reuse = false) {
  if (Busy()) return;
  const Request = Reuse ? State.Pending : { ...Body, revision: State.View.revision, requestId: crypto.randomUUID() };
  if (!Request) return;
  const Ticket = mfBegin();
  State.Pending = Request; State.Busy = true; Ticket.Hooks.setBusy?.(true); State.Error = ''; State.Confirm = null; mfRender();
  try {
    const Data = await mfRequest(Request, Ticket.Hooks);
    if (!mfAcceptResponse(Data, Ticket)) return;
    State.Pending = null; State.Quote = null;
  } catch (Failure) {
    if (!mfCurrent(Ticket)) return;
    State.Error = Failure.message || '操作未完成。';
    try { mfAcceptResponse(await mfRequest(undefined, Ticket.Hooks), Ticket); } catch { /* Keep the same request for an explicit retry. */ }
  } finally {
    if (mfCurrent(Ticket)) { State.Busy = false; Ticket.Hooks.setBusy?.(false); mfRender(); }
  }
}
async function mfPreviewAndCommit(Action, Confirm = false) {
  if (Busy()) return;
  const Ticket = mfBegin();
  const Revision = State.View.revision; const Serial = ++State.QuoteSerial;
  State.Busy = true; State.Error = ''; mfRender();
  try {
    const Data = await mfRequest({ kind: 'quote', revision: Revision, action: Action }, Ticket.Hooks);
    if (!mfCurrent(Ticket) || Serial !== State.QuoteSerial || Revision !== State.View?.revision) return;
    State.Quote = Data.quote;
    if (!Data.quote?.enabled) { State.Error = Data.quote?.reason || '此操作暂不可用。'; return; }
    if (Confirm) State.Confirm = { title: Action.kind === 'withdraw' ? '确认收功离关' : Action.kind === 'next' && State.View.round < 8 ? '确认散去余气并推进' : '确认永久定型', text: `${Data.quote.effectText} ${Action.kind === 'seal' || State.View.round === 8 ? `预计基础气血+${Data.quote.reward.baseHpGain}。成功后不能补修。` : ''}`, body: { kind: 'action', action: Action } };
    else { State.Busy = false; await mfCommit({ kind: 'action', action: Action }); }
  } catch (Failure) { if (mfCurrent(Ticket)) State.Error = Failure.message; }
  finally { if (mfCurrent(Ticket)) { State.Busy = false; mfRender(); } }
}
async function mfRefresh() {
  if (Busy()) return;
  const Ticket = mfBegin(); State.Busy = true; mfRender();
  try {
    if (mfAcceptResponse(await mfRequest(undefined, Ticket.Hooks), Ticket)) State.Error = '';
  } catch (Failure) { if (mfCurrent(Ticket)) State.Error = Failure.message; }
  finally { if (mfCurrent(Ticket)) { State.Busy = false; mfRender(); } }
}

export function mfBindZhuji(Host, Hooks = {}) {
  if (!Host) return;
  const Scope = String(typeof Hooks.scope === 'function' ? Hooks.scope() : Hooks.scope ?? State.View?.lifeId ?? '');
  const ScopeChanged = Scope !== State.Scope;
  if (ScopeChanged) {
    const View = State.View; mfResetZhuji(); State.View = View; State.Scope = Scope;
  }
  State.Host = Host; State.Hooks = Hooks;
  if (ScopeChanged) mfRender();
  // Classic call() renders before releasing its transport lock in finally.
  // Reconcile once the caller's synchronous stack has released that lock.
  const BoundEpoch = State.Epoch;
  queueMicrotask(() => { if (State.Host === Host && State.Epoch === BoundEpoch) mfRender(); });
  Host.onclick = async Event => {
    if (State.Host !== Host) return;
    const Button = Event.target.closest('button'); if (!Button || !Host.contains(Button) || Button.disabled) return;
    if (Button.hasAttribute('data-zhuji-cancel')) { State.Confirm = null; mfRender(); return; }
    if (Button.hasAttribute('data-zhuji-confirm')) { const Body = State.Confirm?.body; if (Body) await mfCommit(Body); return; }
    if (Button.hasAttribute('data-zhuji-retry')) { await mfCommit(null, true); return; }
    if (Button.hasAttribute('data-zhuji-refresh')) { await mfRefresh(); return; }
    if (Busy()) return;
    if (Button.dataset.zhujiRoute) { State.Route = Button.dataset.zhujiRoute; mfRender(); return; }
    if (Button.dataset.zhujiKind) { State.Kind = Button.dataset.zhujiKind; State.Source = null; State.Quote = null; mfRender(); return; }
    if (Button.hasAttribute('data-zhuji-cancel-source')) { State.Source = null; mfRender(); return; }
    if (Button.dataset.zhujiNode) { State.SelectedId = Button.dataset.zhujiNode; mfRender(); return; }
    if (Button.hasAttribute('data-zhuji-selected')) { const Item = State.View.choices.find(Candidate => Candidate.action.targetId === State.SelectedId); if (Item) await mfPreviewAndCommit(Item.action); return; }
    if (Button.dataset.zhujiElement) {
      const Element = Button.dataset.zhujiElement;
      if (State.Kind === 'convert' && !State.Source) { State.Source = Element; mfRender(); return; }
      if (State.Kind === 'convert' && State.Source === Element) { State.Source = null; mfRender(); return; }
      const Item = mfElementChoice(Element); if (Item) await mfPreviewAndCommit(Item.action); return;
    }
    const Command = Button.dataset.zhujiCommand;
    if (Command === 'start') {
      const Pills = Number(Host.querySelector('[data-zhuji-pills]')?.value);
      const Core = Boolean(Host.querySelector('[data-zhuji-core]')?.checked);
      if (!Number.isInteger(Pills) || Pills < 0 || Pills > Math.min(State.View.pillMaximum ?? 6, State.View.pillAvailable)) { State.Error = '请选择库存与本次上限范围内的整数筑基丹。'; mfRender(); return; }
      State.Pills = Pills; State.Core = Core;
      State.Confirm = { title: '确认准备并入关', text: `以${State.Route === 'elements' ? '五行凝练' : '周天冲穴'}筑基，消耗${State.View.cost}灵石、${Pills}枚筑基丹${Core ? '、1枚灵核' : ''}，闭关90天。`, body: { kind: 'start', route: State.Route, pills: Pills, core: Core } }; mfRender(); return;
    }
    if (Command) {
      const Action = { kind: Command };
      if (Command === 'next' && Host.querySelector('[data-zhuji-keep]')) {
        const Keep = [];
        for (const Input of Host.querySelectorAll('[data-zhuji-keep]')) {
          const Count = Number(Input.value); if (!Number.isInteger(Count) || Count < 0 || Count > State.View.resources[Input.dataset.zhujiKeep]) { State.Error = '保留数量须为库存范围内的整数。'; mfRender(); return; }
          Keep.push(...Array(Count).fill(Input.dataset.zhujiKeep));
        }
        Action.keep = Keep;
      }
      await mfPreviewAndCommit(Action, Command === 'seal' || Command === 'withdraw' || Command === 'next' && (State.View.round === 8 || Boolean(Action.keep)));
    }
  };
  Host.oninput = Event => {
    if (State.Host !== Host) return;
    const Input = Event.target;
    if (Input.matches('[data-zhuji-pills]')) State.Pills = Number(Input.value);
    if (Input.matches('[data-zhuji-core]')) State.Core = Input.checked;
    if (Input.matches('[data-zhuji-keep]')) {
      State.Keep ||= { ...State.View.resources }; State.Keep[Input.dataset.zhujiKeep] = Number(Input.value); State.KeepRevision = State.View.revision;
      ++State.QuoteSerial; State.Confirm = null;
    }
  };
}
