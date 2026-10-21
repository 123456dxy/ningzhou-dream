const StorageKey = 'meridian-loop-session';
const ShapeNames = { empty: '空穴', straight: '直脉', corner: '弯脉' };
const PieceMarks = { gather: '蓄', condense: '凝', soothe: '稳' };
const State = { Session: sessionStorage.getItem(StorageKey), View: null, Draft: new Map(), Selected: null,
    Settings: null, Quote: null, QuoteAction: null, QuoteRevision: null, QuoteSerial: 0, QuotePending: false,
    Busy: false, Blocked: false, PendingRequest: null, Animation: null, AnimationTimer: null, ToastTimer: null,
    ModalAction: null, ResultKey: null };
const mfById = Id => document.getElementById(Id);
const mfEscape = Value => String(Value ?? '').replace(/[&<>"']/g, Character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[Character]));
const mfNumber = Value => Number.isFinite(Value) ? Value.toLocaleString('zh-CN', { maximumFractionDigits: 1 }) : '—';
const mfCellData = Cell => ({ index: Cell.index, shape: Cell.shape, rotation: Cell.shape === 'empty' ? 0 : Cell.rotation % (Cell.shape === 'straight' ? 2 : 4), pieceId: Cell.pieceId ?? null });
const mfSameCell = (First, Second) => { const Left = mfCellData(First); const Right = mfCellData(Second); return Left.shape === Right.shape && Left.rotation === Right.rotation && Left.pieceId === Right.pieceId; };
const mfBoard = () => (State.View?.board || []).map(Cell => State.Draft.has(Cell.index) ? { ...Cell, ...State.Draft.get(Cell.index) } : Cell);
const mfChanges = () => [...State.Draft.values()].sort((First, Second) => First.index - Second.index).map(mfCellData);
const mfSignature = () => JSON.stringify(mfChanges());
const mfPiece = Id => State.View?.pieces?.find(Piece => Piece.id === Id);
const mfFixed = Cell => Cell?.fixed || State.View?.fixed?.some(Fixed => Fixed.index === Cell?.index);
const mfFixedLabel = Cell => State.View?.fixed?.find(Fixed => Fixed.index === Cell.index)?.label || Cell.label || '固定关窍';
const mfCanPlay = () => State.View?.status === 'active' && !State.Busy && !State.Blocked;

function mfToast(Message) {
    mfById('toast').textContent = Message; mfById('toast').classList.add('show');
    clearTimeout(State.ToastTimer); State.ToastTimer = setTimeout(() => mfById('toast').classList.remove('show'), 4200);
}

function mfAccept(Data, AllowReset = false) {
    if (Data.sessionId) { State.Session = Data.sessionId; sessionStorage.setItem(StorageKey, State.Session); }
    if (Data.view && (AllowReset || !State.View || Data.view.revision >= State.View.revision)) State.View = Data.view;
    if (State.View && !State.Settings) State.Settings = { pills: State.View.settings.pills, core: State.View.settings.core, talent: State.View.settings.talent };
}

async function mfRequest(Path, Body) {
    const Controller = new AbortController();
    const Timer = setTimeout(() => Controller.abort(), 12000);
    try {
        const Response = await fetch(`/api/${Path}`, { method: Body === undefined ? 'GET' : 'POST',
            headers: { 'Content-Type': 'application/json', ...(State.Session ? { 'x-loop-session': State.Session } : {}) },
            ...(Body === undefined ? {} : { body: JSON.stringify(Body) }), signal: Controller.signal });
        const Data = await Response.json(); mfAccept(Data, Path === 'session' || Path === 'start');
        if (!Response.ok) { const Failure = new Error(Data.error || '连接中断，请重试。'); Failure.status = Response.status; throw Failure; }
        return Data;
    } catch (Failure) {
        if (Failure.name === 'AbortError') throw new Error('响应超时，正在核对本局状态。');
        throw Failure;
    } finally { clearTimeout(Timer); }
}

function mfStopAnimation() {
    clearTimeout(State.AnimationTimer); State.Animation = null;
    for (const Cell of document.querySelectorAll('.loop-cell')) Cell.classList.remove('is-preview-path', 'is-actual-path', 'is-flow-current');
}

function mfInvalidateQuote() {
    State.QuoteSerial++; State.QuotePending = false; State.Quote = null; State.QuoteAction = null; State.QuoteRevision = null;
    State.ModalAction = null; mfStopAnimation();
}

function mfSetCell(Cell) {
    const Base = State.View.board.find(Item => Item.index === Cell.index);
    if (!Base || mfFixed(Base) || !mfCanPlay()) return;
    if (mfSameCell(Base, Cell)) State.Draft.delete(Cell.index); else State.Draft.set(Cell.index, mfCellData(Cell));
    mfInvalidateQuote(); mfRender();
}

// Local geometry is for drawing edited pipes; route validity and all effects remain server-owned.
function mfPorts(Cell) {
    if (!State.Draft.has(Cell.index) && Array.isArray(Cell.ports)) return Cell.ports;
    const Base = Cell.shape === 'straight' ? [0, 2] : Cell.shape === 'corner' ? [0, 1] : [];
    return Base.map(Port => (Port + Cell.rotation) % 4);
}

function mfPipeSvg(Cell) {
    const Points = [[50, 0], [100, 50], [50, 100], [0, 50]];
    const Ports = mfPorts(Cell);
    const Path = Ports.map(Port => `M50 50L${Points[Port][0]} ${Points[Port][1]}`).join('');
    return `<svg viewBox="0 0 100 100" aria-hidden="true"><path class="pipe" d="${Path}"/><path class="pipe-core" d="${Path}"/></svg>`;
}

function mfBlockedIndices() {
    const Result = new Set();
    const Blocked = State.Quote?.blockedCell;
    if (Number.isInteger(Blocked)) Result.add(Blocked); else if (Number.isInteger(Blocked?.index)) Result.add(Blocked.index);
    for (const Issue of State.Quote?.issues || []) if (Number.isInteger(Issue.index)) Result.add(Issue.index);
    return Result;
}

function mfRenderBoard() {
    const Broken = mfBlockedIndices();
    mfById('board').innerHTML = mfBoard().map(Cell => {
        const Fixed = mfFixed(Cell); const Piece = mfPiece(Cell.pieceId); const StoresQi = Piece?.kind === 'gather';
        const Classes = [Fixed ? 'is-fixed' : '', State.Selected === Cell.index ? 'is-selected' : '',
            State.Draft.has(Cell.index) ? 'is-edited' : '', Broken.has(Cell.index) ? 'is-broken' : '',
            State.Animation?.visited.has(Cell.index) ? State.Animation.kind === 'preview' ? 'is-preview-path' : 'is-actual-path' : ''].filter(Boolean).join(' ');
        const Label = Fixed ? mfFixedLabel(Cell) : ShapeNames[Cell.shape] || Cell.label || '经脉';
        return `<button class="loop-cell ${Classes}" data-index="${Cell.index}" role="gridcell" aria-selected="${State.Selected === Cell.index}" aria-label="第${Math.floor(Cell.index / 5) + 1}行第${Cell.index % 5 + 1}列，${mfEscape(Label)}${Piece ? `，${mfEscape(Piece.label)}${StoresQi ? `，蓄能${Piece.charge}/${Piece.capacity}` : ''}` : ''}${State.Draft.has(Cell.index) ? '，本圈已改造' : ''}" title="${mfEscape(Label)}${Piece ? ` · ${mfEscape(Piece.hint || '')}` : ''}" ${State.Busy ? 'disabled' : ''}>${mfPipeSvg(Cell)}<span class="cell-coordinate">${Cell.index + 1}</span>${Fixed ? `<span class="fixed-name">${mfEscape(Label)}</span>` : Piece ? `<span class="piece-mark ${Piece.kind}">${PieceMarks[Piece.kind] || '术'}</span>${StoresQi ? `<span class="piece-charge">${Piece.charge}/${Piece.capacity}</span>` : ''}` : ''}</button>`;
    }).join('');
}

function mfRenderEditor() {
    const Cell = mfBoard().find(Item => Item.index === State.Selected);
    const CanEdit = mfCanPlay() && Cell && !mfFixed(Cell);
    const Installed = mfBoard().filter(Item => Item.pieceId).length;
    const Maximum = State.View.rules?.maxFunctionalPieces;
    mfById('editor-heading').textContent = Cell ? mfFixed(Cell) ? mfFixedLabel(Cell) : `第${Cell.index + 1}穴 · ${ShapeNames[Cell.shape]}` : '选择一处经脉';
    mfById('edit-count').textContent = `${State.View.lapCount === 0 ? `首圈自由 · 已改${State.Draft.size}` : `已改${State.Draft.size}/${State.View.rules.maxEdits}`} · 装${Installed}/${Maximum ?? '—'}`;
    mfById('selection-note').textContent = !Cell ? '点棋盘上的经脉格，再旋转或更换形状。' : mfFixed(Cell) ? '固定关窍不可改造；真气须沿其端口通过。' : '草稿不会扣息；试运报价后，正式运行才提交。';
    for (const Button of document.querySelectorAll('[data-tool]')) Button.disabled = !CanEdit;
    const Select = mfById('piece-select');
    Select.innerHTML = '<option value="">不装功能件</option>' + (State.View.pieces || []).map(Piece => {
        const Used = mfBoard().find(Item => Item.pieceId === Piece.id);
        const Elsewhere = Used && Used.index !== Cell?.index;
        return `<option value="${mfEscape(Piece.id)}" ${Elsewhere ? 'disabled' : ''}>${mfEscape(Piece.label)}${Piece.kind === 'gather' ? ` · 蓄能${Piece.charge}/${Piece.capacity}` : ''}${Elsewhere ? ` · 在${Used.index + 1}穴` : ''}</option>`;
    }).join('');
    Select.value = Cell?.pieceId || ''; Select.disabled = !CanEdit || Cell.shape === 'empty';
    const Piece = mfPiece(Cell?.pieceId);
    mfById('piece-hint').textContent = Piece?.hint || '移动功能件时，先从旧格拆下，再装入新格；按最终草稿原子提交。';
    mfById('undo-draft').disabled = !mfCanPlay() || !State.Draft.size;
    mfById('clear-board').disabled = !mfCanPlay();
}

function mfQuoteHtml(Quote) {
    if (!Quote) return '<span class="eyebrow">本圈试运</span><p>改造后先试运，查看路线、代价与每穴结果。</p>';
    if (!Quote.enabled) return `<span class="eyebrow">尚不能运行</span><p>${mfEscape(Quote.reason)}</p>`;
    const Cost = Quote.cost || {};
    const Reward = Quote.after?.sealReward;
    return `<span class="eyebrow">只读预览 · 未结算</span><p class="quote-cost">本次耗时 ${mfNumber(Cost.time)} 息 · 改造 ${mfNumber(Cost.edits)} 格 · 行气 ${mfNumber(Cost.travel)} 段</p><ul>${(Quote.changes || []).map(Change => `<li>${mfEscape(Change)}</li>`).join('')}</ul>${Reward ? `<p>预计底蕴 ${mfNumber(Quote.after.score)} 分 · 定型基础生命＋${mfNumber(Reward.baseHpGain)}，斗法等效＋${mfNumber(Reward.battleHpEquivalent)}</p>` : ''}`;
}

function mfRenderQuote() {
    const Valid = State.QuoteRevision === State.View?.revision;
    mfById('quote-panel').classList.toggle('is-refused', Boolean(State.Quote && !State.Quote.enabled));
    mfById('quote-panel').innerHTML = State.QuotePending ? '<span class="eyebrow">只读试运</span><p>正在核对本圈线路……</p>' : mfQuoteHtml(Valid ? State.Quote : null);
    const Ready = mfCanPlay() && Valid && State.Quote?.enabled && State.QuoteAction?.kind === 'run';
    mfById('trial').disabled = !mfCanPlay() || State.QuotePending;
    mfById('run').disabled = !Ready;
    mfById('run').textContent = State.Busy ? '正在结算……' : Ready ? '正式运行一圈 · 提交结算' : '先试运，再正式运行';
    for (const Id of ['seal', 'withdraw']) mfById(Id).disabled = !mfCanPlay() || State.QuotePending;
    if (State.Blocked) { mfById('trial').disabled = State.Busy; mfById('trial').textContent = State.Busy ? '正在恢复……' : '重新连接 · 恢复此局'; }
    else mfById('trial').textContent = '只读试运一圈';
}

function mfRender() {
    const View = State.View;
    if (!View) { mfById('trial').textContent = '重新连接 · 恢复此局'; return; }
    document.body.setAttribute('aria-busy', String(State.Busy));
    const Badge = mfById('phase-badge');
    Badge.textContent = State.Blocked ? '连接待恢复' : View.status !== 'active' ? ({ success: '道基已定', withdrawn: '已收功', failed: '本次未成' }[View.status] || View.status) : State.Animation ? State.Animation.kind === 'preview' ? '只读试运 · 未结算' : '正式运行 · 已结算' : `改造态 · 已行${View.lapCount}圈`;
    Badge.classList.toggle('is-preview', State.Animation?.kind === 'preview'); Badge.classList.toggle('is-actual', State.Animation?.kind === 'actual');
    const Stats = [['余时', `${View.timeLeft}/${View.timeTotal}`], ['气势', View.power], ['失衡', View.disorder], ['元液', View.liquid], ['当前底蕴', `${View.score}分`]];
    mfById('status-strip').innerHTML = Stats.map(([Name, Value]) => `<div class="status-item">${Name}<strong>${mfNumber(Value) === '—' && typeof Value === 'string' ? mfEscape(Value) : mfNumber(Value)}</strong></div>`).join('');
    mfRenderBoard(); mfRenderEditor(); mfRenderQuote();
    mfById('pill-count').textContent = State.Settings.pills;
    mfById('core').checked = State.Settings.core; mfById('talent').value = State.Settings.talent;
    mfById('preparation-summary').textContent = `本局${View.settings.pills}丹${View.settings.core ? ' · 灵核' : ''}`;
    mfById('preparation-hint').textContent = View.rules?.preparationHint || '准备实际收益由服务端结算；调整后点击重新入关，本圈不会随草稿改变。';
    for (const Control of mfById('preparation').querySelectorAll('button,input,select')) Control.disabled = State.Busy || State.Blocked;
    const History = View.history || [];
    mfById('history-count').textContent = History.length;
    mfById('history-count').parentElement.title = `稳定${View.stableLaps}圈 · 贯通${View.qualifiedLaps}圈`;
    mfById('history-list').innerHTML = [...History].reverse().slice(0, 30).map(Item => `<li>${mfEscape(typeof Item === 'string' ? Item : Item.text || Item.event || '')}</li>`).join('');
    mfById('rules-hint').innerHTML = (View.rules?.catalog || []).map(Item => `<p><strong>${mfEscape(Item.label)}</strong>：${mfEscape(Item.hint)}</p>`).join('');
}

function mfAnimate(Kind, Path, Trace) {
    mfStopAnimation();
    if (!Path?.length) return;
    const Animation = { kind: Kind, visited: new Set(), current: null };
    State.Animation = Animation; let Step = 0;
    const Tick = () => {
        if (State.Animation !== Animation) return;
        const Index = Path[Step]; Animation.visited.add(Index); Animation.current = Index;
        for (const Cell of document.querySelectorAll('.loop-cell')) {
            const CellIndex = Number(Cell.dataset.index);
            Cell.classList.toggle(Kind === 'preview' ? 'is-preview-path' : 'is-actual-path', Animation.visited.has(CellIndex));
            Cell.classList.toggle('is-flow-current', CellIndex === Index);
        }
        const Event = Trace?.[Math.min(Step, Trace.length - 1)];
        mfById('trace-bar').textContent = `${Kind === 'preview' ? '只读试运 · 未结算' : '正式运行 · 已结算'}${Event ? `：${Event.event} · 气势${Event.power} · 失衡${Event.disorder}` : `：真气经过第${Index + 1}穴`}`;
        if (++Step < Path.length) State.AnimationTimer = setTimeout(Tick, 145);
        else { Animation.current = null; for (const Cell of document.querySelectorAll('.is-flow-current')) Cell.classList.remove('is-flow-current'); }
    };
    if (matchMedia('(prefers-reduced-motion: reduce)').matches) { Step = Path.length - 1; for (const Index of Path) Animation.visited.add(Index); }
    mfRender(); Tick();
}

async function mfPreview(Kind = 'run') {
    if (!mfCanPlay() || State.QuotePending) return;
    mfStopAnimation(); State.Quote = null; State.QuoteAction = null; State.QuotePending = true;
    const Serial = ++State.QuoteSerial; const Revision = State.View.revision; const Signature = mfSignature();
    const Action = Kind === 'run' ? { kind: 'run', changes: mfChanges() } : { kind: Kind };
    mfRender();
    try {
        const Data = await mfRequest('quote', { revision: Revision, action: Action });
        if (Serial !== State.QuoteSerial || Revision !== State.View.revision || Signature !== mfSignature()) return;
        State.Quote = Data.quote; State.QuoteAction = Action; State.QuoteRevision = Revision;
        if (Kind !== 'run' && Data.quote.enabled) mfConfirm(Kind, Data.quote);
        else if (Kind === 'run') mfAnimate('preview', Data.quote.path, Data.quote.trace);
    } catch (Failure) {
        if (Serial === State.QuoteSerial) { State.Quote = { enabled: false, reason: Failure.message }; State.QuoteRevision = State.View?.revision; mfToast(Failure.message); }
    } finally { if (Serial === State.QuoteSerial) { State.QuotePending = false; mfRender(); } }
}

function mfConfirm(Kind, Quote) {
    State.ModalAction = Kind;
    mfById('modal-kicker').textContent = Kind === 'seal' ? '一朝凝基，此生不改' : '结束本次闭关';
    mfById('modal-title').textContent = Kind === 'seal' ? '确认永久定型' : '确认收功离关';
    mfById('modal-body').innerHTML = mfQuoteHtml(Quote);
    mfById('modal-confirm').textContent = Kind === 'seal' ? '确认凝基定型' : '确认收功';
    mfById('modal-confirm').classList.remove('is-hidden'); mfById('modal-backdrop').classList.remove('is-hidden'); mfById('modal-close').focus();
}

function mfShowResult() {
    const View = State.View; const Key = `${View.revision}:${View.status}`;
    if (View.status === 'active' || State.ResultKey === Key) return;
    State.ResultKey = Key; State.ModalAction = null;
    mfById('modal-kicker').textContent = '本次闭关 · 实际结算';
    mfById('modal-title').textContent = View.status === 'success' ? '道基已定' : View.status === 'withdrawn' ? '收功归宁' : '冲关未成';
    const Reward = View.reward || {};
    mfById('modal-body').innerHTML = `<p>${mfEscape(View.summary)}</p><p>最终底蕴 ${mfNumber(View.score)} 分 · 正式行气 ${View.lapCount} 圈</p><div class="result-reward"><div>永久基础生命<strong>＋${mfNumber(Reward.baseHpGain)}</strong></div><div>斗法等效生命<strong>＋${mfNumber(Reward.battleHpEquivalent)}</strong></div></div>`;
    mfById('modal-confirm').classList.add('is-hidden'); mfById('modal-backdrop').classList.remove('is-hidden'); mfById('modal-close').focus();
}

async function mfMutate(Path, Content) {
    if (State.Busy || State.Blocked || !State.View) return;
    const Request = { revision: State.View.revision, requestId: crypto.randomUUID(), ...Content };
    State.PendingRequest = { Path, Request }; State.Busy = true; mfInvalidateQuote(); mfById('modal-backdrop').classList.add('is-hidden'); mfRender();
    try {
        await mfRequest(Path, Request); State.PendingRequest = null; State.Draft.clear();
        if (Path === 'start') { State.Settings = { ...State.View.settings }; State.Selected = null; State.ResultKey = null; }
        mfToast(Path === 'start' ? '已重新入关，基础回路就绪。' : Content.action.kind === 'run' ? '本圈已结算，回到改造态。' : '本次选择已结算。');
        State.Busy = false; mfRender();
        if (Path === 'action' && Content.action.kind === 'run' && State.View.lastLap) mfAnimate('actual', State.View.lastLap.path, State.View.lastLap.trace);
        if (State.View.status !== 'active') mfShowResult();
    } catch (Failure) {
        mfToast(Failure.message);
        try { await mfRequest('state'); State.PendingRequest = null; State.Draft.clear(); if (State.View.status !== 'active') mfShowResult(); }
        catch { State.Blocked = true; mfToast('连接尚未恢复，操作已锁定。请重新连接后继续。'); }
    } finally { State.Busy = false; mfRender(); }
}

async function mfConnect() {
    if (State.Busy) return;
    State.Busy = true;
    try {
        if (State.Session) {
            try { await mfRequest('state'); }
            catch (Failure) { if (Failure.status !== 401) throw Failure; State.Session = null; State.View = null; await mfRequest('session', {}); }
        } else await mfRequest('session', {});
        State.Blocked = false; State.PendingRequest = null; mfInvalidateQuote(); State.Draft.clear();
    } catch (Failure) { State.Blocked = true; mfToast(Failure.message); }
    finally { State.Busy = false; mfRender(); }
}

document.addEventListener('click', Event => {
    const Button = Event.target.closest('button'); if (!Button || Button.disabled || State.Busy) return;
    if (Event.detail > 1 && ['trial', 'run', 'seal', 'withdraw', 'restart', 'modal-confirm'].includes(Button.id)) return;
    if (Button.id === 'modal-close') { State.ModalAction = null; mfById('modal-backdrop').classList.add('is-hidden'); return; }
    if (State.Blocked || !State.View) { if (Button.id === 'trial') void mfConnect(); return; }
    if (Button.dataset.index !== undefined) { State.Selected = Number(Button.dataset.index); mfRenderBoard(); mfRenderEditor(); return; }
    if (Button.dataset.tool) {
        const Cell = mfBoard().find(Item => Item.index === State.Selected); if (!Cell || mfFixed(Cell)) return;
        if (Button.dataset.tool === 'rotate') mfSetCell({ ...Cell, rotation: (Cell.rotation + 1) % 4 });
        else mfSetCell({ ...Cell, shape: Button.dataset.tool, rotation: 0, pieceId: Button.dataset.tool === 'empty' ? null : Cell.pieceId });
        return;
    }
    if (Button.dataset.pillStep) { State.Settings.pills = Math.max(0, Math.min(6, State.Settings.pills + Number(Button.dataset.pillStep))); mfById('pill-count').textContent = State.Settings.pills; return; }
    if (Button.id === 'undo-draft') { State.Draft.clear(); mfInvalidateQuote(); mfRender(); return; }
    if (Button.id === 'clear-board') {
        for (const Cell of State.View.board) if (!mfFixed(Cell)) { const Empty = { ...Cell, shape: 'empty', rotation: 0, pieceId: null }; if (mfSameCell(Cell, Empty)) State.Draft.delete(Cell.index); else State.Draft.set(Cell.index, mfCellData(Empty)); }
        mfInvalidateQuote(); mfRender(); return;
    }
    if (Button.id === 'trial') return void mfPreview();
    if (Button.id === 'run' && State.Quote?.enabled && State.QuoteAction?.kind === 'run' && State.QuoteRevision === State.View.revision) return void mfMutate('action', { action: State.QuoteAction });
    if (Button.id === 'seal' || Button.id === 'withdraw') return void mfPreview(Button.id);
    if (Button.id === 'modal-confirm' && State.ModalAction && State.Quote?.enabled && State.QuoteRevision === State.View.revision) return void mfMutate('action', { action: State.QuoteAction });
    if (Button.id === 'restart') return void mfMutate('start', { settings: { ...State.Settings } });
});
mfById('piece-select').addEventListener('change', Event => {
    const Cell = mfBoard().find(Item => Item.index === State.Selected); if (!Cell || mfFixed(Cell)) return;
    mfSetCell({ ...Cell, pieceId: Event.target.value || null });
});
mfById('core').addEventListener('change', Event => { if (State.Settings) State.Settings.core = Event.target.checked; });
mfById('talent').addEventListener('change', Event => { if (State.Settings) State.Settings.talent = Event.target.value; });
document.addEventListener('keydown', Event => { if (Event.key === 'Escape') { State.ModalAction = null; mfById('modal-backdrop').classList.add('is-hidden'); } });
void mfConnect();
