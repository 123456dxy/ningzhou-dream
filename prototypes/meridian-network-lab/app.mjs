const StorageKey = 'meridian-network-session';
const State = { Session: sessionStorage.getItem(StorageKey), View: null, Settings: null, Busy: false, Blocked: false,
    SelectedId: null, Quote: null, QuoteAction: null, QuoteRevision: null, QuoteSerial: 0, Quoting: false,
    Confirm: null, ResultRevision: null, Error: null, ToastTimer: null, EventTimer: null, Drag: null };
const mfById = Id => document.getElementById(Id);
const mfEscape = Value => String(Value ?? '').replace(/[&<>"']/g, Character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[Character]));
const mfNumber = Value => Number.isFinite(Value) ? Value.toLocaleString('zh-CN', { maximumFractionDigits: 1 }) : '—';
const mfChoice = Kind => State.View?.choices?.find(Choice => Choice.action.kind === Kind);
const mfTargetChoice = Id => State.View?.choices?.find(Choice => Choice.action.targetId === Id && ['strike', 'move'].includes(Choice.action.kind));
const mfNode = Id => State.View?.nodes?.find(Node => Node.id === Id);
const mfCanAct = () => Boolean(State.View?.status === 'active' && !State.Busy && !State.Blocked);
const mfQuoteValid = () => Boolean(State.Quote && State.QuoteRevision === State.View?.revision && !State.Quoting);

function mfToast(Message) {
    mfById('toast').textContent = Message; mfById('toast').classList.add('show');
    clearTimeout(State.ToastTimer); State.ToastTimer = setTimeout(() => mfById('toast').classList.remove('show'), 4500);
}

function mfAccept(Data, Reset = false) {
    if (Data.sessionId) { State.Session = Data.sessionId; sessionStorage.setItem(StorageKey, State.Session); }
    if (Data.view && (Reset || !State.View || Data.view.revision >= State.View.revision)) State.View = Data.view;
    if (State.View && !State.Settings) State.Settings = { ...State.View.settings };
}

async function mfRequest(Path, Body) {
    const Controller = new AbortController(); const Timer = setTimeout(() => Controller.abort(), 12000);
    try {
        const Response = await fetch(`/api/${Path}`, { method: Body === undefined ? 'GET' : 'POST',
            headers: { 'Content-Type': 'application/json', ...(State.Session ? { 'x-network-session': State.Session } : {}) },
            ...(Body === undefined ? {} : { body: JSON.stringify(Body) }), signal: Controller.signal });
        const Data = await Response.json(); mfAccept(Data, Path === 'session');
        if (!Response.ok) { const Failure = new Error(Data.error || '连接暂时中断。'); Failure.status = Response.status; throw Failure; }
        return Data;
    } catch (Failure) {
        if (Failure.name === 'AbortError') throw new Error('响应超时，正在核对本局状态。');
        throw Failure;
    } finally { clearTimeout(Timer); }
}

function mfClearQuote() {
    ++State.QuoteSerial; State.Quote = null; State.QuoteAction = null; State.QuoteRevision = null; State.Quoting = false;
}

function mfCloseConfirm() {
    State.Confirm = null; mfById('confirmBackdrop').classList.add('is-hidden');
}

function mfSetNotice(Message, IsError = false) {
    mfById('notice').textContent = Message || '点选只预览，确认才冲穴。';
    mfById('notice').classList.toggle('is-error', IsError);
}

function mfRewardHtml(Reward) {
    if (!Reward) return '<p>本次未产生永久生命收益。</p>';
    return `<p>永久基础生命 <strong>＋${mfNumber(Reward?.baseHpGain)}</strong><br>斗法等效生命 <strong>＋${mfNumber(Reward?.battleHpEquivalent)}</strong></p>`;
}

function mfRenderGraph() {
    const View = State.View; if (!View) return;
    const Nodes = View.nodes || []; const ById = new Map(Nodes.map(Node => [Node.id, Node]));
    mfById('mapEdges').innerHTML = (View.edges || []).map(Edge => {
        const From = ById.get(Edge.from); const To = ById.get(Edge.to); if (!From || !To) return '';
        const Next = From.id === View.positionId && To.isAdjacent || To.id === View.positionId && From.isAdjacent;
        const Classes = ['route-edge', !From.revealed || !To.revealed ? 'is-unseen' : '', From.opened && To.opened ? 'is-open' : '', Next ? 'is-next' : '', Edge.locked ? 'is-locked' : ''].filter(Boolean).join(' ');
        return `<path class="${Classes}" data-from="${mfEscape(From.id)}" data-to="${mfEscape(To.id)}" d="M${From.x} ${From.y}L${To.x} ${To.y}"/>`;
    }).join('');
    mfById('mapNodes').innerHTML = Nodes.map(Node => {
        const Revealed = Boolean(Node.revealed); const Label = Revealed ? Node.label : '未明穴';
        const Classes = ['meridian-node', Revealed ? '' : 'is-unknown', Node.kind === 'gate' ? 'is-gate' : '', Node.opened ? 'is-open' : '', Node.isAdjacent ? 'is-adjacent' : '', Node.isCurrent ? 'is-current' : '', Node.blocked ? 'is-blocked' : ''].filter(Boolean).join(' ');
        const Symbol = !Revealed ? '·' : Node.id === 'start' ? '丹' : Node.kind === 'gate' ? Label.slice(0, 1) : Node.opened ? '✓' : Node.kind === 'gather' ? '◉' : Node.kind === 'peek' ? '◇' : Node.kind === 'boost' ? '＋' : Node.kind === 'link' ? '⌁' : Node.kind === 'refine' ? '✧' : '·';
        const Status = Node.isCurrent ? '，真气当前所在' : Node.opened ? '，已贯通' : Node.isAdjacent ? '，相邻可选' : '，点击窥见';
        return `<button class="${Classes}" data-node="${mfEscape(Node.id)}" style="left:${Node.x}%;top:${Node.y}%" aria-label="${mfEscape(Label + Status)}" aria-pressed="false"><span class="node-dot"><span>${mfEscape(Symbol)}</span></span><span class="node-label">${mfEscape(Label)}</span>${Revealed && Number.isFinite(Node.points) && Node.points > 0 ? `<span class="node-points">+${mfNumber(Node.points)}</span>` : ''}</button>`;
    }).join('');
    mfById('mapLoading').classList.toggle('is-hidden', Nodes.length > 0);
    mfUpdateSelection();
    const Current = ById.get(View.positionId); const Orb = mfById('flowMarker'); Orb.classList.toggle('is-hidden', !Current);
    if (Current) { Orb.style.left = `${Current.x}%`; Orb.style.top = `${Current.y}%`; }
    mfById('positionLabel').textContent = Current ? `真气 · ${Current.label}` : '丹田候气';
}

function mfUpdateSelection() {
    mfById('mapNodes').querySelectorAll('[data-node]').forEach(Button => {
        const IsSelected = Button.dataset.node === State.SelectedId;
        Button.classList.toggle('is-selected', IsSelected); Button.setAttribute('aria-pressed', String(IsSelected));
    });
    mfById('mapEdges').querySelectorAll('[data-from]').forEach(Edge => {
        Edge.classList.toggle('is-selected', Boolean(State.SelectedId && (Edge.dataset.from === State.View?.positionId && Edge.dataset.to === State.SelectedId || Edge.dataset.to === State.View?.positionId && Edge.dataset.from === State.SelectedId)));
    });
}

function mfStat(Label, Value, Suffix = '') {
    return `<div class="detail-stat">${Label}<strong>${mfEscape(mfNumber(Value))}<small>${mfEscape(Suffix)}</small></strong></div>`;
}

function mfRenderDetail() {
    const View = State.View; const Node = mfNode(State.SelectedId); const Quote = mfQuoteValid() ? State.Quote : null;
    const Button = mfById('primaryAction'); const IsMove = State.QuoteAction?.kind === 'move';
    if (View && View.status !== 'active') {
        mfById('detailKind').textContent = '本次闭关 · 实际结算'; mfById('detailStatus').textContent = '已收功';
        mfById('detailName').textContent = View.status === 'success' ? '道基已成' : '收功归宁';
        mfById('detailEffect').textContent = View.summary || '本次闭关已结束，可在入关准备开始新一局。';
        mfById('detailStats').innerHTML = mfStat('最终根基', View.score, '分') + mfStat('剩余真气', View.qi, '气');
        mfById('quoteOutcomes').innerHTML = mfRewardHtml(View.reward); Button.textContent = '本次闭关已结束'; Button.disabled = true;
        mfById('actionReason').textContent = '打开「入关准备」可重新入关。'; return;
    }
    if (!Node) {
        mfById('detailKind').textContent = '选穴 · 未结算'; mfById('detailStatus').textContent = '窥脉';
        mfById('detailName').textContent = '择穴而行'; mfById('detailEffect').textContent = '一缕真气，自丹田而发。先查看相邻亮穴，沿任、督、冲三脉寻找自己的周天。';
        mfById('detailStats').innerHTML = ''; mfById('quoteOutcomes').innerHTML = '<p>点选只预览，按下冲穴才会消耗真气。</p>';
        Button.disabled = true; Button.textContent = '选取相邻穴位'; mfById('actionReason').textContent = ''; return;
    }
    const Revealed = Boolean(Node.revealed);
    mfById('detailKind').textContent = !Revealed ? '尚未窥见' : Node.kind === 'gate' ? '主关 · 周天必经' : Node.id === 'start' ? '归元 · 丹田' : '经穴 · 选中预览';
    mfById('detailStatus').textContent = Node.isCurrent ? '真气所在' : Node.opened ? '已经贯通' : Node.blocked ? '次数已尽' : Node.isAdjacent ? '相邻可达' : '尚未相邻';
    mfById('detailName').textContent = Revealed ? Node.label : '未明穴';
    mfById('detailEffect').textContent = Revealed ? Quote?.effectText || Node.effectText || (Node.id === 'start' ? '三关贯通后，回到此处归元定基。' : '沿经脉前进，待相邻时可冲击此穴。') : '前路尚未窥明。冲通附近穴位或借窥脉之效，才能知晓此穴数值与效果。';
    mfById('detailStats').innerHTML = !Revealed ? '' : Node.opened ? mfStat('行气耗气', Quote?.cost?.qi, '气') + mfStat('本穴根基', Node.points, '分') : mfStat('冲穴成功', Quote?.successPercent ?? Node.successPercent, '%') + mfStat('冲穴耗气', Quote?.cost?.qi ?? Node.cost, '气') + mfStat('剩余冲穴', Node.attemptsLeft, `/${mfNumber(Node.maxAttempts)}`) + mfStat('贯通根基', Quote?.points ?? Node.points, '分');
    const Success = Quote?.outcomes?.success; const Failure = Quote?.outcomes?.failure;
    let Outcomes = !Revealed ? '<p>远处只有脉象，暂无可确认报价。</p>' : State.Quoting ? '<p>正在核对本次冲穴报价…</p>' : !Quote ? '<p>本次只查看；先沿经脉行至相邻。</p>' : !Quote.enabled ? `<p>${mfEscape(Quote.reason || '当前不可提交此动作。')}</p>` : '';
    if (Quote?.enabled && Success) Outcomes += `<p class="outcome-success">${IsMove ? '行气后' : '若成功'}：余气 <strong>${mfNumber(Success.qi)}</strong>${Number.isFinite(Success.score) ? ` · 根基 <strong>${mfNumber(Success.score)}</strong>` : ''}</p>`;
    if (Quote?.enabled && Failure && !IsMove) Outcomes += `<p class="outcome-failure">若失败：余气 ${mfNumber(Failure.qi)}${Number.isFinite(Failure.attemptsLeft) ? ` · 余 ${mfNumber(Failure.attemptsLeft)} 次` : ''}，停留原穴。</p>`;
    if (Quote?.enabled && !Success && Quote.changes?.length) Outcomes += Quote.changes.map(Change => `<p>${mfEscape(Change)}</p>`).join('');
    mfById('quoteOutcomes').innerHTML = Outcomes;
    Button.textContent = State.Quoting ? '正在核对报价…' : IsMove ? `行气至此 · 耗${mfNumber(Quote?.cost?.qi)}气` : Quote?.enabled ? `冲击此穴 · ${mfNumber(Quote.successPercent)}% / 耗${mfNumber(Quote.cost?.qi)}气` : Node.isCurrent ? '真气停驻此穴' : !Revealed ? '尚未窥见此穴' : !Node.isAdjacent ? '沿经脉行至相邻' : '此穴暂不可冲';
    Button.disabled = !mfCanAct() || !Quote?.enabled || !mfQuoteValid();
    mfById('actionReason').textContent = State.Quoting ? '确认前会核对当前局面。' : Quote?.reason || (Node.opened && Node.isAdjacent ? '行气只移动真气，不会重复获得根基。' : Quote?.enabled ? '只选穴不结算；点击上方按钮才提交。' : Node.isCurrent ? '从周围亮穴继续，或通三关后归元。' : '');
}

function mfRenderControls() {
    const View = State.View; document.body.setAttribute('aria-busy', String(State.Busy));
    if (View) mfById('mapHint').textContent = View.status !== 'active' ? View.summary || '本次闭关已结算。' : State.Busy ? '真气流转中…' : '沿经脉行气，贯通三关。';
    mfById('reconnectAction').classList.toggle('is-hidden', !State.Blocked); mfById('reconnectAction').disabled = State.Busy;
    mfById('mapNodes').querySelectorAll('button').forEach(Button => { Button.disabled = State.Busy; });
    for (const [Kind, Id] of [['restore', 'restoreAction'], ['seal', 'sealAction'], ['withdraw', 'withdrawAction']]) {
        const Choice = mfChoice(Kind); const Button = mfById(Id); Button.disabled = !mfCanAct() || !Choice?.quote?.enabled;
        Button.title = Choice?.quote?.reason || Choice?.label || '';
    }
    const Restore = mfChoice('restore'); const Failed = mfNode(View?.lastFailedId);
    mfById('restoreAction').textContent = !View?.coreAvailable ? '灵核已用尽' : `灵核 · 恢复${Failed?.revealed ? Failed.label : '本穴'}次数`;
    mfById('restoreHint').textContent = Restore?.quote?.enabled ? `仅恢复${Failed?.label || '刚失败穴位'}次数至上限，不返还真气。` : Restore?.quote?.reason || '仅恢复刚失败穴位的次数，不返还真气。';
    const Seal = mfChoice('seal'); mfById('sealHint').textContent = Seal?.quote?.enabled ? '三关已通、真气归田。可确认永久收益。' : Seal?.quote?.reason || '通任、督、冲三关，再回丹田。';
    const Buff = View?.buff; mfById('buffHint').classList.toggle('is-hidden', !Buff);
    if (Buff) { const Target = mfNode(Buff.targetId); mfById('buffHint').textContent = `聚势 ＋${mfNumber(Buff.bonusPercent)}%${Target?.revealed ? ` · 绑定${Target.label}` : ' · 留待下一穴'}。同穴重试仍有效。`; }
    for (const Control of mfById('preparePanel').querySelectorAll('button,input,select')) Control.disabled = State.Busy || State.Blocked;
    if (State.Settings) {
        mfById('pillsInput').value = State.Settings.pills; mfById('pillsInput').textContent = State.Settings.pills;
        mfById('coreInput').checked = State.Settings.core; mfById('talentInput').value = State.Settings.talent;
        mfById('pillMinus').disabled ||= State.Settings.pills <= 0; mfById('pillPlus').disabled ||= State.Settings.pills >= 6;
    }
    mfById('confirmAction').disabled = State.Busy || !State.Confirm || State.Confirm.revision !== View?.revision;
    mfRenderDetail();
}

function mfRender(WithMap = true) {
    const View = State.View;
    if (View) {
        mfById('qiValue').textContent = mfNumber(View.qi); mfById('qiInitial').textContent = `/ ${mfNumber(View.qiInitial)}`;
        mfById('scoreValue').textContent = mfNumber(View.score);
        mfById('rewardValue').innerHTML = View.reward ? `实际定基收益<br>基础生命 ＋${mfNumber(View.reward.baseHpGain)} · 斗法 ＋${mfNumber(View.reward.battleHpEquivalent)}` : '通三关 · 回丹田<br>归元后确认永久收益';
        mfById('gateList').innerHTML = (View.gates || []).map(Gate => `<li class="${Gate.opened ? 'is-open' : ''}"><i></i>${mfEscape(Gate.label)}${Gate.opened ? ' ✓' : ''}</li>`).join('');
        mfById('mapHint').textContent = View.status !== 'active' ? View.summary || '本次闭关已结算。' : State.Busy ? '真气流转中…' : '沿经脉行气，贯通三关。';
        mfById('preparationHint').textContent = View.rules?.preparationHint || '调整后重新入关生效，本局进度将清空。';
        mfById('guideHints').innerHTML = (View.rules?.guideHints || []).map(Hint => `<p>${mfEscape(Hint)}</p>`).join('');
        mfById('historyCount').textContent = View.history?.length || 0;
        mfById('historyList').innerHTML = [...(View.history || [])].reverse().slice(0, 60).map(Item => `<li>${mfEscape(typeof Item === 'string' ? Item : Item.text || '')}</li>`).join('');
        if (WithMap) mfRenderGraph();
    }
    mfRenderControls();
}

async function mfRecover(Failure) {
    mfCloseConfirm(); mfClearQuote(); State.Error = Failure.message; mfSetNotice(Failure.message, true); mfToast(Failure.message);
    try {
        await mfRequest('state'); State.Blocked = false; mfRender();
        if (Failure.status === 409) mfSetNotice('局面已刷新。请重新选穴确认，本次旧报价已取消。', true);
        else mfSetNotice('已重新核对本局状态，可继续操作。请以当前真气与手记为准。', true);
    } catch { State.Blocked = true; mfSetNotice('连接尚未恢复，动作已锁定。重新连接后继续。', true); }
}

async function mfInspect(Id) {
    if (!State.View || State.Busy || State.Blocked) return;
    State.SelectedId = Id; State.Error = null; mfClearQuote(); mfUpdateSelection();
    const Node = mfNode(Id); const Choice = mfTargetChoice(Id);
    if (!Node?.revealed || Node.isCurrent || !Choice) { mfRenderDetail(); return; }
    const Revision = State.View.revision; const Serial = ++State.QuoteSerial;
    State.Quoting = true; State.QuoteAction = Choice.action; mfRenderDetail();
    try {
        const Data = await mfRequest('quote', { revision: Revision, action: Choice.action });
        if (Serial !== State.QuoteSerial || State.SelectedId !== Id || State.View.revision !== Revision || State.Busy) return;
        State.Quote = Data.quote; State.QuoteAction = Choice.action; State.QuoteRevision = Revision;
    } catch (Failure) {
        if (Serial !== State.QuoteSerial) return;
        if (Failure.status === 409 || Failure.status === 401) await mfRecover(Failure);
        else { State.Error = Failure.message; mfSetNotice('报价读取失败。再次选择此穴即可恢复。', true); mfToast(Failure.message); }
    } finally { if (Serial === State.QuoteSerial) { State.Quoting = false; mfRenderDetail(); } }
}

async function mfAnimate(Previous) {
    const View = State.View; const Last = View.lastEvent; if (!Last) return;
    const From = Previous.nodes?.find(Node => Node.id === Previous.positionId); const To = mfNode(View.positionId);
    const Moving = From && To && From.id !== To.id;
    if (Moving) {
        const Orb = mfById('flowMarker'); Orb.style.transition = 'none'; Orb.style.left = `${From.x}%`; Orb.style.top = `${From.y}%`;
        void Orb.offsetWidth; Orb.style.transition = '';
        await new Promise(Resolve => requestAnimationFrame(() => { Orb.style.left = `${To.x}%`; Orb.style.top = `${To.y}%`; setTimeout(Resolve, matchMedia('(prefers-reduced-motion: reduce)').matches ? 0 : 480); }));
    }
    if (Last.kind !== 'strike') return;
    const Target = mfNode(Last.to); if (!Target) return;
    const Failed = Last.success === false; const Class = Failed ? ' is-failure' : '';
    mfById('eventLayer').innerHTML = `<span class="event-wave${Class}" style="left:${Target.x}%;top:${Target.y}%"></span><span class="event-label${Class}" style="left:${Target.x}%;top:${Target.y}%">${Failed ? '冲穴未成' : `贯通${Number.isFinite(Last.gain) && Last.gain > 0 ? ` · 根基＋${mfNumber(Last.gain)}` : ''}`}</span>`;
    clearTimeout(State.EventTimer); State.EventTimer = setTimeout(() => { mfById('eventLayer').innerHTML = ''; }, 1750);
}

function mfResult() {
    const View = State.View; if (!View || View.status === 'active' || State.ResultRevision === View.revision) return;
    State.ResultRevision = View.revision; State.Confirm = null;
    mfById('confirmKicker').textContent = '本次闭关 · 实际结算'; mfById('confirmTitle').textContent = View.status === 'success' ? '三关归元，道基已成' : '收功归宁';
    mfById('confirmBody').innerHTML = `<p>${mfEscape(View.summary || '')}</p><p>最终根基 <strong>${mfNumber(View.score)}</strong> 分</p>${mfRewardHtml(View.reward)}`;
    mfById('confirmAction').classList.add('is-hidden'); mfById('confirmCancel').textContent = '回看经络';
    mfById('confirmBackdrop').classList.remove('is-hidden'); mfById('confirmCancel').focus();
}

async function mfMutate(Path, Content) {
    if (State.Busy || State.Blocked || !State.View) return;
    const Previous = State.View; const Request = { revision: Previous.revision, requestId: crypto.randomUUID(), ...Content };
    State.Busy = true; State.Error = null; mfCloseConfirm(); mfClearQuote(); mfRender(false);
    try {
        await mfRequest(Path, Request); State.Blocked = false;
        if (Path === 'start') { State.Settings = { ...State.View.settings }; State.SelectedId = null; State.ResultRevision = null; mfToggleDrawer('preparePanel', false); mfById('eventLayer').innerHTML = ''; }
        mfRender();
        if (Path === 'action') await mfAnimate(Previous);
        const Last = State.View.history?.at(-1); mfSetNotice(typeof Last === 'string' ? Last : Last?.text || State.View.summary || '新局已入关。先选穴，再确认冲击。');
    } catch (Failure) { await mfRecover(Failure); }
    finally {
        State.Busy = false; mfRenderControls();
        if (State.SelectedId && !State.Blocked && State.View?.status === 'active') void mfInspect(State.SelectedId);
        mfResult();
    }
}

async function mfPrepareConfirm(Kind) {
    if (!mfCanAct()) return;
    const Choice = mfChoice(Kind); if (!Choice?.quote?.enabled) return;
    const Revision = State.View.revision; State.Busy = true; mfClearQuote(); mfRender(false);
    try {
        const Data = await mfRequest('quote', { revision: Revision, action: Choice.action });
        if (State.View.revision !== Revision || !Data.quote.enabled) { mfSetNotice(Data.quote.reason || '局面已变化，请重新确认。', true); return; }
        State.Confirm = { action: Choice.action, revision: Revision };
        mfById('confirmKicker').textContent = Kind === 'seal' ? '三关已通 · 归元定基' : '结束本次闭关';
        mfById('confirmTitle').textContent = Kind === 'seal' ? '确认永久根基' : Kind === 'restore' ? '消耗灵核，恢复本穴次数' : '确认收功离关';
        const Quote = Data.quote; const After = { ...Quote.after, ...Quote.outcomes?.success };
        mfById('confirmBody').innerHTML = (Quote.changes || []).map(Change => `<p>${mfEscape(Change)}</p>`).join('') + (Kind === 'seal' ? `<p>本次定基结果 <strong>${mfNumber(After.score ?? State.View.score)}</strong> 分。</p>${mfRewardHtml(After.reward || Quote.reward || State.View.reward)}<p>确认后本局根基永久定型。</p>` : Kind === 'restore' ? '<p>只恢复刚失败穴位的冲击次数至上限；已经消耗的真气不会返还。灵核只能使用一次。</p>' : '<p>结束本局，按下方权威报价收功。</p>' + mfRewardHtml(After.reward || State.View.reward));
        mfById('confirmAction').textContent = Kind === 'seal' ? '确认定基 · 永久收益' : Kind === 'restore' ? '使用灵核恢复次数' : '确认收功';
        mfById('confirmAction').classList.remove('is-hidden'); mfById('confirmCancel').textContent = '再想一想';
        mfById('confirmBackdrop').classList.remove('is-hidden'); mfById('confirmCancel').focus();
    } catch (Failure) { await mfRecover(Failure); }
    finally { State.Busy = false; mfRenderControls(); }
}

function mfToggleDrawer(Id, Open) {
    const Panel = mfById(Id); const ShouldOpen = Open ?? Panel.classList.contains('is-hidden');
    for (const [PanelId, ToggleId] of [['preparePanel', 'prepareToggle'], ['rulesPanel', 'rulesToggle'], ['logPanel', 'logToggle']]) {
        const IsOpen = PanelId === Id && ShouldOpen;
        mfById(PanelId).classList.toggle('is-hidden', !IsOpen); mfById(ToggleId).setAttribute('aria-expanded', String(IsOpen));
    }
}

async function mfConnect() {
    if (State.Busy) return;
    State.Busy = true; mfClearQuote(); mfCloseConfirm(); mfRender(false);
    try {
        if (State.Session) {
            try { await mfRequest('state'); }
            catch (Failure) {
                if (Failure.status !== 401) throw Failure;
                State.Session = null; State.View = null; State.Settings = null; sessionStorage.removeItem(StorageKey); await mfRequest('session', {});
            }
        } else await mfRequest('session', {});
        State.Blocked = false; State.Error = null; State.SelectedId = null; mfSetNotice('一缕真气始于丹田。点选相邻亮穴，查看耗气与成败，再确认。'); mfRender();
        if (matchMedia('(max-width:760px)').matches) { const Map = mfById('mapViewport'); Map.scrollLeft = (Map.scrollWidth - Map.clientWidth) / 2; Map.scrollTop = 160; }
    } catch (Failure) { State.Blocked = true; State.Error = Failure.message; mfSetNotice(Failure.message, true); mfToast(Failure.message); }
    finally { State.Busy = false; mfRenderControls(); mfResult(); }
}

document.addEventListener('click', Event => {
    const Button = Event.target.closest('button'); if (!Button || Button.disabled) return;
    if (Button.dataset.close) return mfToggleDrawer(Button.dataset.close, false);
    const Drawers = { prepareToggle: 'preparePanel', rulesToggle: 'rulesPanel', logToggle: 'logPanel' };
    if (Drawers[Button.id]) return mfToggleDrawer(Drawers[Button.id]);
    if (Button.id === 'confirmCancel') return mfCloseConfirm();
    if (State.Busy || Event.detail > 1) return;
    if (Button.id === 'reconnectAction') return void mfConnect();
    if (!State.View || State.Blocked) return;
    if (Button.dataset.node) return void mfInspect(Button.dataset.node);
    if (Button.id === 'primaryAction') {
        if (mfCanAct() && mfQuoteValid() && State.Quote.enabled && State.QuoteAction?.targetId === State.SelectedId) return void mfMutate('action', { action: State.QuoteAction });
    }
    if (['restoreAction', 'sealAction', 'withdrawAction'].includes(Button.id)) return void mfPrepareConfirm({ restoreAction: 'restore', sealAction: 'seal', withdrawAction: 'withdraw' }[Button.id]);
    if (Button.id === 'confirmAction') {
        if (State.Confirm?.revision === State.View.revision) return void mfMutate('action', { action: State.Confirm.action });
        mfCloseConfirm(); mfSetNotice('局面已变化，请重新选穴确认。', true); return;
    }
    if (Button.id === 'pillMinus' || Button.id === 'pillPlus') { State.Settings.pills = Math.max(0, Math.min(6, State.Settings.pills + (Button.id === 'pillPlus' ? 1 : -1))); return mfRenderControls(); }
    if (Button.id === 'startAction') return void mfMutate('start', { settings: { ...State.Settings } });
});
mfById('coreInput').addEventListener('change', Event => { if (State.Settings && !State.Busy) State.Settings.core = Event.target.checked; });
mfById('talentInput').addEventListener('change', Event => { if (State.Settings && !State.Busy) State.Settings.talent = Event.target.value; });
document.addEventListener('keydown', Event => {
    if (Event.key === 'Escape') { mfCloseConfirm(); mfToggleDrawer('preparePanel', false); }
    if (Event.key === 'Tab' && !mfById('confirmBackdrop').classList.contains('is-hidden')) {
        const Focusables = [...mfById('confirmBackdrop').querySelectorAll('button:not(:disabled):not(.is-hidden)')];
        const First = Focusables[0]; const Last = Focusables.at(-1);
        if (Event.shiftKey && document.activeElement === First) { Event.preventDefault(); Last?.focus(); }
        else if (!Event.shiftKey && document.activeElement === Last) { Event.preventDefault(); First?.focus(); }
    }
});
const MapViewport = mfById('mapViewport');
MapViewport.addEventListener('pointerdown', Event => {
    if (!matchMedia('(max-width:760px)').matches || Event.pointerType !== 'mouse' || Event.target.closest('button')) return;
    State.Drag = { x: Event.clientX, y: Event.clientY, left: MapViewport.scrollLeft, top: MapViewport.scrollTop, pointerId: Event.pointerId };
    MapViewport.setPointerCapture(Event.pointerId); MapViewport.classList.add('is-dragging');
});
MapViewport.addEventListener('pointermove', Event => {
    const Drag = State.Drag; if (!Drag) return;
    MapViewport.scrollLeft = Drag.left - (Event.clientX - Drag.x); MapViewport.scrollTop = Drag.top - (Event.clientY - Drag.y);
});
function mfEndDrag() { State.Drag = null; MapViewport.classList.remove('is-dragging'); }
MapViewport.addEventListener('pointerup', mfEndDrag); MapViewport.addEventListener('pointercancel', mfEndDrag);
void mfConnect();
