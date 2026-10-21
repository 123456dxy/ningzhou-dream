const StorageKey = 'meridian-flow-session';
const State = { Session: sessionStorage.getItem(StorageKey), View: null, Settings: null, Busy: false, Blocked: false,
    PreviewKey: null, Inspected: null, Error: null, Serial: 0, Confirm: null, ResultKey: null,
    Flow: null, FlowTimer: null, ToastTimer: null, PendingRequest: null };
const mfById = Id => document.getElementById(Id);
const mfEscape = Value => String(Value ?? '').replace(/[&<>"']/g, Character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[Character]));
const mfNumber = Value => Number.isFinite(Value) ? Value.toLocaleString('zh-CN', { maximumFractionDigits: 1 }) : '—';
const mfSigned = Value => Value > 0 ? `＋${mfNumber(Value)}` : Value < 0 ? `−${mfNumber(-Value)}` : '不变';
const mfKey = Action => JSON.stringify(Action);
const mfChoices = () => State.View?.choices || [];
const mfChoice = Key => mfChoices().find(Choice => mfKey(Choice.action) === Key);
const mfKindChoice = Kind => mfChoices().find(Choice => Choice.action.kind === Kind);
const mfNodeChoice = Id => mfChoices().find(Choice => Choice.action.kind === 'move' && Choice.action.targetId === Id);
const mfCanAct = () => State.View?.status === 'active' && !State.Busy && !State.Blocked;
const mfVisualPosition = Node => {
    const Mobile = matchMedia('(max-width: 900px)').matches;
    const Width = mfById('graph').clientWidth; const Inset = Mobile ? 33 : 62;
    const X = Mobile && Node.id === 'detour' ? 70 : Node.x;
    return { x: Width ? Math.min(100 - Inset / Width * 100, Math.max(Inset / Width * 100, X)) : X, y: Node.y };
};
const mfNodePosition = Node => { const Position = mfVisualPosition(Node); return { left: `${Position.x}%`, top: `${Position.y}%` }; };

function mfToast(Message) {
    mfById('toast').textContent = Message; mfById('toast').classList.add('show');
    clearTimeout(State.ToastTimer); State.ToastTimer = setTimeout(() => mfById('toast').classList.remove('show'), 4200);
}

function mfAccept(Data, AllowReset = false) {
    if (Data.sessionId) { State.Session = Data.sessionId; sessionStorage.setItem(StorageKey, State.Session); }
    if (Data.view && (AllowReset || !State.View || Data.view.revision >= State.View.revision)) State.View = Data.view;
    if (State.View && !State.Settings) State.Settings = { ...State.View.settings };
}

async function mfRequest(Path, Body) {
    const Controller = new AbortController(); const Timer = setTimeout(() => Controller.abort(), 12000);
    try {
        const Response = await fetch(`/api/${Path}`, { method: Body === undefined ? 'GET' : 'POST',
            headers: { 'Content-Type': 'application/json', ...(State.Session ? { 'x-flow-session': State.Session } : {}) },
            ...(Body === undefined ? {} : { body: JSON.stringify(Body) }), signal: Controller.signal });
        const Data = await Response.json(); mfAccept(Data, Path === 'session' || Path === 'start');
        if (!Response.ok) { const Failure = new Error(Data.error || '连接中断，请重试。'); Failure.status = Response.status; throw Failure; }
        return Data;
    } catch (Failure) {
        if (Failure.name === 'AbortError') throw new Error('响应超时，正在核对本局状态。');
        throw Failure;
    } finally { clearTimeout(Timer); }
}

function mfStopFlow() { clearTimeout(State.FlowTimer); State.Flow = null; }

function mfQuoteHtml(Choice) {
    if (State.Error) return `<span class="eyebrow">本次操作未提交</span><p>${mfEscape(State.Error)}</p>`;
    if (!Choice) return '<span class="eyebrow">选择预览 · 未结算</span><p>只点发光的相邻穴可以前进；其余穴位可查看条件。</p>';
    const Quote = Choice.quote;
    if (!Quote?.enabled) return `<span class="eyebrow">${mfEscape(Choice.label)} · 暂不可用</span><p>${mfEscape(Quote?.reason || '当前不能选择此动作。')}</p>`;
    const After = Quote.after || {};
    return `<span class="eyebrow">${mfEscape(Choice.label)} · 未结算</span><p class="quote-change">${Quote.cost?.free ? '本次免费调气' : `消耗 ${mfNumber(Quote.cost?.operations)} 次行气`}</p><ul>${(Quote.changes || []).map(Change => `<li>${mfEscape(Change)}</li>`).join('')}</ul>${Number.isFinite(After.power) ? `<p>结算后气势 <strong>${mfNumber(After.power)}</strong> · 剩余 <strong>${mfNumber(After.operationsLeft)}</strong> 次<br>预计底蕴 <strong>${mfNumber(After.score)}</strong> 分</p>` : ''}${Quote.outcome?.unknownNextStage ? '<p>下一脉尚未揭晓，实际进入后才生成路线。</p>' : ''}${After.status && After.status !== 'active' && After.reward ? `<p>永久基础生命＋${mfNumber(After.reward.baseHpGain)} · 斗法等效＋${mfNumber(After.reward.battleHpEquivalent)}</p>` : ''}`;
}

function mfRenderGraph() {
    const View = State.View; const Preview = mfChoice(State.PreviewKey);
    const Nodes = View.nodes || []; const ById = new Map(Nodes.map(Node => [Node.id, Node]));
    mfById('edge-layer').innerHTML = `<defs><marker id="edge-arrow" markerWidth="3" markerHeight="3" refX="1.7" refY="1.5" orient="auto"><path d="M0 0L3 1.5L0 3" fill="#96b491"/></marker></defs>` + (View.edges || []).map(Edge => {
        const From = ById.get(Edge.from); const To = ById.get(Edge.to); if (!From || !To) return '';
        const Moving = State.Flow?.from.id === From.id && State.Flow?.to.id === To.id;
        const IsNext = From.id === View.positionId && Boolean(mfNodeChoice(To.id));
        const IsPreview = IsNext && Preview?.action.targetId === To.id;
        const Start = mfVisualPosition(From); const End = mfVisualPosition(To);
        return `<path class="route-edge${Moving ? ' is-moving' : ''}${IsNext ? ' is-next' : ''}${IsPreview ? ' is-preview' : ''}${From.visited && To.visited ? ' is-passed' : ''}" d="M${Start.x} ${Start.y}L${End.x} ${End.y}" marker-end="url(#edge-arrow)"/>`;
    }).join('');
    mfById('node-layer').innerHTML = Nodes.map(Node => {
        const Choice = mfNodeChoice(Node.id); const Position = mfNodePosition(Node);
        const Window = Node.minPower === null || Node.minPower === undefined ? null : `${Node.minPower}—${Node.maxPower}`;
        const Classes = [Node.id === 'start' ? 'is-start' : '', Node.isCurrent ? 'is-current' : '', Node.visited ? 'is-visited' : '',
            Node.isAdjacent ? 'is-adjacent' : '', State.Inspected === Node.id ? 'is-inspected' : ''].filter(Boolean).join(' ');
        const Status = Node.isCurrent ? '当前' : Node.visited ? '已过' : Node.isAdjacent ? '可前往' : '';
        return `<button class="meridian-node ${Classes}" data-node="${mfEscape(Node.id)}" style="left:${Position.left};top:${Position.top}" aria-label="${mfEscape(Node.label)}${Window ? `，适宜气势${Window}，匹配奖励${Node.bonus}底蕴，过穴后气势${mfSigned(Node.powerDelta)}` : '，起行处'}${Node.isAdjacent ? '，相邻可前往' : '，点击查看'}" aria-disabled="${Boolean(Choice && !Choice.quote.enabled)}" ${State.Busy ? 'disabled' : ''}><strong>${mfEscape(Node.label)}</strong>${Window ? `<span class="node-range">气势 ${Window}</span><span class="node-reward">吻合 ＋${mfNumber(Node.bonus)}底蕴</span><span class="node-delta">过穴 ${mfSigned(Node.powerDelta)}气势</span>` : '<span class="node-delta">循脉起行</span>'}${Status ? `<span class="node-state">${Status}</span>` : ''}</button>`;
    }).join('');
    const Current = State.Flow?.from || ById.get(View.positionId);
    const Orb = mfById('qi-orb'); Orb.classList.toggle('is-hidden', !Current); Orb.classList.toggle('is-moving', Boolean(State.Flow)); Orb.classList.toggle('is-resting', !State.Flow);
    if (Current) Object.assign(Orb.style, mfNodePosition(Current));
    mfById('graph-loading').classList.toggle('is-hidden', Nodes.length > 0);
}

function mfRenderDetail() {
    if (State.View.status !== 'active') { mfById('detail-title').textContent = State.View.status === 'success' ? '根基已定' : '本次闭关已结束'; mfById('detail-body').textContent = State.View.summary || '本局已结算，可以在修行手记查看经过。'; return; }
    const Node = State.View.nodes?.find(Item => Item.id === State.Inspected);
    if (!Node) { mfById('detail-title').textContent = State.View.atExit ? '本脉已通，收势待行' : '点发光穴，真气便前行'; mfById('detail-body').textContent = State.View.atExit ? '气势与余次会保留。进入下一脉时，才揭晓新的穴位与路线。' : '每一穴只看经过前的气势是否吻合。条件不合仍能走，只会错失本穴底蕴；过穴变化会影响下一步。'; return; }
    mfById('detail-title').textContent = Node.label;
    mfById('detail-body').innerHTML = Node.minPower === null || Node.minPower === undefined ? '真气从此处出发，选择发光的相邻穴继续。' : `适宜气势 <strong>${mfNumber(Node.minPower)}—${mfNumber(Node.maxPower)}</strong>，吻合可得 <strong>＋${mfNumber(Node.bonus)} 底蕴</strong>。<br>经过后气势 <strong>${mfSigned(Node.powerDelta)}</strong>。${Node.isCurrent ? '你现在停在这里。' : Node.visited ? '本穴已经经过。' : !Node.isAdjacent ? '当前不相邻，本次仅查看。' : '点击本穴即可提交行气。'}`;
}

function mfRender() {
    const View = State.View; document.body.setAttribute('aria-busy', String(State.Busy));
    mfById('reconnect').classList.toggle('is-hidden', !State.Blocked);
    if (!View) { for (const Id of ['charge', 'release', 'withdraw', 'restart']) mfById(Id).disabled = true; mfById('reconnect').disabled = State.Busy; return; }
    const StageIndex = Math.max(0, (View.stage || 1) - 1);
    mfById('stage-title').textContent = `${View.stageNames?.[StageIndex] || `第${View.stage}脉`} · 行气图`;
    mfById('phase-badge').textContent = State.Blocked ? '连接待恢复' : View.status !== 'active' ? '本次闭关已结算' : State.Busy ? '真气流转中' : View.atExit ? '已到本脉终点' : '只点发光的相邻穴';
    mfById('status-strip').innerHTML = `<div class="status-item">当前气势<strong>${mfNumber(View.power)}</strong></div><div class="status-item">剩余行气<strong>${mfNumber(View.operationsLeft)}<small>/${mfNumber(View.operationsTotal)}</small></strong></div><div class="status-item">${View.status === 'active' ? '预计底蕴' : '最终底蕴'}<strong>${mfNumber(View.score)}<small>分</small></strong></div>`;
    mfById('stage-track').innerHTML = (View.stageNames || []).map((Name, Index) => `<li class="${Index < StageIndex ? 'is-done' : Index === StageIndex ? 'is-current' : ''}"><span class="stage-number">${Index < StageIndex ? '✓' : Index + 1}</span>${mfEscape(Name)}${Index > StageIndex ? ' · 未知' : ''}</li>`).join('');
    mfRenderGraph(); mfRenderDetail();
    const Preview = mfChoice(State.PreviewKey); mfById('quote-panel').innerHTML = mfQuoteHtml(Preview); mfById('quote-panel').classList.toggle('is-refused', Boolean(State.Error || Preview && !Preview.quote?.enabled));
    for (const Kind of ['charge', 'release', 'advance', 'withdraw']) {
        const Button = mfById(Kind); const Choice = mfKindChoice(Kind); Button.disabled = !mfCanAct() || !Choice?.quote?.enabled;
        Button.title = Choice?.quote?.enabled ? Choice.label : Choice?.quote?.reason || '当前不可用';
        if (Kind === 'advance') Button.classList.toggle('is-hidden', !View.atExit || View.status !== 'active');
    }
    mfById('reconnect').disabled = State.Busy;
    const Adjustment = ['charge', 'release'].map(Kind => mfKindChoice(Kind)).filter(Boolean);
    mfById('adjust-note').textContent = Adjustment.map(Choice => `${Choice.label}：${Choice.quote.enabled ? Choice.quote.cost?.free ? '本段首次免费' : `耗${Choice.quote.cost?.operations}次` : Choice.quote.reason}`).join('；');
    mfById('preparation-summary').textContent = `本局${View.settings.pills}丹${View.settings.core ? ' · 灵核' : ''}`;
    mfById('pill-count').textContent = State.Settings.pills; mfById('core').checked = State.Settings.core; mfById('talent').value = State.Settings.talent;
    mfById('preparation-hint').textContent = View.rules?.preparationHint || '准备效果由服务端给出，调整后重新入关生效。';
    for (const Control of mfById('preparation').querySelectorAll('button,input,select')) Control.disabled = State.Busy || State.Blocked;
    mfById('history-count').textContent = View.history?.length || 0;
    mfById('history-list').innerHTML = [...(View.history || [])].reverse().slice(0, 30).map(Item => `<li>${mfEscape(typeof Item === 'string' ? Item : Item.text || Item.event || '')}</li>`).join('');
    mfById('rules-hint').innerHTML = (View.rules?.guideHints || []).map(Hint => `<p>${mfEscape(Hint)}</p>`).join('');
}

function mfInspect(Id, PreviewOnly = false) {
    if (!State.View || State.Busy) return;
    const Choice = mfNodeChoice(Id); State.Inspected = Id; State.Error = null;
    State.PreviewKey = Choice ? mfKey(Choice.action) : null;
    if (!PreviewOnly) mfRenderGraph();
    mfRenderDetail(); mfById('quote-panel').innerHTML = mfQuoteHtml(Choice); mfById('quote-panel').classList.toggle('is-refused', Boolean(Choice && !Choice.quote.enabled));
}

function mfShowResult() {
    const View = State.View; const Key = `${View.revision}:${View.status}`;
    if (View.status === 'active' || State.ResultKey === Key) return;
    State.ResultKey = Key; State.Confirm = null;
    mfById('modal-kicker').textContent = '本次闭关 · 实际结算'; mfById('modal-title').textContent = View.status === 'success' ? '三脉已通，道基已定' : View.status === 'withdrawn' ? '收功归宁' : '本次冲关未成';
    const Reward = View.reward || {};
    mfById('modal-body').innerHTML = `<p>${mfEscape(View.summary)}</p><p>最终底蕴 ${mfNumber(View.score)} 分</p><div class="result-reward"><div>永久基础生命<strong>＋${mfNumber(Reward.baseHpGain)}</strong></div><div>斗法等效生命<strong>＋${mfNumber(Reward.battleHpEquivalent)}</strong></div></div>`;
    mfById('modal-confirm').classList.add('is-hidden'); mfById('modal-backdrop').classList.remove('is-hidden'); mfById('modal-close').focus();
}

async function mfConfirm(Choice) {
    if (!mfCanAct()) return;
    const Serial = ++State.Serial; const Revision = State.View.revision; State.Busy = true; mfRender();
    try {
        const Data = await mfRequest('quote', { revision: Revision, action: Choice.action });
        if (Serial !== State.Serial || Revision !== State.View.revision) return;
        const Current = { ...Choice, quote: Data.quote };
        if (!Data.quote.enabled) { State.Error = Data.quote.reason; return; }
        State.Confirm = { action: Choice.action, revision: Revision };
        mfById('modal-kicker').textContent = Choice.action.kind === 'withdraw' ? '结束本次闭关' : '一朝凝基，此生不改';
        mfById('modal-title').textContent = Choice.action.kind === 'withdraw' ? '确认收功离关' : '确认冲过最后一穴';
        mfById('modal-body').innerHTML = mfQuoteHtml(Current);
        mfById('modal-confirm').textContent = Choice.action.kind === 'withdraw' ? '确认收功' : '确认冲穴 · 永久定型';
        mfById('modal-confirm').classList.remove('is-hidden'); mfById('modal-backdrop').classList.remove('is-hidden'); mfById('modal-close').focus();
    } catch (Failure) { State.Error = Failure.message; mfToast(Failure.message); }
    finally { if (Serial === State.Serial) { State.Busy = false; mfRender(); } }
}

async function mfMoveFlow(Previous, Action) {
    if (Action.kind !== 'move' || Previous.stage !== State.View.stage) return;
    const From = Previous.nodes?.find(Node => Node.id === Previous.positionId); const To = State.View.nodes?.find(Node => Node.id === State.View.positionId);
    if (!From || !To || From.id === To.id) return;
    State.Flow = { from: From, to: To }; mfRender();
    const Last = State.View.lastMove;
    mfById('flow-note').textContent = `实际行气：${From.label} → ${To.label}。${Last ? `气势${Last.powerBefore}→${Last.powerAfter}，${Last.matched ? `吻合，底蕴＋${Last.gain}` : '未吻合，本穴未得底蕴'}。` : ''}`;
    if (!matchMedia('(prefers-reduced-motion: reduce)').matches) {
        await new Promise(Resolve => requestAnimationFrame(() => requestAnimationFrame(() => { Object.assign(mfById('qi-orb').style, mfNodePosition(To)); State.FlowTimer = setTimeout(Resolve, 450); })));
    }
    State.Flow = null;
}

async function mfMutate(Path, Content) {
    if (State.Busy || State.Blocked || !State.View) return;
    const Previous = State.View; const Request = { revision: Previous.revision, requestId: crypto.randomUUID(), ...Content };
    ++State.Serial; State.Busy = true; State.Error = null; State.Confirm = null; State.PendingRequest = { Path, Request }; mfStopFlow(); mfById('modal-backdrop').classList.add('is-hidden'); mfRender();
    try {
        await mfRequest(Path, Request); State.PendingRequest = null; State.PreviewKey = null; State.Inspected = null;
        if (Path === 'start') { State.Settings = { ...State.View.settings }; State.ResultKey = null; mfById('flow-note').textContent = '新局已入关。只点发光的相邻穴，或先调气再行。'; }
        else if (Content.action.kind === 'advance') mfById('flow-note').textContent = '下一脉现已揭晓。气势和余次均沿本局保留。';
        else if (Content.action.kind === 'move') await mfMoveFlow(Previous, Content.action);
        else { const Last = State.View.history?.at(-1); mfById('flow-note').textContent = typeof Last === 'string' ? Last : Last?.text || State.View.summary; }
        State.Busy = false; mfRender(); if (State.View.status !== 'active') mfShowResult();
    } catch (Failure) {
        State.Error = Failure.message; mfToast(Failure.message);
        try { await mfRequest('state'); State.PendingRequest = null; State.PreviewKey = null; if (State.View.status !== 'active') mfShowResult(); }
        catch { State.Blocked = true; mfToast('连接尚未恢复，操作已锁定。请重新连接后继续。'); }
    } finally { State.Busy = false; mfRender(); }
}

function mfChoose(Choice) {
    if (!mfCanAct() || !Choice) return;
    State.PreviewKey = mfKey(Choice.action); State.Error = null;
    if (!Choice.quote?.enabled) { mfRender(); return; }
    if (Choice.action.kind === 'withdraw' || Choice.quote.after?.status && Choice.quote.after.status !== 'active') void mfConfirm(Choice);
    else void mfMutate('action', { action: Choice.action });
}

async function mfConnect() {
    if (State.Busy) return;
    State.Busy = true; mfRender();
    try {
        if (State.Session) {
            try { await mfRequest('state'); }
            catch (Failure) { if (Failure.status !== 401) throw Failure; State.Session = null; State.View = null; await mfRequest('session', {}); }
        } else await mfRequest('session', {});
        State.Blocked = false; State.PendingRequest = null; State.Error = null; State.PreviewKey = null; State.Inspected = null;
    } catch (Failure) { State.Blocked = true; State.Error = Failure.message; mfToast(Failure.message); }
    finally { State.Busy = false; mfRender(); if (State.View?.status !== 'active' && State.View) mfShowResult(); }
}

document.addEventListener('click', Event => {
    const Button = Event.target.closest('button'); if (!Button || Button.disabled || State.Busy) return;
    if (Button.id === 'modal-close') { State.Confirm = null; mfById('modal-backdrop').classList.add('is-hidden'); return; }
    if (Event.detail > 1 && !Button.dataset.pillStep) return;
    if (Button.id === 'reconnect') return void mfConnect();
    if (!State.View || State.Blocked) return;
    if (Button.dataset.node) { mfInspect(Button.dataset.node); return mfChoose(mfNodeChoice(Button.dataset.node)); }
    if (['charge', 'release', 'advance', 'withdraw'].includes(Button.id)) return mfChoose(mfKindChoice(Button.id));
    if (Button.id === 'modal-confirm' && State.Confirm?.revision === State.View.revision) return void mfMutate('action', { action: State.Confirm.action });
    if (Button.dataset.pillStep) { State.Settings.pills = Math.max(0, Math.min(6, State.Settings.pills + Number(Button.dataset.pillStep))); mfById('pill-count').textContent = State.Settings.pills; return; }
    if (Button.id === 'restart') return void mfMutate('start', { settings: { ...State.Settings } });
});

function mfFocusPreview(Event) {
    const Target = Event.target.closest('[data-node],#charge,#release,#advance,#withdraw'); if (!Target || State.Busy || !State.View) return;
    if (Target.dataset.node) mfInspect(Target.dataset.node, true);
    else { const Choice = mfKindChoice(Target.id); State.Error = null; State.PreviewKey = Choice ? mfKey(Choice.action) : null; mfById('quote-panel').innerHTML = mfQuoteHtml(Choice); mfById('quote-panel').classList.toggle('is-refused', Boolean(Choice && !Choice.quote.enabled)); }
}
document.addEventListener('pointerover', mfFocusPreview); document.addEventListener('focusin', mfFocusPreview);
window.addEventListener('resize', () => { if (State.View && !State.Busy) mfRenderGraph(); });
mfById('core').addEventListener('change', Event => { if (State.Settings) State.Settings.core = Event.target.checked; });
mfById('talent').addEventListener('change', Event => { if (State.Settings) State.Settings.talent = Event.target.value; });
document.addEventListener('keydown', Event => { if (Event.key === 'Escape') { State.Confirm = null; mfById('modal-backdrop').classList.add('is-hidden'); } });
void mfConnect();
